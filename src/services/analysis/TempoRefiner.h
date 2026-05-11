#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>

namespace samplar {

/**
 * Result of narrow-band autocorrelation tempo refinement.
 */
struct TempoRefinementResult
{
    double refinedBPM = 0.0;
    double confidence = 0.0;   // 0..1 (peak autocorr / autocorr[0])
    bool   improved   = false; // true if refinement produced a meaningful update
    juce::String reasoning;
};

/** Refine a tempo estimate using sample-level autocorrelation of the onset
 *  envelope restricted to a narrow band around the hint.
 *
 *  Runs AFTER a coarse detector (Beat This!) provides the correct octave/metric.
 *  Uses parabolic interpolation on the autocorr peak for sub-frame precision.
 *
 *  @param drumStem        Drum stem (will be downmixed to mono if multi-channel)
 *  @param sampleRate      Audio sample rate (Hz)
 *  @param hintBpm         The coarse BPM estimate (typically within ~1.3% of true)
 *  @param tolerance       Search band: hintBpm * [1-tolerance, 1+tolerance] (default ±5%)
 *  @return refined BPM with parabolic-interpolated peak; or hintBpm + confidence=0
 *          if no valid peak / catastrophic disagreement (>10% off).
 */
TempoRefinementResult refineTempoNearBPM(const juce::AudioBuffer<float>& drumStem,
                                          double sampleRate,
                                          double hintBpm,
                                          double tolerance = 0.05);

} // namespace samplar
