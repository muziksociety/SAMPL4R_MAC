#include "BeatThisDetector.h"
#include "AnalysisDsp.h"

#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>

namespace samplar {

//==============================================================================
// Pure helpers (no ONNX) - always compiled, unit-testable.
//==============================================================================
namespace {

double medianOf(std::vector<double> v)
{
    if (v.empty()) return 0.0;
    const size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(mid), v.end());
    return v[mid];
}

// FNV-1a over the buffer shape + a strided sample of every channel. Two
// copies of the same stem hash identically; different songs practically never
// collide (and the shape is part of the key).
uint64_t fingerprint(const juce::AudioBuffer<float>& b, double sr)
{
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&h](const void* p, size_t n)
    {
        const auto* c = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; ++i) { h ^= c[i]; h *= 1099511628211ULL; }
    };
    const int ns = b.getNumSamples(), nc = b.getNumChannels();
    mix(&ns, sizeof(ns)); mix(&nc, sizeof(nc)); mix(&sr, sizeof(sr));
    const int stride = std::max(1, ns / 8192);
    for (int ch = 0; ch < nc; ++ch)
    {
        const float* rd = b.getReadPointer(ch);
        for (int i = 0; i < ns; i += stride) mix(&rd[i], sizeof(float));
    }
    return h;
}

struct ResultCache
{
    std::mutex lock;
    std::deque<std::pair<uint64_t, BeatThisDetector::Result>> entries; // newest at back
    static constexpr size_t kMax = 4;

    static ResultCache& get() { static ResultCache c; return c; }

    bool find(uint64_t key, BeatThisDetector::Result& out)
    {
        std::lock_guard<std::mutex> g(lock);
        for (auto& e : entries)
            if (e.first == key) { out = e.second; return true; }
        return false;
    }
    void put(uint64_t key, const BeatThisDetector::Result& r)
    {
        std::lock_guard<std::mutex> g(lock);
        for (auto it = entries.begin(); it != entries.end(); ++it)
            if (it->first == key) { entries.erase(it); break; }
        entries.emplace_back(key, r);
        while (entries.size() > kMax) entries.pop_front();
    }
    void clear() { std::lock_guard<std::mutex> g(lock); entries.clear(); }
};

} // namespace

BeatThisDetector::TempoEstimate BeatThisDetector::estimateTempo(const std::vector<double>& beats)
{
    TempoEstimate te;
    const size_t n = beats.size();
    if (n < 3) return te;

    std::vector<double> iois;
    iois.reserve(n - 1);
    for (size_t i = 1; i < n; ++i) iois.push_back(beats[i] - beats[i - 1]);

    const double medianIOI = medianOf(iois);
    if (medianIOI <= 1e-6) return te;
    te.medianBpm = 60.0 / medianIOI;
    te.bpm       = te.medianBpm;

    double period = medianIOI;
    std::vector<char> use(n, 1);      // residual-inlier mask
    std::vector<int>  seg(n, 0);
    std::vector<double> k(n, 0.0);
    bool fitted = false;

    for (int iter = 0; iter < 3; ++iter)
    {
        // (a) beat numbers relative to the current period; break segments at
        //     non-integer IOIs or very long gaps (tempo change / break).
        int s = 0; double kk = 0.0;
        seg[0] = 0; k[0] = 0.0;
        for (size_t i = 1; i < n; ++i)
        {
            const double r = iois[i - 1] / period;
            const double m = std::round(r);
            if (m >= 1.0 && m <= 8.0 && std::abs(r - m) <= 0.2)
                kk += m;
            else
            {
                ++s; kk = 0.0;
            }
            seg[i] = s; k[i] = kk;
        }

        // (b) common slope, per-segment intercepts (within-segment centring).
        const int numSeg = s + 1;
        std::vector<double> sk(static_cast<size_t>(numSeg), 0.0), st(static_cast<size_t>(numSeg), 0.0);
        std::vector<int> cnt(static_cast<size_t>(numSeg), 0);
        for (size_t i = 0; i < n; ++i)
            if (use[i]) { sk[(size_t) seg[i]] += k[i]; st[(size_t) seg[i]] += beats[i]; ++cnt[(size_t) seg[i]]; }

        double num = 0.0, den = 0.0;
        int used = 0;
        for (size_t i = 0; i < n; ++i)
        {
            const size_t sg = (size_t) seg[i];
            if (! use[i] || cnt[sg] < 2) continue;
            const double dk = k[i] - sk[sg] / cnt[sg];
            const double dt = beats[i] - st[sg] / cnt[sg];
            num += dk * dt; den += dk * dk; ++used;
        }
        if (used < 8 || den <= 1e-9) break;
        const double b = num / den;
        if (! (b > 0.2 && b < 2.5)) break;
        period = b; fitted = true; te.inliers = used;

        // (c) residual outlier rejection for the next pass.
        std::vector<double> res; res.reserve(n);
        std::vector<double> resAll(n, 0.0);
        for (size_t i = 0; i < n; ++i)
        {
            const size_t sg = (size_t) seg[i];
            if (cnt[sg] < 1) continue;
            const double a = st[sg] / std::max(1, cnt[sg]) - b * sk[sg] / std::max(1, cnt[sg]);
            resAll[i] = beats[i] - (a + b * k[i]);
            if (use[i]) res.push_back(std::abs(resAll[i]));
        }
        const double mad = medianOf(res);
        const double thr = std::max(0.025, 3.0 * 1.4826 * mad);
        for (size_t i = 0; i < n; ++i) use[i] = (std::abs(resAll[i]) <= thr) ? 1 : 0;
    }

    if (fitted) te.bpm = 60.0 / period;

    int good = 0;
    for (double v : iois)
        if (std::abs(v - period) / period <= 0.10) ++good;
    te.confidence = static_cast<double>(good) / static_cast<double>(iois.size());
    te.valid = (te.bpm > 30.0 && te.bpm < 300.0);
    return te;
}

