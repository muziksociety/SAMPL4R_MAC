#include "OnnxSeparationProvider.h"
#include "HtdemucsIstft.h"
#include "GpuAdapterProbe.h"
#include "OrtRuntime.h"

#include <juce_dsp/juce_dsp.h>

#include <onnxruntime_cxx_api.h>
#if SAMPLAR_HAS_DIRECTML
 #ifndef NOMINMAX
  #define NOMINMAX            // dml_provider_factory.h pulls in <windows.h>: keep std::min/max
 #endif
 #include <dml_provider_factory.h>
#endif

#include <cmath>
#include <cstring>
#include <algorithm>
#include <mutex>
#include <new>
#include <random>
#include <thread>

// ============================================================================
// OnnxSeparationProvider - mirror of sevagh/demucs.onnx reference inference
// (src/model_apply.cpp + src/demucs.hpp), ported from Sample Monsta 5 (453e7733)
// with SAMPLAR additions: streamed overlap-add + float WAV output (bounded memory,
// audit C3/C9), mid-chunk cancel via RunOptions::SetTerminate (C2), no spin-wait /
// physical-1 threads / below-normal priority (C13), process-wide GPU kill flag.
//
// Pipeline constants - sevagh/demucs.onnx src/demucs.hpp:
//   SUPPORTED_SAMPLE_RATE = 44100
//   SEGMENT_LEN_SECS      = 7.8     -> 343 980 samples per chunk
//   MAX_SHIFT_SECS        = 0.5     -> 22 050 samples
//   OVERLAP               = 0.25    -> stride = (1-0.25)*segment = 257 985
//   TRANSITION_POWER      = 1.0     (triangular weights, not squared)
//
// Differences from a generic Demucs Python apply.py (all matched here):
//   - ref_std uses ddof=1 (sample stddev, n-1).
//   - symmetric zero pad by MAX_SHIFT on BOTH sides, one random shift in [0, MAX_SHIFT),
//     shifted_audio length = length + max_shift - shift_offset; crop offset
//     = max_shift - shift_offset, extent = length.
//   - the loop reads min(segment, remaining) so the last chunk may be shorter.
//   - out[seg_off : seg_off + chunk_len] += weight[:chunk_len] * chunk_out,
//     sum_weight likewise; out /= sum_weight; out = out*ref_std + ref_mean.
// Streaming: chunk k+1 starts at seg_off + stride, so after chunk k every sample in
// [seg_off, seg_off + stride) is final and is written out immediately; the accumulator
// only spans one segment.
// ============================================================================

namespace samplar {

// ----------------------------------------------------------------------------
// Constants from sevagh/demucs.onnx src/demucs.hpp
// ----------------------------------------------------------------------------
static constexpr int   kSampleRate       = 44100;
static constexpr float kSegmentLenSecs   = 7.8f;
static constexpr float kMaxShiftSecs     = 0.5f;
static constexpr float kOverlap          = 0.25f;
static constexpr float kTransitionPower  = 1.0f;

static constexpr int   kSegmentSamples   = (int) (kSegmentLenSecs * (float) kSampleRate); // 343980
static constexpr int   kMaxShiftSamples  = (int) (kMaxShiftSecs   * (float) kSampleRate); // 22050
static constexpr int   kStrideSamples    = (int) ((1.0f - kOverlap) * (float) kSegmentSamples); // 257985

static constexpr int   kNumStems         = 4;
static constexpr int   kNumChannels      = 2;

// Split model (tools/separation/split_htdemucs.py): body outputs, in this order.
static constexpr const char* kBodyModelFileName = "htdemucs_body.onnx";
static constexpr const char* kBodySpecOutput    = "/Concat_18_output_0";   // [8, 4098, frames]
static constexpr const char* kBodyTimeOutput    = "/Add_7_output_0";       // [1, 4, 2, segment]
// DirectML only on a GPU with this much dedicated VRAM (body peak ~1.1 GB). Integrated GPUs
// report ~128 MB and measured slower than the CPU (Intel HD 530: 516 s vs 213 s per song).
static constexpr std::uint64_t kMinGpuDedicatedBytes = 2ull << 30;

// ----------------------------------------------------------------------------
// Process-wide state
// ----------------------------------------------------------------------------
static std::atomic<bool> sGpuFailedThisProcess { false };

static std::mutex& diagMutex()
{
    static std::mutex m;
    return m;
}

static juce::String& lastRunBackendStorage()
{
    static juce::String s;
    return s;
}

struct CachedGpuProbe
{
    bool suitable = false;
    GpuAdapterInfo info;
};

// One adapter probe per process (D3D12CreateDevice per adapter is not free). The GPU set of a
// machine does not change while a DAW runs in practice; a hot-plugged eGPU is picked up on restart.
static const CachedGpuProbe& cachedGpuProbe()
{
    static std::once_flag once;
    static CachedGpuProbe probe;
    std::call_once (once, []
    {
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        probe.suitable = pickDiscreteGpuAdapter (kMinGpuDedicatedBytes, probe.info);
        juce::Logger::writeToLog ("[ONNX] GPU probe (cached for this process): best adapter '"
                                  + juce::String (probe.info.name.c_str()) + "' index=" + juce::String (probe.info.index)
                                  + " dedicated=" + juce::String ((juce::int64) (probe.info.dedicatedVideoMemory >> 20)) + " MB -> "
                                  + (probe.suitable ? "suitable" : "rejected (< 2 GB dedicated: integrated / too small / none)")
                                  + " (" + juce::String (juce::Time::getMillisecondCounterHiRes() - t0, 1) + " ms)");
    });
    return probe;
}

// ORT intra-op pool threads at below-normal priority so the DAW's audio + UI threads win
// (audit C13). ORT calls these instead of std::thread for every pool thread of the session.
static OrtCustomThreadHandle createLowPriorityOrtThread (void* /*options*/, OrtThreadWorkerFn fn, void* param)
{
    // Called on the thread that creates the session - possibly the detached creator thread
    // (S-M3), which must not log: the instance (and its logger) may be gone by then.
    const bool quiet = isDetachedOrtCreatorThread();
    try
    {
        auto* t = new std::thread ([fn, param]
        {
            setCurrentThreadPriorityLevel (belowNormalThreadPriorityLevel());
            fn (param);
        });
        if (! quiet)
            juce::Logger::writeToLog ("[ONNX]   ORT pool thread created (below-normal priority)");
        return reinterpret_cast<OrtCustomThreadHandle> (t);
    }
    catch (...)
    {
        if (! quiet)
            juce::Logger::writeToLog ("[ONNX]   ORT pool thread creation FAILED");
        return nullptr;
    }
}

static void joinLowPriorityOrtThread (OrtCustomThreadHandle handle)
{
    auto* t = reinterpret_cast<std::thread*> (const_cast<OrtCustomHandleType*> (handle));
    if (t == nullptr)
        return;
    if (t->joinable())
        t->join();
    delete t;
}

// Restores the calling thread's priority when separate() returns.
struct ScopedLowThreadPriority
{
    int previous;
    ScopedLowThreadPriority() : previous (getCurrentThreadPriorityLevel())
    {
        setCurrentThreadPriorityLevel (belowNormalThreadPriorityLevel());
        juce::Logger::writeToLog ("[ONNX]   worker thread priority " + juce::String (previous) + " -> below normal ("
                                  + juce::String (getCurrentThreadPriorityLevel()) + ")");
    }
    ~ScopedLowThreadPriority() { setCurrentThreadPriorityLevel (previous); }
};

// Polls the caller's cancel hook while ORT runs and terminates the running chunk (audit C2).
// Without it a cancel waits for the current 7.8 s segment: ~2 s on a GPU, ~9 s on the CPU.
class CancelWatcher
{
public:
    CancelWatcher (const std::function<bool()>& poll, Ort::RunOptions& runOptions)
    {
        if (! poll)
            return;
        thread_ = std::thread ([this, poll, &runOptions]
        {
            while (! stop_.load())
            {
                if (poll())
                {
                    firedAtMs_ = juce::Time::getMillisecondCounterHiRes();
                    fired_ = true;
                    runOptions.SetTerminate();
                    juce::Logger::writeToLog ("[ONNX]   CancelWatcher: cancel requested -> RunOptions::SetTerminate()");
                    return;
                }
                std::this_thread::sleep_for (std::chrono::milliseconds (25));
            }
        });
    }

