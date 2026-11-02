// SepHardeningTests - checks for the separation / analysis hardening (impl_sep2):
//   * BeatThis: own cancellable pipeline is bit-identical to the vendored BeatThis::process_audio
//   * cancel latency (< 200 ms) for BeatThis / HybridTempo / Downbeat on a 10-min signal,
//     including a detector cancelled while WAITING for another detector's inference
//   * separation: NaN/Inf input sanitised, out-of-phase input finite, > 44.1 kHz input
//     anti-aliased, 180-char song name (MAX_PATH), cache entry validation (tampered stem -> miss),
//     validateCachedStemFile
//   * multi-process stem cache (child processes of this exe, --cache-worker)
//   * --ort-missing-check: run from a copy WITHOUT onnxruntime.dll -> clean "runtime unavailable"

#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include "services/analysis/BeatThisDetector.h"
#include "services/analysis/DownbeatDetector.h"
#include "services/analysis/HybridTempoDetector.h"
#include "services/separation/OrtRuntime.h"
#include "services/separation/OnnxSeparationProvider.h"
#include "services/separation/StemCacheLock.h"
#include "services/FullSongService.h"
#include "app/EventBus.h"
#include "utils/DataPaths.h"

#if WITH_BEAT_THIS
 #include "beat_this_api.h"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include <vector>

namespace samplar { namespace sep2tests {

namespace {

int gChecks = 0, gFailures = 0;

void check(bool ok, const juce::String& what)
{
    ++gChecks;
    if (! ok) ++gFailures;
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
    std::fflush(stdout);
    juce::Logger::writeToLog(juce::String("[SepHardeningTests] ") + (ok ? "PASS " : "FAIL ") + what);
}

double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }

constexpr double kSR = 44100.0;
constexpr double kPi = 3.14159265358979323846;

// 4/4 drum loop (kick 1+3, snare 2+4, 8th hats) at `bpm`, mono.
juce::AudioBuffer<float> makeDrums(double seconds, double bpm, uint32_t seed = 7)
{
    const int n = (int) (seconds * kSR);
    juce::AudioBuffer<float> b(1, n);
    b.clear();
    float* x = b.getWritePointer(0);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> uni(-1.0f, 1.0f);
    const double beat = 60.0 / bpm;
    for (int k = 0; k * beat * 0.5 < seconds; ++k)
    {
        const int at = (int) (k * beat * 0.5 * kSR);
        const int beatIdx = k / 2;
        const bool onBeat = (k % 2) == 0;
        // hat
        for (int i = 0; i < (int) (0.05 * kSR) && at + i < n; ++i)
            x[at + i] += 0.15f * uni(rng) * (float) std::exp(-(i / kSR) / 0.015);
        if (! onBeat) continue;
        if (beatIdx % 2 == 0)   // kick
        {
            double ph = 0.0;
            for (int i = 0; i < (int) (0.3 * kSR) && at + i < n; ++i)
            {
                const double t = i / kSR, f = 50.0 + 100.0 * std::exp(-t / 0.03);
                ph += 2.0 * kPi * f / kSR;
                x[at + i] += 0.8f * (float) (std::exp(-t / 0.1) * std::sin(ph));
            }
        }
        else                    // snare
            for (int i = 0; i < (int) (0.2 * kSR) && at + i < n; ++i)
            {
                const double t = i / kSR;
                x[at + i] += 0.5f * (float) std::exp(-t / 0.05) * (0.8f * uni(rng) + 0.3f * (float) std::sin(2 * kPi * 190 * t));
            }
    }
    return b;
}

bool writeFloatWav(const juce::File& f, const juce::AudioBuffer<float>& b, double sr)
{
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os(f.createOutputStream());
    if (os == nullptr) return false;
    juce::WavAudioFormat wav;
    const auto opts = juce::AudioFormatWriterOptions{}.withSampleRate(sr).withNumChannels(b.getNumChannels())
                          .withBitsPerSample(32).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto w = wav.createWriterFor(os, opts);
    return w != nullptr && w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
}

bool loadWav(const juce::File& f, juce::AudioBuffer<float>& out)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
    if (r == nullptr) return false;
    out.setSize((int) r->numChannels, (int) r->lengthInSamples);
    return r->read(&out, 0, (int) r->lengthInSamples, 0, true, true);
}

bool allFinite(const juce::AudioBuffer<float>& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite(b.getSample(c, i))) return false;
    return true;
}

