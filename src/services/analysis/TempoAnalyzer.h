#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>

namespace samplar {

/**
 * TempoAnalyzer - BPM and downbeat detection using autocorrelation
 *
 * Uses spectral flux for onset detection, then autocorrelation of the
 * onset envelope to find the dominant tempo. Perceptual weighting
 * centers the preference around 120 BPM for natural musical tempos.
 */
class TempoAnalyzer
{
public:
    //==========================================================================
    // Configuration
    //==========================================================================

    struct Config {
        // FFT settings
        int fftSize = 2048;
        int hopSize = 512;

        // Tempo range (BPM)
        float minBPM = 60.0f;
        float maxBPM = 200.0f;
        float preferredBPM = 120.0f;  // Center of perceptual weighting

        // Analysis settings
        float onsetThreshold = 0.1f;   // Spectral flux threshold
        bool detectDownbeat = true;    // Whether to find beat 1
    };

    //==========================================================================
    // Result
    //==========================================================================

    struct Result {
        // Tempo detection
        float bpm = 0.0f;              // Detected BPM (0 = not detected)
        float confidence = 0.0f;       // 0-1 confidence in BPM

        // Beat positions (in seconds)
        std::vector<float> beatPositions;  // All detected beat positions

        // Downbeat detection
        int downbeatIndex = 0;         // Index in beatPositions of beat 1
        float downbeatConfidence = 0.0f;

        // Sample classification hint
        bool isLoop = false;           // True if tempo detected with high confidence
        float loopLengthBeats = 0.0f;  // Estimated loop length in beats

        // Error handling
        juce::String error;
    };

    //==========================================================================
    // Public API
    //==========================================================================

    TempoAnalyzer();
    ~TempoAnalyzer() = default;

    /**
     * Analyze tempo from audio file
     */
    Result analyze(const juce::File& audioFile, const Config& config = Config());

    /**
     * Analyze tempo from audio buffer
     */
    Result analyze(const juce::AudioBuffer<float>& buffer, double sampleRate,
                   const Config& config = Config());

    /**
     * Quick BPM-only analysis (faster, no beat positions)
     */
    float detectBPM(const juce::AudioBuffer<float>& buffer, double sampleRate,
                    const Config& config = Config());

private:
    //==========================================================================
    // Onset Detection (Spectral Flux)
    //==========================================================================

    /**
     * Compute spectral flux onset envelope
     * Returns normalized onset strength at each hop
     */
    std::vector<float> computeOnsetEnvelope(const float* samples, int numSamples,
                                            double sampleRate, const Config& config);

    /**
     * Pick peaks in onset envelope
     */
    std::vector<int> pickOnsetPeaks(const std::vector<float>& envelope,
                                    float threshold);

    //==========================================================================
    // Tempo Detection (Autocorrelation)
    //==========================================================================

    /**
     * Compute autocorrelation of onset envelope
     */
    std::vector<float> computeAutocorrelation(const std::vector<float>& envelope);

    /**
     * Find tempo from autocorrelation peaks with perceptual weighting
     */
    float findTempoFromAutocorr(const std::vector<float>& autocorr,
                                double hopRate, const Config& config,
                                float& outConfidence);

    /**
     * Apply perceptual tempo weighting (preference for ~120 BPM)
     */
    float applyTempoWeighting(float bpm, float preferredBPM);

    //==========================================================================
    // Downbeat Detection
    //==========================================================================

    /**
     * Find the downbeat (beat 1) position
     * Uses low-frequency energy emphasis to find strong beats
     */
    int findDownbeat(const float* samples, int numSamples, double sampleRate,
                     const std::vector<float>& beatPositions, float& confidence);

    /**
     * Compute low-frequency energy at a given position
     */
    float computeLowFreqEnergy(const float* samples, int numSamples,
                               double sampleRate, int position, int windowSize);

    //==========================================================================
    // Beat Grid
    //==========================================================================

    /**
     * Generate beat grid from detected tempo and starting position
     */
    std::vector<float> generateBeatGrid(float bpm, float startTime,
                                        float duration, double sampleRate);

    //==========================================================================
    // Loop Analysis
    //==========================================================================

    /**
     * Check if sample appears to be a loop based on tempo alignment
     */
    bool analyzeLoopCharacteristics(float bpm, float duration,
                                    float& outLoopBeats);

    //==========================================================================
    // Utilities
    //==========================================================================

    juce::AudioFormatManager formatManager_;

    // Cached FFT
    std::unique_ptr<juce::dsp::FFT> fft_;
    int currentFFTSize_ = 0;

    void ensureFFTSize(int size);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TempoAnalyzer)
};

} // namespace samplar