    ~CancelWatcher()
    {
        stop_ = true;
        if (thread_.joinable())
            thread_.join();
    }

    bool fired() const noexcept { return fired_.load(); }
    double firedAtMs() const noexcept { return firedAtMs_.load(); }

private:
    std::thread thread_;
    std::atomic<bool> stop_ { false };
    std::atomic<bool> fired_ { false };
    std::atomic<double> firedAtMs_ { 0.0 };
};

// ----------------------------------------------------------------------------
juce::String OnnxSeparationProvider::stemFileName (const juce::String& base, Stem stem)
{
    static const char* names[] = { "Drums", "Bass", "Other", "Vocals" };
    return base + "_" + names[(int) stem] + ".wav";
}

juce::String OnnxSeparationProvider::lastRunBackend()
{
    std::lock_guard<std::mutex> lk (diagMutex());
    return lastRunBackendStorage();
}

bool OnnxSeparationProvider::gpuFailedThisProcess() noexcept
{
    return sGpuFailedThisProcess.load();
}

juce::String OnnxSeparationProvider::describeNextBackend (bool gpuSettingOn)
{
    juce::Logger::writeToLog ("[ONNX] describeNextBackend: gpuSettingOn=" + juce::String ((int) gpuSettingOn));
    juce::String result;
    if (! ensureOrtApi())
    {
        result = "Unavailable (ONNX Runtime missing - reinstall SAMPLAR)";
        juce::Logger::writeToLog ("[ONNX] describeNextBackend -> " + result + ": " + ortUnavailableReason());
        return result;
    }
   #if SAMPLAR_HAS_DIRECTML
    const auto env = juce::SystemStats::getEnvironmentVariable ("SAMPLAR_SEPARATION_DEVICE", "auto").trim().toLowerCase();
    if (! gpuSettingOn)
        result = "CPU (GPU turned off)";
    else if (env == "cpu")
        result = "CPU (forced by SAMPLAR_SEPARATION_DEVICE=cpu)";
    else if (sGpuFailedThisProcess.load())
        result = "CPU (GPU failed earlier - restart the host to retry)";
    else
    {
        const auto& probe = cachedGpuProbe();
        if (probe.suitable)
            result = "GPU: " + juce::String (probe.info.name.c_str()).trim() + " (DirectML)";
        else
            result = "CPU (no discrete GPU with 2 GB+ video memory)";
    }
   #else
    juce::ignoreUnused (gpuSettingOn);
    result = "CPU (this build has no GPU support)";
   #endif
    juce::Logger::writeToLog ("[ONNX] describeNextBackend -> " + result);
    return result;
}

void OnnxSeparationProvider::disableGpuForProcess (const juce::String& reason)
{
    gpuEligible_ = false;
    if (! sGpuFailedThisProcess.exchange (true))
        juce::Logger::writeToLog ("[ONNX]   Decision: GPU disabled for the rest of this process (" + reason + ")");
}

// ----------------------------------------------------------------------------
OnnxSeparationProvider::OnnxSeparationProvider (const juce::File& modelFile, bool allowGpu,
                                                std::function<bool()> creationCancel)
    : creationCancel_ (std::move (creationCancel))
{
    juce::Logger::writeToLog ("[ONNX] === OnnxSeparationProvider ctor START ===");
    juce::Logger::writeToLog ("[ONNX]   model: " + modelFile.getFullPathName()
                              + " allowGpu=" + juce::String ((int) allowGpu));

    // Split model (GPU-capable) when given directly or found next to the full export.
    modelFile_ = modelFile;
    if (modelFile.getFileName() == kBodyModelFileName)
        bodyMode_ = true;
    else if (modelFile.getSiblingFile (kBodyModelFileName).existsAsFile())
    {
        modelFile_ = modelFile.getSiblingFile (kBodyModelFileName);
        bodyMode_ = true;
    }
    juce::Logger::writeToLog ("[ONNX]   Decision: " + juce::String (bodyMode_ ? "body model + host iSTFT" : "full model (legacy, CPU only)")
                              + " -> " + modelFile_.getFullPathName());

    if (! modelFile_.existsAsFile())
    {
        lastError_ = "ONNX model not found: " + modelFile_.getFullPathName();
        juce::Logger::writeToLog ("[ONNX] FAILED: " + lastError_);
        return;
    }

    // Bind the ORT API lazily (ORT_API_MANUAL_INIT): a missing onnxruntime.dll is reported here
    // instead of crashing the host at plugin load (audit S-H2).
    if (! ensureOrtApi())
    {
        runtimeUnavailable_ = true;
        lastError_ = ortUnavailableUserMessage();
        juce::Logger::writeToLog ("[ONNX] FAILED: runtime unavailable: " + ortUnavailableReason());
        return;
    }

    try
    {
        env_ = std::make_shared<Ort::Env> (ORT_LOGGING_LEVEL_WARNING, "samplar-onnx");
        memInfo_ = std::make_unique<Ort::MemoryInfo> (
            Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault));

        // Which ONNX Runtime is live in this process (a DAW may host other ORT users).
        juce::String providers;
        for (const auto& p : Ort::GetAvailableProviders())
            providers << (providers.isEmpty() ? "" : ", ") << juce::String (p);
        juce::Logger::writeToLog ("[ONNX]   ORT version=" + juce::String (Ort::GetVersionString())
                                  + " providers=[" + providers + "]"
                                  + " dll=" + juce::String (loadedModulePath (L"onnxruntime.dll").c_str()));
    }
    catch (const std::exception& e)
    {
        lastError_ = juce::String ("ONNX environment init failed: ") + e.what();
        juce::Logger::writeToLog ("[ONNX] FAILED: " + lastError_);
        return;
    }