bool stemsFinite(const FullSongService::StemFiles& s, juce::String& why)
{
    for (const auto& f : { s.drums, s.bass, s.other, s.vocals })
    {
        juce::AudioBuffer<float> b;
        if (! loadWav(f, b)) { why = "cannot read " + f.getFileName(); return false; }
        if (b.getNumSamples() == 0 || ! allFinite(b)) { why = "non-finite / empty " + f.getFileName(); return false; }
    }
    return true;
}

double goertzelPower(const juce::AudioBuffer<float>& b, double sr, double hz)
{
    const double w = 2.0 * kPi * hz / sr, coeff = 2.0 * std::cos(w);
    double total = 0.0;
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        double s1 = 0.0, s2 = 0.0;
        const float* x = b.getReadPointer(c);
        for (int i = 0; i < b.getNumSamples(); ++i) { const double s0 = x[i] + coeff * s1 - s2; s2 = s1; s1 = s0; }
        total += s1 * s1 + s2 * s2 - coeff * s1 * s2;
    }
    return total;
}

juce::File repoRoot() { return juce::File(SAMPLAR_REPO_ROOT); }

void setEnv(const char* k, const juce::String& v) { _putenv_s(k, v.toRawUTF8()); }

//==============================================================================
// BeatThis parity + cancel
//==============================================================================
#if WITH_BEAT_THIS
void testBeatThisParity()
{
    std::printf("\n== BeatThis: cancellable pipeline vs vendored BeatThis::process_audio ==\n");
    const juce::File exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
    const juce::File model = exeDir.getChildFile("third_party/beat_this/models/beat_this.onnx");
    if (! ensureOrtApi() || ! model.existsAsFile()) { check(false, "ORT + model available for the parity test"); return; }

    const auto buf = makeDrums(75.0, 120.0);   // 3750 mel frames: 4 mel slices, 3 ONNX chunks
    std::vector<float> mono(buf.getReadPointer(0), buf.getReadPointer(0) + buf.getNumSamples());

    BeatThis::BeatThis vendored(model.getFullPathName().toStdString());
    const auto ref = vendored.process_audio(mono, (int) kSR, 1);

    BeatThisDetector::clearCache();
    BeatThisDetector det;
    const auto got = det.detect(buf, kSR);

    bool beatsEqual = ! got.halvedFromDoubleTime && got.beatTimesSec.size() == ref.beats.size();
    for (size_t i = 0; beatsEqual && i < ref.beats.size(); ++i)
        beatsEqual = got.beatTimesSec[i] == (double) ref.beats[i];
    bool dbEqual = got.downbeatTimesSec.size() == ref.downbeats.size();
    for (size_t i = 0; dbEqual && i < ref.downbeats.size(); ++i)
        dbEqual = got.downbeatTimesSec[i] == (double) ref.downbeats[i];
    check(beatsEqual, "beats bit-identical to vendored (" + juce::String((int) ref.beats.size()) + " beats)");
    check(dbEqual, "downbeats bit-identical to vendored (" + juce::String((int) ref.downbeats.size()) + ")");
    check(got.beatCounts == ref.beat_counts, "beat counts identical");
    check(got.valid && std::abs(got.bpm - 120.0) < 0.1, "tempo " + juce::String(got.bpm, 3) + " ~ 120");
}

struct CancelRun { double latencyMs = 1e9; bool cancelled = false; bool valid = true; };

