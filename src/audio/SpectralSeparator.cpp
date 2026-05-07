#include "SpectralSeparator.h"

namespace samplar {

SpectralSeparator::SpectralSeparator()
    : fft_(fftOrder)
    , window_(fftSize, juce::dsp::WindowingFunction<float>::hann)
{
    fftBuffer_.resize(fftSize * 2);  // FFT needs 2x for real-only transform
    windowBuffer_.resize(fftSize);

    // Pre-compute Hann window
    hannWindow_.resize(fftSize);
    for (int i = 0; i < fftSize; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(fftSize);
        hannWindow_[i] = 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * t);
    }

    juce::Logger::writeToLog("[SpectralSeparator] Initialized with FFT size: " + juce::String(fftSize) +
                             ", hop size: " + juce::String(hopSize));
}

SpectralSeparator::~SpectralSeparator() = default;

int SpectralSeparator::frequencyToBin(float frequency, double sampleRate) const
{
    // Bin = frequency * fftSize / sampleRate
    float bin = frequency * static_cast<float>(fftSize) / static_cast<float>(sampleRate);
    return juce::jlimit(0, fftSize / 2, static_cast<int>(std::round(bin)));
}

//==============================================================================
// SPECTRUM CACHING
//==============================================================================

bool SpectralSeparator::cacheSpectrum(const juce::AudioBuffer<float>& input, double sampleRate)
{
    juce::Logger::writeToLog("[SpectralSeparator] === CACHING SPECTRUM ===");

    if (input.getNumSamples() == 0 || input.getNumChannels() == 0)
    {
        juce::Logger::writeToLog("[SpectralSeparator] ERROR: Empty input buffer");
        return false;
    }

    const int numChannels = input.getNumChannels();
    const int numSamples = input.getNumSamples();

    // Calculate number of frames
    int numFrames = 0;
    for (int frameStart = 0; frameStart < numSamples; frameStart += hopSize)
        numFrames++;

    juce::Logger::writeToLog("[SpectralSeparator] Caching " + juce::String(numFrames) +
                             " frames, " + juce::String(numChannels) + " channels");

    try
    {
        // Allocate cache storage
        cachedFrames_.resize(numChannels);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            cachedFrames_[ch].resize(numFrames);
            for (int f = 0; f < numFrames; ++f)
            {
                cachedFrames_[ch][f].resize(fftSize * 2, 0.0f);
            }
        }

        // Working buffer
        std::vector<float> fftData(fftSize * 2, 0.0f);

        // Process each channel
        for (int channel = 0; channel < numChannels; ++channel)
        {
            const float* inputData = input.getReadPointer(channel);
            int frameIndex = 0;

            // Process overlapping frames
            for (int frameStart = 0; frameStart < numSamples; frameStart += hopSize)
            {
                // Clear FFT buffer
                std::fill(fftData.begin(), fftData.end(), 0.0f);

                // Copy and window the input frame
                for (int i = 0; i < fftSize; ++i)
                {
                    int sampleIdx = frameStart + i;
                    if (sampleIdx < numSamples)
                        fftData[i] = inputData[sampleIdx] * hannWindow_[i];
                    else
                        fftData[i] = 0.0f;
                }

                // Forward FFT (real-only)
                fft_.performRealOnlyForwardTransform(fftData.data(), true);

                // Store in cache
                cachedFrames_[channel][frameIndex] = fftData;
                frameIndex++;
            }
        }

        // Store cache metadata
        cachedSampleRate_ = sampleRate;
        cachedNumChannels_ = numChannels;
        cachedNumSamples_ = numSamples;
        cachedNumFrames_ = numFrames;
        cacheValid_ = true;

        juce::Logger::writeToLog("[SpectralSeparator] === SPECTRUM CACHED SUCCESSFULLY ===");
        return true;
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog("[SpectralSeparator] ERROR caching spectrum: " + juce::String(e.what()));
        clearCache();
        return false;
    }
}

void SpectralSeparator::clearCache()
{
    cachedFrames_.clear();
    cachedSampleRate_ = 0.0;
    cachedNumChannels_ = 0;
    cachedNumSamples_ = 0;
    cachedNumFrames_ = 0;
    cacheValid_ = false;
}

