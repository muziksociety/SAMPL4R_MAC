#include "SpectralFluxDetector.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace samplar {

// ── Public API ───────────────────────────────────────────────────────────────

std::vector<int64_t> SpectralFluxDetector::detect(const float* monoData,
                                                   int numSamples,
                                                   double sampleRate) const
{
    if (!monoData || numSamples < (1 << settings_.fftOrder))
        return {};

    juce::Logger::writeToLog("[SpectralFluxDetector] === detect START ===");
    juce::Logger::writeToLog("[SpectralFluxDetector]   samples=" + juce::String(numSamples)
                             + " sr=" + juce::String(sampleRate, 0)
                             + " fftOrder=" + juce::String(settings_.fftOrder)
                             + " hop=" + juce::String(settings_.hopSize));

    // Stage 1: Spectral flux
    auto flux = computeSpectralFlux(monoData, numSamples);
    juce::Logger::writeToLog("[SpectralFluxDetector]   Stage 1: " + juce::String((int)flux.size())
                             + " flux frames computed");

    if (flux.empty()) return {};

    // Stage 2: Adaptive threshold
    auto threshold = computeAdaptiveThreshold(flux);
    juce::Logger::writeToLog("[SpectralFluxDetector]   Stage 2: adaptive threshold computed");

    // Stage 3: Pick peaks above threshold
    auto peakFrames = pickPeaks(flux, threshold);
    juce::Logger::writeToLog("[SpectralFluxDetector]   Stage 3: " + juce::String((int)peakFrames.size())
                             + " raw peaks detected");

    if (peakFrames.empty()) return {};

    // Convert frame indices to sample positions
    std::vector<int64_t> rawOnsets;
    rawOnsets.reserve(peakFrames.size());
    for (int frame : peakFrames)
    {
        int64_t samplePos = static_cast<int64_t>(frame) * settings_.hopSize;
        samplePos = std::min(samplePos, static_cast<int64_t>(numSamples - 1));
        rawOnsets.push_back(samplePos);
    }

    // Stage 4a: Backtrack refinement
    std::vector<int64_t> refined;
    refined.reserve(rawOnsets.size());
    for (int64_t onset : rawOnsets)
        refined.push_back(backtrack(monoData, numSamples, onset));

    // Stage 4b: Minimum spacing filter
    auto result = enforceMinSpacing(refined, sampleRate);

    juce::Logger::writeToLog("[SpectralFluxDetector]   Stage 4: " + juce::String((int)result.size())
                             + " onsets after backtrack + spacing filter");
    juce::Logger::writeToLog("[SpectralFluxDetector] === detect END ===");

    return result;
}

// ── Stage 1: Spectral Flux ──────────────────────────────────────────────────

std::vector<float> SpectralFluxDetector::computeSpectralFlux(const float* data,
                                                              int numSamples) const
{
    const int fftSize = 1 << settings_.fftOrder;
    const int hopSize = settings_.hopSize;
    const int numBins = fftSize / 2;

    juce::dsp::FFT fft(settings_.fftOrder);

    // Hann window
    std::vector<float> window(fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[i] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * i / (fftSize - 1)));

    std::vector<float> fftBuf(fftSize * 2, 0.0f);
    std::vector<float> prevMag(numBins, 0.0f);
    std::vector<float> flux;

    int numFrames = (numSamples - fftSize) / hopSize + 1;
    flux.reserve(numFrames);

    for (int frame = 0; frame < numFrames; ++frame)
    {
        int offset = frame * hopSize;

        // Windowed frame → FFT buffer
        std::fill(fftBuf.begin(), fftBuf.end(), 0.0f);
        for (int i = 0; i < fftSize; ++i)
            fftBuf[i] = data[offset + i] * window[i];

        fft.performRealOnlyForwardTransform(fftBuf.data());

        // Compute magnitude spectrum + half-wave rectified flux
        float frameFlux = 0.0f;
        for (int b = 0; b < numBins; ++b)
        {
            float re = fftBuf[b * 2];
            float im = fftBuf[b * 2 + 1];
            float mag = std::sqrt(re * re + im * im);

            // Half-wave rectification: only count increases
            float diff = mag - prevMag[b];
            if (diff > 0.0f)
                frameFlux += diff;

            prevMag[b] = mag;
        }

        flux.push_back(frameFlux);
    }

    return flux;
}