template <typename Fn>
CancelRun runAndCancel(Fn&& detectFn, std::atomic<bool>& flag, bool flagMeansCancel, int cancelAfterMs)
{
    CancelRun r;
    flag = ! flagMeansCancel;
    std::atomic<double> endMs { 0.0 };
    std::thread t([&] { detectFn(r); endMs = nowMs(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(cancelAfterMs));
    const double setMs = nowMs();
    flag = flagMeansCancel;
    t.join();
    r.latencyMs = endMs.load() - setMs;
    return r;
}

// One latency sample can exceed the budget when the whole machine is saturated (the
// cancelled thread is simply not scheduled; seen once at 221 ms under a concurrent build).
// A first sample within budget passes at once; otherwise two more are taken and the MEDIAN
// of the three is judged, so a cancel path that is really slow still fails. Every run must
// report cancelled / invalid.
constexpr double kCancelBudgetMs = 200.0;

template <typename RunOnce>
CancelRun judgedCancel(RunOnce&& runOnce, juce::String& samples)
{
    std::vector<CancelRun> runs { runOnce() };
    if (runs.front().latencyMs >= kCancelBudgetMs)
    {
        runs.push_back(runOnce());
        runs.push_back(runOnce());
    }
    bool allCancelled = true, anyValid = false;
    samples.clear();
    for (const auto& r : runs)
    {
        allCancelled = allCancelled && r.cancelled;
        anyValid = anyValid || r.valid;
        samples << (samples.isEmpty() ? "" : "/") << juce::String(r.latencyMs, 1);
    }
    std::sort(runs.begin(), runs.end(), [](const CancelRun& a, const CancelRun& b) { return a.latencyMs < b.latencyMs; });
    CancelRun median = runs[runs.size() / 2];
    median.cancelled = allCancelled;
    median.valid = anyValid;
    if (runs.size() > 1)
        samples = "median of " + juce::String((int) runs.size()) + " (" + samples + ")";
    return median;
}

void testCancel(bool quick)
{
    std::printf("\n== Cancel latency on a 10-min signal (budget 200 ms; median of 3 if the first sample is over) ==\n");
    const auto longBuf = makeDrums(600.0, 128.0);
    std::atomic<bool> flag { false };

    // Review S-M3: cancel while the ONNX session is being CREATED (~2 s, no detector alive, so
    // the shared engine does not exist yet). The detector returns at once; the abandoned
    // creation finishes on its own pinned thread and the next detector waits for it.
    {
        juce::String samples;
        const auto r = judgedCancel([&]
        {
            BeatThisDetector::clearCache();
            return runAndCancel([&](CancelRun& out)
            {
                BeatThisDetector det;
                const auto res = det.detect(longBuf, kSR, AnalysisCancel(&flag));
                out.cancelled = res.cancelled; out.valid = res.valid;
            }, flag, true, 150);
        }, samples);
        check(r.cancelled && ! r.valid && r.latencyMs < kCancelBudgetMs,
              "S-M3: BeatThis cancel during ONNX session creation -> cancelled=" + juce::String((int) r.cancelled)
              + " latency " + juce::String(r.latencyMs, 1) + " ms " + samples);
    }

    BeatThisDetector warm;
    if (! warm.isAvailable()) { check(false, "BeatThis available for the cancel test (after an abandoned creation)"); return; }
    const int delays[] = { 60, 400, 1500, 5000 };   // downmix/resample, mel, ONNX, ONNX
    for (int d : delays)
    {
        if (quick && d == 5000) continue;
        juce::String samples;
        const auto r = judgedCancel([&]
        {
            BeatThisDetector::clearCache();
            return runAndCancel([&](CancelRun& out)
            {
                BeatThisDetector det;
                const auto res = det.detect(longBuf, kSR, AnalysisCancel(&flag));
                out.cancelled = res.cancelled; out.valid = res.valid;
            }, flag, true, d);
        }, samples);
        check(r.cancelled && ! r.valid && r.latencyMs < kCancelBudgetMs,
              "BeatThis cancel at " + juce::String(d) + " ms -> cancelled=" + juce::String((int) r.cancelled)
              + " latency " + juce::String(r.latencyMs, 1) + " ms " + samples);
    }

    // HybridTempoDetector with an "alive" flag (SModeService::alive_ polarity).
    {
        juce::String samples;
        const auto r = judgedCancel([&]
        {
            BeatThisDetector::clearCache();
            return runAndCancel([&](CancelRun& out)
            {
                HybridTempoDetector h;
                const auto res = h.detect(longBuf, kSR, HybridTempoDetector::Config(), AnalysisCancel::whenFalse(&flag));
                out.cancelled = res.cancelled; out.valid = res.bpm > 0.0f;
            }, flag, false, 2000);
        }, samples);
        check(r.cancelled && ! r.valid && r.latencyMs < kCancelBudgetMs,
              "HybridTempo cancel (alive flag) latency " + juce::String(r.latencyMs, 1) + " ms, no DSP fallback ran " + samples);
    }
    // DownbeatDetector
    {
        juce::String samples;
        const auto r = judgedCancel([&]
        {
            BeatThisDetector::clearCache();
            return runAndCancel([&](CancelRun& out)
            {
                const auto res = DownbeatDetector::detect(longBuf, kSR, 128.0, AnalysisCancel(&flag));
                out.cancelled = res.cancelled; out.valid = res.valid;
            }, flag, true, 2000);
        }, samples);
        check(r.cancelled && ! r.valid && r.latencyMs < kCancelBudgetMs,
              "Downbeat cancel latency " + juce::String(r.latencyMs, 1) + " ms, no comb fallback ran " + samples);
    }
    // A detector WAITING for the shared session (another inference running) cancels promptly.
    {
        const auto otherBuf = makeDrums(600.0, 100.0, 99);
        juce::String samples;
        const auto r = judgedCancel([&]
        {
            BeatThisDetector::clearCache();
            std::atomic<bool> flagA { false };
            std::thread a([&] { BeatThisDetector d; d.detect(longBuf, kSR, AnalysisCancel(&flagA)); });
            std::this_thread::sleep_for(std::chrono::milliseconds(800));
            auto run = runAndCancel([&](CancelRun& out)
            {
                BeatThisDetector d;
                const auto res = d.detect(otherBuf, kSR, AnalysisCancel(&flag));
                out.cancelled = res.cancelled; out.valid = false;   // valid is not part of this check
            }, flag, true, 600);
            flagA = true;
            a.join();
            return run;
        }, samples);
        check(r.cancelled && r.latencyMs < kCancelBudgetMs,
              "cancel while waiting for another inference: latency " + juce::String(r.latencyMs, 1) + " ms " + samples);
    }
    // Default token: still runs to completion (old call sites unchanged).
    {
        BeatThisDetector::clearCache();
        const auto shortBuf = makeDrums(30.0, 128.0);
        HybridTempoDetector h;
        const auto res = h.detect(shortBuf, kSR);
        check(! res.cancelled && std::abs(res.bpm - 128.0f) < 0.5f,
              "no-cancel call site still detects " + juce::String(res.bpm, 2) + " BPM");
    }
}
#endif

//==============================================================================
// Separation
//==============================================================================
FullSongService::Result separate(const juce::File& input, bool useCache, const juce::File& outDir = {})
{
    EventBus bus;
    FullSongService svc(bus);
    FullSongService::Config cfg;
    cfg.drumsOnly = false;
    cfg.deviceOverride = "cpu";
    cfg.useStemCache = useCache;
    cfg.outputDirOverride = outDir;
    return svc.separateDrums(input, cfg, "sep2_");
}

juce::AudioBuffer<float> stereoMusic(double seconds, double sr, uint32_t seed)
{
    const int n = (int) (seconds * sr);
    juce::AudioBuffer<float> b(2, n);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> uni(-1.0f, 1.0f);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const float tone = 0.25f * (float) std::sin(2 * kPi * 220.0 * t) + 0.15f * (float) std::sin(2 * kPi * 330.0 * t);
        const float hit = (std::fmod(t, 0.5) < 0.05) ? 0.4f * uni(rng) * (float) std::exp(-std::fmod(t, 0.5) / 0.02) : 0.0f;
        b.setSample(0, i, tone + hit);
        b.setSample(1, i, 0.9f * tone + hit);
    }
    return b;
}

void testSeparation()
{
    std::printf("\n== Separation hardening (CPU, private cache dir) ==\n");
    if (! ensureOrtApi()) { check(false, "ORT available for separation tests"); return; }
    const juce::File tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("samplar_sep2_tests");
    tmp.deleteRecursively();
    tmp.createDirectory();
    const juce::File cacheDir = tmp.getChildFile("cache");
    setEnv("SAMPLAR_STEM_CACHE_DIR", cacheDir.getFullPathName());
    DataPaths::setAppDirectory(repoRoot());

    // 1) NaN/Inf input + 180-char name (MAX_PATH) + cache commit
    const juce::String longName = "Very long song title " + juce::String::repeatedString("x", 150) + " (Extended Club Mix)";
    const juce::File nanSong = tmp.getChildFile(longName + ".wav");
    {
        auto b = stereoMusic(3.0, kSR, 1);
        for (int i = 1000; i < 1500; ++i) b.setSample(i % 2, i * 37 % b.getNumSamples(), std::numeric_limits<float>::quiet_NaN());
        b.setSample(0, 777, std::numeric_limits<float>::infinity());
        b.setSample(1, 778, -std::numeric_limits<float>::infinity());
        check(writeFloatWav(nanSong, b, kSR), "wrote NaN/Inf test song (" + juce::String(longName.length()) + "-char name)");
    }
    auto r1 = separate(nanSong, true);
    juce::String why;
    check(r1.error.isEmpty() && r1.stems.isComplete(), "NaN song separated: error='" + r1.error + "'");
    check(r1.error.isEmpty() && stemsFinite(r1.stems, why), "NaN song stems are finite " + why);
    check(r1.stems.drums.getFileName().length() <= 72 && r1.stems.drums.getFullPathName().length() < 240,
          "stem path short: " + juce::String(r1.stems.drums.getFullPathName().length()) + " chars");
    check(r1.outputDir.getParentDirectory() == cacheDir, "committed into the private cache");

    auto r2 = separate(nanSong, true);
    check(r2.fromCache && r2.stems.isComplete(), "second call is a validated cache HIT");
    check(FullSongService::validateCachedStemFile(r2.stems.bass), "validateCachedStemFile(valid entry) == true");

    // 1b) review N-23: stems inside a temporary ".partial-*" / ".evict-*" dir are never valid
    //     (any process prunes them); a ".copy-*" dir (commit-failure fallback) with its
    //     entry.json is a stable, valid entry. A lock wait cancelled by unload -> invalid (N-9).
    {
        const auto entry = r2.outputDir;
        const auto partial = cacheDir.getChildFile(entry.getFileName() + ".partial-123");
        const auto evicted = cacheDir.getChildFile(entry.getFileName() + ".evict-123");
        const auto copy    = cacheDir.getChildFile(entry.getFileName() + ".copy-123");
        check(entry.copyDirectoryTo(partial) && entry.copyDirectoryTo(evicted) && entry.copyDirectoryTo(copy),
              "made .partial / .evict / .copy test dirs");
        check(! FullSongService::validateCachedStemFile(partial.getChildFile(r2.stems.bass.getFileName())),
              "N-23: validateCachedStemFile(.partial-*) == false");
        check(! FullSongService::validateCachedStemFile(evicted.getChildFile(r2.stems.bass.getFileName())),
              "N-23: validateCachedStemFile(.evict-*) == false");
        check(FullSongService::validateCachedStemFile(copy.getChildFile(r2.stems.bass.getFileName())),
              "N-23: validateCachedStemFile(.copy-* with entry.json) == true");

        // Another holder keeps the cache lock: a cancelled wait returns false fast (never the
        // unvalidated file), instead of blocking up to 15 s.
        CrossProcessFileLock holder(cacheDir.getChildFile(".cache.lock").getFullPathName().toWideCharPointer());
        check(holder.tryLock() == CrossProcessFileLock::Try::Acquired, "test holds the cache lock");
        std::atomic<bool> unload { false };
        std::thread canceller([&] { std::this_thread::sleep_for(std::chrono::milliseconds(150)); unload = true; });
        const double t0 = nowMs();
        const bool v = FullSongService::validateCachedStemFile(r2.stems.bass, [&] { return unload.load(); });
        const double ms = nowMs() - t0;
        canceller.join();
        holder.unlock();
        check(! v && ms < 1000.0, "N-9: validate blocked on the cache lock is cancelled in " + juce::String(ms, 0)
              + " ms and returns false");
        partial.deleteRecursively();
        evicted.deleteRecursively();
        copy.deleteRecursively();
    }

    // 1c) review S-M3: cancelling while the htdemucs session is being created (3-5 s) returns
    //     at once; nothing is disabled; the next separation (step 2) waits for the abandoned
    //     load and then works normally.
    {
        juce::File model = DataPaths::getModelsDirectory().getChildFile("htdemucs_body.onnx");
        if (! model.existsAsFile()) model = DataPaths::getModelsDirectory().getChildFile("htdemucs.onnx");
        if (! model.existsAsFile())
            check(false, "S-M3: htdemucs model found for the creation-cancel test");
        else
        {
            std::atomic<bool> stop { false };
            std::atomic<double> setMs { 0.0 };
            std::thread canceller([&] { std::this_thread::sleep_for(std::chrono::milliseconds(300)); setMs = nowMs(); stop = true; });
            OnnxSeparationProvider p(model, false, [&] { return stop.load(); });
            const double endMs = nowMs();
            canceller.join();
            const double latency = endMs - setMs.load();
            check(! p.isReady() && p.wasCancelled() && latency < 300.0,
                  "S-M3: htdemucs session creation cancelled: ready=" + juce::String((int) p.isReady())
                  + " cancelled=" + juce::String((int) p.wasCancelled()) + " latency " + juce::String(latency, 1) + " ms");
        }
    }

    // 2) tampered stem -> entry invalid -> re-separated and replaced
    {
        juce::FileOutputStream os(r2.stems.vocals);
        if (os.openedOk()) { os.setPosition(0); os.truncate(); os.write("RIFF", 4); }
    }
    check(! FullSongService::validateCachedStemFile(r2.stems.vocals), "validateCachedStemFile(truncated stem) == false");
    auto r3 = separate(nanSong, true);
    check(! r3.fromCache && r3.error.isEmpty() && r3.stems.isComplete(), "truncated stem -> MISS, re-separated");
    auto r4 = separate(nanSong, true);
    check(r4.fromCache, "replaced entry is a HIT again");

    // 3) source changed -> stored stems no longer valid for it
    nanSong.setLastModificationTime(juce::Time::getCurrentTime() + juce::RelativeTime::seconds(5));
    check(! FullSongService::validateCachedStemFile(r4.stems.drums), "validateCachedStemFile after the source changed == false");

    // 4) out-of-phase input (L = -R): finite stems, no Inf from a 1e8 gain
    {
        const juce::File f = tmp.getChildFile("out_of_phase.wav");
        auto b = stereoMusic(3.0, kSR, 2);
        b.copyFrom(1, 0, b, 0, 0, b.getNumSamples());
        b.applyGain(1, 0, b.getNumSamples(), -1.0f);
        writeFloatWav(f, b, kSR);
        auto r = separate(f, false, tmp.getChildFile("oop_out"));
        check(r.error.isEmpty() && stemsFinite(r.stems, why), "out-of-phase stems finite: error='" + r.error + "' " + why);
    }

    // 5) 48 kHz: a 23 kHz tone must not alias to 21.1 kHz (anti-alias low-pass, S-M6)
    {
        const double sr48 = 48000.0;
        const int n = (int) (3.0 * sr48);
        juce::AudioBuffer<float> b(2, n);
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr48;
            const float v = 0.3f * (float) std::sin(2 * kPi * 1000.0 * t) + 0.3f * (float) std::sin(2 * kPi * 23000.0 * t);
            b.setSample(0, i, v); b.setSample(1, i, v);
        }
        const juce::File f = tmp.getChildFile("tone48k.wav");
        writeFloatWav(f, b, sr48);
        auto r = separate(f, false, tmp.getChildFile("aa_out"));
        juce::AudioBuffer<float> sum;
        bool ok = r.error.isEmpty();
        for (const auto& s : { r.stems.drums, r.stems.bass, r.stems.other, r.stems.vocals })
        {
            juce::AudioBuffer<float> x;
            ok = ok && loadWav(s, x);
            if (! ok) break;
            if (sum.getNumSamples() == 0) { sum.makeCopyOf(x); continue; }
            for (int c = 0; c < 2; ++c) sum.addFrom(c, 0, x, c, 0, juce::jmin(sum.getNumSamples(), x.getNumSamples()));
        }
        const double p1k = ok ? goertzelPower(sum, 44100.0, 1000.0) : 0.0;
        const double pAlias = ok ? goertzelPower(sum, 44100.0, 44100.0 - 23000.0) : 1.0;
        const double db = 10.0 * std::log10((pAlias + 1e-30) / (p1k + 1e-30));
        check(ok && db < -40.0, "48 kHz input: 21.1 kHz alias vs 1 kHz = " + juce::String(db, 1) + " dB (< -40)");
    }
}

