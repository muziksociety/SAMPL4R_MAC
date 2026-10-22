#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <cstdint>

namespace samplar {

/**
 * SpectralFluxDetector - STFT-based transient detection.
 *
 * Pipeline:
 *   1. STFT spectral flux (half-wave rectified magnitude difference)
 *   2. Adaptive threshold (local median * multiplier) + a floor relative to
 *      the strongest flux peak (no spurious onsets in near-silent stems)
 *   3. Peak picking
 *   4. Attack refinement (C17): flux frame f covers samples
 *      [f*hop, f*hop + fftSize), so the hit lies INSIDE that window, not at
 *      its start. Search [f*hop - fftSize - backtrackSamples,
 *      f*hop + fftSize) for the
 *      sample where short-time energy steps up (analysis::findAttackSample),
 *      i.e. the actual first sample of the attack (old code returned the
 *      window start, 12-23 ms early).
 *   5. Boundary placement: optional pre-roll, then snap to the nearest zero
 *      crossing within zeroCrossMaxMs (backward-biased) so slice edges are
 *      click-free even with MicroFade OFF.
 *   6. Minimum spacing filter (prevents double-triggers)
 *
 * MicroFade: playback applies a kMicroFadeMs linear fade-in at a one-shot
 * voice start. With preRollMs < 0 (auto, default) the boundary is placed
 * kMicroFadeMs BEFORE the attack while MicroFade is ON, so the fade ramps over
 * the pre-attack samples and the transient itself plays intact; with
 * MicroFade OFF there is no pre-roll and the zero-crossing snap alone
 * de-clicks the edge.
 *
 * Pure algorithm class - no JUCE Component, no threading.
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
        int backtrackSamples = 256;      // Extra reach BEFORE the flux frame start for the attack search
        double minSpacingSec = 0.05;     // Minimum 50ms between onsets
        float minPeakRelative = 0.03f;   // Flux peaks below this fraction of the max flux are ignored
        double attackWindowMs = 1.0;     // Energy window for the attack-step search
        bool snapToZeroCrossing = true;  // Snap boundaries to a zero crossing
        double zeroCrossMaxMs = 1.0;     // Max backward snap distance (forward = 1/4 of it)
        double preRollMs = -1.0;         // <0: auto (kMicroFadeMs if MicroFade ON, else 0)
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

    /** Stage 4: refine a flux peak frame to the attack sample (-1 = reject: silent region). */
    int64_t refineAttack(const float* data, int numSamples, int frame, double sampleRate) const;

    /** Stage 5: pre-roll + zero-crossing snap of an attack sample to a slice boundary. */
    int64_t placeBoundary(const float* data, int numSamples, int64_t attack, double sampleRate) const;

public:
    /** Attack samples of the last detect() call (before pre-roll/snap), for diagnostics/tests. */
    const std::vector<int64_t>& getLastAttackSamples() const { return lastAttacks_; }

private:
    mutable std::vector<int64_t> lastAttacks_;

    /** Stage 4b: Filter onsets that are too close together. */
    std::vector<int64_t> enforceMinSpacing(const std::vector<int64_t>& onsets,
                                            double sampleRate) const;

    /** Helper: compute median of a float vector. */
    static float median(std::vector<float> v);
};

} // namespace samplar