int BeatThisDetector::estimateBeatsPerBar(const std::vector<double>& beats,
                                          const std::vector<double>& downbeats)
{
    if (beats.size() < 4 || downbeats.size() < 2) return 0;
    std::vector<double> counts;
    auto indexOf = [&beats](double t) -> int
    {
        auto it = std::lower_bound(beats.begin(), beats.end(), t - 1e-4);
        if (it == beats.end()) return -1;
        return (std::abs(*it - t) < 0.03) ? static_cast<int>(it - beats.begin()) : -1;
    };
    for (size_t i = 1; i < downbeats.size(); ++i)
    {
        const int a = indexOf(downbeats[i - 1]), b = indexOf(downbeats[i]);
        if (a >= 0 && b > a) counts.push_back(static_cast<double>(b - a));
    }
    if (counts.empty()) return 0;
    const int m = static_cast<int>(std::lround(medianOf(counts)));
    return (m >= 2 && m <= 7) ? m : 0;
}

void BeatThisDetector::clearCache() { ResultCache::get().clear(); }

bool BeatThisDetector::isHalfTimeFeel(const std::vector<float>& mono, double sampleRate,
                                      const std::vector<double>& beats,
                                      const std::vector<double>& downbeats)
{
    if (mono.empty() || sampleRate <= 0.0 || beats.size() < 16 || downbeats.size() < 4) return false;
    const TempoEstimate te = estimateTempo(beats);
    if (! te.valid || te.bpm < 120.0) return false;

    double envSR = 0.0;
    const auto env = analysis::spectralFluxEnvelope(mono.data(), static_cast<int>(mono.size()), sampleRate,
                                                    1024, 256, 300.0, 5000.0, envSR);
    if (env.empty() || envSR <= 0.0) return false;
    // flux frame f covers [f*hop, f*hop+1024): a hit at t peaks around frame (t - ~640 samples)/hop
    auto strengthAt = [&](double t) -> double
    {
        const double centre = (t * sampleRate - 640.0) / 256.0;
        const int lo = std::max(0, static_cast<int>(std::floor(centre - 0.03 * envSR)));
        const int hi = std::min(static_cast<int>(env.size()) - 1, static_cast<int>(std::ceil(centre + 0.04 * envSR)));
        float m = 0.0f;
        for (int f = lo; f <= hi; ++f) m = std::max(m, env[static_cast<size_t>(f)]);
        return m;
    };

    double sum[4] = {}; int bars = 0;
    for (size_t d = 0; d + 1 < downbeats.size(); ++d)
    {
        auto a = std::lower_bound(beats.begin(), beats.end(), downbeats[d] - 1e-4);
        auto b = std::lower_bound(beats.begin(), beats.end(), downbeats[d + 1] - 1e-4);
        if (b - a != 4) continue;
        for (int p = 0; p < 4; ++p) sum[p] += strengthAt(*(a + p));
        ++bars;
    }
    if (bars < 4) return false;
    const double s2 = sum[1] / bars, s3 = sum[2] / bars, s4 = sum[3] / bars;
    const bool half = s3 > 1.5 * std::max(s2, s4);
    juce::Logger::writeToLog("[BeatThis] half-time check over " + juce::String(bars) + " bars @ "
        + juce::String(te.bpm, 2) + " BPM: pos1=" + juce::String(sum[0] / bars, 3) + " pos2=" + juce::String(s2, 3)
        + " pos3=" + juce::String(s3, 3) + " pos4=" + juce::String(s4, 3)
        + (half ? " -> snare on 3 only: HALF-TIME (beats are 8th notes)" : " -> beat level OK"));
    return half;
}

} // namespace samplar

