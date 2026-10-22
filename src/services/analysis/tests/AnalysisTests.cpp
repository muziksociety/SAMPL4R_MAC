// AnalysisTests - synthetic regression tests for the analysis workstream
// (C10 downbeats, C11 FFT layout, C12 BeatThis-first, C16 IBI tempo fit,
// C17 onset timing + zero-crossing snap, KeyDetector) plus an optional
// benchmark on a real drum stem.
//
//   AnalysisTests.exe            run all synthetic tests (+ real stem if cached)
//   AnalysisTests.exe --stem F   also benchmark drum stem file F
//   AnalysisTests.exe --quick    skip the slow BeatThis click-track sweep
//
// Logs from the detectors go to AnalysisTests.log next to the exe.

#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include "services/analysis/AnalysisDsp.h"
#include "services/analysis/BeatThisDetector.h"
#include "services/analysis/DownbeatDetector.h"
#include "services/analysis/HybridTempoDetector.h"
#include "services/analysis/KeyDetector.h"
#include "services/analysis/OctaveCorrection.h"
#include "services/analysis/TempoRefiner.h"
#include "services/smode/SpectralFluxDetector.h"
#include "audio/MicroFade.h"

#include <chrono>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace samplar;

namespace samplar { namespace sep2tests {
int runAll(bool quick, int& checks);
int cacheWorkerMain(const juce::String& song, const juce::String& cacheDir, const juce::String& id);
int ortMissingCheckMain();
}}

namespace {

//==============================================================================
// Harness
//==============================================================================
int gFailures = 0, gChecks = 0;

void check(bool ok, const std::string& what)
{
    ++gChecks;
    if (! ok) ++gFailures;
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    juce::Logger::writeToLog(juce::String("[AnalysisTests] ") + (ok ? "PASS " : "FAIL ") + what);
}

std::string fmt(const char* f, ...)
{
    char buf[512];
    va_list ap; va_start(ap, f); std::vsnprintf(buf, sizeof(buf), f, ap); va_end(ap);
    return buf;
}

double nowMs()
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

constexpr double kSR = 44100.0;
constexpr double kPi = 3.14159265358979323846;

//==============================================================================
// Synthetic drums
//==============================================================================
struct Synth
{
    std::mt19937 rng { 1234 };
    std::uniform_real_distribution<float> uni { -1.0f, 1.0f };

