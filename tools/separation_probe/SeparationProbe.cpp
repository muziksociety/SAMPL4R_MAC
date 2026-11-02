// SeparationProbe - headless check of FullSongService / OnnxSeparationProvider.
//
//   SeparationProbe [input.wav] [--modes cpu,gpu] [--legacy] [--cancel-after <s>]
//                   [--cache] [--out <dir>] [--quiet]
//
// Default input: <repo>/tests/test_input.wav. For every mode the song is separated with the
// backend forced ("cpu" / "gpu"; gpu silently falls back to the CPU when no suitable GPU),
// the stem cache bypassed and SAMPLAR_SEPARATION_SHIFT=0 (identical random shift, so runs
// are comparable sample for sample). Reports wall time, backend, per-stem RMS, peak working
// set, and max |diff| + SNR of every mode against the first one.
//   --legacy        also run the full htdemucs.onnx on the CPU (old path) as the reference
//   --cancel-after  cancel the (first) run after <s> seconds and report the cancel latency
//   --cache         run through the persistent stem cache twice (miss, then hit)
// Exit code 0 = every run behaved as expected.

#include "services/FullSongService.h"
#include "services/separation/GpuAdapterProbe.h"
#include "utils/DataPaths.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <thread>

#ifndef SAMPLAR_REPO_ROOT
 #define SAMPLAR_REPO_ROOT "."
#endif

namespace {

struct ConsoleLogger : juce::Logger
{
    bool quiet = false;
    double t0 = juce::Time::getMillisecondCounterHiRes();
    void logMessage (const juce::String& m) override
    {
        if (quiet && ! (m.contains ("[ONNX]") || m.contains ("[FullSongService]") || m.contains ("[OnnxSep]")
                        || m.contains ("[HtdemucsIstft]") || m.contains ("[Fft]") || m.contains ("[Probe]")))
            return;
        if (quiet && m.contains ("Separating chunk"))
            return;
        std::printf ("[%8.2f] %s\n", (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, m.toRawUTF8());
        std::fflush (stdout);
    }
};

void probeLog (const juce::String& m) { juce::Logger::writeToLog ("[Probe] " + m); }

struct StemAudio
{
    std::map<juce::String, juce::AudioBuffer<float>> byName;
};

bool loadStems (const samplar::FullSongService::StemFiles& s, StemAudio& out)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    const std::pair<const char*, juce::File> files[] = {
        { "drums", s.drums }, { "bass", s.bass }, { "other", s.other }, { "vocals", s.vocals } };
    for (const auto& [name, f] : files)
    {
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
        if (r == nullptr)
        {
            probeLog ("cannot read stem " + f.getFullPathName());
            return false;
        }
        juce::AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
        r->read (&b, 0, b.getNumSamples(), 0, true, true);
        probeLog (juce::String (name) + ": " + f.getFileName() + " bits=" + juce::String (r->bitsPerSample)
                  + (r->usesFloatingPointData ? " float" : " int") + " len=" + juce::String (b.getNumSamples()));
        out.byName[name] = std::move (b);
    }
    return true;
}

double rmsOf (const juce::AudioBuffer<float>& b)
{
    double ss = 0.0;
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        const float* p = b.getReadPointer (c);
        for (int i = 0; i < b.getNumSamples(); ++i)
            ss += (double) p[i] * p[i];
    }
    return std::sqrt (ss / juce::jmax (1.0, (double) b.getNumChannels() * b.getNumSamples()));
}

