#include "OnsetDetector.h"
#include <cmath>
#include <algorithm>

namespace samplar {

// ============================================================================
// ONSET DETECTION - SPECTRAL FLUX ALGORITHM
// ============================================================================

OnsetDetector::Result OnsetDetector::detect(
    const juce::AudioBuffer<float>& buffer,
    double sampleRate,
    int fftSize,
    int hopSize,
    float threshold,
    float minGapMs
) {
    Result result;

    juce::Logger::writeToLog("[OnsetDetector] === detect START ===");
    juce::Logger::writeToLog("[OnsetDetector]   sampleRate: " + juce::String(sampleRate) +
                            ", fftSize: " + juce::String(fftSize) +
                            ", hopSize: " + juce::String(hopSize));
    juce::Logger::writeToLog("[OnsetDetector]   threshold: " + juce::String(threshold, 3) +
                            ", minGapMs: " + juce::String(minGapMs, 1));

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    const int numBins = fftSize / 2 + 1;

    juce::Logger::writeToLog("[OnsetDetector]   Buffer: " + juce::String(numSamples) +
                            " samples, " + juce::String(numChannels) + " channels");

    if (numSamples < fftSize) {
        juce::Logger::writeToLog("[OnsetDetector] WARNING: Buffer too short for FFT");
        juce::Logger::writeToLog("[OnsetDetector] === detect END (no onsets) ===");
        return result;
    }

    // Mix to mono
    std::vector<float> mono(static_cast<size_t>(numSamples), 0.0f);
    for (int ch = 0; ch < numChannels; ++ch) {
        const float* channelData = buffer.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i) {
            mono[static_cast<size_t>(i)] += channelData[i] / static_cast<float>(numChannels);
        }
    }
    juce::Logger::writeToLog("[OnsetDetector]   Converted to mono");

    // Create FFT
    int fftOrder = static_cast<int>(std::log2(fftSize));
    juce::dsp::FFT fft(fftOrder);

    // Compute spectral flux
    std::vector<float> fftData(static_cast<size_t>(fftSize * 2), 0.0f);
    std::vector<float> prevSpectrum(static_cast<size_t>(numBins), 0.0f);
    std::vector<float> flux;
    std::vector<int> framePositions;

    int frameCount = 0;
    for (int frame = 0; frame + fftSize <= numSamples; frame += hopSize) {
        // Copy and apply Hann window
        for (int i = 0; i < fftSize; ++i) {
            float window = 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi *
                                                   static_cast<float>(i) / static_cast<float>(fftSize - 1));
            fftData[static_cast<size_t>(i)] = mono[static_cast<size_t>(frame + i)] * window;
        }
        std::fill(fftData.begin() + fftSize, fftData.end(), 0.0f);

        // FFT
        fft.performRealOnlyForwardTransform(fftData.data());

        // Compute magnitude spectrum and half-wave rectified spectral flux
        float fluxValue = 0.0f;
        for (int bin = 0; bin < numBins; ++bin) {
            float real = fftData[static_cast<size_t>(bin * 2)];
            float imag = fftData[static_cast<size_t>(bin * 2 + 1)];
            float mag = std::sqrt(real * real + imag * imag);

            // Half-wave rectified: only positive changes count
            float diff = mag - prevSpectrum[static_cast<size_t>(bin)];
            if (diff > 0.0f)
                fluxValue += diff;

            prevSpectrum[static_cast<size_t>(bin)] = mag;
        }

        flux.push_back(fluxValue);
        framePositions.push_back(frame);
        ++frameCount;
    }

    juce::Logger::writeToLog("[OnsetDetector]   Processed " + juce::String(frameCount) + " frames");

    if (flux.empty()) {
        juce::Logger::writeToLog("[OnsetDetector] WARNING: No flux computed");
        juce::Logger::writeToLog("[OnsetDetector] === detect END (no onsets) ===");
        return result;
    }

    // Normalize flux
    float maxFlux = 0.0f;
    for (float f : flux)
        maxFlux = std::max(maxFlux, f);

    if (maxFlux > 0.0f) {
        for (float& f : flux)
            f /= maxFlux;
    }

    juce::Logger::writeToLog("[OnsetDetector]   Max flux (pre-norm): " + juce::String(maxFlux, 2));

    // Pick peaks
    const int minDistanceFrames = static_cast<int>(sampleRate * (minGapMs / 1000.0) / hopSize);
    juce::Logger::writeToLog("[OnsetDetector]   Peak picking: threshold=" + juce::String(threshold, 3) +
                            ", minGap=" + juce::String(minDistanceFrames) + " frames");

    auto peakFrames = pickPeaks(flux, threshold, minDistanceFrames);

    juce::Logger::writeToLog("[OnsetDetector]   Found " + juce::String(static_cast<int>(peakFrames.size())) + " peaks");

    // Convert to sample positions and strengths
    for (int frameIdx : peakFrames) {
        int samplePos = framePositions[static_cast<size_t>(frameIdx)];
        float strength = flux[static_cast<size_t>(frameIdx)];

        result.samplePositions.push_back(samplePos);
        result.strengths.push_back(strength);
    }

    juce::Logger::writeToLog("[OnsetDetector]   Detected " + juce::String(static_cast<int>(result.samplePositions.size())) + " onsets");
    juce::Logger::writeToLog("[OnsetDetector] === detect END ===");

    return result;
}

// ============================================================================
// PEAK PICKING
// ============================================================================

std::vector<int> OnsetDetector::pickPeaks(
    const std::vector<float>& flux,
    float threshold,
    int minDistanceFrames
) {
    std::vector<int> peaks;

    for (size_t i = 1; i + 1 < flux.size(); ++i) {
        if (flux[i] <= threshold)
            continue;

        // Local maximum check
        if (flux[i] > flux[i - 1] && flux[i] >= flux[i + 1]) {
            // Check distance from last peak
            if (peaks.empty() ||
                static_cast<int>(i) - peaks.back() >= minDistanceFrames) {
                peaks.push_back(static_cast<int>(i));
            }
            else if (flux[i] > flux[static_cast<size_t>(peaks.back())]) {
                // Replace weaker peak with this stronger one
                peaks.back() = static_cast<int>(i);
            }
        }
    }

    return peaks;
}

// ============================================================================
// UTILITY METHODS
// ============================================================================

std::vector<float> OnsetDetector::samplesToTimes(
    const std::vector<int>& samplePositions,
    double sampleRate
) {
    std::vector<float> times;
    times.reserve(samplePositions.size());

    for (int samplePos : samplePositions) {
        float time = static_cast<float>(samplePos) / static_cast<float>(sampleRate);
        times.push_back(time);
    }

    return times;
}

} // namespace samplar
