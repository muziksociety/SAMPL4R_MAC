#include "AnalysisDsp.h"

#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <deque>

namespace samplar {
namespace analysis {

std::vector<float> downmixToMono(const juce::AudioBuffer<float>& src)
{
    const int n   = src.getNumSamples();
    const int nCh = src.getNumChannels();
    std::vector<float> mono(static_cast<size_t>(std::max(0, n)), 0.0f);
    if (n <= 0 || nCh <= 0) return mono;

    const float scale = 1.0f / static_cast<float>(nCh);
    for (int ch = 0; ch < nCh; ++ch)
    {
        const float* rd = src.getReadPointer(ch);
        for (int i = 0; i < n; ++i)
            mono[static_cast<size_t>(i)] += rd[i] * scale;
    }
    return mono;
}

juce::AudioBuffer<float> downmixToMonoBuffer(const juce::AudioBuffer<float>& src)
{
    const int n = src.getNumSamples();
    juce::AudioBuffer<float> mono(1, std::max(0, n));
    mono.clear();
    if (n <= 0 || src.getNumChannels() <= 0) return mono;

    const float scale = 1.0f / static_cast<float>(src.getNumChannels());
    for (int ch = 0; ch < src.getNumChannels(); ++ch)
        mono.addFrom(0, 0, src, ch, 0, n, scale);
    return mono;
}

std::vector<float> spectralFluxEnvelope(const float* mono, int numSamples,
                                        double sampleRate,
                                        int fftSize, int hopSize,
                                        double lowHz, double highHz,
                                        double& outEnvSR)
{
    outEnvSR = 0.0;
    std::vector<float> envelope;
    if (mono == nullptr || sampleRate <= 0.0 || hopSize <= 0 || fftSize < 4
        || numSamples < fftSize)
        return envelope;

    const int fftOrder = static_cast<int>(std::lround(std::log2(static_cast<double>(fftSize))));
    if ((1 << fftOrder) != fftSize)
    {
        juce::Logger::writeToLog("[AnalysisDsp] spectralFluxEnvelope: fftSize "
                                 + juce::String(fftSize) + " is not a power of two");
        return envelope;
    }
    juce::dsp::FFT fft(fftOrder);

    std::vector<float> window(static_cast<size_t>(fftSize));
    for (int i = 0; i < fftSize; ++i)
        window[static_cast<size_t>(i)] =
            0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi
                                    * static_cast<float>(i)
                                    / static_cast<float>(fftSize - 1)));

    const double binHz = sampleRate / static_cast<double>(fftSize);
    const int numBins  = fftSize / 2 + 1;
    const int binLow   = juce::jlimit(0, numBins - 1, static_cast<int>(std::ceil (lowHz  / binHz)));
    const int binHigh  = juce::jlimit(0, numBins - 1, static_cast<int>(std::floor(highHz / binHz)));

    std::vector<float> fftBuf (static_cast<size_t>(fftSize) * 2, 0.0f);
    std::vector<float> prevMag(static_cast<size_t>(numBins), 0.0f);
    std::vector<float> curMag (static_cast<size_t>(numBins), 0.0f);

    const int numFrames = (numSamples - fftSize) / hopSize + 1;
    envelope.reserve(static_cast<size_t>(numFrames));

    for (int f = 0; f < numFrames; ++f)
    {
        const int pos = f * hopSize;
        // Real-only FFT input: first fftSize floats = real samples, rest = 0.
        for (int i = 0; i < fftSize; ++i)
            fftBuf[static_cast<size_t>(i)] = mono[pos + i] * window[static_cast<size_t>(i)];
        std::fill(fftBuf.begin() + fftSize, fftBuf.end(), 0.0f);

        fft.performRealOnlyForwardTransform(fftBuf.data(), true);

        for (int k = 0; k < numBins; ++k)
        {
            const float re = fftBuf[static_cast<size_t>(k) * 2];
            const float im = fftBuf[static_cast<size_t>(k) * 2 + 1];
            curMag[static_cast<size_t>(k)] = std::sqrt(re * re + im * im);
        }

        float flux = 0.0f;
        if (f > 0)
        {
            for (int k = binLow; k <= binHigh; ++k)
            {
                const float d = curMag[static_cast<size_t>(k)] - prevMag[static_cast<size_t>(k)];
                if (d > 0.0f) flux += d;
            }
        }
        envelope.push_back(flux);
        prevMag.swap(curMag);
    }

    outEnvSR = sampleRate / static_cast<double>(hopSize);
    return envelope;
}

std::vector<float> autocorrelationFFT(const std::vector<float>& x, int maxLag)
{
    const int n = static_cast<int>(x.size());
    if (n <= 0 || maxLag < 0) return {};
    maxLag = std::min(maxLag, n - 1);

    double mean = 0.0;
    for (float v : x) mean += v;
    mean /= static_cast<double>(n);

    // Zero-pad to >= 2n so circular correlation equals linear correlation.
    int order = 1;
    while ((1 << order) < 2 * n) ++order;
    const int size = 1 << order;

    juce::dsp::FFT fft(order);
    std::vector<float> buf(static_cast<size_t>(size) * 2, 0.0f);
    for (int i = 0; i < n; ++i)
        buf[static_cast<size_t>(i)] = static_cast<float>(x[static_cast<size_t>(i)] - mean);

    fft.performRealOnlyForwardTransform(buf.data());
    for (int k = 0; k < size; ++k)
    {
        const float re = buf[static_cast<size_t>(k) * 2];
        const float im = buf[static_cast<size_t>(k) * 2 + 1];
        buf[static_cast<size_t>(k) * 2]     = re * re + im * im;
        buf[static_cast<size_t>(k) * 2 + 1] = 0.0f;
    }
    fft.performRealOnlyInverseTransform(buf.data());

    std::vector<float> r(static_cast<size_t>(maxLag) + 1, 0.0f);
    const float r0 = buf[0];
    if (r0 <= 0.0f) return r;
    for (int lag = 0; lag <= maxLag; ++lag)
        r[static_cast<size_t>(lag)] = buf[static_cast<size_t>(lag)] / r0;
    return r;
}

