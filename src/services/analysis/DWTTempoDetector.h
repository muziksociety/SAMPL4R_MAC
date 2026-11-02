#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <vector>
#include <cmath>

namespace samplar {

/**
 * DWT-based Tempo Detector
 *
 * Based on Tzanetakis, Essl, and Cook's approach using
 * Discrete Wavelet Transform for beat detection.
 *
 * Uses Daubechies-4 (db4) wavelet with 4-level decomposition.
 */
class DWTTempoDetector
{
public:
    struct Result
    {
        float bpm = 0.0f;
        float confidence = 0.0f;
        std::vector<float> windowBPMs;  // BPMs from each window (for debugging)
    };

    struct Config
    {
        int numLevels = 2;              // DWT decomposition levels (2=4x downsample for higher precision)
        float minBPM = 40.0f;
        float maxBPM = 220.0f;
        float windowSeconds = 6.0f;     // Analysis window size (longer = more precise)
        float hopSeconds = 3.0f;        // Window hop size (50% overlap)
    };

    DWTTempoDetector() = default;

    /**
     * Detect BPM using DWT-based algorithm.
     * Returns BPM with confidence score.
     */
    // Clang rejects `= Config()` as a default argument inside the enclosing class
    // (NSDMIs not parsed yet), so the default-config forms are forwarding overloads.
    Result detect(const juce::AudioBuffer<float>& buffer,
                  double sampleRate,
                  const Config& config);
    Result detect(const juce::AudioBuffer<float>& buffer, double sampleRate)
    {
        return detect(buffer, sampleRate, Config{});
    }

    /**
     * Detect BPM from a file.
     */
    Result detect(const juce::File& audioFile,
                  const Config& config);
    Result detect(const juce::File& audioFile) { return detect(audioFile, Config{}); }

private:
    juce::AudioFormatManager formatManager_;
    bool formatManagerInitialized_ = false;

    // Daubechies-4 wavelet coefficients
    static constexpr int db4Length = 8;
    static constexpr float db4Lo[8] = {
        -0.010597401784997278f,
         0.032883011666982945f,
         0.030841381835986965f,
        -0.18703481171888114f,
        -0.02798376941698385f,
         0.6308807679295904f,
         0.7148465705525415f,
         0.23037781330885523f
    };
    static constexpr float db4Hi[8] = {
        -0.23037781330885523f,
         0.7148465705525415f,
        -0.6308807679295904f,
        -0.02798376941698385f,
         0.18703481171888114f,
         0.030841381835986965f,
        -0.032883011666982945f,
        -0.010597401784997278f
    };

    /**
     * Perform one level of DWT decomposition.
     * Returns approximation (low-pass) and detail (high-pass) coefficients.
     */
    void dwtDecompose(const std::vector<float>& input,
                      std::vector<float>& approx,
                      std::vector<float>& detail);

    /**
     * Apply low-pass envelope filter: y[n] = 0.01*x[n] + 0.99*y[n-1]
     */
    void envelopeFilter(std::vector<float>& signal);

    /**
     * Full-wave rectification with mean subtraction.
     */
    void rectifyAndNormalize(std::vector<float>& signal);

    /**
     * Downsample by factor.
     */
    std::vector<float> downsample(const std::vector<float>& signal, int factor);

    /**
     * Compute autocorrelation.
     */
    std::vector<float> autocorrelate(const std::vector<float>& signal);

    /**
     * Find BPM from autocorrelation peaks.
     */
    float findBPMFromAutocorr(const std::vector<float>& autocorr,
                               double sampleRate,
                               int downsampleFactor,
                               const Config& config,
                               float& confidence);

    /**
     * Process a single window and return BPM estimate.
     */
    float processWindow(const float* samples,
                        int numSamples,
                        double sampleRate,
                        const Config& config,
                        float& confidence);
};

} // namespace samplar
