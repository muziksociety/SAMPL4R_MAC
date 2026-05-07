#pragma once

#include "TempoAnalyzer.h"
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <vector>

namespace samplar {

/**
 * LoopDivider - Beat-grid-aware loop segmentation
 *
 * Uses TempoAnalyzer's beat grid to divide loops into bar-aligned segments.
 * This is step 3 in the tempo-first pipeline:
 *   1. Audio Loading
 *   2. Tempo Analysis (TempoAnalyzer)
 *   3. Loop Division (LoopDivider) <-- THIS
 *   4. Sample Slicing (BeatAlignedSlicer)
 *   5. Classification (DrumAnalyzer)
 *   6. Variation Generation
 */
class LoopDivider
{
public:
    //==========================================================================
    // Configuration
    //==========================================================================

    struct Config {
        // Division settings
        int beatsPerBar = 4;           // Time signature numerator (4/4 = 4)
        int barsPerSegment = 1;        // How many bars per segment (1, 2, 4, etc.)

        // Boundary handling
        bool snapToZeroCrossing = true;   // Smooth segment boundaries
        int zeroCrossingSearchMs = 5;     // Search window for zero crossing (ms)

        // Minimum requirements
        float minSegmentBeats = 1.0f;     // Minimum beats for a valid segment
        float minConfidence = 0.3f;       // Minimum tempo confidence to proceed
    };

    //==========================================================================
    // Result Types
    //==========================================================================

    struct LoopSegment {
        int startSample = 0;           // Start sample position
        int endSample = 0;             // End sample position
        int barIndex = 0;              // Which bar this is (0-indexed)
        int beatIndex = 0;             // Starting beat index
        float numBeats = 0.0f;         // Number of beats in this segment
        float numBars = 0.0f;          // Number of bars in this segment
        float startTime = 0.0f;        // Start time in seconds
        float endTime = 0.0f;          // End time in seconds
    };

    struct Result {
        std::vector<LoopSegment> segments;
        float bpm = 0.0f;              // Detected BPM
        float confidence = 0.0f;       // Tempo detection confidence
        int totalBars = 0;             // Total bars detected
        float totalBeats = 0.0f;       // Total beats detected
        bool isValidLoop = false;      // Whether this appears to be a valid loop
        juce::String error;            // Error message if any
    };

    //==========================================================================
    // Public API
    //==========================================================================

    LoopDivider();
    ~LoopDivider() = default;

    /**
     * Divide a loop using pre-computed tempo analysis
     * This is the preferred method when tempo has already been analyzed
     */
    Result divide(const juce::AudioBuffer<float>& buffer,
                  double sampleRate,
                  const TempoAnalyzer::Result& tempoResult,
                  const Config& config = Config());

    /**
     * Divide a loop, running tempo analysis internally
     * Convenience method that runs TempoAnalyzer first
     */
    Result divideWithAnalysis(const juce::AudioBuffer<float>& buffer,
                               double sampleRate,
                               const Config& config = Config(),
                               const TempoAnalyzer::Config& tempoConfig = TempoAnalyzer::Config());

    /**
     * Divide an audio file
     */
    Result divideFile(const juce::File& audioFile,
                      const Config& config = Config(),
                      const TempoAnalyzer::Config& tempoConfig = TempoAnalyzer::Config());

    /**
     * Extract a segment as a new audio buffer
     */
    juce::AudioBuffer<float> extractSegment(const juce::AudioBuffer<float>& source,
                                            const LoopSegment& segment);

    /**
     * Export all segments to files
     */
    juce::Array<juce::File> exportSegments(const juce::AudioBuffer<float>& source,
                                           double sampleRate,
                                           const std::vector<LoopSegment>& segments,
                                           const juce::File& outputDir,
                                           const juce::String& prefix = "segment_");

private:
    //==========================================================================
    // Internal Methods
    //==========================================================================

    /**
     * Calculate segment boundaries from beat grid
     */
    std::vector<LoopSegment> calculateSegments(const std::vector<float>& beatPositions,
                                                double sampleRate,
                                                int totalSamples,
                                                const Config& config);

    /**
     * Snap a sample position to the nearest zero crossing
     */
    int snapToZeroCrossing(const float* samples, int numSamples,
                           int targetSample, int searchSamples);

    /**
     * Validate if the audio appears to be a proper loop
     */
    bool validateLoop(float bpm, float confidence, int numBeats,
                      float duration, const Config& config);

    //==========================================================================
    // Components
    //==========================================================================

    TempoAnalyzer tempoAnalyzer_;
    juce::AudioFormatManager formatManager_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoopDivider)
};

} // namespace samplar