void compare (const StemAudio& ref, const StemAudio& test, const juce::String& label)
{
    for (const auto& [name, rb] : ref.byName)
    {
        const auto it = test.byName.find (name);
        if (it == test.byName.end())
            continue;
        const auto& tb = it->second;
        const int n = juce::jmin (rb.getNumSamples(), tb.getNumSamples());
        double sig = 0.0, err = 0.0, maxAbs = 0.0;
        for (int c = 0; c < juce::jmin (rb.getNumChannels(), tb.getNumChannels()); ++c)
        {
            const float* a = rb.getReadPointer (c);
            const float* b = tb.getReadPointer (c);
            for (int i = 0; i < n; ++i)
            {
                const double d = (double) a[i] - (double) b[i];
                sig += (double) a[i] * a[i];
                err += d * d;
                maxAbs = juce::jmax (maxAbs, std::abs (d));
            }
        }
        const double snr = err > 0.0 ? 10.0 * std::log10 (sig / err) : 999.0;
        probeLog ("COMPARE " + label + " " + name + ": maxAbsDiff=" + juce::String (maxAbs, 7)
                  + " SNR=" + juce::String (snr, 1) + " dB (len ref=" + juce::String (rb.getNumSamples())
                  + " test=" + juce::String (tb.getNumSamples()) + ")");
    }
}

} // namespace

