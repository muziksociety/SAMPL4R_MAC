#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>
#include <vector>

namespace samplar {

/**
 * BeatThisDetector
 *
 * Neural beat tracker based on "Beat This!" (Foscarin et al., ISMIR 2024).
 * Wraps the vendored beat_this_cpp port + ONNX Runtime.
 *
 * Produces explicit per-beat timestamps; BPM is derived from the median
 * inter-beat interval. This is robust to the metric-ratio (96 vs 126 BPM
 * on 16th-note hat patterns) ambiguity that traps autocorrelation-based
 * detectors, because the model is trained on beat positions directly.
 *
 * Model file lives at: third_party/beat_this/models/beat_this.onnx
 * Loaded once on first use (lazy init). Inference is RT-unsafe — call from
 * the background BPM-detection worker thread only.
 */
class BeatThisDetector
{
public:
    struct Result
    {
        double bpm        = 0.0;
        double confidence = 0.0;          // [0, 1]
        std::vector<double> beatTimesSec; // raw per-beat timestamps in seconds
        bool   valid      = false;
    };

    BeatThisDetector();
    ~BeatThisDetector();

    BeatThisDetector(const BeatThisDetector&) = delete;
    BeatThisDetector& operator=(const BeatThisDetector&) = delete;

    /**
     * Run inference on a drum stem (or full mix) and return tempo estimate.
     * Always safe to call — on any internal failure returns {valid=false}.
     *
     * @param drumStem   Source audio (any number of channels; downmixed internally)
     * @param sampleRate Sample rate of drumStem
     */
    Result detect(const juce::AudioBuffer<float>& drumStem, double sampleRate);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace samplar
