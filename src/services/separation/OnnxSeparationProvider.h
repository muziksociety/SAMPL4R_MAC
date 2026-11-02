#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>
#include <memory>
#include <atomic>
#include <array>
#include <vector>

// Forward-declare ORT types so this header doesn't drag the C++ API
// into every consumer's translation unit.
namespace Ort
{
    struct Env;
    struct Session;
    struct MemoryInfo;
    struct RunOptions;
}

namespace samplar {

class HtdemucsIstft;

/**
 * OnnxSeparationProvider - pure C++/ONNX Runtime drum/bass/vocals/other
 * stem separation (ported from Sample Monsta 5, commit 453e7733).
 *
 * Models (models/ next to the plugin binary):
 *   htdemucs_body.onnx  (preferred) - Demucs v4 cut right before the baked-in inverse
 *                       STFT (tools/separation/split_htdemucs.py). Outputs the complex
 *                       spectrogram [8, 4098, frames] + the time branch [1, 4, 2, samples];
 *                       HtdemucsIstft (samplar::Fft / PFFFT) finishes "sources" on the host.
 *                       Runs on the GPU (DirectML) or the CPU - same model, same maths.
 *   htdemucs.onnx       (legacy / dev fallback) - smank/htdemucs-onnx full export with the
 *                       STFT/iSTFT inside the graph. CPU only (its iSTFT ConvTranspose does
 *                       not fit DirectML).
 *
 * Stem order (canonical Demucs v4): drums, bass, other, vocals.
 *
 * Inference pipeline mirrors sevagh/demucs.onnx (src/model_apply.cpp):
 *   1. Per-track normalization (ref = channel mean, ref_std with ddof=1).
 *   2. Symmetric pad by MAX_SHIFT samples on both sides (zeros).
 *   3. Random shift in [0, MAX_SHIFT) for time-equivariance.
 *   4. Overlapping segments of 7.8 s at stride 0.75 * segment, triangular weights.
 *   5. out /= sum_weight, out = out * ref_std + ref_mean, crop to the original length.
 *
 * Device selection (ctor, logged as "GPU: ..." / "Decision: ..."):
 *   GPU (DirectML) only when ALL hold: DirectML build (SAMPLAR_HAS_DIRECTML), body model,
 *   env SAMPLAR_SEPARATION_DEVICE != "cpu", caller allows it (user setting), no GPU failure
 *   earlier in this process, a discrete GPU with >= 2 GB dedicated VRAM, and the bundled
 *   DirectML.dll loads. Any GPU failure (session create or Run) falls back to the CPU and
 *   retries the chunk; the failure disables the GPU for the rest of the process.
 *
 * Memory (audit C3): the source is decoded straight into the padded/normalised buffer
 * (2 floats per sample) and the overlap-add is STREAMED - every finished region is written
 * to the float WAVs immediately - so peak memory is ~8 bytes per input sample + one segment,
 * independent of the number of stems. Songs longer than kMaxDurationSeconds are refused with
 * a clear message; std::bad_alloc is caught and reported, never thrown to the caller.
 *
 * Cancel (audit C2): shouldCancel is polled between chunks AND by a watcher thread that calls
 * Ort::RunOptions::SetTerminate(), so a cancel aborts the chunk that is running.
 *
 * Host friendliness (audit C13): intra-op threads = physical cores - 1, no spin-waiting,
 * ORT pool threads and the calling thread run at below-normal priority during separate().
 *
 * Output: 32-bit float stereo WAVs at 44.1 kHz named stemFileName(base, stem).
 *
 * Thread model: ctor + separate() run on one background thread. The progress callback is
 * invoked synchronously on that thread; the caller marshals to the message thread itself.
 */
class OnnxSeparationProvider
{
public:
    enum class Stem : int { Drums = 0, Bass = 1, Other = 2, Vocals = 3 };

    struct StemFiles
    {
        juce::File drums;
        juce::File bass;
        juce::File other;
        juce::File vocals;
        bool isValid() const noexcept { return drums.existsAsFile(); }
        bool isComplete() const noexcept
        {
            return drums.existsAsFile() && bass.existsAsFile() && other.existsAsFile() && vocals.existsAsFile();
        }
    };

    using ProgressFn = std::function<void (float progress01, const juce::String& status)>;

    // Longest input accepted (audit C3 duration guard).
    static constexpr double kMaxDurationSeconds = 20.0 * 60.0;

    /** @param allowGpu false forces the CPU (user setting "Use GPU" OFF).
     *  @param creationCancel optional: polled while the ONNX session is created (3-5 s). When it
     *         fires the ctor returns at once with isReady() == false and wasCancelled() == true;
     *         the session is finished and released on a detached thread (review S-M3,
     *         createOrtSessionCancellable). A cancel never disables the GPU. */
    explicit OnnxSeparationProvider (const juce::File& modelFile, bool allowGpu = true,
                                     std::function<bool()> creationCancel = {});
    ~OnnxSeparationProvider();