    // ---- Device decision (GPU only for the body model; see header) ----
    bool wantGpu = false;
    const auto deviceOverride = juce::SystemStats::getEnvironmentVariable ("SAMPLAR_SEPARATION_DEVICE", "auto").trim().toLowerCase();
   #if SAMPLAR_HAS_DIRECTML
    if (! bodyMode_)
        juce::Logger::writeToLog ("[ONNX]   GPU: skipped (full model's baked-in iSTFT does not fit DirectML)");
    else if (deviceOverride == "cpu")
        juce::Logger::writeToLog ("[ONNX]   GPU: skipped (SAMPLAR_SEPARATION_DEVICE=cpu)");
    else if (! allowGpu)
        juce::Logger::writeToLog ("[ONNX]   GPU: skipped (setting 'Use GPU for stem separation' is OFF)");
    else if (sGpuFailedThisProcess.load())
        juce::Logger::writeToLog ("[ONNX]   GPU: skipped (GPU failed earlier in this process)");
    else
    {
        const auto& probe = cachedGpuProbe();
        gpuAdapterIndex_ = probe.info.index;
        gpuName_ = juce::String (probe.info.name.c_str()).trim();
        std::wstring dmlPath;
        if (probe.suitable && preloadBundledDirectML (dmlPath))
        {
            juce::Logger::writeToLog ("[ONNX]   DirectML preloaded: " + juce::String (dmlPath.c_str()));
            wantGpu = true;
        }
        else if (probe.suitable)
            juce::Logger::writeToLog ("[ONNX]   GPU: skipped (bundled DirectML.dll not found next to the plugin, or another DirectML.dll ("
                                      + juce::String (loadedModulePath (L"DirectML.dll").c_str()) + ") is already loaded)");
        else
            juce::Logger::writeToLog ("[ONNX]   GPU: skipped (no discrete GPU with >= 2 GB dedicated VRAM)");
    }
   #else
    juce::ignoreUnused (allowGpu, deviceOverride);
    juce::Logger::writeToLog ("[ONNX]   GPU: not available in this build (CPU ONNX Runtime)");
   #endif

    gpuEligible_ = wantGpu;
    ready_ = createSession (wantGpu);
    if (! ready_ && cancelled_)
    {
        juce::Logger::writeToLog ("[ONNX] === ctor CANCELLED during session creation ===");
        return;   // a cancel is not a GPU failure: nothing disabled, no CPU retry
    }
    if (! ready_ && wantGpu)
    {
        juce::Logger::writeToLog ("[ONNX]   Decision: GPU session failed -> CPU");
        disableGpuForProcess ("DirectML session creation failed");
        ready_ = createSession (false);
        if (! ready_ && cancelled_)
            return;
        if (ready_)
            lastError_.clear();   // the DirectML error is not an error of this provider any more
    }
    if (! ready_)
        return;

    if (bodyMode_)
    {
        istft_ = std::make_unique<HtdemucsIstft>();
        istftOut_.assign ((size_t) kNumStems * kNumChannels * kSegmentSamples, 0.0f);
    }

    juce::Logger::writeToLog ("[ONNX] === ctor SUCCESS (provider=" + activeEP_
                              + " model=" + modelFile_.getFileName()
                              + " segment=" + juce::String (kSegmentSamples)
                              + " stride=" + juce::String (kStrideSamples) + ") ===");
}

bool OnnxSeparationProvider::createSession (bool useGpu)
{
    juce::Logger::writeToLog ("[ONNX] === createSession START === gpu=" + juce::String ((int) useGpu));
    session_.reset();
    activeEP_ = "CPU";
    try
    {
        Ort::SessionOptions so;
        // Leave one physical core to the DAW (audit C13); never spin-wait between ops.
        const int physical = juce::jmax (1, (int) juce::SystemStats::getNumPhysicalCpus());
        const int intra = juce::jmax (1, physical - 1);
        so.SetIntraOpNumThreads (intra);
        so.SetInterOpNumThreads (1);
        so.AddConfigEntry ("session.intra_op.allow_spinning", "0");
        so.AddConfigEntry ("session.inter_op.allow_spinning", "0");
        so.SetCustomCreateThreadFn (createLowPriorityOrtThread);
        so.SetCustomJoinThreadFn (joinLowPriorityOrtThread);
        so.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);
        so.SetExecutionMode (ORT_SEQUENTIAL);              // required by DirectML

       #if SAMPLAR_HAS_DIRECTML
        if (useGpu)
        {
            so.DisableMemPattern();                        // required by DirectML
            const OrtDmlApi* dml = nullptr;
            Ort::ThrowOnError (Ort::GetApi().GetExecutionProviderApi ("DML", ORT_API_VERSION,
                                                                      reinterpret_cast<const void**> (&dml)));
            Ort::ThrowOnError (dml->SessionOptionsAppendExecutionProvider_DML (so, gpuAdapterIndex_));
        }
       #else
        juce::ignoreUnused (useGpu);
       #endif

        juce::Logger::writeToLog ("[ONNX]   physical=" + juce::String (physical) + " intra=" + juce::String (intra)
                                  + " inter=1 spinning=off pool=below-normal mode=SEQUENTIAL gopt=ALL"
                                  + (useGpu ? " +DirectML adapter " + juce::String (gpuAdapterIndex_) : juce::String()));

        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        // Detached + cancellable (review S-M3): an unload during the 3-5 s model load returns
        // at once; the abandoned session is finished and released on its own pinned thread.
        auto created = createOrtSessionCancellable (env_, modelFile_, std::move (so), creationCancel_,
                                                    useGpu ? "htdemucs DirectML" : "htdemucs CPU");
        if (created.cancelled)
        {
            cancelled_ = true;
            lastError_ = "Cancelled by user";
            juce::Logger::writeToLog ("[ONNX]   Decision: session creation cancelled after "
                                      + juce::String (created.ms, 0) + " ms (no GPU fallback, nothing disabled)");
            return false;
        }
        if (created.session == nullptr)
            throw std::runtime_error (created.error.isNotEmpty() ? created.error.toStdString()
                                                                 : std::string ("session creation failed"));
        session_ = std::move (created.session);
        activeEP_ = useGpu ? "DirectML (" + gpuName_ + ")" : juce::String ("CPU");

        const size_t numIn  = session_->GetInputCount();
        const size_t numOut = session_->GetOutputCount();
        juce::Logger::writeToLog ("[ONNX]   session ready in " + juce::String ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, 2)
                                  + " s: provider=" + activeEP_ + " inputs=" + juce::String ((int) numIn)
                                  + " outputs=" + juce::String ((int) numOut));

        Ort::AllocatorWithDefaultOptions discoverAlloc;
        for (size_t i = 0; i < numOut; ++i)
        {
            auto nameHolder = session_->GetOutputNameAllocated (i, discoverAlloc);
            auto shape = session_->GetOutputTypeInfo (i).GetTensorTypeAndShapeInfo().GetShape();
            juce::String s = "[";
            for (size_t k = 0; k < shape.size(); ++k)
                s << (k > 0 ? "," : "") << juce::String ((juce::int64) shape[k]);
            juce::Logger::writeToLog ("[ONNX]   output[" + juce::String ((int) i) + "] name="
                                      + juce::String (nameHolder.get()) + " shape=" + s + "]");
        }
        juce::Logger::writeToLog ("[ONNX] === createSession END (ok) ===");
        return true;
    }
    catch (const std::exception& e)
    {
        session_.reset();
        activeEP_ = "CPU";
        lastError_ = juce::String ("ONNX session init failed (") + (useGpu ? "DirectML" : "CPU") + "): " + e.what();
        juce::Logger::writeToLog ("[ONNX] FAILED: " + lastError_);
        return false;
    }
}

OnnxSeparationProvider::~OnnxSeparationProvider()
{
    juce::Logger::writeToLog ("[ONNX] OnnxSeparationProvider dtor (provider=" + activeEP_ + ")");
}