SpectralSeparator::SeparationResult SpectralSeparator::separateFromCache(const CrossoverFrequencies& crossovers)
{
    SeparationResult result;
    result.sampleRate = cachedSampleRate_;

    if (!cacheValid_)
    {
        result.success = false;
        result.errorMessage = "No valid spectrum cache";
        juce::Logger::writeToLog("[SpectralSeparator] ERROR: No valid cache for separateFromCache()");
        return result;
    }

    juce::Logger::writeToLog("[SpectralSeparator] === SEPARATING FROM CACHE (instant) ===");

    try
    {
        // Calculate bin ranges for each band
        std::array<int, NumBands> bandStartBins;
        std::array<int, NumBands> bandEndBins;

        bandStartBins[0] = 0;
        bandEndBins[0] = frequencyToBin(crossovers.lowToMidLow, cachedSampleRate_);

        bandStartBins[1] = bandEndBins[0];
        bandEndBins[1] = frequencyToBin(crossovers.midLowToMid, cachedSampleRate_);

        bandStartBins[2] = bandEndBins[1];
        bandEndBins[2] = frequencyToBin(crossovers.midToMidHigh, cachedSampleRate_);

        bandStartBins[3] = bandEndBins[2];
        bandEndBins[3] = frequencyToBin(crossovers.midHighToHigh, cachedSampleRate_);

        bandStartBins[4] = bandEndBins[3];
        bandEndBins[4] = fftSize / 2;

        // Initialize output buffers
        for (int b = 0; b < NumBands; ++b)
        {
            result.bands[b].setSize(cachedNumChannels_, cachedNumSamples_);
            result.bands[b].clear();
        }

        // Working buffer for inverse FFT
        std::vector<float> bandFftData(fftSize * 2, 0.0f);

        // Process each channel from cache
        for (int channel = 0; channel < cachedNumChannels_; ++channel)
        {
            int frameIndex = 0;

            // Process each cached frame
            for (int frameStart = 0; frameStart < cachedNumSamples_; frameStart += hopSize)
            {
                if (frameIndex >= cachedNumFrames_)
                    break;

                const auto& cachedFft = cachedFrames_[channel][frameIndex];

                // Split cached spectrum into bands
                for (int b = 0; b < NumBands; ++b)
                {
                    // Clear band FFT data
                    std::fill(bandFftData.begin(), bandFftData.end(), 0.0f);

                    // Copy frequency bins for this band from cache
                    for (int bin = bandStartBins[b]; bin < bandEndBins[b] && bin <= fftSize / 2; ++bin)
                    {
                        bandFftData[bin * 2] = cachedFft[bin * 2];         // Real
                        bandFftData[bin * 2 + 1] = cachedFft[bin * 2 + 1]; // Imag
                    }

                    // Inverse FFT
                    fft_.performRealOnlyInverseTransform(bandFftData.data());

                    // Overlap-add to output with window
                    float* outputData = result.bands[b].getWritePointer(channel);
                    for (int i = 0; i < fftSize; ++i)
                    {
                        int outIdx = frameStart + i;
                        if (outIdx < cachedNumSamples_)
                        {
                            outputData[outIdx] += bandFftData[i] * hannWindow_[i] * 0.5f;
                        }
                    }
                }

                frameIndex++;
            }
        }

        result.success = true;
        juce::Logger::writeToLog("[SpectralSeparator] === SEPARATION FROM CACHE COMPLETE ===");
    }
    catch (const std::exception& e)
    {
        result.success = false;
        result.errorMessage = juce::String("Exception: ") + e.what();
        juce::Logger::writeToLog("[SpectralSeparator] ERROR: " + result.errorMessage);
    }

    return result;
}

//==============================================================================
// MAIN SEPARATION (auto-caches)
//==============================================================================

SpectralSeparator::SeparationResult SpectralSeparator::separate(
    const juce::AudioBuffer<float>& input,
    double sampleRate,
    const CrossoverFrequencies& crossovers)
{
    // If cache is valid and matches input, use fast path
    if (cacheValid_ &&
        cachedSampleRate_ == sampleRate &&
        cachedNumChannels_ == input.getNumChannels() &&
        cachedNumSamples_ == input.getNumSamples())
    {
        juce::Logger::writeToLog("[SpectralSeparator] Using cached spectrum for instant separation");
        return separateFromCache(crossovers);
    }

    // Otherwise, cache spectrum first then separate
    juce::Logger::writeToLog("[SpectralSeparator] === STARTING SEPARATION (with caching) ===");
    juce::Logger::writeToLog("[SpectralSeparator] Input: " + juce::String(input.getNumSamples()) +
                             " samples, " + juce::String(input.getNumChannels()) + " channels");

    SeparationResult result;
    result.sampleRate = sampleRate;

    if (input.getNumSamples() == 0 || input.getNumChannels() == 0)
    {
        result.success = false;
        result.errorMessage = "Input buffer is empty";
        juce::Logger::writeToLog("[SpectralSeparator] ERROR: Empty input buffer");
        return result;
    }

    // For very short samples, just copy to all bands (no separation possible)
    if (input.getNumSamples() < 256)
    {
        juce::Logger::writeToLog("[SpectralSeparator] Sample too short for FFT, copying to all bands");
        for (int b = 0; b < NumBands; ++b)
        {
            result.bands[b].makeCopyOf(input);
            float gain = 1.0f / NumBands;
            result.bands[b].applyGain(gain);
        }
        result.success = true;
        return result;
    }

    // Cache the spectrum
    if (!cacheSpectrum(input, sampleRate))
    {
        result.success = false;
        result.errorMessage = "Failed to cache spectrum";
        return result;
    }

    // Now separate from cache
    return separateFromCache(crossovers);
}

} // namespace samplar
