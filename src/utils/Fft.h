#pragma once
// samplar::Fft - drop-in replacement for juce::dsp::FFT backed by PFFFT (SIMD: SSE on x86-64,
// NEON on ARM / Apple Silicon). Same constructor (order = log2 size) and the same real
// transforms with JUCE's exact buffer layout and scaling, so call sites only change the type:
//
//   performRealOnlyForwardTransform (d)      in : d[0..N)  real samples (buffer holds 2N floats)
//                                            out: interleaved complex bins 0..N-1, unscaled,
//                                                 bins > N/2 mirrored as conjugates (as JUCE's
//                                                 fallback engine does regardless of the flag)
//   performRealOnlyInverseTransform (d)      in : interleaved complex bins 0..N/2 (rest ignored)
//                                            out: d[0..N) real, scaled 1/N; d[N..2N) zeroed
//   performFrequencyOnlyForwardTransform (d) out: |X_k| for k < limit, zeros up to 2N, where
//                                                 limit = N/2+1 (ignoreNegative) or N
//
// Why: JUCE uses its scalar fallback FFT on Windows (no IPP/MKL/FFTW in this build). It runs
// a full complex FFT for real input and takes a per-instance spin lock on every call. PFFFT
// does a real FFT with SIMD and no locks.
//
// Threading: an Fft object may be used from several threads at once. PFFFT setups are
// read-only and cached per size for the whole process; scratch buffers are thread_local.
// Sizes PFFFT cannot do (N < its minimum real size, 32 with SSE/NEON) use juce::dsp::FFT.
//
// PFFFT is BSD-licensed: third_party/pffft/LICENSE.txt ships in the bundle's Contents/Resources/licenses.

#include <juce_dsp/juce_dsp.h>
#include <sm5_pffft_prefix.h>   // sm5_pffft_* symbol names (see third_party/pffft/README_SM5.md)
#include <pffft/pffft.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>

namespace samplar {

class Fft
{
public:
    explicit Fft (int order)
        : order_ (order), size_ (1 << order)
    {
        setup_ = sharedSetup (size_);
        if (setup_ == nullptr)
            fallback_ = std::make_unique<juce::dsp::FFT> (order);
        logEngineOnce();
    }

    int getSize() const noexcept { return size_; }

    // True when this instance runs on PFFFT (false: juce::dsp::FFT fallback for tiny sizes).
    bool usesPffft() const noexcept { return setup_ != nullptr; }

    void performRealOnlyForwardTransform (float* d, bool onlyCalculateNonNegativeFrequencies = false) const noexcept
    {
        if (fallback_ != nullptr) { fallback_->performRealOnlyForwardTransform (d, onlyCalculateNonNegativeFrequencies); return; }
        const float* p = forwardPacked (d);
        const int n = size_, half = n / 2;
        // Packed: p[0] = F(0), p[1] = F(N/2), p[2k], p[2k+1] = re, im of bin k (1 <= k < N/2).
        // Unpack in place into d as interleaved complex bins 0..N-1.
        std::memcpy (d + 2, p + 2, sizeof (float) * (size_t) (n - 2));
        d[0] = p[0];  d[1] = 0.0f;
        d[n] = p[1];  d[n + 1] = 0.0f;
        for (int k = half + 1; k < n; ++k)
        {
            d[2 * k]     =  d[2 * (n - k)];
            d[2 * k + 1] = -d[2 * (n - k) + 1];
        }
    }

    void performRealOnlyInverseTransform (float* d) const noexcept
    {
        if (fallback_ != nullptr) { fallback_->performRealOnlyInverseTransform (d); return; }
        const int n = size_;
        auto& s = scratch (n);
        s.buf[0] = d[0];        // re(bin 0)
        s.buf[1] = d[n];        // re(bin N/2)
        std::memcpy (s.buf + 2, d + 2, sizeof (float) * (size_t) (n - 2));
        pffft_transform_ordered (setup_, s.buf, s.buf, s.work, PFFFT_BACKWARD);
        const float scale = 1.0f / (float) n;
        for (int i = 0; i < n; ++i)
            d[i] = s.buf[i] * scale;
        std::memset (d + n, 0, sizeof (float) * (size_t) n);
    }