// ----------------------------------------------------------------------------
bool OnnxSeparationProvider::loadAudioPadded (const juce::File& input, int padOffset,
                                              std::vector<float>& padL, std::vector<float>& padR,
                                              int& lengthOut, juce::String& error,
                                              const std::function<bool()>& cancelPoll)
{
    juce::Logger::writeToLog ("[ONNX] [load] === loadAudioPadded START === " + input.getFullPathName());
    lengthOut = 0;

    juce::AudioFormatManager fmt;
    fmt.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (fmt.createReaderFor (input));
    if (reader == nullptr)
    {
        error = "Failed to read input audio (unsupported format?): " + input.getFullPathName();
        juce::Logger::writeToLog ("[ONNX] [load] FAILED: " + error);
        return false;
    }

    const double srcSr = reader->sampleRate;
    const juce::int64 srcLen64 = reader->lengthInSamples;
    const int srcCh = (int) reader->numChannels;
    if (srcLen64 <= 0 || srcCh <= 0 || srcSr <= 0.0)
    {
        error = "Input audio is empty";
        juce::Logger::writeToLog ("[ONNX] [load] FAILED: " + error);
        return false;
    }

    // Duration guard BEFORE any large allocation (audit C3).
    const double durationSec = (double) srcLen64 / srcSr;
    juce::Logger::writeToLog ("[ONNX] [load] " + input.getFileName()
                              + " sr=" + juce::String (srcSr) + " ch=" + juce::String (srcCh)
                              + " len=" + juce::String (srcLen64) + " (" + juce::String (durationSec, 1) + " s)");
    if (durationSec > kMaxDurationSeconds)
    {
        error = "This file is too long for stem separation (" + juce::String (durationSec / 60.0, 1)
              + " min). The maximum is " + juce::String ((int) (kMaxDurationSeconds / 60.0))
              + " min - trim the song and try again.";
        juce::Logger::writeToLog ("[ONNX] [load] Decision: refused by duration guard: " + error);
        return false;
    }

    const int srcLen = (int) srcLen64;
    const bool resample = std::abs (srcSr - (double) kSampleRate) > 0.5;
    const int length = resample ? (int) std::round (srcLen * ((double) kSampleRate / srcSr)) : srcLen;
    if (length < 2)
    {
        error = "Audio too short";
        juce::Logger::writeToLog ("[ONNX] [load] FAILED: " + error);
        return false;
    }

    const size_t padded = (size_t) length + 2 * (size_t) padOffset;
    juce::Logger::writeToLog ("[ONNX] [load] allocating padded buffers: 2 x " + juce::String ((juce::int64) padded)
                              + " floats (" + juce::String ((double) padded * 8.0 / 1048576.0, 1) + " MB)");
    padL.assign (padded, 0.0f);
    padR.assign (padded, 0.0f);
    float* dstL = padL.data() + padOffset;
    float* dstR = padR.data() + padOffset;

    // Decode in blocks (no full-length source copy). Same channel handling as before:
    // mono is duplicated, > 2 channels use channels 0/1.
    constexpr int kBlock = 1 << 18;
    juce::AudioBuffer<float> block (juce::jmax (2, srcCh), kBlock);
    auto cancelled = [&]
    {
        if (! (cancelPoll && cancelPoll()))
            return false;
        error = "Cancelled by user";
        juce::Logger::writeToLog ("[ONNX] [load] Decision: cancelled while decoding");
        return true;
    };

    if (! resample)
    {
        for (int pos = 0; pos < srcLen; pos += kBlock)
        {
            if (cancelled())
                return false;
            const int n = juce::jmin (kBlock, srcLen - pos);
            block.clear();
            reader->read (&block, 0, n, pos, true, srcCh > 1);
            if (srcCh == 1)
                block.copyFrom (1, 0, block, 0, 0, n);
            std::memcpy (dstL + pos, block.getReadPointer (0), sizeof (float) * (size_t) n);
            std::memcpy (dstR + pos, block.getReadPointer (1), sizeof (float) * (size_t) n);
        }
    }
    else
    {
        // Linear interpolation, streamed over SEQUENTIAL source blocks (identical maths to the
        // previous version when upsampling). Downsampling (> 44.1 kHz, audit S-M6) first runs an
        // elliptic low-pass (passband to ~20.6 kHz, >= 90 dB down at 22.05 kHz) over the source
        // so content between 22.05 kHz and the source Nyquist does not alias into the stems.
        const double step = (double) (srcLen - 1) / juce::jmax (1, length - 1);
        juce::OwnedArray<juce::dsp::IIR::Filter<double>> aaL, aaR;
        if (srcSr > (double) kSampleRate + 0.5)
        {
            const double transitionHz = 1500.0;
            const double cutoffHz = (double) kSampleRate * 0.5 - transitionHz * 0.5;
            auto coeffs = juce::dsp::FilterDesign<double>::designIIRLowpassHighOrderEllipticMethod (
                cutoffHz, srcSr, transitionHz / srcSr, -0.05, -90.0);
            for (auto* c : coeffs)
            {
                aaL.add (new juce::dsp::IIR::Filter<double> (c));
                aaR.add (new juce::dsp::IIR::Filter<double> (c));
            }
            if (coeffs.isEmpty())
            {
                // Never silently skip it (S-M6): refuse rather than produce aliased stems.
                error = "Internal error: anti-alias filter design failed for " + juce::String (srcSr, 0) + " Hz input";
                juce::Logger::writeToLog ("[ONNX] [load] FAILED: " + error);
                return false;
            }
            juce::Logger::writeToLog ("[ONNX] [load] anti-alias low-pass: elliptic, " + juce::String (coeffs.size())
                                      + " biquad sections, cutoff " + juce::String (cutoffHz, 0) + " Hz @ " + juce::String (srcSr, 0));
        }
        auto filterInPlace = [] (juce::OwnedArray<juce::dsp::IIR::Filter<double>>& chain, float* x, int n)
        {
            for (int k = 0; k < n; ++k)
            {
                double v = (double) x[k];
                for (auto* f : chain)
                    v = f->processSample (v);
                x[k] = (float) v;
            }
        };

        float prevL = 0.0f, prevR = 0.0f;   // source sample (pos - 1) from the previous block
        int outI = 0;
        for (int pos = 0; pos < srcLen && outI < length; pos += kBlock)
        {
            if (cancelled())
                return false;
            const int n = juce::jmin (kBlock, srcLen - pos);
            block.clear();
            reader->read (&block, 0, n, pos, true, srcCh > 1);
            if (srcCh == 1)
                block.copyFrom (1, 0, block, 0, 0, n);
            if (! aaL.isEmpty())
            {
                filterInPlace (aaL, block.getWritePointer (0), n);
                filterInPlace (aaR, block.getWritePointer (1), n);
            }
            const float* L = block.getReadPointer (0);
            const float* R = block.getReadPointer (1);
            auto sampleL = [&] (int idx) { return idx < pos ? prevL : L[idx - pos]; };
            auto sampleR = [&] (int idx) { return idx < pos ? prevR : R[idx - pos]; };
            for (; outI < length; ++outI)
            {
                const double srcIdx = outI * step;
                const int idx0 = (int) srcIdx;
                const int idx1 = juce::jmin (srcLen - 1, idx0 + 1);
                if (idx1 >= pos + n)
                    break;                       // needs the next block
                const float frac = (float) (srcIdx - idx0);
                const float l0 = sampleL (idx0), l1 = sampleL (idx1);
                const float r0 = sampleR (idx0), r1 = sampleR (idx1);
                dstL[outI] = l0 + (l1 - l0) * frac;
                dstR[outI] = r0 + (r1 - r0) * frac;
            }
            prevL = L[n - 1];
            prevR = R[n - 1];
        }
        juce::Logger::writeToLog ("[ONNX] [load] resampled " + juce::String (srcSr)
                                  + " -> 44100 (" + juce::String (srcLen) + " -> " + juce::String (length) + " samples)");
    }

    lengthOut = length;
    juce::Logger::writeToLog ("[ONNX] [load] === loadAudioPadded END === length=" + juce::String (length));
    return true;
}

