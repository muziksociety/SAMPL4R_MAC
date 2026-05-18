#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>
#include <memory>
#include <atomic>
#include <array>

// Forward-declare ORT types so this header doesn't drag the C++ API
// into every consumer's translation unit.
namespace Ort
{
    struct Env;
    struct Session;
    struct MemoryInfo;
}

namespace samplar {

/**
 * OnnxSeparationProvider — pure C++/ONNX Runtime drum/bass/vocals/other
 * stem separation. Drop-in replacement for the previous Python-based pipeline.
 *
 * Model: resources/models/htdemucs.onnx  (smank/htdemucs-onnx — Demucs v4
 *        hybrid transformer exported with STFT/iSTFT BAKED INTO THE GRAPH).
 *   inputs : mix     [1, 2, samples]  f32   (44.1 kHz stereo, dynamic length)
 *   outputs: sources [1, 4, 2, samples] f32  (4 stems x stereo x samples)
 *
 * Stem order (canonical Demucs v4): drums, bass, other, vocals.
 *
 * Inference pipeline mirrors sevagh/demucs.onnx (src/model_apply.cpp):
 *   1. Per-track normalization:
 *      ref       = audio.mean(axis=channels)   [length]
 *      ref_mean  = ref.mean()                  scalar
 *      ref_std   = ref.std(ddof=1)             scalar
 *      audio'    = (audio - ref_mean) / ref_std
 *   2. Symmetric pad by MAX_SHIFT samples on both sides (zeros).
 *   3. Random shift in [0, MAX_SHIFT) for time-equivariance.
 *   4. Loop overlapping segments of SEGMENT_LEN_SECS at stride
 *      (1 - OVERLAP) * SEGMENT_LEN_SECS, run model on each, accumulate
 *      with triangular weights raised to TRANSITION_POWER (=1.0).
 *   5. out /= sum_weight  (weighted overlap-add normalisation)
 *   6. Denormalize: out = out * ref_std + ref_mean
 *   7. Crop to original length (slice off [max_shift - shift_offset .. +length]).
 *
 * Constants (sevagh demucs.hpp):
 *   SUPPORTED_SAMPLE_RATE = 44100
 *   SEGMENT_LEN_SECS      = 7.8    -> 343 980 samples per chunk
 *   MAX_SHIFT_SECS        = 0.5
 *   OVERLAP               = 0.25
 *   TRANSITION_POWER      = 1.0
 *
 * Thread model: ctor + separate() both run on a background thread.
 * The progress callback is invoked synchronously on that same thread — the
 * caller (FullSongService) marshals to the message thread itself.
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
    };

    using ProgressFn = std::function<void (float progress01, const juce::String& status)>;

    explicit OnnxSeparationProvider (const juce::File& modelFile);
    ~OnnxSeparationProvider();

    /** Returns true if the ONNX session was constructed successfully. */
    bool isReady() const noexcept { return ready_; }

    /** Last error message (empty when no error). */
    juce::String lastError() const { return lastError_; }

    /** Which execution provider was actually selected ("DirectML" / "CPU"). */
    juce::String activeExecutionProvider() const { return activeEP_; }

    /**
     * Separate the input audio file into 4 stems. Writes WAV files into
     * outputDir as "<basename>_(Drums|Bass|Other|Vocals).wav".
     *
     * @param drumsOnly   if true, only the drums WAV is written (the other
     *                    3 tensors are still computed — htdemucs does not
     *                    support per-stem inference).
     * @param shouldCancel optional cancel poll; if it returns true the run
     *                    aborts cleanly (returns false, lastError set).
     */
    bool separate (const juce::File& inputAudio,
                   const juce::File& outputDir,
                   bool drumsOnly,
                   ProgressFn progress,
                   std::function<bool()> shouldCancel = {});

    /** Output file paths from the most recent successful separate() call. */
    StemFiles lastStems() const { return lastStems_; }

private:
    // ORT plumbing
    std::unique_ptr<Ort::Env>      env_;
    std::unique_ptr<Ort::Session>  session_;
    std::unique_ptr<Ort::MemoryInfo> memInfo_;
    bool ready_ { false };
    juce::String lastError_;
    juce::String activeEP_ { "CPU" };

    StemFiles lastStems_;

    // Audio loading helper — decodes any format JUCE can read into 44.1 kHz
    // stereo planar float32 [2][N].
    bool loadAudio441Stereo (const juce::File& input,
                             std::vector<float>& outChL,
                             std::vector<float>& outChR,
                             juce::String& error);

    // Write a stereo float buffer [2][N] to a 16-bit PCM WAV at 44.1 kHz.
    bool writeStemWav (const juce::File& path,
                       const float* chL,
                       const float* chR,
                       int numSamples,
                       juce::String& error);
};

} // namespace samplar
