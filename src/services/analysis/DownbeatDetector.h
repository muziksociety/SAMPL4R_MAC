#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <cstdint>

namespace samplar {

/**
 * DownbeatDetector — estimates the sample offset of beat 1 in a drum stem.
 *
 * Algorithm (onset-strength + comb-filter phase projection):
 *   1. Downmix to mono.
 *   2. Compute spectral-flux onset envelope at ~100 Hz hop rate
 *      (FFT size 2048, hop 1024, Hann window). Restrict bands to 50 Hz - 4 kHz.
 *   3. Convert known BPM to beat period in onset frames.
 *   4. Project envelope onto a complex sinusoid at the beat rate over the
 *      first ~8 bars and read off the phase. That phase * tau is the
 *      fractional-frame offset of beat 1.
 *   5. Sanity-check and snap to the first onset peak if it sits before the
 *      estimate (handles pickup notes).
 *
 * Runs synchronously and should be called from a background thread —
 * see SModePage::runBpmDetectionOnDrumStem() for the launch pattern.
 */
class DownbeatDetector
{
public:
    struct Result
    {
        int64_t  beat1Sample = 0;   // sample offset of beat 1 in the input buffer
        double   confidence  = 0.0; // |comb projection magnitude| / sum(flux), 0..1
        bool     valid       = false;
    };

    /** Detect beat 1 in a drum stem.
     *  @param drumStem      Drum-only audio buffer (mono or stereo; will be downmixed).
     *  @param sampleRate    Audio sample rate (Hz).
     *  @param bpm           Pre-detected BPM. Must be in [60, 220].
     *  @return Result with beat1Sample, confidence, valid. */
    static Result detect(const juce::AudioBuffer<float>& drumStem,
                         double sampleRate,
                         double bpm);
};

} // namespace samplar