// ----------------------------------------------------------------------------
bool OnnxSeparationProvider::separate (const juce::File& inputAudio,
                                       const juce::File& outputDir,
                                       bool drumsOnly,
                                       ProgressFn progress,
                                       std::function<bool()> shouldCancel,
                                       const juce::String& outputBaseName)
{
    const juce::String base = outputBaseName.isNotEmpty() ? outputBaseName : inputAudio.getFileNameWithoutExtension();
    juce::Logger::writeToLog ("[ONNX] === separate START ===");
    juce::Logger::writeToLog ("[ONNX]   input: "  + inputAudio.getFullPathName());
    juce::Logger::writeToLog ("[ONNX]   output: " + outputDir.getFullPathName());
    juce::Logger::writeToLog ("[ONNX]   drumsOnly=" + juce::String ((int) drumsOnly));

    lastStems_ = {};
    cancelled_ = false;

    // Every allocation in here is inside this try: a std::bad_alloc (very long file, low RAM)
    // becomes an error message instead of std::terminate() in the host (audit C3).
    bool ok = false;
    try
    {
        ok = separateImpl (inputAudio, outputDir, drumsOnly, progress, shouldCancel, base);
    }
    catch (const std::bad_alloc&)
    {
        lastError_ = "Not enough memory to separate this song. Close other programs or use a shorter file.";
        juce::Logger::writeToLog ("[ONNX] FAILED (std::bad_alloc): " + lastError_);
        ok = false;
    }
    catch (const std::exception& e)
    {
        lastError_ = juce::String ("Stem separation failed: ") + e.what();
        juce::Logger::writeToLog ("[ONNX] FAILED (exception): " + lastError_);
        ok = false;
    }

    if (! ok)
    {
        // Never leave half-written stems behind.
        for (int s = 0; s < kNumStems; ++s)
        {
            const auto f = outputDir.getChildFile (stemFileName (base, (Stem) s));
            if (f.existsAsFile())
            {
                f.deleteFile();
                juce::Logger::writeToLog ("[ONNX]   removed partial stem: " + f.getFileName());
            }
        }
        lastStems_ = {};
    }

    {
        std::lock_guard<std::mutex> lk (diagMutex());
        if (ok)
            lastRunBackendStorage() = activeEP_;
    }
    juce::Logger::writeToLog ("[ONNX] === separate END === ok=" + juce::String ((int) ok)
                              + " cancelled=" + juce::String ((int) cancelled_)
                              + (ok ? juce::String() : " error=" + lastError_)
                              + " peakWorkingSet=" + juce::String ((juce::int64) (processPeakWorkingSetBytes() >> 20)) + " MB");
    return ok;
}