    void kick(std::vector<float>& x, int64_t at, float amp)
    {
        double phase = 0.0;
        const int len = (int) (0.35 * kSR);
        for (int i = 0; i < len && at + i < (int64_t) x.size(); ++i)
        {
            const double t = i / kSR;
            const double f = 50.0 + 110.0 * std::exp(-t / 0.03);
            phase += 2.0 * kPi * f / kSR;
            x[(size_t) (at + i)] += amp * (float) (std::exp(-t / 0.12) * std::sin(phase - 2.0 * kPi * f / kSR));
        }
    }
    void snare(std::vector<float>& x, int64_t at, float amp)
    {
        const int len = (int) (0.25 * kSR);
        for (int i = 0; i < len && at + i < (int64_t) x.size(); ++i)
        {
            const double t = i / kSR;
            const float noise = uni(rng);
            const float tone = (float) std::sin(2.0 * kPi * 190.0 * t);
            x[(size_t) (at + i)] += amp * (float) std::exp(-t / 0.06) * (0.75f * noise + 0.35f * tone);
        }
    }
    void hat(std::vector<float>& x, int64_t at, float amp, double decay = 0.03)
    {
        const int len = (int) (6 * decay * kSR);
        float prev = 0.0f;
        for (int i = 0; i < len && at + i < (int64_t) x.size(); ++i)
        {
            const float n = uni(rng);
            const float hp = n - prev; prev = n;          // crude high-pass
            x[(size_t) (at + i)] += amp * 0.5f * (float) std::exp(-(i / kSR) / decay) * hp;
        }
    }
    void crash(std::vector<float>& x, int64_t at, float amp)
    {
        hat(x, at, amp, 0.6);
    }
};

enum class Pattern { Rock, Offbeat, HipHop, Dnb };

const char* patternName(Pattern p)
{
    switch (p) { case Pattern::Rock: return "rock"; case Pattern::Offbeat: return "offbeat";
                 case Pattern::HipHop: return "hiphop"; case Pattern::Dnb: return "dnb"; }
    return "?";
}

struct Track
{
    juce::AudioBuffer<float> buffer;
    double firstDownbeatSec = 0.0;
    double barSec = 0.0;
    std::vector<int64_t> hits; // every hit start sample (sorted, unique)
};

// leadSec of silence, then one pickup beat (hat only), then `bars` bars of 4/4.
Track makeTrack(double bpm, Pattern pat, int bars, double leadSec, uint32_t seed = 1)
{
    Synth s; s.rng.seed(seed);
    std::vector<int64_t> hits;
    const double beat = 60.0 / bpm;
    const double start = leadSec + beat; // first downbeat
    const double total = start + bars * 4 * beat + 1.0;
    std::vector<float> x((size_t) (total * kSR), 0.0f);
    auto at = [&](double beatPos) { const int64_t v = (int64_t) std::llround((start + beatPos * beat) * kSR); hits.push_back(v); return v; };

    s.hat(x, at(-1.0), 0.25f); // pickup
    for (int b = 0; b < bars; ++b)
    {
        const double o = b * 4.0;
        if (b % 2 == 0) s.crash(x, at(o), 0.30f);
        switch (pat)
        {
            case Pattern::Rock:
                s.kick(x, at(o + 0), 0.9f); s.kick(x, at(o + 2), 0.8f);
                s.snare(x, at(o + 1), 0.5f); s.snare(x, at(o + 3), 0.5f);
                for (int h = 0; h < 8; ++h) s.hat(x, at(o + h * 0.5), 0.2f);
                break;
            case Pattern::Offbeat: // syncopated kicks, hats only on the "and"s
                s.kick(x, at(o + 0), 0.9f); s.kick(x, at(o + 1.5), 0.8f); s.kick(x, at(o + 2.75), 0.7f);
                s.snare(x, at(o + 1), 0.5f); s.snare(x, at(o + 3), 0.5f);
                for (int h = 0; h < 4; ++h) s.hat(x, at(o + h + 0.5), 0.35f, 0.08);
                break;
            case Pattern::HipHop:
                s.kick(x, at(o + 0), 0.9f); s.kick(x, at(o + 0.75), 0.6f); s.kick(x, at(o + 2.5), 0.8f);
                s.snare(x, at(o + 1), 0.55f); s.snare(x, at(o + 3), 0.55f);
                for (int h = 0; h < 16; ++h) s.hat(x, at(o + h * 0.25), (h % 2) ? 0.1f : 0.18f);
                break;
            case Pattern::Dnb:
                s.kick(x, at(o + 0), 0.9f); s.kick(x, at(o + 2.5), 0.8f);
                s.snare(x, at(o + 1), 0.55f); s.snare(x, at(o + 3), 0.55f);
                for (int h = 0; h < 8; ++h) s.hat(x, at(o + h * 0.5), 0.15f);
                break;
        }
    }

    Track t;
    t.buffer.setSize(2, (int) x.size());
    for (int ch = 0; ch < 2; ++ch)
        t.buffer.copyFrom(ch, 0, x.data(), (int) x.size());
    t.firstDownbeatSec = start;
    t.barSec = 4.0 * beat;
    std::sort(hits.begin(), hits.end());
    hits.erase(std::unique(hits.begin(), hits.end()), hits.end());
    t.hits = std::move(hits);
    return t;
}

//==============================================================================
// C11: FFT layout
//==============================================================================
// Faithful copy of the OLD (buggy) TempoRefiner/OctaveCorrection envelope, to
// show the regression test would have caught it.
std::vector<float> oldLayoutEnvelope(const float* x, int n, double sr, int N, int hop, double lo, double hi)
{
    juce::dsp::FFT fft((int) std::lround(std::log2((double) N)));
    std::vector<float> w((size_t) N);
    for (int i = 0; i < N; ++i) w[(size_t) i] = 0.5f * (1.0f - std::cos(2.0f * (float) kPi * i / (N - 1)));
    const double binHz = sr / N;
    const int bLo = std::max(1, (int) std::floor(lo / binHz)), bHi = std::min(N / 2, (int) std::ceil(hi / binHz));
    std::vector<float> buf((size_t) N * 2), prev((size_t) N / 2), cur((size_t) N / 2), env;
    bool first = true;
    for (int pos = 0; pos + N <= n; pos += hop)
    {
        for (int i = 0; i < N; ++i) { buf[(size_t) i * 2] = x[pos + i] * w[(size_t) i]; buf[(size_t) i * 2 + 1] = 0.0f; }
        fft.performRealOnlyForwardTransform(buf.data());
        for (int k = 0; k < N / 2; ++k) cur[(size_t) k] = std::hypot(buf[(size_t) k * 2], buf[(size_t) k * 2 + 1]);
        float f = 0.0f;
        if (! first) for (int k = bLo; k < bHi; ++k) f += std::max(0.0f, cur[(size_t) k] - prev[(size_t) k]);
        env.push_back(f); first = false; prev.swap(cur);
    }
    return env;
}

std::vector<float> toneBursts(double freq, int n)
{
    std::vector<float> x((size_t) n, 0.0f);
    for (int b = 0; b * 0.5 * kSR + 0.1 * kSR < n; ++b)
    {
        const int s0 = (int) (b * 0.5 * kSR);
        for (int i = 0; i < (int) (0.1 * kSR) && s0 + i < n; ++i)
            x[(size_t) (s0 + i)] = 0.5f * (float) (std::sin(2.0 * kPi * freq * i / kSR)
                                   * 0.5 * (1.0 - std::cos(2.0 * kPi * i / (0.1 * kSR)))); // smooth burst
    }
    return x;
}

void testFftLayout()
{
    std::printf("\n== C11: spectral-flux FFT layout (TempoRefiner / OctaveCorrection) ==\n");
    const int n = (int) (4 * kSR);
    const int N = 2048, hop = 512;
    double sr = 0.0;

    auto sum = [](const std::vector<float>& v) { double s = 0; for (float f : v) s += f; return s; };

    // (a) band rejection: 6 kHz bursts must be ~invisible in the 50-4000 Hz band.
    const auto in1k  = toneBursts(1000.0, n), in6k = toneBursts(6000.0, n);
    const double newIn  = sum(analysis::spectralFluxEnvelope(in1k.data(), n, kSR, N, hop, 50, 4000, sr));
    const double newOut = sum(analysis::spectralFluxEnvelope(in6k.data(), n, kSR, N, hop, 50, 4000, sr));
    const double oldIn  = sum(oldLayoutEnvelope(in1k.data(), n, kSR, N, hop, 50, 4000));
    const double oldOut = sum(oldLayoutEnvelope(in6k.data(), n, kSR, N, hop, 50, 4000));
    std::printf("  6k/1k in-band flux ratio: new %.4f  old %.4f\n", newOut / newIn, oldOut / oldIn);
    check(newOut / newIn < 0.02, fmt("6 kHz rejected by 50-4000 Hz band (ratio %.4f < 0.02)", newOut / newIn));
    check(oldOut / oldIn > 0.2, fmt("old layout leaks 6 kHz into band (ratio %.3f) - test discriminates", oldOut / oldIn));

    // (b) time alignment: an impulse at t peaks the flux when it sits ~3N/4 - hop/2
    //     into the window (Hann rising edge). The old layout saw only the first
    //     N/2 samples -> peak one hop later in the frame sequence.
    std::vector<float> imp((size_t) n, 0.0f);
    const int t = 40000; imp[(size_t) t] = 1.0f;
    auto argmax = [](const std::vector<float>& v) { return (int) (std::max_element(v.begin(), v.end()) - v.begin()); };
    const int fNew = argmax(analysis::spectralFluxEnvelope(imp.data(), n, kSR, N, hop, 0, kSR / 2, sr));
    const int fOld = argmax(oldLayoutEnvelope(imp.data(), n, kSR, N, hop, 0, kSR / 2));
    const int pNew = t - fNew * hop, pOld = t - fOld * hop, expectP = 3 * N / 4 - hop / 2;
    std::printf("  impulse offset in peak frame: new %d  old %d  expected ~%d\n", pNew, pOld, expectP);
    check(std::abs(pNew - expectP) <= hop / 2, fmt("impulse lands at expected window offset (new %d)", pNew));
    check(std::abs(pOld - expectP) > hop / 2, fmt("old layout misplaces it (old %d) - test discriminates", pOld));

    // (c) TempoRefiner precision from a wrong-ish hint on a 120 BPM track.
    Track tr = makeTrack(120.0, Pattern::Rock, 24, 0.3);
    auto ref = refineTempoNearBPM(tr.buffer, kSR, 117.5, 0.05);
    check(ref.improved && std::abs(ref.refinedBPM - 120.0) < 0.3,
          fmt("TempoRefiner 117.5 hint -> %.3f (|err| < 0.3)", ref.refinedBPM));

    // (d) OctaveCorrection keeps 120 and corrects 60 -> 120 on an 8th-hat rock beat.
    const auto mono = analysis::downmixToMonoBuffer(tr.buffer);
    auto oc1 = correctOctaveFromBuffer(120.0, mono, kSR);
    std::printf("  octave: T=120 -> %.2f density %.2f Hz\n", oc1.correctedBPM, oc1.onsetDensityHz);
    check(std::abs(oc1.correctedBPM - 120.0) < 1.0, fmt("OctaveCorrection keeps 120 (got %.2f)", oc1.correctedBPM));

    // (e) FFT autocorrelation == naive autocorrelation (DWT/TempoAnalyzer C12 speedup).
    std::mt19937 rng(7); std::normal_distribution<float> g;
    std::vector<float> v(3000); for (auto& e : v) e = g(rng);
    const auto fa = analysis::autocorrelationFFT(v, 2999);
    double mean = 0; for (float e : v) mean += e; mean /= v.size();
    double r0 = 0; std::vector<double> naive(3000);
    for (int lag = 0; lag < 3000; ++lag) { double s = 0; for (int i = 0; i + lag < 3000; ++i) s += (v[i] - mean) * (v[i + lag] - mean); naive[lag] = s; }
    r0 = naive[0]; double maxErr = 0;
    for (int lag = 0; lag < 3000; ++lag) maxErr = std::max(maxErr, std::abs(naive[lag] / r0 - fa[(size_t) lag]));
    check(maxErr < 1e-4, fmt("FFT autocorrelation matches O(n^2) loop (max err %.2e)", maxErr));
}

//==============================================================================
// C16: tempo from beat list
//==============================================================================
void testTempoEstimator()
{
    std::printf("\n== C16: BeatThis tempo estimator (IBI fit over beat numbers) ==\n");
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    for (double bpm : { 70.0, 84.86, 95.0, 128.0, 174.0 })
    {
        const double ibi = 60.0 / bpm;
        std::vector<double> beats;
        for (int k = 0; k < 256; ++k)
        {
            if (k > 3 && u(rng) < 0.12) continue;                    // 12% missed beats (fills / breaks)
            double t = 0.8 + k * ibi;
            t = std::round(t * 50.0) / 50.0;                           // 50 fps model frames
            beats.push_back(t);
        }
        // a 16-beat break (gap) in the middle
        std::vector<double> withGap;
        for (double t : beats) if (t < 60.0 || t > 60.0 + 16 * ibi) withGap.push_back(t);

        const auto te = BeatThisDetector::estimateTempo(withGap);

        // Old estimator: span / index-difference of inliers (biased by missed beats)
        const double oldMean = (withGap.back() - withGap.front()) / (double) (withGap.size() - 1);
        std::printf("  %.2f BPM: new %.4f  median %.4f  old-index-mean %.4f  (inliers %d/%d)\n",
                    bpm, te.bpm, te.medianBpm, 60.0 / oldMean, te.inliers, (int) withGap.size());
        check(te.valid && std::abs(te.bpm - bpm) < 0.05, fmt("IBI fit %.2f -> %.4f (|err| < 0.05)", bpm, te.bpm));
    }
}

//==============================================================================
// C12 / C10: BeatThis-first tempo + downbeats on synthetic drum tracks
//==============================================================================
bool octaveError(double got, double truth)
{
    const double r = got / truth;
    for (double m : { 0.5, 2.0, 1.0 / 3.0, 3.0, 2.0 / 3.0, 1.5, 0.75, 4.0 / 3.0 })
        if (std::abs(r - m) < 0.03) return true;
    return false;
}

void testTempoSweep(bool beatThisAvailable)
{
    std::printf("\n== C12: tempo sweep (BeatThis first, DSP fallback) ==\n");
    struct Case { double bpm; Pattern pat; };
    const Case cases[] = {
        { 70, Pattern::HipHop }, { 85, Pattern::HipHop }, { 85, Pattern::Offbeat }, { 95, Pattern::HipHop },
        { 120, Pattern::Rock }, { 120, Pattern::Offbeat }, { 128, Pattern::Rock }, { 140, Pattern::Offbeat },
        { 174, Pattern::Dnb },
    };
    for (const auto& c : cases)
    {
        const int bars = std::max(16, (int) std::lround(c.bpm / 4.0)); // ~60 s
        Track tr = makeTrack(c.bpm, c.pat, bars, 0.37, (uint32_t) c.bpm);

        if (beatThisAvailable)
        {
            HybridTempoDetector det; HybridTempoDetector::Config cfg; cfg.minBPM = 50; cfg.maxBPM = 220;
            const double t0 = nowMs();
            auto r = det.detect(tr.buffer, kSR, cfg);
            const double ms = nowMs() - t0;
            std::printf("  %5.1f %-8s primary: %.3f (%s, conf %.2f) %.0f ms\n", c.bpm, patternName(c.pat),
                        r.bpm, r.getSourceName().toRawUTF8(), r.confidence, ms);
            check(std::abs(r.bpm - c.bpm) <= 0.5 && ! (octaveError(r.bpm, c.bpm) && std::abs(r.bpm - c.bpm) > 1.0),
                  fmt("%.0f BPM %s: primary %.3f within +-0.5", c.bpm, patternName(c.pat), r.bpm));

            // C10: downbeat / grid offset from BeatThis downbeats
            BeatThisDetector::clearCache();
            auto db = DownbeatDetector::detect(tr.buffer, kSR, c.bpm);
            const double expect = std::fmod(tr.firstDownbeatSec, tr.barSec);
            double err = (db.beat1Sample / kSR) - expect;
            err -= tr.barSec * std::round(err / tr.barSec);
            std::printf("        downbeat: source=%s beatsPerBar=%d offset %.4f s (expect %.4f) err %.2f ms\n",
                        db.source == DownbeatDetector::Source::BeatThis ? "BeatThis" : "Comb",
                        db.beatsPerBar, db.beat1Sample / kSR, expect, err * 1000.0);
            check(db.valid && db.source == DownbeatDetector::Source::BeatThis && std::abs(err) < 0.005,
                  fmt("%.0f BPM %s: bar offset recovered within 5 ms (err %.2f ms)", c.bpm, patternName(c.pat), err * 1000.0));
        }

        // DSP fallback path (BeatThis disabled) - must not octave-err.
        {
            HybridTempoDetector det; HybridTempoDetector::Config cfg; cfg.minBPM = 50; cfg.maxBPM = 220;
            cfg.useBeatThis = false;
            const double t0 = nowMs();
            auto r = det.detect(tr.buffer, kSR, cfg);
            const double ms = nowMs() - t0;
            std::printf("  %5.1f %-8s fallback: %.3f (%s) %.0f ms\n", c.bpm, patternName(c.pat),
                        r.bpm, r.getSourceName().toRawUTF8(), ms);
            // Informational: the DSP fallback only runs without a confident
            // BeatThis result; octave/metric ambiguity is inherent there.
            std::printf("        [INFO] fallback %s\n",
                        std::abs(r.bpm - c.bpm) <= 0.5 ? "within +-0.5" : (octaveError(r.bpm, c.bpm) ? "OCTAVE/METRIC ambiguity" : "OFF"));
        }
    }
}

void testCombFallbackDownbeat()
{
    std::printf("\n== C10: comb-filter fallback reports beat phase only ==\n");
    Track tr = makeTrack(100.0, Pattern::Rock, 16, 0.5);
    auto db = DownbeatDetector::detectCombFilter(tr.buffer, kSR, 100.0);
    const double beat = 0.6;
    double err = db.beat1Sample / kSR - std::fmod(tr.firstDownbeatSec, beat);
    err -= beat * std::round(err / beat);
    check(db.valid && db.source == DownbeatDetector::Source::CombFilter && db.beatsPerBar == 0,
          "comb result flagged as beat-phase only (source=CombFilter, beatsPerBar=0)");
    check(std::abs(err) < 0.010, fmt("comb beat phase within 10 ms (err %.2f ms)", err * 1000.0));

    // detectFromDownbeats with exact downbeats (no model): offset = first downbeat mod bar.
    std::vector<double> beats, downs;
    for (int k = 0; k < 64; ++k) { beats.push_back(tr.firstDownbeatSec + k * beat); if (k % 4 == 0) downs.push_back(beats.back()); }
    auto d2 = DownbeatDetector::detectFromDownbeats(tr.buffer, kSR, 100.0, beats, downs, 0.95);
    double e2 = d2.beat1Sample / kSR - std::fmod(tr.firstDownbeatSec, 2.4);
    check(d2.valid && d2.beatsPerBar == 4 && std::abs(e2) < 0.002,
          fmt("detectFromDownbeats: bar offset err %.3f ms, beatsPerBar=%d", e2 * 1000.0, d2.beatsPerBar));
}

//==============================================================================
// C17: onset timing + zero-crossing snap
//==============================================================================
void testOnsets()
{
    std::printf("\n== C17: transient onset timing + zero-crossing snap ==\n");
    Synth s; s.rng.seed(99);
    std::mt19937 rng(5);
    std::uniform_real_distribution<double> gap(0.12, 0.40);
    std::normal_distribution<float> noise(0.0f, 3e-4f); // ~ -70 dB floor
    const int n = (int) (20.0 * kSR);
    std::vector<float> x((size_t) n);
    for (auto& v : x) v = noise(rng);

    std::vector<int64_t> truth;
    double t = 0.3;
    int which = 0;
    while (t < 19.0)
    {
        const int64_t at = (int64_t) std::llround(t * kSR) + (int64_t) (which * 7 % 13); // non-grid sample positions
        truth.push_back(at);
        switch (which % 3) { case 0: s.kick(x, at, 0.8f); break; case 1: s.snare(x, at, 0.6f); break; default: s.hat(x, at, 0.4f); }
        ++which;
        t += gap(rng);
    }

    auto run = [&](double preRollMs, bool microFade, std::vector<int64_t>& attacks)
    {
        microFadeEnabledFlag().store(microFade);
        SpectralFluxDetector::Settings st; st.preRollMs = preRollMs;
        SpectralFluxDetector det(st);
        auto b = det.detect(x.data(), n, kSR);
        attacks = det.getLastAttackSamples();
        return b;
    };

    std::vector<int64_t> attacks;
    auto bounds = run(0.0, false, attacks);

    // match each truth onset to the nearest detected attack
    int matched = 0; double maxErrMs = 0.0, sumErr = 0.0;
    for (const int64_t& tr : truth)
    {
        int64_t best = -1; int64_t bd = INT64_MAX;
        for (int64_t a : attacks) if (std::llabs(a - tr) < bd) { bd = std::llabs(a - tr); best = a; }
        if (best >= 0 && bd < (int64_t) (0.02 * kSR))
        {
            ++matched; const double e = (best - tr) * 1000.0 / kSR; maxErrMs = std::max(maxErrMs, std::abs(e)); sumErr += e;
            if (std::abs(e) >= 1.0)
            {
                const size_t idx = (size_t) (&tr - truth.data());
                std::printf("    miss: onset #%d (%s) at %.4f s err %.3f ms\n", (int) idx,
                            idx % 3 == 0 ? "kick" : (idx % 3 == 1 ? "snare" : "hat"), tr / kSR, e);
            }
        }
    }
    std::printf("  %d truth onsets, %d detected, %d matched; attack err max %.3f ms mean %.3f ms\n",
                (int) truth.size(), (int) attacks.size(), matched, maxErrMs, matched ? sumErr / matched : 0.0);
    check(matched == (int) truth.size(), fmt("all %d transients detected (%d matched)", (int) truth.size(), matched));
    check((int) attacks.size() <= (int) truth.size() + 1, fmt("no spurious onsets (%d detected)", (int) attacks.size()));
    check(maxErrMs < 1.0, fmt("onset timing error < 1 ms (max %.3f ms)", maxErrMs));

    // boundaries: zero crossing, within [-0.25 ms, +1 ms] of the attack (preRoll 0)
    int zcOk = 0, posOk = 0;
    for (size_t i = 0; i < bounds.size() && i < attacks.size(); ++i)
    {
        const int64_t b = bounds[i], a = attacks[i];
        const bool zc = (b > 0 && x[(size_t) b - 1] * x[(size_t) b] <= 0.0f) || (b + 1 < n && x[(size_t) b] * x[(size_t) b + 1] <= 0.0f);
        // No crossing at all inside the search window (e.g. a 50 Hz kick tail
        // has 10 ms half-periods) -> boundary legitimately stays on the attack.
        bool anyZcInWindow = false;
        const int back = (int) std::lround(0.001 * kSR);
        for (int64_t i = std::max<int64_t>(1, a - back); i <= std::min<int64_t>(n - 1, a + back / 4); ++i)
            if (x[(size_t) i - 1] * x[(size_t) i] <= 0.0f) { anyZcInWindow = true; break; }
        zcOk += (zc || (! anyZcInWindow && b == a)) ? 1 : 0;
        const double d = (a - b) * 1000.0 / kSR;
        posOk += (d >= -0.26 && d <= 1.01) ? 1 : 0;
    }
    check(zcOk == (int) bounds.size(), fmt("every boundary on a zero crossing when one exists within 1 ms (%d/%d)", zcOk, (int) bounds.size()));
    check(posOk == (int) bounds.size(), fmt("boundaries within [-0.25, +1] ms of attack (%d/%d)", posOk, (int) bounds.size()));

    // MicroFade ON + auto pre-roll: boundary ~kMicroFadeMs before the attack.
    std::vector<int64_t> a2;
    auto b2 = run(-1.0, true, a2);
    int preOk = 0;
    for (size_t i = 0; i < b2.size() && i < a2.size(); ++i)
    {
        const double d = (a2[i] - b2[i]) * 1000.0 / kSR;
        preOk += (d >= kMicroFadeMs - 0.26 && d <= kMicroFadeMs + 1.01) ? 1 : 0;
    }
    check(preOk == (int) b2.size(), fmt("MicroFade ON: boundary %.0f ms pre-roll so the fade-in ends at the attack (%d/%d)",
                                        kMicroFadeMs, preOk, (int) b2.size()));
    microFadeEnabledFlag().store(true);

    // Near-silent stem: no onsets from noise alone.
    std::vector<float> quiet((size_t) n);
    for (auto& v : quiet) v = noise(rng) * 0.1f;
    SpectralFluxDetector det;
    auto q = det.detect(quiet.data(), n, kSR);
    check(q.size() <= 2, fmt("near-silent stem yields no spurious onsets (%d)", (int) q.size()));
}


// The OLD SpectralFluxDetector pipeline (window-start onset + 256-sample
// min-energy backtrack), reproduced to report before/after timing error.
std::vector<int64_t> legacyOnsets(const float* x, int n, double sr)
{
    const int N = 1024, hop = 512;
    juce::dsp::FFT fft(10);
    std::vector<float> w((size_t) N), buf((size_t) N * 2), prev((size_t) N / 2, 0.0f), flux;
    for (int i = 0; i < N; ++i) w[(size_t) i] = 0.5f * (1.0f - std::cos(2.0f * (float) kPi * i / (N - 1)));
    for (int f = 0; f + N <= n; f += hop)
    {
        std::fill(buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = x[f + i] * w[(size_t) i];
        fft.performRealOnlyForwardTransform(buf.data());
        float fl = 0.0f;
        for (int b = 0; b < N / 2; ++b)
        {
            const float m = std::hypot(buf[(size_t) b * 2], buf[(size_t) b * 2 + 1]);
            fl += std::max(0.0f, m - prev[(size_t) b]); prev[(size_t) b] = m;
        }
        flux.push_back(fl);
    }
    std::vector<int64_t> out;
    const int nf = (int) flux.size();
    for (int i = 1; i + 1 < nf; ++i)
    {
        std::vector<float> loc(flux.begin() + std::max(0, i - 5), flux.begin() + std::min(nf - 1, i + 5) + 1);
        std::nth_element(loc.begin(), loc.begin() + loc.size() / 2, loc.end());
        if (! (flux[(size_t) i] > loc[loc.size() / 2] * 1.5f && flux[(size_t) i] > flux[(size_t) i - 1] && flux[(size_t) i] >= flux[(size_t) i + 1])) continue;
        const int64_t peak = (int64_t) i * hop;
        int64_t best = peak; float minE = 1e30f;
        for (int64_t pos = peak; pos >= std::max<int64_t>(0, peak - 256); pos -= 32)
        {
            float e = 0; const int len = (int) std::min<int64_t>(32, n - pos);
            for (int k = 0; k < len; ++k) e += x[pos + k] * x[pos + k];
            e /= std::max(1, len);
            if (e < minE) { minE = e; best = pos; }
        }
        if (out.empty() || best - out.back() >= (int64_t) (0.05 * sr)) out.push_back(best);
    }
    return out;
}

struct OnsetStats { int matched = 0; double maxAbsMs = 0, meanMs = 0, within1ms = 0; };
OnsetStats matchOnsets(const std::vector<int64_t>& truth, const std::vector<int64_t>& det)
{
    OnsetStats st; int in1 = 0; double sum = 0;
    for (int64_t t : truth)
    {
        int64_t bd = INT64_MAX, best = 0;
        for (int64_t a : det) if (std::llabs(a - t) < bd) { bd = std::llabs(a - t); best = a; }
        if (bd > (int64_t) (0.03 * kSR)) continue;
        const double e = (best - t) * 1000.0 / kSR;
        ++st.matched; sum += e; st.maxAbsMs = std::max(st.maxAbsMs, std::abs(e)); if (std::abs(e) < 1.0) ++in1;
    }
    st.meanMs = st.matched ? sum / st.matched : 0; st.within1ms = st.matched ? (double) in1 / st.matched : 0;
    return st;
}

void testOnsetsDenseLoop()
{
    std::printf("\n== C17: dense drum loop (95 BPM hip-hop, 16th hats) - new vs legacy ==\n");
    Track tr = makeTrack(95.0, Pattern::HipHop, 8, 0.25, 77);
    const auto mono = analysis::downmixToMono(tr.buffer);
    microFadeEnabledFlag().store(false);
    SpectralFluxDetector det;
    det.detect(mono.data(), (int) mono.size(), kSR);
    microFadeEnabledFlag().store(true);
    const auto nw = matchOnsets(tr.hits, det.getLastAttackSamples());
    const auto old = matchOnsets(tr.hits, legacyOnsets(mono.data(), (int) mono.size(), kSR));
    std::printf("  %d hits | new: matched %d, |err| max %.2f ms, mean %+.2f ms, %.0f%% < 1 ms | legacy: matched %d, max %.2f ms, mean %+.2f ms, %.0f%% < 1 ms\n",
                (int) tr.hits.size(), nw.matched, nw.maxAbsMs, nw.meanMs, nw.within1ms * 100,
                old.matched, old.maxAbsMs, old.meanMs, old.within1ms * 100);
    check(nw.matched >= (int) (0.9 * tr.hits.size()), fmt("dense loop: >= 90%% of hits detected (%d/%d)", nw.matched, (int) tr.hits.size()));
    check(nw.within1ms >= 0.95, fmt("dense loop: >= 95%% of matched onsets within 1 ms (%.0f%%)", nw.within1ms * 100));
}

//==============================================================================
// Key detection
//==============================================================================
void addNote(std::vector<float>& x, int64_t at, int len, double midi, float amp)
{
    const double f0 = 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
    for (int h = 1; h <= 6; ++h)
    {
        const double f = f0 * h;
        if (f > 8000.0) break;
        for (int i = 0; i < len && at + i < (int64_t) x.size(); ++i)
        {
            const double env = std::min(1.0, i / (0.01 * kSR)) * std::exp(-(i / kSR) / 1.2);
            x[(size_t) (at + i)] += (float) (amp / h * env * std::sin(2.0 * kPi * f * i / kSR));
        }
    }
}

void testKey()
{
    std::printf("\n== KeyDetector: synthetic chord progressions ==\n");
    struct Chord { int root; bool minor; };
    struct Case { int tonic; bool minor; std::vector<Chord> prog; const char* desc; };
    // degrees relative to the tonic
    auto majorProg = [](int t) { return std::vector<Chord>{ { t, false }, { t + 5, false }, { t + 7, false }, { t + 9, true }, { t + 5, false }, { t + 7, false }, { t, false } }; };
    auto minorProg = [](int t) { return std::vector<Chord>{ { t, true }, { t + 5, true }, { t + 7, false }, { t, true }, { t + 8, false }, { t + 3, false }, { t + 7, false }, { t, true } }; };
    std::vector<Case> cases = {
        { 0, false, majorProg(0), "I-IV-V-vi-IV-V-I" },  { 7, false, majorProg(7), "" },
        { 3, false, majorProg(3), "" },                  { 11, false, majorProg(11), "" },
        { 9, true, minorProg(9), "i-iv-V-i-VI-III-V-i" }, { 6, true, minorProg(6), "" },
        { 2, true, minorProg(2), "" },                   { 1, true, minorProg(1), "" },
    };
    for (const auto& c : cases)
    {
        const int chordLen = (int) (1.5 * kSR);
        std::vector<float> x((size_t) (chordLen * c.prog.size() + kSR), 0.0f);
        for (size_t i = 0; i < c.prog.size(); ++i)
        {
            const int r = ((c.prog[i].root % 12) + 12) % 12;
            const int64_t at = (int64_t) i * chordLen;
            addNote(x, at, chordLen, 48 + r, 0.20f);                              // bass root (octave 3)
            addNote(x, at, chordLen, 60 + r, 0.12f);                              // triad (octave 4)
            addNote(x, at, chordLen, 60 + r + (c.prog[i].minor ? 3 : 4), 0.12f);
            addNote(x, at, chordLen, 60 + r + 7, 0.12f);
        }
        juce::AudioBuffer<float> buf(1, (int) x.size());
        buf.copyFrom(0, 0, x.data(), (int) x.size());
        auto k = KeyDetector::detect(buf, kSR);
        const auto expectMode = c.minor ? KeyDetector::Mode::Minor : KeyDetector::Mode::Major;
        const juce::String expectName = KeyDetector::pitchClassName(c.tonic) + (c.minor ? " minor" : " major");
        std::printf("  expect %-9s got %-9s (%s) r=%.3f conf=%.2f runner-up %s %s\n",
                    expectName.toRawUTF8(), k.getName().toRawUTF8(), k.getCamelot().toRawUTF8(),
                    k.correlation, k.confidence, KeyDetector::pitchClassName(k.secondTonic).toRawUTF8(),
                    k.secondMode == KeyDetector::Mode::Major ? "major" : "minor");
        check(k.valid && k.tonic == c.tonic && k.mode == expectMode,
              fmt("key %s detected (%s)", expectName.toRawUTF8(), k.getName().toRawUTF8()));
    }
    KeyDetector::Result cm; cm.valid = true; cm.tonic = 0; cm.mode = KeyDetector::Mode::Major;
    KeyDetector::Result am; am.valid = true; am.tonic = 9; am.mode = KeyDetector::Mode::Minor;
    check(cm.getCamelot() == "8B" && am.getCamelot() == "8A", "Camelot codes: C major = 8B, A minor = 8A");

    juce::AudioBuffer<float> silent(1, (int) kSR * 2); silent.clear();
    check(! KeyDetector::detect(silent, kSR).valid, "silence -> invalid key");
}

//==============================================================================
// Real drum stem benchmark
//==============================================================================
void benchStem(const juce::File& f)
{
    std::printf("\n== Real stem: %s ==\n", f.getFileName().toRawUTF8());
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
    if (! r) { std::printf("  cannot read stem - skipped\n"); return; }
    juce::AudioBuffer<float> buf((int) r->numChannels, (int) r->lengthInSamples);
    r->read(&buf, 0, buf.getNumSamples(), 0, true, true);
    const double sr = r->sampleRate;
    std::printf("  %.1f s @ %.0f Hz, %d ch\n", buf.getNumSamples() / sr, sr, buf.getNumChannels());

    BeatThisDetector::clearCache();
    HybridTempoDetector det; HybridTempoDetector::Config cfg; cfg.minBPM = 50; cfg.maxBPM = 220;
    double t0 = nowMs();
    auto res = det.detect(buf, sr, cfg);
    const double hybMs = nowMs() - t0;
    t0 = nowMs();
    auto db = DownbeatDetector::detect(buf, sr, res.bpm);
    const double dbMs = nowMs() - t0;
    std::printf("  NEW hybrid: %.3f BPM (%s, conf %.2f, beats %d, downbeats %d, beatsPerBar %d) %.0f ms\n",
                res.bpm, res.getSourceName().toRawUTF8(), res.confidence, (int) res.beatTimesSec.size(),
                (int) res.downbeatTimesSec.size(), res.beatsPerBar, hybMs);
    std::printf("  NEW downbeat: offset %.4f s (first downbeat %.4f s), source=%s, conf %.3f, %.0f ms (BeatThis cache hit)\n",
                db.beat1Sample / sr, db.firstDownbeatSample / sr,
                db.source == DownbeatDetector::Source::BeatThis ? "BeatThis" : "Comb", db.confidence, dbMs);
    check(std::abs(res.bpm - 85.0) < 0.5, fmt("real stem ~85 BPM (got %.3f)", res.bpm));
    {
        // downbeat spacing histogram (in beats) - meter diagnostics
        int hist[10] = {};
        const auto& B = res.beatTimesSec; const auto& D = res.downbeatTimesSec;
        for (size_t i = 1; i < D.size(); ++i)
        {
            const auto a = std::lower_bound(B.begin(), B.end(), D[i - 1] - 1e-4) - B.begin();
            const auto b = std::lower_bound(B.begin(), B.end(), D[i] - 1e-4) - B.begin();
            hist[std::min<long long>(9, (long long) (b - a))]++;
        }
        std::printf("  downbeat spacing (beats): ");
        for (int i = 1; i < 10; ++i) if (hist[i]) std::printf("%d:%d ", i, hist[i]);
        std::printf("\n  first downbeats:");
        for (size_t i = 0; i < D.size() && i < 12; ++i) std::printf(" %.2f", D[i]);
        std::printf("\n");
    }

    auto comb = DownbeatDetector::detectCombFilter(buf, sr, res.bpm);
    std::printf("  legacy comb beat phase: %.4f s (conf %.3f)\n", comb.beat1Sample / sr, comb.confidence);

    cfg.useBeatThis = false;
    t0 = nowMs();
    auto fb = det.detect(buf, sr, cfg);
    std::printf("  DSP fallback only (FFT autocorr + DWT now O(n log n)): %.3f BPM %.0f ms\n", fb.bpm, nowMs() - t0);

    t0 = nowMs();
    auto key = KeyDetector::detect(buf, sr);
    std::printf("  key on drum stem (not meaningful, timing only): %s %.0f ms\n", key.getName().toRawUTF8(), nowMs() - t0);
}

struct FileOnlyLogger : juce::Logger
{
    juce::FileOutputStream* out = nullptr;
    void logMessage(const juce::String& m) override { if (out) { out->writeText(m + "\n", false, false, nullptr); } }
};

} // namespace

int main(int argc, char** argv)
{
    juce::File exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
    juce::File logFile = exeDir.getChildFile("AnalysisTests.log");
    logFile.deleteFile();
    juce::FileOutputStream logStream(logFile);
    FileOnlyLogger logger; logger.out = logStream.openedOk() ? &logStream : nullptr;
    juce::Logger::setCurrentLogger(&logger);

    // Separation/analysis hardening modes (impl_sep2).
    if (argc >= 5 && std::string(argv[1]) == "--cache-worker")
        return sep2tests::cacheWorkerMain(juce::String::fromUTF8(argv[2]), juce::String::fromUTF8(argv[3]),
                                          juce::String::fromUTF8(argv[4]));
    if (argc >= 2 && std::string(argv[1]) == "--ort-missing-check")
    {
        const int rc = sep2tests::ortMissingCheckMain();
        juce::Logger::setCurrentLogger(nullptr);
        return rc;
    }
    bool sepOnly = false;
    for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == "--sep2-only") sepOnly = true;

