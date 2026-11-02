#include "KeyDetector.h"
#include "AnalysisDsp.h"

#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace samplar {

namespace {

// Krumhansl & Kessler (1982) probe-tone profiles, index 0 = tonic.
constexpr double kMajorProfile[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
constexpr double kMinorProfile[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

constexpr double kMinFreqHz   = 65.0;   // C2
constexpr double kMaxFreqHz   = 5000.0;
constexpr float  kSilenceRms  = 1.0e-3f; // -60 dBFS frame gate

double pearson(const std::array<double, 12>& x, const double* profile, int tonic)
{
    double mx = 0.0, my = 0.0;
    for (int i = 0; i < 12; ++i) { mx += x[(size_t) i]; my += profile[i]; }
    mx /= 12.0; my /= 12.0;
    double sxy = 0.0, sxx = 0.0, syy = 0.0;
    for (int pc = 0; pc < 12; ++pc)
    {
        const double a = x[(size_t) pc] - mx;
        const double b = profile[(pc - tonic + 12) % 12] - my;
        sxy += a * b; sxx += a * a; syy += b * b;
    }
    if (sxx <= 1e-15 || syy <= 1e-15) return 0.0;
    return sxy / std::sqrt(sxx * syy);
}

} // namespace

juce::String KeyDetector::pitchClassName(int pc)
{
    static const char* names[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return (pc >= 0 && pc < 12) ? juce::String(names[pc]) : juce::String("?");
}

juce::String KeyDetector::Result::getName() const
{
    if (! valid || tonic < 0) return "unknown";
    return pitchClassName(tonic) + (mode == Mode::Major ? " major" : " minor");
}

juce::String KeyDetector::Result::getCamelot() const
{
    if (! valid || tonic < 0) return "";
    const int majorTonic = (mode == Mode::Major) ? tonic : (tonic + 3) % 12; // relative major
    const int number = ((majorTonic * 7) % 12 + 7) % 12 + 1;                  // C major = 8
    return juce::String(number) + (mode == Mode::Major ? "B" : "A");
}

std::array<double, 12> KeyDetector::computeChroma(const juce::AudioBuffer<float>& buffer, double sampleRate,
                                                 AnalysisCancel cancel, bool* wasCancelled)
{
    std::array<double, 12> chroma {};
    if (wasCancelled != nullptr) *wasCancelled = false;
    if (buffer.getNumSamples() <= 0 || buffer.getNumChannels() <= 0 || sampleRate <= 0.0)
        return chroma;

    const std::vector<float> mono = analysis::downmixToMono(buffer);
    const int n = static_cast<int>(mono.size());

    const int order = juce::jlimit(10, 15, static_cast<int>(std::lround(std::log2(0.186 * sampleRate))));
    const int size  = 1 << order;
    const int hop   = size / 2;
    if (n < size)
    {
        juce::Logger::writeToLog("[KeyDetector] buffer shorter than one analysis frame ("
                                 + juce::String(size) + " samples)");
        return chroma;
    }

    juce::dsp::FFT fft(order);
    std::vector<float> window(static_cast<size_t>(size));
    for (int i = 0; i < size; ++i)
        window[(size_t) i] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * i / (size - 1)));

    const double binHz = sampleRate / size;
    const int kLo = std::max(2, static_cast<int>(std::floor(kMinFreqHz / binHz)));
    const int kHi = std::min(size / 2 - 2, static_cast<int>(std::ceil(kMaxFreqHz / binHz)));

    std::vector<float> buf(static_cast<size_t>(size) * 2, 0.0f);
    std::vector<float> mag(static_cast<size_t>(size / 2 + 1), 0.0f);
    int usedFrames = 0, totalFrames = 0;

    for (int pos = 0; pos + size <= n; pos += hop)
    {
        if ((totalFrames & 31) == 0 && cancel.requested())
        {
            if (wasCancelled != nullptr) *wasCancelled = true;
            return {};
        }
        ++totalFrames;
        double sumSq = 0.0;
        for (int i = 0; i < size; ++i)
        {
            const float v = mono[(size_t) (pos + i)];
            sumSq += static_cast<double>(v) * v;
            buf[(size_t) i] = v * window[(size_t) i];
        }
        if (std::sqrt(sumSq / size) < kSilenceRms) continue;
        std::fill(buf.begin() + size, buf.end(), 0.0f);
        fft.performRealOnlyForwardTransform(buf.data(), true);

        float frameMax = 0.0f;
        for (int k = 0; k <= size / 2; ++k)
        {
            const float re = buf[(size_t) k * 2], im = buf[(size_t) k * 2 + 1];
            mag[(size_t) k] = std::sqrt(re * re + im * im);
            if (k >= kLo && k <= kHi) frameMax = std::max(frameMax, mag[(size_t) k]);
        }
        if (frameMax <= 0.0f) continue;

        std::array<double, 12> frame {};
        const float peakFloor = frameMax * 0.01f; // -40 dB re frame peak
        for (int k = kLo; k <= kHi; ++k)
        {
            const float m = mag[(size_t) k];
            if (m < peakFloor || m <= mag[(size_t) k - 1] || m < mag[(size_t) k + 1]) continue;

            // Parabolic interpolation on log magnitude for the true peak frequency.
            const double a = std::log(mag[(size_t) k - 1] + 1e-12);
            const double b = std::log(m + 1e-12);
            const double c = std::log(mag[(size_t) k + 1] + 1e-12);
            const double den = a - 2.0 * b + c;
            const double delta = (std::abs(den) > 1e-12) ? juce::jlimit(-0.5, 0.5, 0.5 * (a - c) / den) : 0.0;
            const double freq = (k + delta) * binHz;
            if (freq < kMinFreqHz || freq > kMaxFreqHz) continue;

            const double midi = 69.0 + 12.0 * std::log2(freq / 440.0);
            const double nearest = std::round(midi);
            const double dev = midi - nearest;                    // -0.5 .. 0.5 semitone
            const double tuneW = std::pow(std::cos(juce::MathConstants<double>::pi * dev), 2.0);
            const int pc = (static_cast<int>(nearest) % 12 + 12) % 12;
            frame[(size_t) pc] += tuneW * std::log1p(100.0 * m / frameMax);
        }

        double sum = 0.0;
        for (double v : frame) sum += v;
        if (sum <= 0.0) continue;
        for (int i = 0; i < 12; ++i) chroma[(size_t) i] += frame[(size_t) i] / sum;
        ++usedFrames;
    }

    double total = 0.0;
    for (double v : chroma) total += v;
    if (total > 0.0) for (double& v : chroma) v /= total;

    juce::Logger::writeToLog("[KeyDetector] chroma from " + juce::String(usedFrames) + "/"
                             + juce::String(totalFrames) + " frames (fft " + juce::String(size) + ")");
    return chroma;
}

KeyDetector::Result KeyDetector::detectFromChroma(const std::array<double, 12>& chroma)
{
    Result r;
    r.chroma = chroma;
    double total = 0.0;
    for (double v : chroma) total += v;
    if (total <= 0.0)
    {
        juce::Logger::writeToLog("[KeyDetector] empty chroma -> invalid");
        return r;
    }

    double best = -2.0, second = -2.0;
    int bestT = -1, secondT = -1;
    Mode bestM = Mode::Major, secondM = Mode::Major;
    for (int m = 0; m < 2; ++m)
    {
        const double* prof = (m == 0) ? kMajorProfile : kMinorProfile;
        for (int t = 0; t < 12; ++t)
        {
            const double c = pearson(chroma, prof, t);
            const Mode mode = (m == 0) ? Mode::Major : Mode::Minor;
            if (c > best)
            {
                second = best; secondT = bestT; secondM = bestM;
                best = c; bestT = t; bestM = mode;
            }
            else if (c > second)
            {
                second = c; secondT = t; secondM = mode;
            }
        }
    }

    r.tonic = bestT; r.mode = bestM; r.correlation = best;
    r.secondTonic = secondT; r.secondMode = secondM; r.secondCorrelation = second;
    r.confidence = juce::jlimit(0.0, 1.0, (best - second) / 0.1) * juce::jlimit(0.0, 1.0, best);
    r.valid = bestT >= 0 && best > 0.0;
    return r;
}

KeyDetector::Result KeyDetector::detect(const juce::AudioBuffer<float>& buffer, double sampleRate, AnalysisCancel cancel)
{
    juce::Logger::writeToLog("[KeyDetector] === detect START === samples=" + juce::String(buffer.getNumSamples())
                             + " ch=" + juce::String(buffer.getNumChannels()) + " sr=" + juce::String(sampleRate, 0));
    bool cancelled = false;
    const auto chroma = computeChroma(buffer, sampleRate, cancel, &cancelled);
    if (cancelled)
    {
        Result c;
        c.cancelled = true;
        juce::Logger::writeToLog("[KeyDetector] === detect CANCELLED ===");
        return c;
    }
    Result r = detectFromChroma(chroma);
    juce::Logger::writeToLog("[KeyDetector] === detect END === key=" + r.getName() + " (" + r.getCamelot()
        + ") r=" + juce::String(r.correlation, 3) + " conf=" + juce::String(r.confidence, 3)
        + " runner-up=" + pitchClassName(r.secondTonic)
        + (r.secondMode == Mode::Major ? " major" : " minor") + " r=" + juce::String(r.secondCorrelation, 3));
    return r;
}

} // namespace samplar
