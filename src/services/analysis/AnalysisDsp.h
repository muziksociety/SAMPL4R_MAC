#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cstdint>
#include <vector>

namespace samplar {
namespace analysis {

/** Downmix any-channel buffer to a mono vector (average of channels). */
std::vector<float> downmixToMono(const juce::AudioBuffer<float>& src);

/** Same, as a 1-channel AudioBuffer (for APIs that take a buffer). */
juce::AudioBuffer<float> downmixToMonoBuffer(const juce::AudioBuffer<float>& src);

/**
 * Half-wave-rectified spectral-flux onset envelope.
 *
 * Correct juce::dsp::FFT real-only layout: the FIRST fftSize floats of a
 * 2*fftSize scratch buffer hold the windowed real frame (rest zero); after
 * performRealOnlyForwardTransform the buffer holds interleaved (re, im)
 * pairs for bins 0..fftSize/2. (C11: the old code wrote the frame as
 * interleaved complex data, so the FFT only saw half the frame.)
 *
 * Frame f covers samples [f*hop, f*hop + fftSize). Envelope value 0 is 0.
 * Only bins whose centre frequency lies in [lowHz, highHz] contribute.
 *
 * @param outEnvSR receives the envelope frame rate (sampleRate / hop).
 */
std::vector<float> spectralFluxEnvelope(const float* mono, int numSamples,
                                        double sampleRate,
                                        int fftSize, int hopSize,
                                        double lowHz, double highHz,
                                        double& outEnvSR);

/**
 * Mean-removed autocorrelation of x for lags [0, maxLag], computed with an
 * FFT (O(n log n)) instead of the O(n^2) time-domain loop. Result is
 * normalised so r[0] == 1 (all zeros if x is constant).
 */
std::vector<float> autocorrelationFFT(const std::vector<float>& x, int maxLag);

/**
 * Sample-accurate attack position inside [lo, hi]: argmax over n of
 *   E(n, n+win) / (max_{m in [n-hold, n-win]} E(m, m+win) + floor),
 * E = mean x^2, floor = 1e-3 * (peak window energy in the region). Peaks
 * where energy steps up above everything in the preceding hold span (the
 * first sample of the hit), also on top of a previous hit's decaying /
 * low-frequency tail. Evaluated on the raw signal AND its first difference
 * (HF-emphasised); the larger score wins per sample.
 * Returns -1 if the region is silent/invalid.
 */
int64_t findAttackSample(const float* x, int64_t numSamples,
                         int64_t lo, int64_t hi, int winSamples, int holdSamples);

/**
 * Snap pos to the nearest zero crossing (index i with x[i-1]*x[i] <= 0,
 * returning whichever of the pair has the smaller |x|), searching up to
 * maxBack samples earlier and maxFwd samples later; earlier wins ties.
 * Returns pos unchanged if none is found.
 */
int64_t snapToZeroCrossing(const float* x, int64_t numSamples,
                           int64_t pos, int maxBack, int maxFwd);

} // namespace analysis
} // namespace samplar
