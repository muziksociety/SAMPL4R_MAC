#include "BeatThisDetector.h"

#if WITH_BEAT_THIS

#include "beat_this_api.h"
#include "../../utils/DataPaths.h"

#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <mutex>

namespace samplar {

//==============================================================================
// Impl
//==============================================================================
struct BeatThisDetector::Impl
{
    std::unique_ptr<BeatThis::BeatThis> engine;
    std::once_flag                      initFlag;
    bool                                initSucceeded = false;
    juce::String                        initErrorMsg;

    void ensureInitialised()
    {
        std::call_once(initFlag, [this]
        {
            try
            {
                juce::File modelFile = resolveModelPath();
                if (! modelFile.existsAsFile())
                {
                    initErrorMsg = "Model file not found: " + modelFile.getFullPathName();
                    juce::Logger::writeToLog("[BeatThis] " + initErrorMsg);
                    return;
                }

                juce::Logger::writeToLog("[BeatThis] Loading ONNX model: " +
                                         modelFile.getFullPathName());

                const std::string path = modelFile.getFullPathName().toStdString();
                engine = std::make_unique<BeatThis::BeatThis>(path);
                initSucceeded = true;

                juce::Logger::writeToLog("[BeatThis] ONNX session initialised successfully");
            }
            catch (const std::exception& e)
            {
                initErrorMsg = juce::String("ONNX init failed: ") + e.what();
                juce::Logger::writeToLog("[BeatThis] " + initErrorMsg);
            }
            catch (...)
            {
                initErrorMsg = "Unknown ONNX initialisation error";
                juce::Logger::writeToLog("[BeatThis] " + initErrorMsg);
            }
        });
    }

