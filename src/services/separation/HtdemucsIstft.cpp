#include "HtdemucsIstft.h"

#include <cmath>
#include <cstring>

namespace samplar {

int HtdemucsIstft::orderOf (int n)
{
    int order = 0;
    while ((1 << order) < n) ++order;
    jassert ((1 << order) == n);   // nFft must be a power of two
    return order;
}

HtdemucsIstft::HtdemucsIstft (Config c)
    : cfg_ (c), bins_ (c.nFft / 2 + 1), fft_ (orderOf (c.nFft))
{
    hann_.resize ((size_t) cfg_.nFft);
    for (int n = 0; n < cfg_.nFft; ++n)   // periodic Hann, as torch.hann_window
        hann_[(size_t) n] = (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * n / cfg_.nFft));
    fftBuf_.resize ((size_t) cfg_.nFft * 2);
    juce::Logger::writeToLog ("[HtdemucsIstft] ready: nFft=" + juce::String (cfg_.nFft) + " hop=" + juce::String (cfg_.hop)
        + " scale=" + juce::String (cfg_.scale) + " segment=" + juce::String (cfg_.segment)
        + " fft=" + juce::String (fft_.usesPffft() ? "PFFFT " : "juce ") + Fft::simdArch());
}

void HtdemucsIstft::ensureEnvelope (int frames)
{
    if (frames == envFrames_)
        return;
    const int length = (frames - 1) * cfg_.hop + cfg_.nFft;
    std::vector<double> env ((size_t) length, 0.0);
    for (int f = 0; f < frames; ++f)
        for (int n = 0; n < cfg_.nFft; ++n)
            env[(size_t) (f * cfg_.hop + n)] += (double) hann_[(size_t) n] * hann_[(size_t) n];
    invEnv_.resize ((size_t) length);
    for (int i = 0; i < length; ++i)   // the ONNX export clamps the same tiny edge values; they are cropped away
        invEnv_[(size_t) i] = env[(size_t) i] > 1.0e-11 ? cfg_.scale / env[(size_t) i] : 0.0;
    ola_.assign ((size_t) length, 0.0);
    envFrames_ = frames;
    juce::Logger::writeToLog ("[HtdemucsIstft] envelope for " + juce::String (frames) + " frames, length " + juce::String (length));
}

void HtdemucsIstft::process (const float* spec, int frames, const float* timeBranch, float* out)
{
    ensureEnvelope (frames);
    const int n = cfg_.nFft, k2 = 2 * bins_;
    const int length = (frames - 1) * cfg_.hop + n;
    const int rows = cfg_.stems * cfg_.channels;

    for (int row = 0; row < rows; ++row)
    {
        std::fill (ola_.begin(), ola_.end(), 0.0);
        const float* re = spec + (size_t) row * k2 * frames;      // [bin][frame]
        const float* im = re + (size_t) bins_ * frames;

        for (int t = 0; t < frames; ++t)
        {
            float* d = fftBuf_.data();
            for (int k = 0; k < bins_; ++k)
            {
                d[2 * k]     = re[(size_t) k * frames + t];
                d[2 * k + 1] = im[(size_t) k * frames + t];
            }
            fft_.performRealOnlyInverseTransform (d);           // 1/N scaled, = numpy irfft
            double* y = ola_.data() + (size_t) t * cfg_.hop;
            for (int i = 0; i < n; ++i)
                y[i] += (double) d[i] * hann_[(size_t) i];
        }

        const float* tb = timeBranch + (size_t) row * cfg_.segment;
        float* o = out + (size_t) row * cfg_.segment;
        const int base = cfg_.padStart + cfg_.cropStart;
        for (int j = 0; j < cfg_.segment; ++j)
        {
            const int idx = base + j;
            const double v = idx < length ? ola_[(size_t) idx] * invEnv_[(size_t) idx] : 0.0;
            o[j] = (float) ((double) tb[j] + v);
        }
    }
}

} // namespace samplar