bool OnnxSeparationProvider::separateImpl (const juce::File& inputAudio,
                                           const juce::File& outputDir,
                                           bool drumsOnly,
                                           const ProgressFn& progress,
                                           const std::function<bool()>& shouldCancel,
                                           const juce::String& base)
{
    if (! ready_)
    {
        if (lastError_.isEmpty()) lastError_ = "ONNX provider not initialised";
        return false;
    }
    creationCancel_ = shouldCancel;   // session (re)creation below is cancellable (S-M3)
    // The GPU session is released after every run (see below), so re-create it here.
    if (session_ == nullptr)
    {
        if (gpuEligible_ && sGpuFailedThisProcess.load())
            gpuEligible_ = false;
        juce::Logger::writeToLog ("[ONNX]   Decision: no live session -> creating ("
                                  + juce::String (gpuEligible_ ? "GPU" : "CPU") + ")");
        const bool gpuOk = gpuEligible_ && createSession (true);
        if (! gpuOk && (cancelled_ || ! createSession (false)))
        {
            if (! cancelled_)
                ready_ = false;
            return false;
        }
    }
    // Release the DirectML session when this run ends (success, failure or cancel): it pins
    // the weights + the run's memory pool (~1.1 GB VRAM). Several plugin instances in one DAW
    // would otherwise hold that for the whole session. Re-creating costs a few seconds.
    struct GpuSessionReleaser
    {
        OnnxSeparationProvider& p;
        ~GpuSessionReleaser()
        {
            if (p.gpuActive() || p.gpuSkippedThisRun_)
            {
                p.session_.reset();   // after an OOM fallback: drop the CPU session so the next run tries the GPU again
                juce::Logger::writeToLog (p.gpuSkippedThisRun_ ? "[ONNX]   fallback CPU session released (next run retries the GPU)"
                                                               : "[ONNX]   GPU session released after run (VRAM freed)");
                p.gpuSkippedThisRun_ = false;
            }
        }
    } gpuSessionReleaser { *this };

    if (! inputAudio.existsAsFile())
    {
        lastError_ = "Input audio not found: " + inputAudio.getFullPathName();
        return false;
    }
    if (! outputDir.createDirectory())
    {
        lastError_ = "Cannot create stem output folder: " + outputDir.getFullPathName();
        return false;
    }

    ScopedLowThreadPriority lowPriority;

    // Cancel plumbing first: the watcher also covers decode + the first ONNX Run.
    runOptions_ = std::make_unique<Ort::RunOptions>();
    CancelWatcher cancelWatcher (shouldCancel, *runOptions_);
    auto cancelledNow = [&] { return cancelWatcher.fired() || (shouldCancel && shouldCancel()); };

    if (progress) progress (0.02f, "Loading audio");

    // ------------------------------------------------------------------------
    // STEP 1+2 - decode straight into the symmetric zero pad (max_shift both sides).
    // ------------------------------------------------------------------------
    std::vector<float> padL, padR;
    int length = 0;
    if (! loadAudioPadded (inputAudio, kMaxShiftSamples, padL, padR, length, lastError_, cancelledNow))
    {
        cancelled_ = cancelledNow();
        return false;
    }

    const double audioDuration = length / (double) kSampleRate;
    juce::Logger::writeToLog ("[ONNX]   loaded " + juce::String (length)
                              + " samples (" + juce::String (audioDuration, 2) + " s)");

    // ------------------------------------------------------------------------
    // Per-track normalization (sevagh model_apply.cpp:73-77), streamed (no ref buffer):
    //   ref_mean_0[i] = (L[i] + R[i]) / 2,  ref_mean = mean(ref_mean_0)
    //   ref_std = sqrt( sum((ref_mean_0 - ref_mean)^2) / (N - 1) )     (ddof=1)
    //   audio'  = (audio - ref_mean) / ref_std
    // ------------------------------------------------------------------------
    // Audit S-M1: NaN/Inf input (broken decoder / corrupt file) would pass every guard below
    // and turn all 4 stems into NaN that then get cached. Zero non-finite samples first.
    {
        juce::int64 nonFinite = 0;
        float* l = padL.data() + kMaxShiftSamples;
        float* r = padR.data() + kMaxShiftSamples;
        for (int i = 0; i < length; ++i)
        {
            if (! std::isfinite (l[i])) { l[i] = 0.0f; ++nonFinite; }
            if (! std::isfinite (r[i])) { r[i] = 0.0f; ++nonFinite; }
        }
        if (nonFinite > 0)
            juce::Logger::writeToLog ("[ONNX]   [sanitise] zeroed " + juce::String (nonFinite)
                                      + " non-finite input samples (NaN/Inf)");
    }

    const float* srcL = padL.data() + kMaxShiftSamples;
    const float* srcR = padR.data() + kMaxShiftSamples;
    double sum = 0.0;
    for (int i = 0; i < length; ++i)
        sum += 0.5 * ((double) srcL[i] + (double) srcR[i]);
    const double refMean = sum / (double) length;

    double sqSum = 0.0;
    for (int i = 0; i < length; ++i)
    {
        const double d = 0.5 * ((double) srcL[i] + (double) srcR[i]) - refMean;
        sqSum += d * d;
    }
    const int denom = (length > 1) ? (length - 1) : 1; // ddof=1
    double refStd = std::sqrt (sqSum / (double) denom);
    // Floor (audit S-M1): the reference is the MID channel, so out-of-phase material (L = -R)
    // has refStd ~ 0 although it is loud -> a 1e8 gain and Inf stems. When the mid deviation is
    // far below the stereo level, normalise by the stereo RMS instead; silence floors at 1e-4.
    // Normal stereo material (mid std ~ stereo RMS) keeps the exact reference normalisation.
    {
        double stereoSq = 0.0;
        for (int i = 0; i < length; ++i)
        {
            const double dl = (double) srcL[i] - refMean, dr = (double) srcR[i] - refMean;
            stereoSq += 0.5 * (dl * dl + dr * dr);
        }
        const double stereoRms = std::sqrt (stereoSq / (double) denom);
        const double midStd = refStd;
        if (refStd < 0.1 * stereoRms)
            refStd = stereoRms;
        if (! (refStd >= 1.0e-4))   // also catches NaN
            refStd = 1.0e-4;
        if (refStd != midStd)
            juce::Logger::writeToLog ("[ONNX]   [norm] Decision: mid std " + juce::String (midStd, 8)
                                      + " -> refStd " + juce::String (refStd, 8) + " (stereo RMS "
                                      + juce::String (stereoRms, 8) + ": silent or out-of-phase input)");
    }

    juce::Logger::writeToLog ("[ONNX]   [norm] refMean=" + juce::String (refMean, 8)
                              + " refStd=" + juce::String (refStd, 8));

    {
        const float refMeanF = (float) refMean;
        const float invStdF  = (float) (1.0 / refStd);
        float* l = padL.data() + kMaxShiftSamples;
        float* r = padR.data() + kMaxShiftSamples;
        for (int i = 0; i < length; ++i)
        {
            l[i] = (l[i] - refMeanF) * invStdF;
            r[i] = (r[i] - refMeanF) * invStdF;
        }
    }

    // ------------------------------------------------------------------------
    // STEP 3 - random shift offset in [0, max_shift) (sevagh model_apply.cpp:88-92).
    // thread_local: two plugin instances separating at once must not share one engine.
    // ------------------------------------------------------------------------
    thread_local std::mt19937 tGen { std::random_device{}() };
    std::uniform_int_distribution<> dist (0, kMaxShiftSamples - 1);
    int shiftOffset = dist (tGen);
    // Test hook: SAMPLAR_SEPARATION_SHIFT=<0..22049> fixes the shift so runs on different
    // devices / model variants can be compared sample for sample (tools, regression checks).
    {
        const auto fixedShift = juce::SystemStats::getEnvironmentVariable ("SAMPLAR_SEPARATION_SHIFT", {});
        if (fixedShift.isNotEmpty() && fixedShift.containsOnly ("0123456789"))
        {
            shiftOffset = juce::jlimit (0, kMaxShiftSamples - 1, fixedShift.getIntValue());
            juce::Logger::writeToLog ("[ONNX]   shift fixed by SAMPLAR_SEPARATION_SHIFT");
        }
    }

    const int shiftedLength = length + kMaxShiftSamples - shiftOffset;
    const int trimStart = kMaxShiftSamples - shiftOffset;   // STEP 8 crop offset
    juce::Logger::writeToLog ("[ONNX]   shift offset = " + juce::String (shiftOffset)
                              + " shifted_length = " + juce::String (shiftedLength)
                              + " trim_start = " + juce::String (trimStart));

    // ------------------------------------------------------------------------
    // STEP 4 - triangular weight (sevagh model_apply.cpp:107-115).
    // ------------------------------------------------------------------------
    std::vector<float> weight ((size_t) kSegmentSamples, 0.0f);
    {
        const int half = kSegmentSamples / 2;
        const float peak = (float) half;
        for (int i = 0; i < half; ++i)
            weight[(size_t) i] = (float) (i + 1) / peak;
        for (int i = 0; i < kSegmentSamples - half; ++i)
            weight[(size_t) (half + i)] = weight[(size_t) (half - 1 - i)];
        if (std::abs (kTransitionPower - 1.0f) > 1.0e-6f)
            for (int i = 0; i < kSegmentSamples; ++i)
                weight[(size_t) i] = std::pow (weight[(size_t) i], kTransitionPower);
    }

    // ------------------------------------------------------------------------
    // ORT names. Full export (smank): 1 input "mix", 1 output "sources".
    // Body model: outputs kBodySpecOutput + kBodyTimeOutput, finished on the host
    // by HtdemucsIstft into the same [4, 2, segment] "sources" layout.
    // ------------------------------------------------------------------------
    Ort::AllocatorWithDefaultOptions alloc;
    const size_t numIn  = session_->GetInputCount();
    const size_t numOut = session_->GetOutputCount();
    const size_t expectedOut = bodyMode_ ? 2 : 1;
    if (numIn != 1 || numOut != expectedOut)
    {
        lastError_ = "Unexpected model contract: numIn=" + juce::String ((int) numIn)
                   + " numOut=" + juce::String ((int) numOut)
                   + " (expected 1/" + juce::String ((int) expectedOut) + " for " + modelFile_.getFileName() + ")";
        juce::Logger::writeToLog ("[ONNX] FAILED: " + lastError_);
        return false;
    }
    auto inputNameHolder  = session_->GetInputNameAllocated (0, alloc);
    auto outputNameHolder = session_->GetOutputNameAllocated (0, alloc);   // full model only
    const juce::String inputName (inputNameHolder.get());
    std::vector<juce::String> runOutputNames;
    if (bodyMode_)
        runOutputNames = { kBodySpecOutput, kBodyTimeOutput };
    else
        runOutputNames = { juce::String (outputNameHolder.get()) };
    juce::Logger::writeToLog ("[ONNX]   input=" + inputName + " outputs="
                              + juce::StringArray (runOutputNames.data(), (int) runOutputNames.size()).joinIntoString (",")
                              + " provider=" + activeEP_);

    // ------------------------------------------------------------------------
    // Output writers: 32-bit float WAV (no 16-bit clipping, audit C9), streamed.
    // Stem order: drums (0), bass (1), other (2), vocals (3).
    // ------------------------------------------------------------------------
    const int numWritten = drumsOnly ? 1 : kNumStems;
    std::array<std::unique_ptr<juce::AudioFormatWriter>, kNumStems> writers;
    std::array<juce::File, kNumStems> stemPaths;
    {
        juce::WavAudioFormat wav;
        const auto opts = juce::AudioFormatWriterOptions{}
                              .withSampleRate ((double) kSampleRate)
                              .withNumChannels (kNumChannels)
                              .withBitsPerSample (32)
                              .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
        for (int s = 0; s < numWritten; ++s)
        {
            stemPaths[(size_t) s] = outputDir.getChildFile (stemFileName (base, (Stem) s));
            stemPaths[(size_t) s].deleteFile();
            std::unique_ptr<juce::OutputStream> os (stemPaths[(size_t) s].createOutputStream());
            if (os == nullptr)
            {
                lastError_ = "Cannot open stem file for writing: " + stemPaths[(size_t) s].getFullPathName();
                juce::Logger::writeToLog ("[ONNX] FAILED: " + lastError_);
                return false;
            }
            writers[(size_t) s] = wav.createWriterFor (os, opts);
            if (writers[(size_t) s] == nullptr)
            {
                lastError_ = "Cannot create WAV writer for: " + stemPaths[(size_t) s].getFullPathName();
                juce::Logger::writeToLog ("[ONNX] FAILED: " + lastError_);
                return false;
            }
            juce::Logger::writeToLog ("[ONNX]   writer: " + stemPaths[(size_t) s].getFileName() + " (32-bit float)");
        }
    }

    // ------------------------------------------------------------------------
    // STEP 5/6 - segment loop with a ONE-segment accumulator.
    // acc[s][c][i] / accW[i] hold samples [segOff, segOff + segment) of the shifted view.
    // ------------------------------------------------------------------------
    int nChunks = 0;
    for (int off = 0; off < shiftedLength; off += kStrideSamples) ++nChunks;
    juce::Logger::writeToLog ("[ONNX]   nChunks=" + juce::String (nChunks)
                              + " segment=" + juce::String (kSegmentSamples)
                              + " stride=" + juce::String (kStrideSamples));

    std::vector<float> acc ((size_t) kNumStems * kNumChannels * kSegmentSamples, 0.0f);
    std::vector<float> accW ((size_t) kSegmentSamples, 0.0f);
    juce::AudioBuffer<float> emitBuf (kNumChannels, kStrideSamples);
    std::array<double, kNumStems> stemSumSq {};
    std::array<float, kNumStems> stemPeak {};
    auto accPtr = [&acc] (int s, int c) { return acc.data() + ((size_t) s * kNumChannels + (size_t) c) * kSegmentSamples; };

    std::array<int64_t, 3> mixShape { 1, kNumChannels, kSegmentSamples };
    std::vector<float> mixBuf ((size_t) kNumChannels * kSegmentSamples, 0.0f);

    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    double modelMs = 0.0, istftMs = 0.0;   // per-stage totals for the log

    juce::int64 nonFiniteOut = 0;
    int chunkIdx = 0;
    for (int segOff = 0; segOff < shiftedLength; segOff += kStrideSamples, ++chunkIdx)
    {
        if (cancelledNow())
        {
            cancelled_ = true;
            lastError_ = "Cancelled by user";
            juce::Logger::writeToLog ("[ONNX] === separate CANCELLED before chunk " + juce::String (chunkIdx) + " ===");
            return false;
        }

        const int chunkLength = std::min (kSegmentSamples, shiftedLength - segOff);

        // Copy chunk into mixBuf (zero-padded right). Source is the shifted view.
        std::memset (mixBuf.data(), 0, sizeof (float) * mixBuf.size());
        std::memcpy (mixBuf.data(), padL.data() + shiftOffset + segOff, sizeof (float) * (size_t) chunkLength);
        std::memcpy (mixBuf.data() + (size_t) kSegmentSamples, padR.data() + shiftOffset + segOff,
                     sizeof (float) * (size_t) chunkLength);

        Ort::Value mixTensor = Ort::Value::CreateTensor<float> (
            *memInfo_, mixBuf.data(), mixBuf.size(), mixShape.data(), mixShape.size());

        // Run the model on this chunk. A GPU failure (device removed, out of memory, driver
        // reset) rebuilds a CPU session and retries the same chunk once; earlier chunks keep
        // their GPU results (GPU vs CPU output differs by < -60 dB).
        const float* sources = nullptr;
        std::vector<Ort::Value> outputs;
        for (int attempt = 0; ; ++attempt)
        {
            try
            {
                const char* inNames[] = { inputName.toRawUTF8() };
                std::vector<const char*> outNames;
                for (const auto& n : runOutputNames) outNames.push_back (n.toRawUTF8());

                const auto r0 = juce::Time::getMillisecondCounterHiRes();
                outputs = session_->Run (*runOptions_, inNames, &mixTensor, 1, outNames.data(), outNames.size());
                const auto r1 = juce::Time::getMillisecondCounterHiRes();
                modelMs += r1 - r0;

                if (outputs.size() != outNames.size() || ! outputs[0].IsTensor())
                {
                    lastError_ = "ONNX Run returned no tensor at chunk " + juce::String (chunkIdx);
                    juce::Logger::writeToLog ("[ONNX] FAILED: " + lastError_);
                    return false;
                }

                if (bodyMode_)
                {
                    const auto specShape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
                    const auto timeCount = outputs[1].GetTensorTypeAndShapeInfo().GetElementCount();
                    if (specShape.size() != 3 || specShape[0] != kNumStems * kNumChannels
                        || specShape[1] != 2 * (4096 / 2 + 1) || timeCount != istftOut_.size())
                    {
                        lastError_ = "Unexpected body model output shape at chunk " + juce::String (chunkIdx);
                        juce::Logger::writeToLog ("[ONNX] FAILED: " + lastError_);
                        return false;
                    }
                    istft_->process (outputs[0].GetTensorData<float>(), (int) specShape[2],
                                     outputs[1].GetTensorData<float>(), istftOut_.data());
                    sources = istftOut_.data();
                    istftMs += juce::Time::getMillisecondCounterHiRes() - r1;
                }
                else
                {
                    sources = outputs[0].GetTensorData<float>();
                }
                break;
            }
            catch (const std::bad_alloc&)
            {
                throw;   // handled in separate()
            }
            catch (const std::exception& e)
            {
                if (cancelledNow())
                {
                    cancelled_ = true;
                    lastError_ = "Cancelled by user";
                    juce::Logger::writeToLog ("[ONNX] === separate CANCELLED inside chunk " + juce::String (chunkIdx)
                                              + " (Run terminated " + juce::String (juce::Time::getMillisecondCounterHiRes() - cancelWatcher.firedAtMs(), 0)
                                              + " ms after the cancel request) ===");
                    return false;
                }
                const bool wasGpu = gpuActive();
                juce::Logger::writeToLog ("[ONNX] Run failed at chunk " + juce::String (chunkIdx)
                                          + " on " + activeEP_ + ": " + e.what());
                if (wasGpu && attempt == 0)
                {
                    juce::Logger::writeToLog ("[ONNX]   Decision: GPU failed -> rebuilding CPU session, retrying chunk "
                                              + juce::String (chunkIdx));
                    // Out of VRAM (another app / plugin instance holds it) is transient: CPU for the
                    // rest of THIS run only, GPU again next run. Anything else (driver reset, device
                    // removed, unsupported op) disables the GPU for the process.
                    const juce::String what (e.what());
                    const bool outOfMemory = what.containsIgnoreCase ("memory") || what.containsIgnoreCase ("8007000E")
                                          || what.containsIgnoreCase ("E_OUTOFMEMORY") || what.containsIgnoreCase ("alloc");
                    if (outOfMemory)
                    {
                        gpuSkippedThisRun_ = true;
                        juce::Logger::writeToLog ("[ONNX]   Decision: GPU out of memory -> CPU for this run only (GPU stays enabled)");
                    }
                    else
                        disableGpuForProcess (juce::String ("Run failed: ") + e.what());
                    if (createSession (false))
                        continue;
                    ready_ = false;                 // no usable session: let the owner rebuild us
                    if (cancelled_)
                        return false;               // cancelled during the CPU rebuild (lastError_ says so)
                }
                lastError_ = juce::String ("ONNX Run failed at chunk ") + juce::String (chunkIdx) + ": " + e.what();
                juce::Logger::writeToLog ("[ONNX] FAILED: " + lastError_);
                return false;
            }
        }

        // sources is [4, 2, segment_samples] row-major: stem, channel, sample.
        if (chunkIdx == 0)
        {
            juce::String wmsg ("[OnnxSep] chunk 0 sources RMS per stem:");
            for (int s = 0; s < kNumStems; ++s)
            {
                const float* sL = sources + (size_t) s * (kNumChannels * kSegmentSamples);
                const float* sR = sL + kSegmentSamples;
                double ss = 0.0;
                for (int i = 0; i < chunkLength; ++i)
                    ss += (double) sL[i] * sL[i] + (double) sR[i] * sR[i];
                wmsg += " " + juce::String (std::sqrt (ss / (double) juce::jmax (1, 2 * chunkLength)), 3);
                if (s < kNumStems - 1) wmsg += ",";
            }
            juce::Logger::writeToLog (wmsg);
        }

        // Accumulate weighted output (sevagh model_apply.cpp:172-190). acc[0] == segOff.
        for (int s = 0; s < kNumStems; ++s)
            for (int c = 0; c < kNumChannels; ++c)
            {
                const float* src = sources + ((size_t) s * kNumChannels + (size_t) c) * kSegmentSamples;
                float* dst = accPtr (s, c);
                for (int i = 0; i < chunkLength; ++i)
                    dst[i] += weight[(size_t) i] * src[i];
            }
        for (int i = 0; i < chunkLength; ++i)
            accW[(size_t) i] += weight[(size_t) i];

        // Emit the samples no later chunk touches: [segOff, segOff + stride), or the rest
        // when this is the last chunk. STEP 7 (out /= sum_weight, denormalise) + STEP 8 (crop).
        const bool lastChunk = segOff + kStrideSamples >= shiftedLength;
        const int emitCount = lastChunk ? chunkLength : kStrideSamples;
        const int firstOut = juce::jmax (0, trimStart - segOff);                     // skip pre-crop
        const int endOut   = juce::jmin (emitCount, trimStart + length - segOff);    // stop at crop end
        if (endOut > firstOut)
        {
            const int n = endOut - firstOut;
            if (emitBuf.getNumSamples() < n)
                emitBuf.setSize (kNumChannels, n, false, false, true);
            for (int s = 0; s < kNumStems; ++s)   // all stems for the RMS log; written: s < numWritten
            {
                for (int c = 0; c < kNumChannels; ++c)
                {
                    const float* a = accPtr (s, c) + firstOut;
                    float* o = emitBuf.getWritePointer (c);
                    for (int i = 0; i < n; ++i)
                    {
                        const double w = (double) accW[(size_t) (firstOut + i)];
                        const double v = ((double) a[i] / (w > 1.0e-12 ? w : 1.0e-12)) * refStd + refMean;
                        if (! std::isfinite ((float) v))
                            ++nonFiniteOut;
                        o[i] = (float) v;
                        stemSumSq[(size_t) s] += v * v;
                        const float av = std::fabs ((float) v);
                        if (av > stemPeak[(size_t) s]) stemPeak[(size_t) s] = av;
                    }
                }
                if (nonFiniteOut > 0)
                {
                    // Never write (and so never cache) NaN/Inf stems (audit S-M1).
                    lastError_ = "Stem separation produced invalid audio (NaN/Inf) at chunk "
                               + juce::String (chunkIdx) + " - the stems were discarded.";
                    juce::Logger::writeToLog ("[ONNX] FAILED: " + lastError_ + " (" + juce::String (nonFiniteOut)
                                              + " non-finite samples, provider=" + activeEP_ + ")");
                    return false;
                }
                if (s < numWritten && ! writers[(size_t) s]->writeFromAudioSampleBuffer (emitBuf, 0, n))
                {
                    lastError_ = "Failed to write WAV samples to: " + stemPaths[(size_t) s].getFullPathName()
                               + " (disk full?)";
                    juce::Logger::writeToLog ("[ONNX] FAILED: " + lastError_);
                    return false;
                }
            }
        }

        // Slide the accumulator by one stride for the next chunk.
        if (! lastChunk)
        {
            const int keep = kSegmentSamples - kStrideSamples;
            for (int s = 0; s < kNumStems; ++s)
                for (int c = 0; c < kNumChannels; ++c)
                {
                    float* a = accPtr (s, c);
                    std::memmove (a, a + kStrideSamples, sizeof (float) * (size_t) keep);
                    std::memset (a + keep, 0, sizeof (float) * (size_t) kStrideSamples);
                }
            std::memmove (accW.data(), accW.data() + kStrideSamples, sizeof (float) * (size_t) keep);
            std::memset (accW.data() + keep, 0, sizeof (float) * (size_t) kStrideSamples);
        }

        const float p = 0.05f + 0.90f * ((float) (chunkIdx + 1) / (float) nChunks);
        if (progress)
            progress (p, "Separating chunk " + juce::String (chunkIdx + 1) + " / " + juce::String (nChunks));
    }

    const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - t0;
    juce::Logger::writeToLog ("[ONNX]   inference done in "
                              + juce::String (elapsedMs / 1000.0, 2) + " s (RTF="
                              + juce::String (elapsedMs / 1000.0 / audioDuration, 3) + ") provider=" + activeEP_
                              + " model=" + juce::String (modelMs / 1000.0, 2) + " s"
                              + (bodyMode_ ? " istft=" + juce::String (istftMs / 1000.0, 2) + " s" : juce::String()));

    // Per-stem RMS / peak (stem ordering sanity check: drums, bass, other, vocals).
    for (int s = 0; s < kNumStems; ++s)
        juce::Logger::writeToLog ("[OnnxSep] stem " + juce::String (s)
                                  + " RMS=" + juce::String (std::sqrt (stemSumSq[(size_t) s] / (double) juce::jmax (1, 2 * length)), 4)
                                  + " peak=" + juce::String (stemPeak[(size_t) s], 4));

    if (progress) progress (0.97f, "Finalising stems");

    // Flush + close the WAV files (writer destructors patch the headers).
    for (int s = 0; s < numWritten; ++s)
        writers[(size_t) s].reset();

    lastStems_.drums = stemPaths[0];
    if (! drumsOnly)
    {
        lastStems_.bass   = stemPaths[1];
        lastStems_.other  = stemPaths[2];
        lastStems_.vocals = stemPaths[3];
    }

    if (progress) progress (1.0f, "Done");
    juce::Logger::writeToLog ("[ONNX] === separate SUCCESS ===");
    return true;
}

} // namespace samplar
