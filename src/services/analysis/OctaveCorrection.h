#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace samplar {

/**
 * Octave Correction (Schreiber & Müller 2014)
 *
 * Resolves tempo octave ambiguity (T vs T/2 vs T*2) using global
 * onset density as a discriminator.
 *
 * Approach:
 *   - Compute onset density (onsets per second) from a spectral-flux ODF.
 *   - For each candidate in {T/3, T/2, T, T*2, T*3}, evaluate
 *       onsets_per_beat = density / (T_i / 60)
 *     against the typical pop/rock/hip-hop/electronic range [1.5, 4.0].
 *   - Loosely favor the 80-160 BPM "sweet spot".
 *   - Apply the bimodal density gate: if density >= 3.5 Hz, prefer
 *     the faster of T vs T/2; else prefer the slower.
 *
 * Pure function: no I/O, no allocations after vectors. Unit-testable.
 */
struct OctaveCorrectionResult
{
    double correctedBPM    = 0.0;
    bool   wasCorrected    = false;
    double onsetDensityHz  = 0.0;
    double winnerScore     = 0.0;
    juce::String reasoning;
};

/**
 * Apply octave correction to a candidate BPM.
 *
 * @param candidateBPM       The fused tempo before correction.
 * @param onsetEnvelope      A precomputed ODF (e.g. spectral flux). Half-wave
 *                           rectified normalization happens internally.
 * @param envelopeSampleRate Frames per second of the ODF (sr / hopSize).
 */
OctaveCorrectionResult correctOctave(double candidateBPM,
                                     const std::vector<float>& onsetEnvelope,
                                     double envelopeSampleRate);

/**
 * Convenience overload: compute an onset envelope from a raw mono buffer and
 * then run octave correction. Uses a lightweight spectral-flux ODF internal
 * to OctaveCorrection.cpp so callers don't need to plumb the TempoAnalyzer
 * envelope through. Cost is one FFT pass over the input.
 */
OctaveCorrectionResult correctOctaveFromBuffer(double candidateBPM,
                                               const juce::AudioBuffer<float>& monoBuffer,
                                               double sampleRate);

} // namespace samplar
