#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_core/juce_core.h>

namespace samplar {

/**
 * BandProcessor - Per-band processing effects
 *
 * Applies HP/LP filtering, gates, and transient boost to individual frequency bands.
 * Used by SpectralSeparateDialog to process each of the 5 separated bands.
 */
class BandProcessor
{
public:
    // Processing settings for a single band
    struct Settings
    {
        // HP/LP filters (normalized 0.0 to 1.0 of band width)
        // HP cuts from left (low end of band), LP cuts from right (high end)
        float hpNormalized = 0.0f;  // 0.0 = no HP, 1.0 = filter entire band from low side
        float lpNormalized = 0.0f;  // 0.0 = no LP, 1.0 = filter entire band from high side

        // Gates (same as LarsDebugDialog)
        bool floorGateEnabled = false;
        float floorGateThreshold = 0.1f;  // Samples below this are invisible/zeroed

        bool ceilingGateEnabled = false;
        float ceilingGateThreshold = 0.9f;  // Samples above this are invisible/zeroed

        // Transient boost (same as LarsDebugDialog, up to 100 dB)
        float transientBoostDb = 0.0f;  // 0 = no boost, max 100 dB
    };

    BandProcessor();
    ~BandProcessor() = default;

    /**
     * Process a band buffer with current settings
     *
     * @param buffer Audio buffer to process (modified in place)
     * @param sampleRate Sample rate
     * @param bandLowFreq Lower frequency bound of this band (Hz)
     * @param bandHighFreq Upper frequency bound of this band (Hz)
     * @param settings Processing settings
     */
    void process(
        juce::AudioBuffer<float>& buffer,
        double sampleRate,
        float bandLowFreq,
        float bandHighFreq,
        const Settings& settings
    );

    /**
     * Compute a "display" buffer that shows what the waveform looks like after processing
     * without modifying the original. Used for real-time waveform preview.
     */
    juce::AudioBuffer<float> computeDisplayBuffer(
        const juce::AudioBuffer<float>& source,
        double sampleRate,
        float bandLowFreq,
        float bandHighFreq,
        const Settings& settings
    );

    /**
     * Get waveform peaks for display (like LarsDebugDialog)
     * Returns normalized peak values for each display column
     */
    static std::vector<float> computeWaveformPeaks(
        const juce::AudioBuffer<float>& buffer,
        int numColumns
    );

private:
    // IIR filter coefficients
    juce::dsp::IIR::Coefficients<float>::Ptr hpCoeffs_;
    juce::dsp::IIR::Coefficients<float>::Ptr lpCoeffs_;

    // Envelope follower state for transient detection
    float envelopeValue_ = 0.0f;

    // Apply individual effects
    void applyHPFilter(juce::AudioBuffer<float>& buffer, float cutoffFreq, double sampleRate);
    void applyLPFilter(juce::AudioBuffer<float>& buffer, float cutoffFreq, double sampleRate);
    void applyFloorGate(juce::AudioBuffer<float>& buffer, float threshold);
    void applyCeilingGate(juce::AudioBuffer<float>& buffer, float threshold);
    void applyTransientBoost(juce::AudioBuffer<float>& buffer, float boostDb, double sampleRate);
};

} // namespace samplar