//==============================================================================
// Multi-process cache
//==============================================================================
juce::String runChildren(const juce::StringArray& songs, const juce::File& cacheDir, juce::StringArray& outputs)
{
    const juce::File exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    juce::OwnedArray<juce::ChildProcess> kids;
    for (int i = 0; i < songs.size(); ++i)
    {
        auto* cp = kids.add(new juce::ChildProcess());
        juce::StringArray args { exe.getFullPathName(), "--cache-worker", songs[i], cacheDir.getFullPathName(), juce::String(i) };
        if (! cp->start(args, juce::ChildProcess::wantStdOut)) return "cannot start worker " + juce::String(i);
    }
    juce::String failures;
    for (int i = 0; i < kids.size(); ++i)
    {
        outputs.add(kids[i]->readAllProcessOutput().trim());
        kids[i]->waitForProcessToFinish(600000);
        if (kids[i]->getExitCode() != 0) failures << "worker " << i << " exit " << (int) kids[i]->getExitCode() << "; ";
    }
    return failures;
}

void testMultiProcess(const juce::File& song)
{
    std::printf("\n== Stem cache: concurrent processes ==\n");
    const juce::File tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("samplar_sep2_mp");
    tmp.deleteRecursively();
    tmp.createDirectory();

    // A) 3 processes, same song, empty cache: one separation, the others wait and HIT; no worker
    //    loses its stems; exactly one entry, no partial / evict / lease leftovers.
    {
        const juce::File cache = tmp.getChildFile("cacheA");
        juce::StringArray out;
        const auto fails = runChildren({ song.getFullPathName(), song.getFullPathName(), song.getFullPathName() }, cache, out);
        for (auto& o : out) std::printf("    %s\n", o.toRawUTF8());
        check(fails.isEmpty(), "same song x3 processes: all workers OK " + fails);
        const auto dirs = cache.findChildFiles(juce::File::findDirectories, false);
        const auto leftovers = cache.findChildFiles(juce::File::findFilesAndDirectories, false, "*.partial-*").size()
                             + cache.findChildFiles(juce::File::findFilesAndDirectories, false, "*.evict-*").size();
        check(dirs.size() == 1 && leftovers == 0, "one cache entry, no leftovers (dirs=" + juce::String(dirs.size())
              + " leftovers=" + juce::String(leftovers) + ")");
        int hits = 0;
        for (auto& o : out) hits += o.contains("fromCache=1") ? 1 : 0;
        check(hits == 2, "1 separation + 2 cache hits (hits=" + juce::String(hits) + ")");
    }

    // B) 1 MB cap: two processes on two songs + an old 2 MB entry. The old entry is evicted;
    //    neither fresh entry is evicted while (recently) in use.
    {
        const juce::File cache = tmp.getChildFile("cacheB");
        const juce::File old = cache.getChildFile("00000000deadbeef");
        old.createDirectory();
        juce::MemoryBlock junk(2 * 1024 * 1024, true);
        old.getChildFile("x_Drums.wav").replaceWithData(junk.getData(), junk.getSize());
        old.getChildFile("entry.json").replaceWithText("{}");
        old.getChildFile("entry.json").setLastModificationTime(juce::Time::getCurrentTime() - juce::RelativeTime::hours(2));

        const juce::File song2 = tmp.getChildFile("second_song.wav");
        song.copyFileTo(song2);
        song2.setLastModificationTime(juce::Time::getCurrentTime() - juce::RelativeTime::minutes(1));
        setEnv("SAMPLAR_STEM_CACHE_MAX_MB", "1");
        juce::StringArray out;
        const auto fails = runChildren({ song.getFullPathName(), song2.getFullPathName() }, cache, out);
        setEnv("SAMPLAR_STEM_CACHE_MAX_MB", "");
        for (auto& o : out) std::printf("    %s\n", o.toRawUTF8());
        check(fails.isEmpty(), "two songs, 1 MB cap: both workers kept their stems " + fails);
        check(! old.exists(), "old idle entry evicted by the cap");
        const auto dirs = cache.findChildFiles(juce::File::findDirectories, false);
        check(dirs.size() == 2, "both fresh entries kept despite the cap (recently used): " + juce::String(dirs.size()));
    }
}

} // namespace