    /** Returns true if the ONNX session was constructed successfully. */
    bool isReady() const noexcept { return ready_; }

    /** True when onnxruntime.dll could not be loaded (reinstall needed); lastError() says so. */
    bool isRuntimeUnavailable() const noexcept { return runtimeUnavailable_; }

    /** Last error message (empty when no error). */
    juce::String lastError() const { return lastError_; }

    /** Which execution provider is active: "CPU" or "DirectML (<adapter name>)". */
    juce::String activeExecutionProvider() const { return activeEP_; }

    /** Model file actually loaded (body model when found). */
    juce::File modelFile() const { return modelFile_; }

    /**
     * Separate the input audio file into 4 stems. Writes 32-bit float WAVs into
     * outputDir named stemFileName (input base name, stem).
     *
     * @param drumsOnly   if true, only the drums WAV is written (all 4 are computed).
     * @param outputBaseName stem file base name ("" = input file name without extension).
     *                    Non-finite input samples are zeroed; a run whose output is not finite
     *                    fails (nothing is written/kept), so NaN stems can never be cached.
     * @param shouldCancel optional cancel poll; when it returns true the run aborts
     *                    (also mid-chunk), returns false, lastError() == "Cancelled by user"
     *                    and the partially written files are deleted.
     */
    bool separate (const juce::File& inputAudio,
                   const juce::File& outputDir,
                   bool drumsOnly,
                   ProgressFn progress,
                   std::function<bool()> shouldCancel = {},
                   const juce::String& outputBaseName = {});

    /** Output file paths from the most recent successful separate() call. */
    StemFiles lastStems() const { return lastStems_; }

    /** True when the last separate() (or the ctor's session creation) ended because of the cancel hook. */
    bool wasCancelled() const noexcept { return cancelled_; }

    /** Exact stem file name: "<base>_<Drums|Bass|Other|Vocals>.wav". */
    static juce::String stemFileName (const juce::String& base, Stem stem);

    // ---- Process-wide diagnostics (Settings dialog / logs) ----

    /** Backend of the most recent finished separation in this process ("" = none yet). */
    static juce::String lastRunBackend();

    /** True when a GPU failure disabled DirectML for the rest of this process. */
    static bool gpuFailedThisProcess() noexcept;

    /** Human-readable backend that the NEXT separation would use, e.g.
        "GPU: NVIDIA GeForce GTX 970M (DirectML)" or "CPU (no discrete GPU with 2 GB+)".
        Probes the adapters once per process (cached; a few ms to ~0.3 s the first time). */
    static juce::String describeNextBackend (bool gpuSettingOn);

private:
    // (Re)creates session_ for modelFile_ on the GPU (DirectML adapter gpuAdapterIndex_)
    // or the CPU. Returns false (session_ null, lastError_ set) on failure.
    bool createSession (bool useGpu);
    bool gpuActive() const noexcept { return activeEP_.startsWith ("DirectML"); }
    void disableGpuForProcess (const juce::String& reason);

    bool separateImpl (const juce::File& inputAudio,
                       const juce::File& outputDir,
                       bool drumsOnly,
                       const ProgressFn& progress,
                       const std::function<bool()>& shouldCancel,
                       const juce::String& base);

    // Decodes `input` into planar 44.1 kHz stereo, written straight into
    // padL/padR at offset `padOffset` (buffers are sized length + 2 * padOffset).
    // cancelPoll is checked between decode blocks (sets lengthOut = 0, error "Cancelled by user").
    bool loadAudioPadded (const juce::File& input, int padOffset,
                          std::vector<float>& padL, std::vector<float>& padR,
                          int& lengthOut, juce::String& error,
                          const std::function<bool()>& cancelPoll);

    juce::File modelFile_;
    bool bodyMode_ { false };                  // htdemucs_body.onnx + host inverse STFT
    std::unique_ptr<HtdemucsIstft> istft_;
    std::vector<float> istftOut_;              // [4][2][segment] "sources" for one chunk
    int gpuAdapterIndex_ { -1 };
    bool gpuEligible_ { false };               // a suitable GPU + DirectML were found
    bool gpuSkippedThisRun_ { false };         // GPU ran out of memory this run: CPU for the rest of it
    juce::String gpuName_;

    // ORT plumbing. env_ is shared with a detached session creator (it must outlive the session).
    std::shared_ptr<Ort::Env>        env_;
    std::function<bool()>            creationCancel_;   // ctor arg, then separate()'s shouldCancel
    std::unique_ptr<Ort::Session>    session_;
    std::unique_ptr<Ort::MemoryInfo> memInfo_;
    std::unique_ptr<Ort::RunOptions> runOptions_;   // per separate(); SetTerminate() cancels
    bool ready_ { false };
    bool runtimeUnavailable_ { false };
    bool cancelled_ { false };
    juce::String lastError_;
    juce::String activeEP_ { "CPU" };

    StemFiles lastStems_;
};

} // namespace samplar