#if WITH_BEAT_THIS

// The vendored beat_this_cpp pieces are reused for the maths (MelSpectrogram, Postprocessor),
// but the ONNX session and the chunk loop live here: the vendored InferenceProcessor runs every
// chunk with Ort::RunOptions{nullptr}, which cannot be terminated (audit U-H1 / A4). Here each
// detect() owns a RunOptions that a watcher thread terminates on cancel, the mel spectrogram is
// computed in frame-exact slices with a cancel check between slices, and the chunking /
// aggregation replicate InferenceProcessor.cpp exactly (AnalysisTests checks bit-identical beats
// against BeatThis::process_audio).
#include "MelSpectrogram.h"
#include "Postprocessor.h"
#include "../separation/OrtRuntime.h"
#include "../../utils/DataPaths.h"

#include <onnxruntime_cxx_api.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <set>
#include <thread>

namespace samplar {

namespace {

// Must match third_party/beat_this (MelSpectrogram.h / InferenceProcessor.h).
constexpr int kModelSampleRate = 22050;
constexpr int kMelHop          = 441;
constexpr int kMelPad          = 512;   // n_fft / 2 (torch.stft center=True, reflect)
constexpr int kNumMels         = 128;
constexpr int kChunkFrames     = 1500;
constexpr int kBorderFrames    = 6;
constexpr int kMelBlockFrames  = 250;   // 5 s of frames per mel slice: ~12 ms between cancel checks
// Downmix / resample: a cancel check every 2^19 samples (~12 s of audio, a few ms of work).
constexpr size_t kCancelCheckSamples = size_t(1) << 19;
constexpr int kMelContext      = 2;     // slice frames 0..1 overlap the slice's own reflection pad

juce::File resolveModelPath()
{
    const juce::File exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                                  .getParentDirectory();
    // 1 - CMake POST_BUILD layout next to the binary
    juce::File candidate = exeDir.getChildFile("third_party/beat_this/models/beat_this.onnx");
    if (candidate.existsAsFile()) return candidate;
    // 2 - flat next to the binary
    candidate = exeDir.getChildFile("beat_this.onnx");
    if (candidate.existsAsFile()) return candidate;
    // 3 - shared models dir
    candidate = DataPaths::getModelsDirectory().getChildFile("beat_this.onnx");
    if (candidate.existsAsFile()) return candidate;
    // 4 - legacy app dir
    candidate = DataPaths::getAppDirectory().getChildFile("third_party/beat_this/models/beat_this.onnx");
    if (candidate.existsAsFile()) return candidate;
    // 5 - source tree (dev)
    return juce::File::getCurrentWorkingDirectory()
               .getChildFile("third_party/beat_this/models/beat_this.onnx");
}

//==============================================================================
// Process-wide engine. Held by weak_ptr globally and shared_ptr by each live
// detector: the session is created once while any detector is alive and is
// destroyed on the owning worker thread (never during DLL static teardown).
// Inference is serialised (one ~83 MB working set at a time).
//==============================================================================
struct SharedEngine
{
    std::shared_ptr<Ort::Env>     env;      // shared with a detached creator (S-M3)
    std::unique_ptr<Ort::Session> session;
    std::mutex runMutex;
};

// `cancel` (optional) is polled while the ONNX session is created (~2 s): an unload stops
// waiting at once and the session is finished + released on a detached, module-pinned thread
// (review S-M3, createOrtSessionCancellable). cancelledOut reports that case.
std::shared_ptr<SharedEngine> acquireEngine(const AnalysisCancel& cancel = {}, bool* cancelledOut = nullptr)
{
    if (cancelledOut != nullptr) *cancelledOut = false;
    static std::mutex                  gLock;
    static std::weak_ptr<SharedEngine> gWeak;
    static bool                        gModelMissing = false;     // permanent: reinstall needed
    static double                      gLastFailMs   = -1.0e12;   // session error: retry later (U-M9)
    constexpr double kRetryAfterFailMs = 60000.0;

    std::lock_guard<std::mutex> g(gLock);
    if (auto existing = gWeak.lock()) return existing;
    if (gModelMissing) return nullptr;
    if (! ensureOrtApi())
    {
        juce::Logger::writeToLog("[BeatThis] ONNX Runtime unavailable (" + ortUnavailableReason()
            + ") - BeatThis disabled, DSP tempo fallback only");
        return nullptr;
    }
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (now - gLastFailMs < kRetryAfterFailMs)
    {
        juce::Logger::writeToLog("[BeatThis] session init failed "
            + juce::String((now - gLastFailMs) / 1000.0, 0) + " s ago - not retrying yet");
        return nullptr;
    }

    try
    {
        const juce::File modelFile = resolveModelPath();
        if (! modelFile.existsAsFile())
        {
            juce::Logger::writeToLog("[BeatThis] Model file not found: " + modelFile.getFullPathName());
            gModelMissing = true;
            return nullptr;
        }
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto e = std::make_shared<SharedEngine>();
        e->env = std::make_shared<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "samplar-beatthis");
        Ort::SessionOptions so;   // defaults, exactly as the vendored beat_this_api.cpp
        std::function<bool()> cancelFn;
        if (cancel.canCancel())
            cancelFn = [cancel] { return cancel.requested(); };
        auto created = createOrtSessionCancellable(e->env, modelFile, std::move(so), cancelFn, "BeatThis");
        if (created.cancelled)
        {
            // Not a failure: no retry back-off, the next detect() creates it again.
            juce::Logger::writeToLog("[BeatThis] session creation cancelled after " + juce::String(created.ms, 0) + " ms");
            if (cancelledOut != nullptr) *cancelledOut = true;
            return nullptr;
        }
        if (created.session == nullptr)
            throw std::runtime_error(created.error.isNotEmpty() ? created.error.toStdString()
                                                                : std::string("session creation failed"));
        e->session = std::move(created.session);
        gWeak = e;
        juce::Logger::writeToLog("[BeatThis] ONNX session initialised from " + modelFile.getFullPathName()
            + " in " + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 0) + " ms");
        return e;
    }
    catch (const std::exception& ex)
    {
        juce::Logger::writeToLog(juce::String("[BeatThis] ONNX init failed: ") + ex.what());
    }
    catch (...)
    {
        juce::Logger::writeToLog("[BeatThis] Unknown ONNX initialisation error");
    }
    gLastFailMs = now;
    return nullptr;
}

