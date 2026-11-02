#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "AnalysisCancel.h"
#include <memory>
#include <vector>

namespace samplar {

/**
 * BeatThisDetector
 *
 * Neural beat tracker based on "Beat This!" (Foscarin et al., ISMIR 2024).
 * Wraps the vendored beat_this_cpp port + ONNX Runtime.
 *
 * Produces explicit per-beat AND per-downbeat timestamps. BPM is a
 * least-squares fit of beat time against BEAT NUMBER (inter-beat-interval
 * based, missed beats bridged, outliers rejected) - see estimateTempo().
 *
 * The ONNX session is process-wide (loaded once, inference serialised), and
 * the last few results are cached by audio-content fingerprint, so the BPM
 * job and the downbeat job running on the same drum stem pay for ONE
 * inference.
 *
 * Model file lives at: third_party/beat_this/models/beat_this.onnx
 * Inference is RT-unsafe - call from a background worker thread only.
 */
class BeatThisDetector
{
public:
    struct Result
    {
        double bpm        = 0.0;
        double confidence = 0.0;              // [0, 1]
        std::vector<double> beatTimesSec;     // per-beat timestamps (s)
        std::vector<double> downbeatTimesSec; // per-downbeat timestamps (s), subset of beats
        std::vector<int>    beatCounts;       // position in bar per beat (1 = downbeat), may be empty
        int    beatsPerBar = 0;               // estimated meter (0 = unknown)
        int    tempoInliers = 0;              // beats used by the tempo fit
        bool   halvedFromDoubleTime = false;  // model beats were 8th notes (half-time feel) -> every other beat kept
        bool   fromCache  = false;            // true if served from the content cache
        bool   cancelled  = false;            // cancel token fired (valid == false)
        bool   valid      = false;
    };

    /** Output of the pure tempo estimator (exposed for unit tests). */
    struct TempoEstimate
    {
        double bpm        = 0.0;
        double medianBpm  = 0.0;
        double confidence = 0.0; // fraction of IOIs within 10% of an integer multiple of the beat period
        int    inliers    = 0;
        bool   valid      = false;
    };

    BeatThisDetector();
    ~BeatThisDetector();

    BeatThisDetector(const BeatThisDetector&) = delete;
    BeatThisDetector& operator=(const BeatThisDetector&) = delete;

    /**
     * Run inference on a drum stem (or full mix) and return beats/downbeats/tempo.
     * Always safe to call - on any internal failure returns {valid=false}.
     * cancel: polled between stages (resample, mel blocks, every ONNX chunk, post) and by a
     * watcher thread that terminates the running ONNX chunk (RunOptions::SetTerminate), so a
     * cancel returns {valid=false, cancelled=true} within ~200 ms. Also honoured while waiting
     * for another detector's inference (the session is shared and serialised).
     */
    Result detect(const juce::AudioBuffer<float>& drumStem, double sampleRate,
                  AnalysisCancel cancel = {});

    /** True if the model loaded (or could load). Triggers lazy init. */
    bool isAvailable();

    /**
     * C16: tempo from beat timestamps using inter-beat intervals.
     *  1. robust median IOI;
     *  2. each beat gets a beat NUMBER k_i = k_{i-1} + round(IOI_i / period),
     *     so a missed beat (IOI ~ 2 periods) does not stretch the fit;
     *     IOIs that are not near an integer multiple, or bridge > 8 beats,
     *     start a new segment (excluded from the joint fit offsets);
     *  3. least-squares t = a_seg + b * k over all segments, iterated twice
     *     with residual outlier rejection (> max(25 ms, 3 * MAD)).
     *  BPM = 60 / b.
     */
    static TempoEstimate estimateTempo(const std::vector<double>& beatTimesSec);

    /** Meter estimate from beats + downbeats (median beats between downbeats, 2..7; 0 if unknown). */
    static int estimateBeatsPerBar(const std::vector<double>& beatTimesSec,
                                   const std::vector<double>& downbeatTimesSec);

    /**
     * Half-time-feel check (octave disambiguation for the neural beats).
     * Over 4-beat bars (from the downbeats), compares mid/high-band onset
     * strength (300-5000 Hz: snare body + noise; kick mostly excluded) at
     * each bar position. Backbeat snares on positions 2 and 4 = beats are at
     * the true tactus; snares only on position 3 (>= 1.5x positions 2 and 4)
     * means the tracker counted 8th notes (e.g. 70 BPM hip-hop as 140), so
     * the true tempo is half. Only considered for tempos >= 120 BPM.
     */
    static bool isHalfTimeFeel(const std::vector<float>& mono, double sampleRate,
                               const std::vector<double>& beatTimesSec,
                               const std::vector<double>& downbeatTimesSec);

    /** Drop all cached results (tests). */
    static void clearCache();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace samplar
