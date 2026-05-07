#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <vector>

namespace samplar {

//==============================================================================
// LOOP DETECTOR - Autocorrelation-based loop boundary detection
//
// Uses the same autocorrelation principle as MiniBPM but at longer time scales
// to find repeating musical sections (loops).
//
// Given BPM, checks correlation at musically meaningful lags:
//   - 1 bar  (4 beats)
//   - 2 bars (8 beats)
//   - 4 bars (16 beats)  <- PRIMARY TARGET
//   - 8 bars (32 beats)
//
// The lag with highest correlation = natural loop length
//==============================================================================

struct LoopInfo {
    int loopLengthBars { 4 };           // Loop length in bars (default 4)
    int loopLengthSamples { 0 };        // Loop length in audio samples
    double loopLengthSeconds { 0.0 };   // Loop length in seconds
    float correlation { 0.0f };         // Autocorrelation score (0-1)
    std::vector<int64_t> loopBoundaries; // Sample positions of loop starts
};

struct SliceResult {
    std::vector<int64_t> loopBoundaries;   // 4-bar loop boundaries (sample positions)
    std::vector<int64_t> sampleBoundaries; // Individual hit boundaries (sample positions)
    double bpm { 0.0 };
    int loopLengthBars { 4 };
};

class LoopDetector {
public:
    LoopDetector() = default;
    ~LoopDetector() = default;

    //==========================================================================
    // Main API
    //==========================================================================

    // Detect loops and samples in audio
    // Returns loop boundaries (4-bar) and sample boundaries (individual hits)
    SliceResult analyze(const float* samples, int64_t numSamples,
                        double sampleRate, double bpm);

    // Set target loop length in bars (default 4)
    void setTargetLoopBars(int bars) { targetLoopBars_ = bars; }

    // Set beats per bar (default 4 for 4/4 time)
    void setBeatsPerBar(int bpb) { beatsPerBar_ = bpb; }

    // Get detailed loop info from last analysis
    const LoopInfo& getLoopInfo() const { return loopInfo_; }

private:
    int targetLoopBars_ { 4 };   // Target 4-bar loops
    int beatsPerBar_ { 4 };      // 4/4 time signature
    LoopInfo loopInfo_;

    //==========================================================================
    // Autocorrelation for loop detection
    //==========================================================================

    // Compute autocorrelation at a specific lag
    float computeAutocorrelation(const float* samples, int64_t numSamples,
                                  int64_t lag, int64_t windowSize);

    // Find the best loop length by checking musical lags
    LoopInfo findBestLoopLength(const float* samples, int64_t numSamples,
                                 double sampleRate, double bpm);

    //==========================================================================
    // Onset detection for individual samples
    //==========================================================================

    // Detect transient onsets for individual sample slicing
    std::vector<int64_t> detectOnsets(const float* samples, int64_t numSamples,
                                       double sampleRate);

    // Compute spectral flux for onset detection
    std::vector<float> computeSpectralFlux(const float* samples, int64_t numSamples,
                                            double sampleRate);

    //==========================================================================
    // Downbeat detection
    //==========================================================================

    // Find the first downbeat (beat 1) in the audio
    // Returns the sample offset to the first detected downbeat
    // Uses onset detection + beat grid alignment to find where beat 1 actually is
    int64_t findFirstDownbeat(const float* samples, int64_t numSamples,
                               double sampleRate, double bpm);
};

} // namespace samplar