// Terminates the running ONNX chunk when the cancel token fires (same idea as the separation
// CancelWatcher). Stops promptly on destruction (condition variable, no sleep to wait out).
class TerminateWatcher
{
public:
    TerminateWatcher(AnalysisCancel cancel, Ort::RunOptions& runOptions)
    {
        if (! cancel.canCancel())
            return;
        thread_ = std::thread([this, cancel, &runOptions]
        {
            std::unique_lock<std::mutex> lk(m_);
            while (! stop_)
            {
                if (cancel.requested())
                {
                    firedAtMs_ = juce::Time::getMillisecondCounterHiRes();
                    fired_ = true;
                    runOptions.SetTerminate();
                    juce::Logger::writeToLog("[BeatThis]   TerminateWatcher: cancel -> RunOptions::SetTerminate()");
                    return;
                }
                cv_.wait_for(lk, std::chrono::milliseconds(10));
            }
        });
    }

    ~TerminateWatcher()
    {
        {
            std::lock_guard<std::mutex> lk(m_);
            stop_ = true;
        }
        cv_.notify_all();
        if (thread_.joinable())
            thread_.join();
    }

    bool fired() const noexcept { return fired_.load(); }
    double firedAtMs() const noexcept { return firedAtMs_.load(); }

private:
    std::thread thread_;
    std::mutex m_;
    std::condition_variable cv_;
    bool stop_ = false;
    std::atomic<bool> fired_ { false };
    std::atomic<double> firedAtMs_ { 0.0 };
};

enum class RunStatus { Ok, Cancelled, Failed };

struct RawBeats
{
    std::vector<float> beats, downbeats;
    std::vector<int>   beatCounts;
};

// Identical maths to the vendored beat_this_api.cpp resample_audio (linear, accumulated pos).
// The cancel check every kCancelCheckSamples outputs does not change the arithmetic.
std::vector<float> resampleTo22050(const std::vector<float>& in, int inRate,
                                   const AnalysisCancel& cancel, bool& cancelled)
{
    cancelled = false;
    if (inRate == kModelSampleRate) return in;
    const double ratio = static_cast<double>(kModelSampleRate) / static_cast<double>(inRate);
    const size_t outSize = static_cast<size_t>(in.size() * ratio);
    std::vector<float> out(outSize);
    const double step = static_cast<double>(inRate) / static_cast<double>(kModelSampleRate);
    double pos = 0.0;
    const size_t inMax = in.size() - 1;
    for (size_t i = 0; i < outSize; ++i)
    {
        if ((i & (kCancelCheckSamples - 1)) == 0 && cancel.requested()) { cancelled = true; return {}; }
        const size_t idx = static_cast<size_t>(pos);
        const double frac = pos - static_cast<double>(idx);
        const float a = in[std::min(idx, inMax)];
        const float b = in[std::min(idx + 1, inMax)];
        out[i] = static_cast<float>(a + (b - a) * frac);
        pos += step;
    }
    return out;
}