    void performFrequencyOnlyForwardTransform (float* d, bool ignoreNegativeFrequencies = false) const noexcept
    {
        if (fallback_ != nullptr) { fallback_->performFrequencyOnlyForwardTransform (d, ignoreNegativeFrequencies); return; }
        const float* p = forwardPacked (d);
        const int n = size_, half = n / 2;
        const int limit = ignoreNegativeFrequencies ? half + 1 : n;
        d[0] = std::abs (p[0]);
        for (int k = 1; k < half; ++k)
            d[k] = std::sqrt (p[2 * k] * p[2 * k] + p[2 * k + 1] * p[2 * k + 1]);
        d[half] = std::abs (p[1]);
        for (int k = half + 1; k < limit; ++k)
            d[k] = d[n - k];
        std::memset (d + limit, 0, sizeof (float) * (size_t) (2 * n - limit));
    }

    // "SSE1", "NEON", ... or "scalar" - for logs/tests.
    static const char* simdArch() noexcept { return pffft_simd_arch(); }

private:
    struct Scratch
    {
        float* buf = nullptr;
        float* work = nullptr;
        int capacity = 0;
        ~Scratch() { release(); }
        void release() { pffft_aligned_free (buf); pffft_aligned_free (work); buf = work = nullptr; capacity = 0; }
    };

    static Scratch& scratch (int n)
    {
        thread_local Scratch s;
        if (s.capacity < n)
        {
            s.release();
            s.buf  = static_cast<float*> (pffft_aligned_malloc (sizeof (float) * (size_t) n));
            s.work = static_cast<float*> (pffft_aligned_malloc (sizeof (float) * (size_t) n));
            s.capacity = n;
        }
        return s;
    }

    // Real forward FFT of d[0..N) into the thread's scratch, PFFFT packed order. The returned
    // pointer stays valid until the next transform on this thread.
    const float* forwardPacked (const float* d) const noexcept
    {
        auto& s = scratch (size_);
        std::memcpy (s.buf, d, sizeof (float) * (size_t) size_);
        pffft_transform_ordered (setup_, s.buf, s.buf, s.work, PFFFT_FORWARD);
        return s.buf;
    }

    // One immutable setup per size for the whole process (PFFFT setups are read-only and
    // safe to share between threads). nullptr when PFFFT cannot do this size.
    static PFFFT_Setup* sharedSetup (int n)
    {
        struct Cache
        {
            std::mutex lock;
            std::map<int, PFFFT_Setup*> bySize;
            ~Cache() { for (auto& kv : bySize) if (kv.second != nullptr) pffft_destroy_setup (kv.second); }
        };
        static Cache cache;
        std::lock_guard<std::mutex> lk (cache.lock);
        auto it = cache.bySize.find (n);
        if (it != cache.bySize.end())
            return it->second;
        PFFFT_Setup* s = (n >= pffft_min_fft_size (PFFFT_REAL)) ? pffft_new_setup (n, PFFFT_REAL) : nullptr;
        cache.bySize[n] = s;
        return s;
    }

    static void logEngineOnce()
    {
        static std::atomic<bool> logged { false };
        if (! logged.exchange (true))
            juce::Logger::writeToLog (juce::String ("[Fft] PFFFT active, SIMD=") + pffft_simd_arch()
                + " (min real size " + juce::String (pffft_min_fft_size (PFFFT_REAL)) + ")");
    }

    int order_ = 0, size_ = 1;
    PFFFT_Setup* setup_ = nullptr;
    std::unique_ptr<juce::dsp::FFT> fallback_;
};

} // namespace samplar