//==============================================================================
// Entry points (called from AnalysisTests main)
//==============================================================================
int cacheWorkerMain(const juce::String& song, const juce::String& cacheDir, const juce::String& id)
{
    setEnv("SAMPLAR_STEM_CACHE_DIR", cacheDir);
    DataPaths::setAppDirectory(repoRoot());
    const juce::File logFile = juce::File(cacheDir).getSiblingFile("worker_" + id + ".log");
    std::unique_ptr<juce::FileLogger> log(new juce::FileLogger(logFile, "worker " + id));
    juce::Logger::setCurrentLogger(log.get());

    const juce::File input(song);
    auto r = separate(input, true);
    juce::String why;
    bool ok = r.error.isEmpty() && r.stems.isComplete() && stemsFinite(r.stems, why);
    juce::int64 sizes[4] = {};
    const juce::File files[] = { r.stems.drums, r.stems.bass, r.stems.other, r.stems.vocals };
    for (int i = 0; i < 4; ++i) sizes[i] = files[i].getSize();
    // Hold the "loaded" stems while the other workers separate / commit / prune.
    juce::Thread::sleep(4000);
    for (int i = 0; ok && i < 4; ++i)
        ok = files[i].existsAsFile() && files[i].getSize() == sizes[i];
    ok = ok && stemsFinite(r.stems, why);
    std::printf("WORKER %s %s fromCache=%d backend=%s dir=%s %s\n", id.toRawUTF8(), ok ? "OK" : "FAIL",
                (int) r.fromCache, r.backend.toRawUTF8(), r.outputDir.getFileName().toRawUTF8(),
                (r.error + " " + why).trim().toRawUTF8());
    juce::Logger::setCurrentLogger(nullptr);
    return ok ? 0 : 1;
}

