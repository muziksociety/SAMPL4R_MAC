#include "OfflineRender.h"
#include "PlaybackEngine.h"
#include <cmath>
#include <limits>
#include <vector>

namespace samplar {
namespace engine {

std::unique_ptr<juce::AudioBuffer<float>> renderSequence(const EngineParams& params,
                                                         const Snapshot& snap,
                                                         double sampleRate,
                                                         uint32_t laneMask,
                                                         int numCells,
                                                         double bpm,
                                                         const std::function<void(float)>& progress,
                                                         int blockSize)
{
    if (sampleRate <= 0.0 || numCells <= 0 || bpm <= 0.0 || laneMask == 0)
        return nullptr;

    const int64_t total64 = (int64_t) std::llround((double) numCells * 60.0 / bpm * sampleRate);
    if (total64 <= 0 || total64 > (int64_t) std::numeric_limits<int>::max())
        return nullptr;
    const int total = (int) total64;

    auto engHolder = std::make_unique<PlaybackEngine>(params);  // ~130 KB: keep off the stack
    auto& eng = *engHolder;
    eng.prepare(sampleRate, blockSize);
    eng.publishSnapshot(&snap);

    Command c;
    c.type = CmdType::SeqStart;
    c.i0   = (int) laneMask;
    c.d0   = bpm;
    eng.pushCommand(c);

    auto out = std::make_unique<juce::AudioBuffer<float>>(2, total);
    out->clear();
    const HostInfo noHost;
    for (int pos = 0; pos < total; pos += blockSize)
    {
        const int n = juce::jmin(blockSize, total - pos);
        eng.process(*out, pos, n, nullptr, noHost);
        if (progress) progress((float) (pos + n) / (float) total);
    }
    eng.publishSnapshot(nullptr);
    return out;
}

std::unique_ptr<juce::AudioBuffer<float>> renderSegment(const EngineParams& params,
                                                        const Snapshot& snap,
                                                        double sampleRate,
                                                        int stem,
                                                        int segIndex,
                                                        double maxSeconds,
                                                        int blockSize)
{
    if (sampleRate <= 0.0 || stem < 0 || stem >= snap.stemCount)
        return nullptr;

    auto engHolder = std::make_unique<PlaybackEngine>(params);  // ~130 KB: keep off the stack
    auto& eng = *engHolder;
    eng.prepare(sampleRate, blockSize);
    eng.publishSnapshot(&snap);

    Command c;
    c.type = CmdType::PlaySegment;
    c.i0 = stem;
    c.i1 = segIndex;
    c.f0 = 1.0f;
    c.b0 = false;
    eng.pushCommand(c);

    const int cap = (int) juce::jmin(maxSeconds * sampleRate, (double) std::numeric_limits<int>::max() / 2);
    juce::AudioBuffer<float> work(2, juce::jmax(blockSize, 1));
    std::vector<float> L, R;
    L.reserve(1 << 16);
    R.reserve(1 << 16);

    const HostInfo noHost;
    int rendered = 0;
    bool started = false;
    while (rendered < cap)
    {
        work.clear();
        eng.process(work, 0, blockSize, nullptr, noHost);
        const bool active = eng.hasActiveVoices();
        if (! started && ! active
            && work.getMagnitude(0, 0, blockSize) == 0.0f && work.getMagnitude(1, 0, blockSize) == 0.0f)
        {
            // Voice never started (empty / muted chop): nothing to export.
            eng.publishSnapshot(nullptr);
            return nullptr;
        }
        started = true;
        L.insert(L.end(), work.getReadPointer(0), work.getReadPointer(0) + blockSize);
        R.insert(R.end(), work.getReadPointer(1), work.getReadPointer(1) + blockSize);
        rendered += blockSize;
        if (! active) break;
    }
    eng.publishSnapshot(nullptr);

    // Trim trailing digital silence left by the last block.
    int len = (int) L.size();
    while (len > 0 && L[(size_t) len - 1] == 0.0f && R[(size_t) len - 1] == 0.0f) --len;
    if (len <= 0) return nullptr;

    auto out = std::make_unique<juce::AudioBuffer<float>>(2, len);
    out->copyFrom(0, 0, L.data(), len);
    out->copyFrom(1, 0, R.data(), len);
    return out;
}

std::shared_ptr<juce::AudioBuffer<float>> resampleStem(const juce::AudioBuffer<float>& src,
                                                       double srcRate,
                                                       double dstRate,
                                                       const std::function<bool()>& shouldAbort)
{
    const int inLen = src.getNumSamples();
    const int numCh = src.getNumChannels();
    if (inLen <= 0 || numCh <= 0 || srcRate <= 0.0 || dstRate <= 0.0)
        return nullptr;

    // Kaiser-windowed sinc (32 zero crossings, beta 9.6, ~-90 dB stopband).
    // When downsampling the cutoff drops to the new Nyquist (anti-aliasing).
    const double ratio  = srcRate / dstRate;                       // input samples per output sample
    const int    outLen = (int) std::ceil((double) inLen / ratio);
    const double cutoff = juce::jmin(1.0, 1.0 / ratio) * 0.97;     // keep a small transition band
    const int    zeroX  = 32;
    const double halfW  = zeroX / cutoff;                          // half width in input samples
    static constexpr int kRes = 512;                                   // table points per input sample
    const double beta   = 9.6;

    auto besselI0 = [](double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 40; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; }
        return sum;
    };
    const int tableLen = (int) std::ceil(halfW * kRes) + 2;
    std::vector<float> table((size_t) tableLen);
    const double i0b = besselI0(beta);
    for (int i = 0; i < tableLen; ++i)
    {
        const double t = (double) i / kRes;                        // distance in input samples
        if (t >= halfW) { table[(size_t) i] = 0.0f; continue; }
        const double xs = juce::MathConstants<double>::pi * cutoff * t;
        const double sinc = (t == 0.0) ? 1.0 : std::sin(xs) / xs;
        const double r = t / halfW;
        const double win = besselI0(beta * std::sqrt(juce::jmax(0.0, 1.0 - r * r))) / i0b;
        table[(size_t) i] = (float) (cutoff * sinc * win);
    }
    auto kernel = [&table, tableLen](double t) noexcept
    {
        const double a = std::abs(t) * kRes;
        const int i = (int) a;
        if (i + 1 >= tableLen) return 0.0f;
        const float fr = (float) (a - i);
        return table[(size_t) i] + fr * (table[(size_t) i + 1] - table[(size_t) i]);
    };

    auto out = std::make_shared<juce::AudioBuffer<float>>(numCh, outLen);
    const int span = (int) std::ceil(halfW);
    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* in = src.getReadPointer(ch);
        float* o = out->getWritePointer(ch);
        for (int j = 0; j < outLen; ++j)
        {
            if ((j & 0xFFFF) == 0 && shouldAbort && shouldAbort()) return nullptr;
            const double x  = j * ratio;
            const int    c  = (int) std::floor(x);
            const int    k0 = juce::jmax(0, c - span + 1);
            const int    k1 = juce::jmin(inLen - 1, c + span);
            double acc = 0.0;
            for (int k = k0; k <= k1; ++k)
                acc += (double) in[k] * kernel(x - k);
            o[j] = (float) acc;
        }
    }
    return out;
}

} // namespace engine
} // namespace samplar
