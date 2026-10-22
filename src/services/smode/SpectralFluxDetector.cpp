#include "SpectralFluxDetector.h"
#include "../analysis/AnalysisDsp.h"
#include "../../audio/MicroFade.h"
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

    // Stage 2b: floor relative to the strongest flux peak (C17: no absolute
    // floor before -> spurious onsets in near-silent stems).
    const float maxFlux = *std::max_element(flux.begin(), flux.end());
    const float floorFlux = maxFlux * settings_.minPeakRelative;
    for (auto& t : threshold) t = std::max(t, floorFlux);

    // Stage 3: Pick peaks above threshold
    auto peakFrames = pickPeaks(flux, threshold);
    juce::Logger::writeToLog("[SpectralFluxDetector]   Stage 3: " + juce::String((int)peakFrames.size())
                             + " raw peaks detected (floor=" + juce::String(floorFlux, 4) + ")");

    lastAttacks_.clear();
    if (peakFrames.empty()) return {};

    // Stage 4: attack refinement inside the analysis window (C17)
    std::vector<int64_t> attacks;
    attacks.reserve(peakFrames.size());
    int rejectedSilent = 0;
    for (int frame : peakFrames)
    {
        const int64_t a = refineAttack(monoData, numSamples, frame, sampleRate);
        if (a < 0) { ++rejectedSilent; continue; }
        attacks.push_back(a);
    }
    std::sort(attacks.begin(), attacks.end());
    attacks.erase(std::unique(attacks.begin(), attacks.end()), attacks.end());
    attacks = enforceMinSpacing(attacks, sampleRate);
    lastAttacks_ = attacks;

    // Stage 5: boundary placement (pre-roll + zero-crossing snap)
    std::vector<int64_t> boundaries;
    boundaries.reserve(attacks.size());
    for (int64_t a : attacks)
        boundaries.push_back(placeBoundary(monoData, numSamples, a, sampleRate));
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());

    juce::Logger::writeToLog("[SpectralFluxDetector]   Stage 4/5: " + juce::String((int) boundaries.size())
                             + " onsets (attack-refined, " + juce::String(rejectedSilent)
                             + " silent rejected, zcSnap=" + juce::String(settings_.snapToZeroCrossing ? 1 : 0) + ")");
    juce::Logger::writeToLog("[SpectralFluxDetector] === detect END ===");

    return boundaries;
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

// ── Stage 4: Attack refinement ──────────────────────────────────────────────

int64_t SpectralFluxDetector::refineAttack(const float* data, int numSamples,
                                           int frame, double sampleRate) const
{
    const int fftSize = 1 << settings_.fftOrder;
    // The flux peak can come a frame late (noisy attacks keep raising flux
    // while they move toward the window centre), so the search also covers
    // one window before the frame start.
    const int64_t lo = std::max<int64_t>(0, static_cast<int64_t>(frame) * settings_.hopSize
                                             - fftSize - settings_.backtrackSamples);
    const int64_t hi = std::min<int64_t>(numSamples - 1,
                                         static_cast<int64_t>(frame) * settings_.hopSize + fftSize - 1);

    // Silence gate: ignore regions whose peak is below -80 dBFS.
    float peak = 0.0f;
    for (int64_t i = lo; i <= hi; ++i) peak = std::max(peak, std::abs(data[i]));
    if (peak < 1.0e-4f) return -1;

    const int win = std::max(8, static_cast<int>(std::lround(settings_.attackWindowMs * 0.001 * sampleRate)));
    const int64_t a = analysis::findAttackSample(data, numSamples, lo, hi, win,
                                                  static_cast<int>(std::lround(0.010 * sampleRate)));
    return a;
}

int64_t SpectralFluxDetector::placeBoundary(const float* data, int numSamples,
                                            int64_t attack, double sampleRate) const
{
    double preRollMs = settings_.preRollMs;
    if (preRollMs < 0.0)
        preRollMs = microFadeEnabledFlag().load(std::memory_order_relaxed) ? kMicroFadeMs : 0.0;

    int64_t pos = attack - static_cast<int64_t>(std::llround(preRollMs * 0.001 * sampleRate));
    pos = juce::jlimit<int64_t>(0, std::max(0, numSamples - 1), pos);

    if (settings_.snapToZeroCrossing)
    {
        const int back = std::max(1, static_cast<int>(std::lround(settings_.zeroCrossMaxMs * 0.001 * sampleRate)));
        pos = analysis::snapToZeroCrossing(data, numSamples, pos, back, back / 4);
    }
    return pos;
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