int ortMissingCheckMain()
{
    std::printf("== Missing onnxruntime.dll check ==\n");
    DataPaths::setAppDirectory(repoRoot());
    const juce::File tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("samplar_sep2_noort");
    tmp.deleteRecursively();
    tmp.createDirectory();
    setEnv("SAMPLAR_STEM_CACHE_DIR", tmp.getChildFile("cache").getFullPathName());

    const bool avail = ensureOrtApi();
    check(! avail, "ensureOrtApi() == false (" + ortUnavailableReason() + ")");

    BeatThisDetector bt;
    const double t0 = nowMs();
    const auto shortBuf = makeDrums(20.0, 120.0);
    const auto r = bt.detect(shortBuf, kSR);
    check(! bt.isAvailable() && ! r.valid && nowMs() - t0 < 1000.0, "BeatThis reports unavailable, invalid result, no crash");

    HybridTempoDetector h;
    const auto hr = h.detect(shortBuf, kSR);
    check(hr.bpm > 0.0f && hr.source != HybridTempoDetector::Result::Source::BeatThis,
          "HybridTempo falls back to DSP: " + juce::String(hr.bpm, 2) + " BPM (" + hr.getSourceName() + ")");

    const juce::File song = tmp.getChildFile("song.wav");
    writeFloatWav(song, stereoMusic(2.0, kSR, 3), kSR);
    auto sr = separate(song, true);
    check(sr.runtimeUnavailable && sr.error.containsIgnoreCase("reinstall"), "separation: runtimeUnavailable, error='" + sr.error + "'");
    check(! FullSongService::isSeparationRuntimeAvailable(), "FullSongService::isSeparationRuntimeAvailable() == false");
    const auto backend = OnnxSeparationProvider::describeNextBackend(true);
    check(backend.startsWith("Unavailable"), "describeNextBackend -> '" + backend + "'");

    std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}

int runAll(bool quick, int& checks)
{
#if WITH_BEAT_THIS
    testBeatThisParity();
    testCancel(quick);
#endif
    testSeparation();
    const juce::File song = repoRoot().getChildFile("tests/test_input.wav");
    if (song.existsAsFile()) testMultiProcess(song);
    else check(false, "tests/test_input.wav present for the multi-process test");
    checks = gChecks;
    return gFailures;
}

}} // namespace samplar::sep2tests