// ── Stage 2: Adaptive Threshold ─────────────────────────────────────────────

std::vector<float> SpectralFluxDetector::computeAdaptiveThreshold(
    const std::vector<float>& flux) const
{
    const int n = static_cast<int>(flux.size());
    const int halfWin = settings_.medianWindowSize / 2;
    const float mult = settings_.thresholdMult;

    std::vector<float> threshold(n);

    for (int i = 0; i < n; ++i)
    {
        int lo = std::max(0, i - halfWin);
        int hi = std::min(n - 1, i + halfWin);

        std::vector<float> localWindow(flux.begin() + lo, flux.begin() + hi + 1);
        float med = median(std::move(localWindow));

        threshold[i] = med * mult;
    }

    return threshold;
}

// ── Stage 3: Peak Picking ───────────────────────────────────────────────────

std::vector<int> SpectralFluxDetector::pickPeaks(const std::vector<float>& flux,
                                                  const std::vector<float>& threshold) const
{
    std::vector<int> peaks;
    const int n = static_cast<int>(flux.size());

    for (int i = 1; i < n - 1; ++i)
    {
        // Must be above adaptive threshold AND be a local maximum
        if (flux[i] > threshold[i] &&
            flux[i] > flux[i - 1] &&
            flux[i] >= flux[i + 1])
        {
            peaks.push_back(i);
        }
    }

    return peaks;
}

// ── Stage 4a: Backtrack Refinement ──────────────────────────────────────────

int64_t SpectralFluxDetector::backtrack(const float* data, int numSamples,
                                         int64_t peakSample) const
{
    // Walk backwards from the detected peak to find where the signal
    // first rises from a local minimum — the true onset point
    const int maxBack = settings_.backtrackSamples;
    int64_t bestPos = peakSample;
    float minEnergy = std::numeric_limits<float>::max();

    int64_t startSearch = std::max((int64_t)0, peakSample - maxBack);

    // Use short RMS windows (32 samples) to find energy minimum before the peak
    static constexpr int kMicroWindow = 32;

    for (int64_t pos = peakSample; pos >= startSearch; pos -= kMicroWindow)
    {
        int len = std::min(kMicroWindow, static_cast<int>(numSamples - pos));
        if (len <= 0) break;

        float sumSq = 0.0f;
        for (int i = 0; i < len; ++i)
            sumSq += data[pos + i] * data[pos + i];
        float energy = sumSq / static_cast<float>(len);

        if (energy < minEnergy)
        {
            minEnergy = energy;
            bestPos = pos;
        }
    }

    return bestPos;
}

// ── Stage 4b: Minimum Spacing Filter ────────────────────────────────────────

std::vector<int64_t> SpectralFluxDetector::enforceMinSpacing(
    const std::vector<int64_t>& onsets, double sampleRate) const
{
    if (onsets.empty()) return {};

    int64_t minGap = static_cast<int64_t>(settings_.minSpacingSec * sampleRate);
    std::vector<int64_t> filtered;
    filtered.push_back(onsets[0]);

    for (size_t i = 1; i < onsets.size(); ++i)
    {
        if (onsets[i] - filtered.back() >= minGap)
            filtered.push_back(onsets[i]);
    }

    return filtered;
}

// ── Helpers ─────────────────────────────────────────────────────────────────

float SpectralFluxDetector::median(std::vector<float> v)
{
    if (v.empty()) return 0.0f;
    size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + mid, v.end());
    return v[mid];
}

} // namespace samplar
