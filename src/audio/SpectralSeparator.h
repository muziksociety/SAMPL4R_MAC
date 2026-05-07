#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <array>
#include <vector>
#include <complex>

namespace samplar {

/**
 * SpectralSeparator - FFT-based frequency band separation with spectrum caching
 *
 * Splits audio into 5 frequency bands using FFT with hard cuts at crossover points.
 * Supports caching the FFT spectrum for instant crossover adjustments.
 *
 * Usage:
 *   1. Call cacheSpectrum() once when loading audio
 *   2. Call separateFromCache() for instant crossover changes
 *   3. Or use separate() which auto-caches on first call
 *
 * Bands: Low, Mid-Low, Mid, Mid-High, High
 */
class SpectralSeparator
{
public:
    // Band identifiers
    enum class Band { Low = 0, MidLow = 1, Mid = 2, MidHigh = 3, High = 4 };
    static constexpr int NumBands = 5;

    // Crossover frequencies structure
    struct CrossoverFrequencies
    {
        float lowToMidLow = 150.0f;     // Hz
        float midLowToMid = 500.0f;     // Hz
        float midToMidHigh = 2000.0f;   // Hz
        float midHighToHigh = 6000.0f;  // Hz

        // Get the low frequency bound for a band
        float getBandLowFreq(Band band) const
        {
            switch (band) {
                case Band::Low:     return 0.0f;
                case Band::MidLow:  return lowToMidLow;
                case Band::Mid:     return midLowToMid;
                case Band::MidHigh: return midToMidHigh;
                case Band::High:    return midHighToHigh;
                default:            return 0.0f;
            }
        }

        // Get the high frequency bound for a band
        float getBandHighFreq(Band band, float nyquist) const
        {
            switch (band) {
                case Band::Low:     return lowToMidLow;
                case Band::MidLow:  return midLowToMid;
                case Band::Mid:     return midToMidHigh;
                case Band::MidHigh: return midHighToHigh;
                case Band::High:    return nyquist;
                default:            return nyquist;
            }
        }
    };

    // Result of separation
    struct SeparationResult
    {
        std::array<juce::AudioBuffer<float>, NumBands> bands;
        double sampleRate = 44100.0;
        bool success = false;
        juce::String errorMessage;
    };

    // Band names for UI
    static juce::String getBandName(Band band)
    {
        switch (band) {
            case Band::Low:     return "LOW";
            case Band::MidLow:  return "MID-LOW";
            case Band::Mid:     return "MID";
            case Band::MidHigh: return "MID-HIGH";
            case Band::High:    return "HIGH";
            default:            return "UNKNOWN";
        }
    }

    // Band colors for UI (matching stem colors from LarsDebugDialog)
    static juce::Colour getBandColour(Band band)
    {
        switch (band) {
            case Band::Low:     return juce::Colour(0xffff6b6b);  // Red (bass)
            case Band::MidLow:  return juce::Colour(0xffffa94d);  // Orange
            case Band::Mid:     return juce::Colour(0xffffd43b);  // Yellow
            case Band::MidHigh: return juce::Colour(0xff69db7c);  // Green
            case Band::High:    return juce::Colour(0xff74c0fc);  // Blue (air)
            default:            return juce::Colours::grey;
        }
    }

    SpectralSeparator();
    ~SpectralSeparator();

    /**
     * Cache the FFT spectrum for later use with separateFromCache()
     * Call this once when loading audio for instant crossover adjustments.
     *
     * @param input Source audio buffer
     * @param sampleRate Sample rate of the audio
     * @return true if caching succeeded
     */
    bool cacheSpectrum(const juce::AudioBuffer<float>& input, double sampleRate);

    /**
     * Check if spectrum is cached and ready for instant separation
     */
    bool hasValidCache() const { return cacheValid_; }

    /**
     * Clear the cached spectrum
     */
    void clearCache();

    /**
     * Separate using cached spectrum (instant - no forward FFT needed)
     * Requires cacheSpectrum() to be called first.
     *
     * @param crossovers Crossover frequency settings
     * @return SeparationResult with 5 separated band buffers
     */
    SeparationResult separateFromCache(const CrossoverFrequencies& crossovers);

    /**
     * Main separation function (auto-caches on first call)
     *
     * @param input Source audio buffer
     * @param sampleRate Sample rate of the audio
     * @param crossovers Crossover frequency settings
     * @return SeparationResult with 5 separated band buffers
     */
    SeparationResult separate(
        const juce::AudioBuffer<float>& input,
        double sampleRate,
        const CrossoverFrequencies& crossovers
    );

private:
    // FFT processing parameters
    static constexpr int fftOrder = 12;  // 4096 samples
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int hopSize = fftSize / 4;  // 75% overlap for smooth reconstruction

    juce::dsp::FFT fft_;
    juce::dsp::WindowingFunction<float> window_;

    // Internal buffers (reused across calls)
    std::vector<float> fftBuffer_;
    std::vector<float> windowBuffer_;

    // ============ SPECTRUM CACHE ============
    // Stores forward FFT results for each frame/channel
    // Structure: cachedFrames_[channel][frameIndex] = complex FFT data
    std::vector<std::vector<std::vector<float>>> cachedFrames_;
    double cachedSampleRate_ = 0.0;
    int cachedNumChannels_ = 0;
    int cachedNumSamples_ = 0;
    int cachedNumFrames_ = 0;
    bool cacheValid_ = false;

    // Pre-computed window for efficiency
    std::vector<float> hannWindow_;

    // Convert frequency to FFT bin index
    int frequencyToBin(float frequency, double sampleRate) const;

    // Process a single FFT frame and split into bands
    void processFrame(
        const float* inputFrame,
        std::array<std::vector<float>&, NumBands> outputFrames,
        const CrossoverFrequencies& crossovers,
        double sampleRate
    );

    // Apply window and overlap-add
    void applyWindow(float* data, int size);
    void overlapAdd(juce::AudioBuffer<float>& output, const float* frame, int position);
};

} // namespace samplar
