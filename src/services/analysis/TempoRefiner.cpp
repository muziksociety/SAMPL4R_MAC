#include "TempoRefiner.h"
#include "AnalysisDsp.h"

#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace samplar {

// C11 fix: the ODF now comes from analysis::spectralFluxEnvelope(), which
// uses the correct juce::dsp::FFT real-only layout (the old local copy wrote
// interleaved complex data into the real-only input, so the FFT only saw
// the first half of each frame and every bin measured 2x its frequency).

//==============================================================================
// Public API
//==============================================================================
TempoRefinementResult refineTempoNearBPM(const juce::AudioBuffer<float>& drumStem,
                                          double sampleRate,
                                          double hintBpm,
                                          double tolerance)
{
    TempoRefinementResult result;
    result.refinedBPM = hintBpm;
    result.confidence = 0.0;
    result.improved   = false;

    if (hintBpm <= 0.0 || sampleRate <= 0.0 || drumStem.getNumSamples() <= 0)
    {
        result.reasoning = "invalid input";
        juce::Logger::writeToLog("[TempoRefiner] invalid input — keeping hint=" + juce::String(hintBpm, 4));
        return result;
    }

    // 1) Downmix to mono
    const std::vector<float> mono = analysis::downmixToMono(drumStem);

    // 2) Compute ODF (spectral flux)
    constexpr int kFftSize = 2048;
    constexpr int kHopSize = 512;
    double envSR = 0.0;
    auto env = analysis::spectralFluxEnvelope(mono.data(), static_cast<int>(mono.size()),
                                              sampleRate, kFftSize, kHopSize,
                                              50.0, 4000.0, envSR);

    if (env.size() < 8 || envSR <= 0.0)
    {
        result.reasoning = "ODF too short";
        juce::Logger::writeToLog("[TempoRefiner] ODF too short (frames=" + juce::String((int) env.size()) + ") — keeping hint");
        return result;
    }

    // Mean-center the envelope (improves autocorr SNR)
    double mean = 0.0;
    for (float v : env) mean += v;
    mean /= static_cast<double>(env.size());
    std::vector<float> envCentered(env.size());
    for (size_t i = 0; i < env.size(); ++i)
        envCentered[i] = env[i] - static_cast<float>(mean);

    // 3) Compute autocorrelation lag band (in ODF frames)
    // audioLag (samples) = sampleRate * 60.0 / BPM
    // frameLag = audioLag / hopSize  =>  frameLag = envSR * 60.0 / BPM
    const double bpmHigh = hintBpm * (1.0 + tolerance); // shorter lag
    const double bpmLow  = hintBpm * (1.0 - tolerance); // longer lag
    const double frameLagLow  = envSR * 60.0 / bpmHigh; // smallest frame lag
    const double frameLagHigh = envSR * 60.0 / bpmLow;  // largest frame lag

    const int lagLo = std::max(1, static_cast<int>(std::floor(frameLagLow)));
    const int lagHi = std::min(static_cast<int>(env.size()) - 2,
                               static_cast<int>(std::ceil (frameLagHigh)));

    if (lagHi <= lagLo + 1)
    {
        result.reasoning = "lag band too narrow";
        juce::Logger::writeToLog("[TempoRefiner] lag band too narrow [" +
            juce::String(lagLo) + "," + juce::String(lagHi) + "] — keeping hint=" +
            juce::String(hintBpm, 4));
        return result;
    }

    // 4) Autocorrelation at zero lag (normalization)
    auto autocorrAt = [&envCentered](int lag) -> double
    {
        if (lag < 0) lag = -lag;
        const int N = static_cast<int>(envCentered.size());
        if (lag >= N) return 0.0;
        double sum = 0.0;
        for (int i = 0; i + lag < N; ++i)
            sum += static_cast<double>(envCentered[(size_t) i]) *
                   static_cast<double>(envCentered[(size_t) (i + lag)]);
        return sum;
    };

    const double ac0 = autocorrAt(0);
    if (ac0 <= 0.0)
    {
        result.reasoning = "zero-lag autocorr is zero";
        juce::Logger::writeToLog("[TempoRefiner] zero-lag autocorr is 0 — keeping hint");
        return result;
    }

    // 5) Find peak in [lagLo, lagHi]
    int peakLag = lagLo;
    double peakVal = autocorrAt(lagLo);
    for (int lag = lagLo + 1; lag <= lagHi; ++lag)
    {
        const double v = autocorrAt(lag);
        if (v > peakVal)
        {
            peakVal = v;
            peakLag = lag;
        }
    }

    if (peakLag <= 0 || peakLag >= static_cast<int>(envCentered.size()) - 1)
    {
        result.reasoning = "peak at boundary";
        juce::Logger::writeToLog("[TempoRefiner] peak at lag-band boundary — keeping hint");
        return result;
    }

    // 6) Parabolic refinement (sub-frame precision)
    //    Fit parabola through (peakLag-1, ym), (peakLag, y0), (peakLag+1, yp).
    //    Vertex offset: delta = 0.5 * (ym - yp) / (ym - 2*y0 + yp)
    const double ym = autocorrAt(peakLag - 1);
    const double y0 = peakVal;
    const double yp = autocorrAt(peakLag + 1);
    const double denom = (ym - 2.0 * y0 + yp);
    double refinedFrameLag = static_cast<double>(peakLag);
    if (std::abs(denom) > 1e-12)
    {
        const double delta = 0.5 * (ym - yp) / denom;
        // Clamp delta to [-1, 1] for safety
        const double clampedDelta = std::max(-1.0, std::min(1.0, delta));
        refinedFrameLag = static_cast<double>(peakLag) + clampedDelta;
    }

    if (refinedFrameLag <= 0.0)
    {
        result.reasoning = "refined lag invalid";
        juce::Logger::writeToLog("[TempoRefiner] refined lag invalid — keeping hint");
        return result;
    }

    // 7) Convert refined frame-lag back to BPM
    // BPM = envSR * 60.0 / frameLag
    const double refinedBpm = envSR * 60.0 / refinedFrameLag;
    const double confidence = std::max(0.0, std::min(1.0, peakVal / ac0));

    // 8) Catastrophic-failure guard: if >10% off hint, abandon
    const double pctDiff = std::abs(refinedBpm - hintBpm) / hintBpm;
    if (pctDiff > 0.10)
    {
        result.refinedBPM = hintBpm;
        result.confidence = confidence;
        result.improved   = false;
        result.reasoning  = "catastrophic-guard tripped (>10% off hint)";
        juce::Logger::writeToLog("[TempoRefiner] hint=" + juce::String(hintBpm, 4)
            + " refined=" + juce::String(refinedBpm, 4)
            + " conf=" + juce::String(confidence, 3)
            + " — REJECTED (>10% off, keeping hint)");
        return result;
    }

    result.refinedBPM = refinedBpm;
    result.confidence = confidence;
    result.improved   = true;
    result.reasoning  = "refined";

    juce::Logger::writeToLog("[TempoRefiner] hint=" + juce::String(hintBpm, 4)
        + " refined=" + juce::String(refinedBpm, 4)
        + " conf=" + juce::String(confidence, 3)
        + " (lag " + juce::String(refinedFrameLag, 3) + " frames)");

    return result;
}

} // namespace samplar