namespace {

// Energy-step score E(n, n+w) / (max_{m in [n-hold, n-w]} E(m, m+w) + floor)
// for n in [lo, hi] over a signal given by its squared samples sq[] starting
// at absolute index p0. Returns an empty vector for a silent region.
std::vector<double> energyStepScores(const std::vector<double>& sq, int64_t p0,
                                     int64_t lo, int64_t hi, int64_t w, int64_t hold)
{
    const int64_t p1 = p0 + static_cast<int64_t>(sq.size());
    std::vector<double> prefix(sq.size() + 1, 0.0);
    for (size_t i = 0; i < sq.size(); ++i) prefix[i + 1] = prefix[i] + sq[i];
    auto energy = [&](int64_t a, int64_t b) -> double // mean over [a, b)
    {
        a = std::max(a, p0); b = std::min(b, p1);
        if (b <= a) return 0.0;
        return (prefix[static_cast<size_t>(b - p0)] - prefix[static_cast<size_t>(a - p0)])
               / static_cast<double>(b - a);
    };

    double peakE = 0.0;
    for (int64_t n = lo; n <= hi; ++n) peakE = std::max(peakE, energy(n, n + w));
    if (peakE <= 1e-14) return {};
    const double floorE = 1e-3 * peakE;

    // "Before" level = MAX short-window energy over the preceding hold span
    // (sliding max via monotonic deque). A plain previous-window energy dips
    // at every zero of a low-frequency tail (50 Hz kick = 10 ms half period),
    // which made the ratio peak there instead of at the next hit's attack.
    std::vector<double> scores(static_cast<size_t>(hi - lo + 1), 0.0);
    std::deque<std::pair<int64_t, double>> dq; // (window start m, energy), decreasing energy
    int64_t nextM = lo - hold;
    for (int64_t n = lo; n <= hi; ++n)
    {
        for (; nextM <= n - w; ++nextM)
        {
            const double e = energy(nextM, nextM + w);
            while (! dq.empty() && dq.back().second <= e) dq.pop_back();
            dq.emplace_back(nextM, e);
        }
        while (! dq.empty() && dq.front().first < n - hold) dq.pop_front();
        const double before = dq.empty() ? 0.0 : dq.front().second;
        scores[static_cast<size_t>(n - lo)] = energy(n, n + w) / (before + floorE);
    }
    return scores;
}

} // namespace

int64_t findAttackSample(const float* x, int64_t numSamples,
                         int64_t lo, int64_t hi, int winSamples, int holdSamples)
{
    if (x == nullptr || numSamples <= 0 || winSamples <= 0) return -1;
    lo = std::max<int64_t>(0, lo);
    hi = std::min<int64_t>(numSamples - 1, hi);
    if (hi <= lo) return -1;

    const int64_t w    = winSamples;
    const int64_t hold = std::max<int64_t>(w, holdSamples);
    const int64_t p0 = std::max<int64_t>(1, lo - hold - w);
    const int64_t p1 = std::min<int64_t>(numSamples, hi + w + 1);
    if (p1 <= p0) return -1;

    // Two views of the region: raw (kick / low-frequency attacks) and first
    // difference (+6 dB/oct; snare / hat attacks on top of a loud
    // low-frequency tail, where the raw view also steps up when the new hit
    // and the old tail's half-cycles superpose). Score = max of both.
    std::vector<double> raw(static_cast<size_t>(p1 - p0)), diff(static_cast<size_t>(p1 - p0));
    for (int64_t i = p0; i < p1; ++i)
    {
        const double v = x[i], d = static_cast<double>(x[i]) - x[i - 1];
        raw [static_cast<size_t>(i - p0)] = v * v;
        diff[static_cast<size_t>(i - p0)] = d * d;
    }
    const auto sRaw  = energyStepScores(raw,  p0, lo, hi, w, hold);
    const auto sDiff = energyStepScores(diff, p0, lo, hi, w, hold);
    if (sRaw.empty() && sDiff.empty()) return -1;

    int64_t best = -1;
    double bestScore = -1.0;
    for (int64_t n = lo; n <= hi; ++n)
    {
        const size_t i = static_cast<size_t>(n - lo);
        const double sc = std::max(sRaw.empty() ? 0.0 : sRaw[i], sDiff.empty() ? 0.0 : sDiff[i]);
        if (sc > bestScore) { bestScore = sc; best = n; }
    }
    return best;
}

int64_t snapToZeroCrossing(const float* x, int64_t numSamples,
                           int64_t pos, int maxBack, int maxFwd)
{
    if (x == nullptr || numSamples < 2 || pos < 0 || pos >= numSamples) return pos;
    auto crossingAt = [&](int64_t i) -> bool
    {
        return i >= 1 && i < numSamples && x[i - 1] * x[i] <= 0.0f;
    };
    auto pick = [&](int64_t i) -> int64_t
    {
        return (std::abs(x[i - 1]) < std::abs(x[i])) ? i - 1 : i;
    };
    const int maxD = std::max(maxBack, maxFwd);
    for (int d = 0; d <= maxD; ++d)
    {
        if (d <= maxBack && crossingAt(pos - d)) return pick(pos - d);
        if (d > 0 && d <= maxFwd && crossingAt(pos + d)) return pick(pos + d);
    }
    return pos;
}

} // namespace analysis
} // namespace samplar