// MelSpectrogram::compute() over the whole song, in slices. Frame f of the full computation
// covers audio[f*hop - pad, f*hop + pad) (reflect-padded at the song edges). A slice that starts
// at sample a*hop reproduces frames a+2.. exactly (its own reflection pad only reaches frames 0..1),
// and a slice that starts at 0 / ends at the song end reflects exactly like the full call, so
// the result is bit-identical to one compute() call while a cancel is seen every block.
std::vector<std::vector<float>> melSpectrogramSliced(const std::vector<float>& audio,
                                                     const AnalysisCancel& cancel, bool& cancelled)
{
    cancelled = false;
    std::vector<std::vector<float>> out;
    const int64_t n = static_cast<int64_t>(audio.size());
    if (n <= 2 * kMelPad) return out;   // the vendored reflect pad needs > n_fft/2 samples
    const int numFrames = static_cast<int>(n / kMelHop) + 1;
    out.reserve(static_cast<size_t>(numFrames));

    MelSpectrogram mel;
    for (int f0 = 0; f0 < numFrames; f0 += kMelBlockFrames)
    {
        if (cancel.requested()) { cancelled = true; return {}; }
        const int f1 = std::min(numFrames, f0 + kMelBlockFrames);
        const int a  = std::max(0, f0 - kMelContext);
        const int64_t sliceStart = static_cast<int64_t>(a) * kMelHop;
        int64_t sliceEnd = static_cast<int64_t>(f1 - 1) * kMelHop + kMelPad;
        if (f1 == numFrames || sliceEnd >= n) sliceEnd = n;   // song end: same reflection as the full call
        std::vector<float> slice(audio.begin() + sliceStart, audio.begin() + sliceEnd);
        auto m = mel.compute(slice);
        for (int f = f0; f < f1; ++f)
        {
            const size_t g = static_cast<size_t>(f - a);
            if (g >= m.size()) return {};   // cannot happen (see bounds above); fail safe
            out.push_back(std::move(m[g]));
        }
    }
    return out;
}

// Same as the vendored calculate_beat_counts (beat_this_api.cpp), without the stderr output.
std::vector<int> beatCountsFor(const std::vector<float>& beats, const std::vector<float>& downbeats)
{
    std::vector<int> counts(beats.size());
    if (beats.empty() || downbeats.empty()) return counts;
    const std::set<float> beatSet(beats.begin(), beats.end());
    for (float d : downbeats)
        if (beatSet.find(d) == beatSet.end()) return counts;

    int startCounter = 1;
    if (downbeats.size() >= 2)
    {
        const auto i0 = std::lower_bound(beats.begin(), beats.end(), downbeats[0]) - beats.begin();
        const auto i1 = std::lower_bound(beats.begin(), beats.end(), downbeats[1]) - beats.begin();
        const auto inFirstBar = i1 - i0;
        const auto pickup = i0;
        startCounter = (pickup < inFirstBar) ? static_cast<int>(inFirstBar - pickup) : 1;
    }
    int counter = startCounter;
    size_t di = 0;
    float next = downbeats[0];
    for (size_t i = 0; i < beats.size(); ++i)
    {
        if (di < downbeats.size() && std::abs(beats[i] - next) < 1e-6f)
        {
            counter = 1;
            ++di;
            next = (di < downbeats.size()) ? downbeats[di] : -1.0f;
        }
        else
            ++counter;
        counts[i] = counter;
    }
    return counts;
}

