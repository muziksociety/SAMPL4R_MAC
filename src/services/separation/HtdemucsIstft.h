#pragma once

#include "../../utils/Fft.h"

#include <vector>

namespace samplar {

/**
 * Host-side inverse STFT that finishes models/htdemucs_body.onnx.
 *
 * The full htdemucs.onnx export carries this step inside the graph as one ConvTranspose
 * (kernel 4096, stride 1024 - a brute-force inverse DFT). GPU execution providers cannot run
 * that node (DirectML: out of memory on 3 GB cards, driver reset on Intel iGPUs) and it is slow
 * on CPU, so tools/separation/split_htdemucs.py cuts the graph right before it and this class
 * does the same maths with an FFT (samplar::Fft / PFFFT):
 *
 *     y   = OLA_hop( irfft_N(X) * hann ) * scale / sum_f hann^2(t - f*hop)
 *     out = timeBranch + y[padStart + cropStart .. + segment]      (per stem and channel)
 *
 * scale = 64 = sqrt(4096) undoes Demucs' normalised STFT. Matches the ONNX tail at ~114 dB.
 *
 * Layouts (row-major, as ONNX Runtime returns them):
 *   spec       [stems*channels][2*bins][frames]   real bins 0..N/2, then imag bins 0..N/2
 *   timeBranch [stems][channels][segment]
 *   out        [stems][channels][segment]          (may alias timeBranch)
 *
 * Not thread-safe: one instance per worker (OnnxSeparationProvider owns one).
 */
class HtdemucsIstft
{
public:
    struct Config
    {
        int   nFft      = 4096;
        int   hop       = 1024;
        float scale     = 64.0f;
        int   padStart  = 2048;      // samples dropped at the start (torch istft center)
        int   cropStart = 1536;      // Demucs' segment crop inside the istft output
        int   segment   = 343980;    // output samples per stem/channel
        int   stems     = 4;
        int   channels  = 2;
    };

    // No `Config c = {}` default: Clang rejects it inside the enclosing class.
    HtdemucsIstft() : HtdemucsIstft (Config {}) {}
    explicit HtdemucsIstft (Config c);

    // Writes out = timeBranch + istft(spec). `frames` comes from the spec tensor shape.
    void process (const float* spec, int frames, const float* timeBranch, float* out);

private:
    static int orderOf (int n);
    void ensureEnvelope (int frames);

    Config cfg_;
    int bins_;
    Fft fft_;
    std::vector<float> hann_;
    std::vector<float> fftBuf_;      // 2N, JUCE/samplar::Fft layout
    std::vector<double> ola_;        // one channel's overlap-add buffer
    std::vector<double> invEnv_;     // scale / sum hann^2, per sample, for `envFrames_`
    int envFrames_ = -1;
};

} // namespace samplar