int main (int argc, char** argv)
{
    ConsoleLogger logger;
    juce::Logger::setCurrentLogger (&logger);

    const juce::File repo (SAMPLAR_REPO_ROOT);
    juce::File input = repo.getChildFile ("tests/test_input.wav");
    juce::StringArray modes { "cpu", "gpu" };
    bool legacy = false, useCache = false;
    double cancelAfter = -1.0;
    juce::File outRoot = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("samplar_separation_probe");

    for (int i = 1; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a == "--modes" && i + 1 < argc)            modes = juce::StringArray::fromTokens (juce::String (argv[++i]), ",", "");
        else if (a == "--legacy")                      legacy = true;
        else if (a == "--cache")                       useCache = true;
        else if (a == "--quiet")                       logger.quiet = true;
        else if (a == "--cancel-after" && i + 1 < argc) cancelAfter = juce::String (argv[++i]).getDoubleValue();
        else if (a == "--out" && i + 1 < argc)         outRoot = juce::File (juce::String (argv[++i]));
        else                                           input = juce::File (a);
    }

    // Same random shift for every run -> sample-comparable stems.
    _putenv_s ("SAMPLAR_SEPARATION_SHIFT", "0");
    // Resolve models from the repo (models/htdemucs_body.onnx).
    samplar::DataPaths::setAppDirectory (repo);

    probeLog ("input=" + input.getFullPathName() + " modes=" + modes.joinIntoString (",")
              + " legacy=" + juce::String ((int) legacy) + " cancelAfter=" + juce::String (cancelAfter)
              + " cache=" + juce::String ((int) useCache));
    if (! input.existsAsFile())
    {
        probeLog ("FAIL: input not found");
        return 2;
    }
    outRoot.deleteRecursively();
    outRoot.createDirectory();

    samplar::EventBus bus;
    int failures = 0;
    std::vector<std::pair<juce::String, StemAudio>> results;

    struct Run { juce::String label, device; bool legacyModel; };
    std::vector<Run> runs;
    if (legacy) runs.push_back ({ "legacy-full-cpu", "cpu", true });
    for (const auto& m : modes) runs.push_back ({ "body-" + m, m, false });

    juce::File legacyModels;
    for (size_t r = 0; r < runs.size(); ++r)
    {
        const auto& run = runs[r];
        if (run.legacyModel)
        {
            // A models folder WITHOUT the body model -> provider keeps the full export.
            legacyModels = outRoot.getChildFile ("legacy_app");
            legacyModels.getChildFile ("models").createDirectory();
            const auto dst = legacyModels.getChildFile ("models/htdemucs.onnx");
            if (! dst.existsAsFile())
                repo.getChildFile ("models/htdemucs.onnx").copyFileTo (dst);
            samplar::DataPaths::setAppDirectory (legacyModels);
        }
        else
        {
            samplar::DataPaths::setAppDirectory (repo);
        }

        samplar::FullSongService svc (bus);
        samplar::FullSongService::Config cfg;
        cfg.drumsOnly = false;
        cfg.deviceOverride = run.device;
        cfg.useStemCache = false;
        cfg.outputDirOverride = outRoot.getChildFile (run.label);
        std::atomic<bool> cancel { false };
        svc.shouldCancel = [&cancel] { return cancel.load(); };

        const bool doCancel = cancelAfter > 0.0 && r == 0;
        probeLog ("=== RUN " + run.label + (doCancel ? " (cancel after " + juce::String (cancelAfter) + " s)" : juce::String()) + " ===");
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        double cancelAt = 0.0;
        std::thread canceller;
        if (doCancel)
            canceller = std::thread ([&]
            {
                std::this_thread::sleep_for (std::chrono::milliseconds ((int) (cancelAfter * 1000.0)));
                cancelAt = juce::Time::getMillisecondCounterHiRes();
                probeLog ("cancel requested at " + juce::String ((cancelAt - t0) / 1000.0, 2) + " s");
                cancel = true;
            });
        auto res = svc.separateDrums (input, cfg, "probe_");
        const double t1 = juce::Time::getMillisecondCounterHiRes();
        if (canceller.joinable()) canceller.join();

        probeLog ("RESULT " + run.label + ": wall=" + juce::String ((t1 - t0) / 1000.0, 2) + " s backend=" + res.backend
                  + " error='" + res.error + "' cancelled=" + juce::String ((int) res.cancelled)
                  + " peakWorkingSet=" + juce::String ((juce::int64) (samplar::processPeakWorkingSetBytes() >> 20)) + " MB"
                  + " workingSetNow=" + juce::String ((juce::int64) (samplar::processWorkingSetBytes() >> 20)) + " MB");

        if (doCancel)
        {
            const double latency = t1 - cancelAt;
            const auto leftovers = cfg.outputDirOverride.findChildFiles (juce::File::findFiles, false, "*.wav").size();
            probeLog ("CANCEL latency=" + juce::String (latency, 0) + " ms leftoverWavs=" + juce::String (leftovers));
            if (! res.cancelled || latency > 3000.0 || leftovers != 0)
            {
                probeLog ("FAIL: cancel not prompt/clean");
                ++failures;
            }
            continue;
        }

        if (res.error.isNotEmpty() || ! res.stems.isComplete())
        {
            probeLog ("FAIL: " + run.label + " produced no complete stem set");
            ++failures;
            continue;
        }
        if (run.device == "gpu" && ! res.backend.startsWith ("DirectML"))
            probeLog ("NOTE: gpu run fell back to " + res.backend);

        StemAudio audio;
        if (! loadStems (res.stems, audio)) { ++failures; continue; }
        juce::String rmsLine = "RMS " + run.label + ":";
        for (const auto& [name, b] : audio.byName)
            rmsLine << " " << name << "=" << juce::String (rmsOf (b), 5);
        probeLog (rmsLine);
        results.emplace_back (run.label, std::move (audio));
    }

    for (size_t i = 1; i < results.size(); ++i)
        compare (results[0].second, results[i].second, results[i].first + " vs " + results[0].first);
    if (results.size() >= 3)
        compare (results[1].second, results[2].second, results[2].first + " vs " + results[1].first);

    if (useCache)
    {
        samplar::DataPaths::setAppDirectory (repo);
        for (int pass = 0; pass < 2; ++pass)
        {
            samplar::FullSongService svc (bus);
            samplar::FullSongService::Config cfg;
            cfg.drumsOnly = false;
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            auto res = svc.separateDrums (input, cfg, "probe_");
            probeLog ("CACHE pass " + juce::String (pass + 1) + ": wall=" + juce::String ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, 2)
                      + " s fromCache=" + juce::String ((int) res.fromCache) + " backend=" + res.backend
                      + " dir=" + res.outputDir.getFullPathName() + " complete=" + juce::String ((int) res.stems.isComplete()));
            if (res.error.isNotEmpty() || ! res.stems.isComplete() || (pass == 1 && ! res.fromCache))
            {
                probeLog ("FAIL: cache pass " + juce::String (pass + 1));
                ++failures;
            }
        }
    }

    probeLog (failures == 0 ? "ALL PASS" : "FAILURES: " + juce::String (failures));
    juce::Logger::setCurrentLogger (nullptr);
    return failures == 0 ? 0 : 1;
}
