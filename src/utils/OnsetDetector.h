#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>

namespace samplar {

/**
 * OnsetDetector - Spectral flux onset detection utility
 *
 * Extracted from BeatAlignedSlicer and IntelligentPatternClassifier
 * to provide a single source of truth for onset detection.
 *
 * Algorithm: Spectral flux with half-wave rectification
 * - Computes magnitude spectra using FFT with Hann window
 * - Calculates flux as sum of positive magnitude differences
 * - Normalizes flux to 0-1 range
 * - Picks peaks above threshold with minimum gap enforcement
 */
class OnsetDetector {
public:
    // Default parameters (preserved from original implementations)
    static constexpr int DEFAULT_FFT_SIZE = 2048;
    static constexpr int DEFAULT_HOP_SIZE = 512;
    static constexpr float DEFAULT_THRESHOLD = 0.1f;
    static constexpr float DEFAULT_MIN_GAP_MS = 50.0f;

    /**
     * Result of onset detection - contains sample positions and strengths
     */
    struct Result {
        std::vector<int> samplePositions;    // Sample index where onset was detected
        std::vector<float> strengths;         // Normalized strength (0-1) of each onset
    };

    /**
     * Detect onsets in an audio buffer using spectral flux
     *
     * @param buffer      Audio buffer (will be converted to mono if stereo)
     * @param sampleRate  Sample rate in Hz
     * @param fftSize     FFT window size (default: 2048)
     * @param hopSize     Hop size between frames (default: 512)
     * @param threshold   Peak threshold (normalized 0-1, default: 0.1)
     * @param minGapMs    Minimum milliseconds between onsets (default: 50)
     * @return Result containing onset sample positions and strengths
     */
    static Result detect(
        const juce::AudioBuffer<float>& buffer,
        double sampleRate,
        int fftSize = DEFAULT_FFT_SIZE,
        int hopSize = DEFAULT_HOP_SIZE,
        float threshold = DEFAULT_THRESHOLD,
        float minGapMs = DEFAULT_MIN_GAP_MS
    );

    /**
     * Convert sample positions to time in seconds
     * Convenience method for callers that need times instead of samples
     */
    static std::vector<float> samplesToTimes(
        const std::vector<int>& samplePositions,
        double sampleRate
    );

private:
    OnsetDetector() = delete;  // Static-only class

    /**
     * Pick peaks from flux envelope
     */
    static std::vector<int> pickPeaks(
        const std::vector<float>& flux,
        float threshold,
        int minDistanceFrames
    );
};

} // namespace samplar
