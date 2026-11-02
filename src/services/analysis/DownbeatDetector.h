#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include "AnalysisCancel.h"
#include <cstdint>
#include <vector>

namespace samplar {

/**
 * DownbeatDetector - estimates the sample offset of BEAT 1 (bar start) in a
 * drum stem, for the S-Mode grid offset.
 *
 * Primary path (C10): the Beat This! neural tracker's DOWNBEATS. Every model
 * downbeat votes for its bar position (beat number mod meter); a near-tie
 * (model marking beats 1 and 3) is broken by kick-band + broadband onset
 * strength. The first model downbeat in the winning phase is refined to the
 * sample-accurate attack (+-30 ms energy step + zero crossing) and reduced
 * modulo one BAR, so the grid offset is the earliest bar start >= 0. BeatThis results are cached by content, so when
 * the BPM job already ran on the same stem this costs no extra inference.
 *
 * Fallback (BeatThis missing / low confidence / < 2 downbeats): the legacy
 * kick-band comb-filter phase projection. It only knows the BEAT phase, so
 * the result is modulo one beat and Result::source == CombFilter,
 * beatsPerBar == 0 (bar position unknown).
 *
 * Runs synchronously - call from a background thread.
 */
class DownbeatDetector
{
public:
    enum class Source { None, BeatThis, CombFilter };

    struct Result
    {
        int64_t  beat1Sample = 0;   // grid offset: bar start (BeatThis) or beat phase (comb), in [0, period)
        double   confidence  = 0.0; // 0..1
        bool     valid       = false;

        // C10 additions (defaults keep old callers unchanged)
        Source   source              = Source::None;
        int      beatsPerBar         = 0;   // 0 = unknown (comb fallback)
        int64_t  periodSamples       = 0;   // modulo used for beat1Sample (bar or beat)
        int64_t  firstDownbeatSample = -1;  // absolute refined anchor downbeat (BeatThis path)
        bool     cancelled           = false; // cancel token fired: valid == false, do not apply
    };

    /** Detect beat 1 in a drum stem (BeatThis first, comb-filter fallback).
     *  Signature unchanged - SModeService::runDownbeatDetectionAsync needs no edit.
     *  @param bpm  Grid BPM (the value the grid is drawn with). Must be in [60, 220]. */
    static Result detect(const juce::AudioBuffer<float>& drumStem,
                         double sampleRate,
                         double bpm,
                         AnalysisCancel cancel = {});

    /** Bar anchor from known beat + downbeat times (e.g. HybridTempoDetector::Result).
     *  No inference. Returns valid=false if fewer than 2 usable downbeats. */
    static Result detectFromDownbeats(const juce::AudioBuffer<float>& drumStem,
                                      double sampleRate,
                                      double bpm,
                                      const std::vector<double>& beatTimesSec,
                                      const std::vector<double>& downbeatTimesSec,
                                      double beatThisConfidence = 1.0);

    /** Legacy comb-filter beat-phase estimate (fallback path, exposed for tests). */
    static Result detectCombFilter(const juce::AudioBuffer<float>& drumStem,
                                   double sampleRate,
                                   double bpm);

    /** BeatThis confidence below which the comb fallback is used. */
    static constexpr double kBeatThisConfidenceFloor = 0.5;
};

} // namespace samplar
