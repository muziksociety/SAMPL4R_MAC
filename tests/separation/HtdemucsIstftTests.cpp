// HtdemucsIstft (FFT inverse STFT that finishes htdemucs_body.onnx) vs a naive inverse-DFT
// reference of the same formula, on a small configuration:
//   y = OLA(irfft(X) * hann) * scale / sum hann^2 ; out = timeBranch + y[padStart+cropStart ..]
// Ported from SM5 tests/unit/HtdemucsIstftTests.cpp (453e7733) to juce::UnitTest.
#include "services/separation/HtdemucsIstft.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace samplar {

namespace {

std::vector<float> naiveReference (const HtdemucsIstft::Config& c, const std::vector<float>& spec,
                                   int frames, const std::vector<float>& tb)
{
    const int n = c.nFft, bins = n / 2 + 1, rows = c.stems * c.channels;
    const int length = (frames - 1) * c.hop + n;
    std::vector<double> hann ((size_t) n), env ((size_t) length, 0.0);
    for (int i = 0; i < n; ++i) hann[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / n);
    for (int f = 0; f < frames; ++f) for (int i = 0; i < n; ++i) env[(size_t) (f * c.hop + i)] += hann[(size_t) i] * hann[(size_t) i];
    std::vector<float> out ((size_t) rows * (size_t) c.segment);
    for (int r = 0; r < rows; ++r)
    {
        std::vector<double> y ((size_t) length, 0.0);
        const float* re = spec.data() + (size_t) r * 2 * (size_t) bins * (size_t) frames;
        const float* im = re + (size_t) bins * (size_t) frames;
        for (int t = 0; t < frames; ++t)
            for (int i = 0; i < n; ++i)
            {
                // irfft: x[i] = (1/N) sum_k c_k (Re X_k cos - Im X_k sin), c_0 = c_{N/2} = 1, else 2
                double acc = 0.0;
                for (int k = 0; k < bins; ++k)
                {
                    const double ck = (k == 0 || k == n / 2) ? 1.0 : 2.0;
                    const double ang = 2.0 * juce::MathConstants<double>::pi * k * i / n;
                    const double imk = (k == 0 || k == n / 2) ? 0.0 : (double) im[(size_t) k * frames + t];
                    acc += ck * ((double) re[(size_t) k * frames + t] * std::cos (ang) - imk * std::sin (ang));
                }
                y[(size_t) (t * c.hop + i)] += acc / n * hann[(size_t) i];
            }
        for (int j = 0; j < c.segment; ++j)
        {
            const int idx = c.padStart + c.cropStart + j;
            const double v = idx < length && env[(size_t) idx] > 1e-11 ? y[(size_t) idx] * c.scale / env[(size_t) idx] : 0.0;
            out[(size_t) r * c.segment + j] = (float) (tb[(size_t) r * c.segment + j] + v);
        }
    }
    return out;
}

double snrDb (const std::vector<float>& ref, const std::vector<float>& got)
{
    double num = 0.0, den = 0.0;
    for (size_t i = 0; i < ref.size(); ++i)
    {
        num += (double) ref[i] * ref[i];
        den += ((double) ref[i] - got[i]) * ((double) ref[i] - got[i]);
    }
    return 10.0 * std::log10 (num / std::max (den, 1e-30));
}

} // namespace

class HtdemucsIstftTests : public juce::UnitTest
{
public:
    HtdemucsIstftTests() : juce::UnitTest ("HtdemucsIstft", "SAMPLAR") {}

    void runTest() override
    {
        HtdemucsIstft::Config c;
        c.nFft = 64; c.hop = 16; c.scale = 8.0f; c.padStart = 32; c.cropStart = 12; c.segment = 200;
        c.stems = 2; c.channels = 2;
        const int frames = 18, bins = c.nFft / 2 + 1, rows = c.stems * c.channels;

        std::mt19937 rng (5);
        std::uniform_real_distribution<float> u (-1.0f, 1.0f);
        std::vector<float> spec ((size_t) rows * 2 * (size_t) bins * frames), tb ((size_t) rows * (size_t) c.segment);
        for (auto& v : spec) v = u (rng);
        for (auto& v : tb) v = u (rng);

        const auto ref = naiveReference (c, spec, frames, tb);
        HtdemucsIstft istft (c);
        std::vector<float> got ((size_t) rows * (size_t) c.segment);

        beginTest ("FFT iSTFT matches the naive inverse-DFT reference (> 100 dB SNR)");
        istft.process (spec.data(), frames, tb.data(), got.data());
        const double snr = snrDb (ref, got);
        logMessage ("    istft vs naive reference SNR " + juce::String (snr, 1) + " dB");
        expect (snr > 100.0, "SNR " + juce::String (snr, 1) + " dB");

        beginTest ("reuse (envelope cache) and in-place output (out aliases timeBranch) are identical");
        auto inplace = tb;
        istft.process (spec.data(), frames, inplace.data(), inplace.data());
        expect (inplace == got);

        beginTest ("a different frame count re-derives the envelope");
        {
            const int frames2 = 22;
            std::vector<float> spec2 ((size_t) rows * 2 * (size_t) bins * frames2);
            for (auto& v : spec2) v = u (rng);
            const auto ref2 = naiveReference (c, spec2, frames2, tb);
            std::vector<float> got2 ((size_t) rows * (size_t) c.segment);
            istft.process (spec2.data(), frames2, tb.data(), got2.data());
            expect (snrDb (ref2, got2) > 100.0);
        }

        beginTest ("zero spectrum leaves the time branch untouched");
        {
            std::vector<float> zero (spec.size(), 0.0f), out ((size_t) rows * (size_t) c.segment);
            istft.process (zero.data(), frames, tb.data(), out.data());
            float maxDiff = 0.0f;
            for (size_t i = 0; i < out.size(); ++i) maxDiff = std::max (maxDiff, std::abs (out[i] - tb[i]));
            expectLessThan (maxDiff, 1.0e-6f);
        }
    }
};

static HtdemucsIstftTests htdemucsIstftTests;

} // namespace samplar