    bool quick = false;
    juce::File stem;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--quick") quick = true;
        else if (a == "--stem" && i + 1 < argc) stem = juce::File(juce::String::fromUTF8(argv[++i]));
    }
    if (stem == juce::File())
    {
        const juce::File cache = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                     .getChildFile("Samplar/stem_cache");
        auto found = cache.findChildFiles(juce::File::findFiles, true, "MAQUETA COMPLETA_(Drums)*");
        if (! found.isEmpty()) stem = found.getFirst();
    }

    BeatThisDetector probe;
    const bool bt = probe.isAvailable();
    std::printf("AnalysisTests - BeatThis model %s\n", bt ? "AVAILABLE" : "NOT available (fallback-only checks)");

    if (sepOnly)
    {
        int c = 0;
        const int f = sep2tests::runAll(quick, c);
        std::printf("\n%d checks, %d failures\n", c, f);
        juce::Logger::setCurrentLogger(nullptr);
        return f == 0 ? 0 : 1;
    }

    testFftLayout();
    testTempoEstimator();
    testCombFallbackDownbeat();
    testOnsets();
    testOnsetsDenseLoop();
    testKey();
    if (! quick) testTempoSweep(bt);
    if (stem.existsAsFile()) benchStem(stem);
    else std::printf("\n(no cached MAQUETA drum stem found - real-stem benchmark skipped)\n");

    {
        int c = 0;
        const int f = sep2tests::runAll(quick, c);
        gChecks += c; gFailures += f;
    }

    std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
    juce::Logger::setCurrentLogger(nullptr);
    return gFailures == 0 ? 0 : 1;
}