// The vendored InferenceProcessor::process_spectrogram (split_piece + aggregate_prediction,
// "keep_first"), with a terminable RunOptions and a cancel check per chunk.
RunStatus runChunks(Ort::Session& session, const std::vector<std::vector<float>>& spect,
                    const AnalysisCancel& cancel, std::vector<float>& beatOut, std::vector<float>& downOut,
                    int& numChunks)
{
    const int len = static_cast<int>(spect.size());
    std::vector<int> starts;
    for (int start = -kBorderFrames; start < len - kBorderFrames; start += (kChunkFrames - 2 * kBorderFrames))
        starts.push_back(start);
    if (starts.empty()) return RunStatus::Failed;
    if (len > kChunkFrames - 2 * kBorderFrames)
        starts.back() = len - (kChunkFrames - kBorderFrames);
    numChunks = static_cast<int>(starts.size());

    Ort::RunOptions runOptions;
    TerminateWatcher watcher(cancel, runOptions);
    const Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    const char* inNames[]  = { "input_spectrogram" };
    const char* outNames[] = { "beat", "downbeat" };

    std::vector<std::pair<std::vector<float>, std::vector<float>>> preds;
    preds.reserve(starts.size());
    std::vector<float> input;
    for (const int start : starts)
    {
        if (cancel.requested()) return RunStatus::Cancelled;
        const int actualStart = std::max(0, start);
        const int actualEnd   = std::min(start + kChunkFrames, len);
        const int body        = std::max(0, actualEnd - actualStart);
        const int leftPad     = std::max(0, -start);
        const int rightPad    = std::max(0, std::min(kBorderFrames, start + kChunkFrames - len));
        const int rows        = body + leftPad + rightPad;
        input.assign(static_cast<size_t>(rows) * kNumMels, 0.0f);
        for (int r = 0; r < body; ++r)
            std::memcpy(input.data() + static_cast<size_t>(leftPad + r) * kNumMels,
                        spect[static_cast<size_t>(actualStart + r)].data(), sizeof(float) * kNumMels);
        const int64_t shape[] = { 1, rows, kNumMels };
        Ort::Value tensor = Ort::Value::CreateTensor<float>(memInfo, input.data(), input.size(), shape, 3);

        std::vector<Ort::Value> outs;
        try
        {
            outs = session.Run(runOptions, inNames, &tensor, 1, outNames, 2);
        }
        catch (const Ort::Exception& e)
        {
            if (watcher.fired() || cancel.requested())
            {
                juce::Logger::writeToLog("[BeatThis]   Run terminated "
                    + juce::String(juce::Time::getMillisecondCounterHiRes() - watcher.firedAtMs(), 0)
                    + " ms after the cancel request");
                return RunStatus::Cancelled;
            }
            juce::Logger::writeToLog(juce::String("[BeatThis]   Run failed: ") + e.what());
            return RunStatus::Failed;
        }
        const auto oshape = outs[0].GetTensorTypeAndShapeInfo().GetShape();
        const size_t count = static_cast<size_t>(oshape[0] * oshape[1]);
        const float* b = outs[0].GetTensorData<float>();
        const float* d = outs[1].GetTensorData<float>();
        preds.emplace_back(std::vector<float>(b, b + count), std::vector<float>(d, d + count));
    }

    beatOut.assign(static_cast<size_t>(len), -1000.0f);
    downOut.assign(static_cast<size_t>(len), -1000.0f);
    for (int i = static_cast<int>(preds.size()) - 1; i >= 0; --i)   // keep_first: reverse order
    {
        const auto& bc = preds[static_cast<size_t>(i)].first;
        const auto& dc = preds[static_cast<size_t>(i)].second;
        const int start = starts[static_cast<size_t>(i)];
        const int size  = static_cast<int>(bc.size());
        const int j0 = (size < 2 * kBorderFrames) ? 0 : kBorderFrames;
        const int j1 = (size < 2 * kBorderFrames) ? size : size - kBorderFrames;
        for (int j = j0; j < j1; ++j)
        {
            const int t = start + j;
            if (t >= 0 && t < len)
            {
                beatOut[static_cast<size_t>(t)] = bc[static_cast<size_t>(j)];
                downOut[static_cast<size_t>(t)] = dc[static_cast<size_t>(j)];
            }
        }
    }
    return RunStatus::Ok;
}

