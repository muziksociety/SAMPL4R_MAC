#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <cstdint>

namespace samplar {

/**
 * SpectralFluxDetector — STFT-based transient detection.
 *
 * 4-stage pipeline:
 *   1. STFT spectral flux (half-wave rectified magnitude difference)
 *   2. Adaptive threshold (median + multiplier over local window)
 *   3. Backtrack refinement (walk backwards to find true onset)
 *   4. Minimum spacing filter (prevents double-triggers)
 *
 * Inspired by INTERSECT's production-grade approach, adapted for S-Mode.
 * This is a pure algorithm class — no JUCE Component, no threading.
 * Call detect() from any thread with a mono float buffer.
 */
class SpectralFluxDetector
{
public:
    struct Settings
    {
        int fftOrder         = 10;       // 2^10 = 1024 samples
        int hopSize          = 512;      // 50% overlap
        float thresholdMult  = 1.5f;     // Multiplier above median for peak detection
        int medianWindowSize = 11;       // Local median window (odd number)
        int backtrackSamples = 256;      // Max samples to walk back for true onset
        double minSpacingSec = 0.05;     // Minimum 50ms between onsets
    };

    SpectralFluxDetector() = default;
    explicit SpectralFluxDetector(const Settings& s) : settings_(s) {}

    /**
     * Detect transient onsets in a mono audio buffer.
     *
     * @param monoData    Pointer to mono float samples
     * @param numSamples  Number of samples
     * @param sampleRate  Sample rate in Hz
     * @return            Vector of onset positions in samples (sorted ascending)
     */
    std::vector<int64_t> detect(const float* monoData, int numSamples, double sampleRate) const;

    Settings& getSettings() { return settings_; }
    const Settings& getSettings() const { return settings_; }

private:
    Settings settings_;

    /** Stage 1: Compute spectral flux curve from STFT. */
    std::vector<float> computeSpectralFlux(const float* data, int numSamples) const;

    /** Stage 2: Adaptive threshold via running median. */
    std::vector<float> computeAdaptiveThreshold(const std::vector<float>& flux) const;

    /** Stage 3: Pick peaks above threshold. */
    std::vector<int> pickPeaks(const std::vector<float>& flux,
                               const std::vector<float>& threshold) const;

    /** Stage 4a: Backtrack each peak to find true onset in raw audio. */
    int64_t backtrack(const float* data, int numSamples, int64_t peakSample) const;

    /** Stage 4b: Filter onsets that are too close together. */
    std::vector<int64_t> enforceMinSpacing(const std::vector<int64_t>& onsets,
                                            double sampleRate) const;

    /** Helper: compute median of a float vector. */
    static float median(std::vector<float> v);
};

} // namespace samplar
