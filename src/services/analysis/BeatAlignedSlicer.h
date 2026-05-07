#pragma once

#include "TempoAnalyzer.h"
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>

namespace samplar {

/**
 * BeatAlignedSlicer - Intelligent sample slicing using beat grid
 *
 * Combines transient detection with beat grid alignment for precise slicing.
 * This is step 4 in the tempo-first pipeline:
 *   1. Audio Loading
 *   2. Tempo Analysis (TempoAnalyzer)
 *   3. Loop Division (LoopDivider)
 *   4. Sample Slicing (BeatAlignedSlicer) <-- THIS
 *   5. Classification (DrumAnalyzer)
 *   6. Variation Generation
 *
 * Key features:
 * - Snaps transients to nearby beats when within threshold
 * - Preserves ghost notes that fall between beats
 * - Quantizes weak transients to beat positions
 * - Exports individual hit samples with beat-aligned boundaries
 */
class BeatAlignedSlicer
{
public:
    //==========================================================================
    // Configuration
    //==========================================================================

    struct Config {
        // Onset detection
        int hopSize = 512;
        int fftSize = 2048;
        float onsetThreshold = 0.1f;   // Spectral flux threshold

        // Beat alignment
        float beatSnapThresholdMs = 20.0f;   // Snap to beat if within this distance
        float ghostNoteThresholdDb = -18.0f; // Below this level = ghost note
        bool keepGhostNotes = true;          // Include ghost notes in output
        bool quantizeWeakOnsets = true;      // Move weak onsets to beat positions

        // Output
        int minSliceLengthMs = 20;           // Minimum slice length
        int maxSliceLengthMs = 5000;         // Maximum slice length
        bool fadeInOut = true;               // Apply short fade to slices
        int fadeMs = 2;                      // Fade length in ms
    };

    //==========================================================================
    // Result Types
    //==========================================================================

    enum class SliceType {
        OnBeat,      // Transient aligned to beat
        OffBeat,     // Transient between beats
        GhostNote,   // Quiet transient
        Quantized    // Weak transient moved to beat
    };

    struct Slice {
        int startSample = 0;
        int endSample = 0;
        float startTime = 0.0f;
        float endTime = 0.0f;
        int beatIndex = -1;           // Nearest beat (-1 if off-beat)
        float beatDistance = 0.0f;    // Distance from nearest beat (seconds)
        SliceType type = SliceType::OffBeat;
        float onsetStrength = 0.0f;   // How strong the onset is
        float peakLevel = 0.0f;       // Peak amplitude in dB
    };

    struct Result {
        std::vector<Slice> slices;
        float bpm = 0.0f;
        float confidence = 0.0f;
        int onBeatCount = 0;          // Slices that landed on beats
        int offBeatCount = 0;         // Slices between beats
        int ghostNoteCount = 0;       // Quiet slices
        int quantizedCount = 0;       // Slices that were quantized
        juce::String error;
    };

    //==========================================================================
    // Public API
    //==========================================================================

    BeatAlignedSlicer();
    ~BeatAlignedSlicer() = default;

    /**
     * Slice audio using pre-computed tempo analysis
     */
    Result slice(const juce::AudioBuffer<float>& buffer,
                 double sampleRate,
                 const TempoAnalyzer::Result& tempoResult,
                 const Config& config = Config());

    /**
     * Slice audio with internal tempo analysis
     */
    Result sliceWithAnalysis(const juce::AudioBuffer<float>& buffer,
                             double sampleRate,
                             const Config& config = Config(),
                             const TempoAnalyzer::Config& tempoConfig = TempoAnalyzer::Config());

    /**
     * Slice an audio file
     */
    Result sliceFile(const juce::File& audioFile,
                     const Config& config = Config(),
                     const TempoAnalyzer::Config& tempoConfig = TempoAnalyzer::Config());

    /**
     * Extract a slice as a new buffer
     */
    juce::AudioBuffer<float> extractSlice(const juce::AudioBuffer<float>& source,
                                          const Slice& slice,
                                          bool applyFade = true,
                                          int fadeSamples = 64);

    /**
     * Export all slices to files
     */
    juce::Array<juce::File> exportSlices(const juce::AudioBuffer<float>& source,
                                         double sampleRate,
                                         const std::vector<Slice>& slices,
                                         const juce::File& outputDir,
                                         const juce::String& prefix = "slice_");

private:
    //==========================================================================
    // Onset Detection
    //==========================================================================

    /**
     * Detect onsets using spectral flux
     */
    std::vector<float> detectOnsets(const juce::AudioBuffer<float>& buffer,
                                     double sampleRate,
                                     const Config& config,
                                     std::vector<float>& onsetStrengths);

    /**
     * Pick peaks from onset envelope
     */
    std::vector<int> pickPeaks(const std::vector<float>& envelope,
                                float threshold,
                                int minDistance);

    //==========================================================================
    // Beat Alignment
    //==========================================================================

    /**
     * Align onsets to beat grid
     */
    std::vector<Slice> alignOnsetsToBeatGrid(const std::vector<float>& onsetTimes,
                                              const std::vector<float>& onsetStrengths,
                                              const std::vector<float>& beatPositions,
                                              double sampleRate,
                                              int totalSamples,
                                              const Config& config);

    /**
     * Find nearest beat position
     */
    int findNearestBeat(float time, const std::vector<float>& beatPositions,
                        float& outDistance);

    /**
     * Classify slice type based on beat alignment and level
     */
    SliceType classifySlice(float beatDistance, float peakLevel,
                            float beatSnapThreshold, float ghostThreshold);

    //==========================================================================
    // Audio Analysis
    //==========================================================================

    /**
     * Calculate peak level in dB for a buffer region
     */
    float calculatePeakLevel(const float* samples, int start, int length);

    /**
     * Apply fade in/out to buffer
     */
    void applyFade(juce::AudioBuffer<float>& buffer, int fadeSamples);

    //==========================================================================
    // Components
    //==========================================================================

    TempoAnalyzer tempoAnalyzer_;
    juce::AudioFormatManager formatManager_;
    std::unique_ptr<juce::dsp::FFT> fft_;
    int currentFFTSize_ = 0;

    void ensureFFTSize(int size);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BeatAlignedSlicer)
};

} // namespace samplar
