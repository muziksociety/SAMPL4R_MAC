#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <array>
#include "AnalysisCancel.h"

namespace samplar {

/**
 * KeyDetector - musical key (tonic + major/minor) of an audio buffer.
 *
 * Algorithm:
 *   1. Mono downmix; STFT (Hann, 8192 @ 44.1/48 kHz scaled to ~186 ms,
 *      hop = size/2).
 *   2. Chroma: spectral peaks (local maxima) between 55 Hz and 5 kHz are
 *      mapped to the nearest equal-tempered pitch class (A4 = 440 Hz), with
 *      a cosine weight on the cents deviation and log-compressed magnitude.
 *      Frames are L1-normalised before summing, so loud passages do not
 *      dominate; silent frames are skipped.
 *   3. Krumhansl-Schmuckler: Pearson correlation of the 12-bin chroma with the
 *      Krumhansl-Kessler major/minor profiles rotated to all 12 tonics.
 *   4. confidence = clamp((r_best - r_second) / 0.1, 0, 1) * clamp(r_best, 0, 1)
 *      (r_second is the best *other* key; relative major/minor are a common
 *      runner-up, reported as secondTonic/secondMode).
 *
 * Pure function, no allocation in the audio thread - call from a worker.
 */
class KeyDetector
{
public:
    enum class Mode { Major, Minor };

    struct Result
    {
        int    tonic       = -1;          // 0 = C, 1 = C#, ... 11 = B; -1 if invalid
        Mode   mode        = Mode::Major;
        double confidence  = 0.0;         // 0..1
        double correlation = 0.0;         // Pearson r of the winning key
        int    secondTonic = -1;
        Mode   secondMode  = Mode::Major;
        double secondCorrelation = 0.0;
        std::array<double, 12> chroma {}; // L1-normalised, index 0 = C
        bool   valid       = false;
        bool   cancelled   = false;       // the AnalysisCancel token fired: do not apply

        /** e.g. "A minor", "F# major"; "unknown" if invalid. */
        juce::String getName() const;
        /** Camelot wheel code for DJ use, e.g. "8A" (A minor), "8B" (C major). */
        juce::String getCamelot() const;
    };

    /** Detect the key of an audio buffer (any channel count). */
    static Result detect(const juce::AudioBuffer<float>& buffer, double sampleRate, AnalysisCancel cancel = {});

    /** Key from a precomputed 12-bin chroma (index 0 = C). */
    static Result detectFromChroma(const std::array<double, 12>& chroma);

    /** 12-bin chroma (L1-normalised) of a buffer. All zero if silent. */
    /** Polls `cancel` every 32 frames; a cancelled run returns all zeros and sets *wasCancelled. */
    static std::array<double, 12> computeChroma(const juce::AudioBuffer<float>& buffer, double sampleRate,
                                                AnalysisCancel cancel = {}, bool* wasCancelled = nullptr);

    static juce::String pitchClassName(int pc);
};

} // namespace samplar