RunStatus runInference(SharedEngine& se, const std::vector<float>& mono, int sampleRate,
                       const AnalysisCancel& cancel, RawBeats& out)
{
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    bool resampleCancelled = false;
    const std::vector<float> audio = resampleTo22050(mono, sampleRate, cancel, resampleCancelled);
    if (resampleCancelled || cancel.requested()) return RunStatus::Cancelled;
    const double t1 = juce::Time::getMillisecondCounterHiRes();

    bool melCancelled = false;
    const auto spect = melSpectrogramSliced(audio, cancel, melCancelled);
    if (melCancelled) return RunStatus::Cancelled;
    if (spect.empty()) return RunStatus::Failed;
    const double t2 = juce::Time::getMillisecondCounterHiRes();

    std::vector<float> beatLogits, downLogits;
    int numChunks = 0;
    const RunStatus st = runChunks(*se.session, spect, cancel, beatLogits, downLogits, numChunks);
    if (st != RunStatus::Ok) return st;
    if (cancel.requested()) return RunStatus::Cancelled;
    const double t3 = juce::Time::getMillisecondCounterHiRes();

    Postprocessor post;
    auto times = post.process(beatLogits, downLogits);
    out.beats      = std::move(times.first);
    out.downbeats  = std::move(times.second);
    out.beatCounts = beatCountsFor(out.beats, out.downbeats);
    juce::Logger::writeToLog("[BeatThis] stages: resample " + juce::String(t1 - t0, 0) + " ms, mel "
        + juce::String(t2 - t1, 0) + " ms (" + juce::String((int) spect.size()) + " frames), onnx "
        + juce::String(t3 - t2, 0) + " ms (" + juce::String(numChunks) + " chunks), post "
        + juce::String(juce::Time::getMillisecondCounterHiRes() - t3, 0) + " ms");
    return RunStatus::Ok;
}

BeatThisDetector::Result cancelledResult(const juce::String& where)
{
    BeatThisDetector::Result r;
    r.cancelled = true;
    juce::Logger::writeToLog("[BeatThis] CANCELLED " + where);
    return r;
}

} // namespace

struct BeatThisDetector::Impl
{
    std::shared_ptr<SharedEngine> shared;
    bool triedInit = false;

    SharedEngine* engine(const AnalysisCancel& cancel = {})
    {
        if (! triedInit)
        {
            bool cancelled = false;
            shared = acquireEngine(cancel, &cancelled);
            triedInit = ! cancelled;   // a cancelled creation is retried by the next call
        }
        return shared.get();
    }
};

BeatThisDetector::BeatThisDetector() : impl_(std::make_unique<Impl>()) {}
BeatThisDetector::~BeatThisDetector() = default;

bool BeatThisDetector::isAvailable() { return impl_->engine() != nullptr; }