    static juce::File resolveModelPath()
    {
        const juce::File exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                                      .getParentDirectory();

        // Priority 1 — matches the CMake POST_BUILD copy layout:
        //   <exeDir>/third_party/beat_this/models/beat_this.onnx
        juce::File candidate = exeDir.getChildFile("third_party/beat_this/models/beat_this.onnx");
        if (candidate.existsAsFile()) return candidate;

        // Priority 2 — flat next to exe (alternate install layout)
        candidate = exeDir.getChildFile("beat_this.onnx");
        if (candidate.existsAsFile()) return candidate;

        // Priority 3 — DataPaths shared models dir (installer puts shared
        // model files there for plugin / app to share)
        candidate = DataPaths::getModelsDirectory().getChildFile("beat_this.onnx");
        if (candidate.existsAsFile()) return candidate;

        // Priority 4 — DataPaths app directory (legacy / installer fallback)
        candidate = DataPaths::getAppDirectory().getChildFile("third_party/beat_this/models/beat_this.onnx");
        if (candidate.existsAsFile()) return candidate;

        // Priority 5 — source-tree dev path (when launched from build dir)
        candidate = juce::File::getCurrentWorkingDirectory()
                        .getChildFile("third_party/beat_this/models/beat_this.onnx");
        return candidate;
    }
};

//==============================================================================
// Public API
//==============================================================================
BeatThisDetector::BeatThisDetector()
    : impl_(std::make_unique<Impl>())
{}

BeatThisDetector::~BeatThisDetector() = default;

BeatThisDetector::Result BeatThisDetector::detect(
    const juce::AudioBuffer<float>& drumStem,
    double sampleRate)
{
    Result out;

    if (drumStem.getNumSamples() <= 0 || drumStem.getNumChannels() <= 0 || sampleRate <= 0.0)
    {
        juce::Logger::writeToLog("[BeatThis] Invalid input buffer/sampleRate");
        return out;
    }

    impl_->ensureInitialised();
    if (! impl_->initSucceeded || impl_->engine == nullptr)
    {
        juce::Logger::writeToLog("[BeatThis] Engine not initialised — returning invalid result");
        return out;
    }

    try
    {
        const int numCh = drumStem.getNumChannels();
        const int numSm = drumStem.getNumSamples();

        // Downmix to mono (api also handles channels arg, but we already have float buffer)
        std::vector<float> mono(static_cast<size_t>(numSm), 0.0f);
        const float scale = 1.0f / static_cast<float>(juce::jmax(1, numCh));
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float* rd = drumStem.getReadPointer(ch);
            for (int i = 0; i < numSm; ++i)
                mono[static_cast<size_t>(i)] += rd[i] * scale;
        }

        juce::Logger::writeToLog("[BeatThis] Running inference on " +
            juce::String(numSm) + " samples @ " +
            juce::String(sampleRate, 0) + " Hz (mono)");

        auto bt = impl_->engine->process_audio(mono,
                                               static_cast<int>(sampleRate),
                                               /*channels=*/1);

        // Convert beat times to double
        out.beatTimesSec.reserve(bt.beats.size());
        for (float t : bt.beats) out.beatTimesSec.push_back(static_cast<double>(t));

        if (out.beatTimesSec.size() < 3)
        {
            juce::Logger::writeToLog("[BeatThis] Too few beats (" +
                juce::String((int) out.beatTimesSec.size()) + ") — invalid");
            return out;
        }

        // Step 1: median IOI as a rough/robust anchor.
        std::vector<double> iois;
        iois.reserve(out.beatTimesSec.size() - 1);
        for (size_t i = 1; i < out.beatTimesSec.size(); ++i)
            iois.push_back(out.beatTimesSec[i] - out.beatTimesSec[i - 1]);

        std::vector<double> sortedIois = iois;
        std::sort(sortedIois.begin(), sortedIois.end());
        const double medianIOI = sortedIois[sortedIois.size() / 2];
        if (medianIOI <= 1e-6)
        {
            juce::Logger::writeToLog("[BeatThis] Degenerate median IOI");
            return out;
        }
        const double medianBpm = 60.0 / medianIOI;

        // Step 2: build inlier set (beats whose neighbouring IOIs are within
        // 8% of the median — drops outliers from tempo changes / drum fills).
        // Each beat index k maps to its time t_k. Linear regression on
        // (k, t_k) for inliers gives the precise constant tempo:
        //   t_k ≈ a + b·k   →   IOI = b   →   BPM = 60 / b.
        // Sub-decimal precision because we use ALL ~250 beats jointly.
        std::vector<double> xs, ys;
        xs.reserve(out.beatTimesSec.size());
        ys.reserve(out.beatTimesSec.size());

        // Mark each beat as inlier if both adjacent IOIs (or the only one)
        // sit within 8% of median IOI.
        auto isInlier = [&](size_t i) -> bool
        {
            const double lo = medianIOI * 0.92;
            const double hi = medianIOI * 1.08;
            const bool leftOk  = (i == 0)
                ? true
                : (iois[i - 1] >= lo && iois[i - 1] <= hi);
            const bool rightOk = (i + 1 >= out.beatTimesSec.size())
                ? true
                : (iois[i]     >= lo && iois[i]     <= hi);
            return leftOk && rightOk;
        };
        for (size_t i = 0; i < out.beatTimesSec.size(); ++i)
        {
            if (isInlier(i))
            {
                xs.push_back(static_cast<double>(i));
                ys.push_back(out.beatTimesSec[i]);
            }
        }

        double regressionBpm = medianBpm;
        bool   regressionOk  = false;
        if (xs.size() >= 8)
        {
            // Least-squares fit y = a + b·x.
            const double n   = static_cast<double>(xs.size());
            double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
            for (size_t i = 0; i < xs.size(); ++i)
            {
                sx  += xs[i];
                sy  += ys[i];
                sxx += xs[i] * xs[i];
                sxy += xs[i] * ys[i];
            }
            const double denom = n * sxx - sx * sx;
            if (std::abs(denom) > 1e-9)
            {
                const double b = (n * sxy - sx * sy) / denom;
                if (b > 1e-6 && b < 2.5)  // reasonable IBI in seconds
                {
                    regressionBpm = 60.0 / b;
                    regressionOk  = true;
                }
            }
        }

        // Best estimator for steady-tempo tracks: mean IBI across the entire
        // span = (last_beat - first_beat) / (N - 1). For a 95 BPM track with
        // 230 beats and ±10 ms frame quantization at the endpoints, the
        // error in total span is ±20 ms; divided by 229 gives ±0.0001 s/IBI
        // ≈ ±0.001 BPM. Median IBI biases to the nearest quantized frame
        // multiple (e.g. 95.000 → quantized to 93.75); mean IBI averages
        // quantization noise across the WHOLE track.
        //
        // We compute mean IBI from INLIER beats only (those whose neighbour
        // IOIs sit within 8% of median) so outliers from drum fills / tempo
        // drift don't poison the average.
        double meanBpm = medianBpm;
        bool   meanOk  = false;
        if (xs.size() >= 8)
        {
            const double span    = ys.back() - ys.front();
            const double nIvls   = static_cast<double>(xs.back() - xs.front());
            if (span > 0.0 && nIvls > 0.0)
            {
                const double meanIBI = span / nIvls;
                if (meanIBI > 1e-6 && meanIBI < 2.5)
                {
                    meanBpm = 60.0 / meanIBI;
                    meanOk  = true;
                }
            }
        }

        // Pick the mean estimator if it agrees with median within 3% (steady
        // tempo). Mean gives precision; median is the robust fallback for
        // tempo-changing material.
        const bool agree = meanOk
                        && std::abs(meanBpm - medianBpm) / medianBpm < 0.03;
        out.bpm = agree ? meanBpm : medianBpm;

        // Confidence: inlier ratio of IOIs (same definition as before — still
        // valid even with regression-based BPM).
        int inlierCount = 0;
        for (double v : iois)
        {
            if (std::abs(v - medianIOI) / medianIOI <= 0.10)
                ++inlierCount;
        }
        out.confidence = (iois.empty() ? 0.0
                                       : static_cast<double>(inlierCount) /
                                         static_cast<double>(iois.size()));

        out.valid = (out.bpm > 30.0 && out.bpm < 300.0);

        juce::Logger::writeToLog("[BeatThis] " +
            juce::String(out.bpm, 4) + " BPM (median=" + juce::String(medianBpm, 4)
            + " mean=" + juce::String(meanBpm, 4)
            + " regression=" + juce::String(regressionBpm, 4)
            + (agree ? " mean picked" : " median picked")
            + "), conf=" + juce::String(out.confidence, 2)
            + ", inliers=" + juce::String((int) xs.size()) + "/"
            + juce::String((int) out.beatTimesSec.size()));

        return out;
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog(juce::String("[BeatThis] Inference threw: ") + e.what());
        return Result{};
    }
    catch (...)
    {
        juce::Logger::writeToLog("[BeatThis] Inference threw unknown exception");
        return Result{};
    }
}

} // namespace samplar

#else // !WITH_BEAT_THIS

// Stub implementation — keeps link surface stable when feature disabled.
namespace samplar {

struct BeatThisDetector::Impl {};
BeatThisDetector::BeatThisDetector()  = default;
BeatThisDetector::~BeatThisDetector() = default;

BeatThisDetector::Result BeatThisDetector::detect(
    const juce::AudioBuffer<float>&, double)
{
    return Result{}; // valid=false, bpm=0, conf=0
}

} // namespace samplar

#endif // WITH_BEAT_THIS