BeatThisDetector::Result BeatThisDetector::detect(const juce::AudioBuffer<float>& drumStem,
                                                  double sampleRate,
                                                  AnalysisCancel cancel)
{
    Result out;
    if (drumStem.getNumSamples() <= 0 || drumStem.getNumChannels() <= 0 || sampleRate <= 0.0)
    {
        juce::Logger::writeToLog("[BeatThis] Invalid input buffer/sampleRate");
        return out;
    }
    if (cancel.requested()) return cancelledResult("before start");

    const uint64_t key = fingerprint(drumStem, sampleRate);
    if (ResultCache::get().find(key, out))
    {
        out.fromCache = true;
        juce::Logger::writeToLog("[BeatThis] cache HIT (" + juce::String((int) out.beatTimesSec.size())
            + " beats, " + juce::String((int) out.downbeatTimesSec.size()) + " downbeats, "
            + juce::String(out.bpm, 3) + " BPM) - inference skipped");
        return out;
    }

    SharedEngine* se = impl_->engine(cancel);
    if (cancel.requested()) return cancelledResult("during model load");
    if (se == nullptr || se->session == nullptr)
    {
        juce::Logger::writeToLog("[BeatThis] Engine not available - returning invalid result");
        return out;
    }

    try
    {
        const int numCh = drumStem.getNumChannels();
        const int numSm = drumStem.getNumSamples();
        std::vector<float> mono(static_cast<size_t>(numSm), 0.0f);
        const float scale = 1.0f / static_cast<float>(numCh);
        // In blocks with a cancel check each (same per-sample channel order = same sums).
        for (int b0 = 0; b0 < numSm; b0 += (int) kCancelCheckSamples)
        {
            if (cancel.requested()) return cancelledResult("during downmix");
            const int b1 = std::min(numSm, b0 + (int) kCancelCheckSamples);
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float* rd = drumStem.getReadPointer(ch);
                for (int i = b0; i < b1; ++i) mono[static_cast<size_t>(i)] += rd[i] * scale;
            }
        }

        juce::Logger::writeToLog("[BeatThis] Running inference on " + juce::String(numSm)
            + " samples @ " + juce::String(sampleRate, 0) + " Hz (mono)"
            + (cancel.canCancel() ? " [cancellable]" : ""));
        const auto t0 = juce::Time::getMillisecondCounterHiRes();

        RawBeats bt;
        {
            // Serialised: wait (cancellably) for another detector's inference, then re-check the
            // cache so the BPM job and the downbeat job on one stem pay for ONE inference (U-M9).
            std::unique_lock<std::mutex> lk(se->runMutex, std::defer_lock);
            bool announced = false;
            while (! lk.try_lock())
            {
                if (cancel.requested()) return cancelledResult("while waiting for another inference");
                if (! announced)
                {
                    announced = true;
                    juce::Logger::writeToLog("[BeatThis] session busy (another detection) -> waiting");
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            if (ResultCache::get().find(key, out))
            {
                out.fromCache = true;
                juce::Logger::writeToLog("[BeatThis] cache HIT after waiting - inference skipped");
                return out;
            }
            const RunStatus st = runInference(*se, mono, static_cast<int>(sampleRate), cancel, bt);
            if (st == RunStatus::Cancelled)
                return cancelledResult("during inference ("
                    + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 0) + " ms in)");
            if (st == RunStatus::Failed)
            {
                juce::Logger::writeToLog("[BeatThis] inference failed - invalid result");
                return Result{};
            }
        }
        juce::Logger::writeToLog("[BeatThis] inference " + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 0)
            + " ms: " + juce::String((int) bt.beats.size()) + " beats, "
            + juce::String((int) bt.downbeats.size()) + " downbeats");

        out.beatTimesSec.reserve(bt.beats.size());
        for (float t : bt.beats) out.beatTimesSec.push_back(static_cast<double>(t));
        out.downbeatTimesSec.reserve(bt.downbeats.size());
        for (float t : bt.downbeats) out.downbeatTimesSec.push_back(static_cast<double>(t));
        out.beatCounts = bt.beatCounts;

        if (out.beatTimesSec.size() < 3)
        {
            juce::Logger::writeToLog("[BeatThis] Too few beats (" + juce::String((int) out.beatTimesSec.size()) + ") - invalid");
            return out;
        }

        if (cancel.requested()) return cancelledResult("before the half-time check");

        // Octave check: snare only on bar position 3 -> model tracked 8th notes.
        if (isHalfTimeFeel(mono, sampleRate, out.beatTimesSec, out.downbeatTimesSec))
        {
            // Keep every other beat, phase-locked to the first downbeat. The
            // model's downbeats now fall on real beats 1 AND 3; they are kept
            // as-is (2-beat "bars") and DownbeatDetector picks the bar phase.
            size_t firstDb = 0;
            if (! out.downbeatTimesSec.empty())
                firstDb = static_cast<size_t>(std::lower_bound(out.beatTimesSec.begin(), out.beatTimesSec.end(),
                                                               out.downbeatTimesSec.front() - 1e-4)
                                              - out.beatTimesSec.begin());
            std::vector<double> halved;
            for (size_t i = firstDb % 2; i < out.beatTimesSec.size(); i += 2) halved.push_back(out.beatTimesSec[i]);
            out.beatTimesSec.swap(halved);
            out.beatCounts.clear();
            out.halvedFromDoubleTime = true;
        }

        const TempoEstimate te = estimateTempo(out.beatTimesSec);
        out.bpm          = te.bpm;
        out.confidence   = te.confidence;
        out.tempoInliers = te.inliers;
        out.beatsPerBar  = estimateBeatsPerBar(out.beatTimesSec, out.downbeatTimesSec);
        out.valid        = te.valid;

        juce::Logger::writeToLog("[BeatThis] " + juce::String(out.bpm, 4) + " BPM (IBI fit over beat numbers; median="
            + juce::String(te.medianBpm, 4) + "), conf=" + juce::String(out.confidence, 2)
            + ", inliers=" + juce::String(te.inliers) + "/" + juce::String((int) out.beatTimesSec.size())
            + ", beatsPerBar=" + juce::String(out.beatsPerBar)
            + ", firstDownbeat=" + (out.downbeatTimesSec.empty() ? juce::String("none")
                                                                 : juce::String(out.downbeatTimesSec.front(), 3) + "s"));

        if (out.valid) ResultCache::get().put(key, out);
        return out;
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog(juce::String("[BeatThis] Inference threw: ") + e.what());
        return Result{};
    }
    catch (...)
    {
        juce::Logger::writeToLog("[BeatThis] Inference threw unknown exception");
        return Result{};
    }
}

} // namespace samplar

#else // !WITH_BEAT_THIS

namespace samplar {

struct BeatThisDetector::Impl {};
BeatThisDetector::BeatThisDetector()  = default;
BeatThisDetector::~BeatThisDetector() = default;
bool BeatThisDetector::isAvailable() { return false; }

BeatThisDetector::Result BeatThisDetector::detect(const juce::AudioBuffer<float>&, double, AnalysisCancel)
{
    return Result{}; // valid=false
}

} // namespace samplar

#endif // WITH_BEAT_THIS
