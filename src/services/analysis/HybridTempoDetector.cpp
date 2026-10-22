#include "HybridTempoDetector.h"
#include "OctaveCorrection.h"
#include "TempoRefiner.h"
#include "AnalysisDsp.h"
#include <cmath>

namespace samplar {

//==============================================================================
// Public API
//==============================================================================

HybridTempoDetector::Result HybridTempoDetector::detect(
    const juce::AudioBuffer<float>& buffer,
    double sampleRate,
    const Config& config,
    AnalysisCancel cancel)
{
    const double tStart = juce::Time::getMillisecondCounterHiRes();
    juce::Logger::writeToLog("=== HYBRID TEMPO DETECTOR ===");
    juce::Logger::writeToLog("[Hybrid] START samples=" + juce::String(buffer.getNumSamples())
        + " ch=" + juce::String(buffer.getNumChannels()) + " sr=" + juce::String(sampleRate, 0)
        + " useBeatThis=" + juce::String(config.useBeatThis ? 1 : 0)
        + " alwaysRunDsp=" + juce::String(config.alwaysRunDsp ? 1 : 0));

    Result result;
    if (buffer.getNumSamples() <= 0 || buffer.getNumChannels() <= 0 || sampleRate <= 0.0)
    {
        juce::Logger::writeToLog("[Hybrid] invalid input -> no result");
        return result;
    }

    // Cancel (shutdown / new song): checked between every stage; BeatThis also aborts its
    // running ONNX inference. A cancelled result never falls through to the DSP fallback.
    auto cancelledResult = [&](const char* where)
    {
        Result r;
        r.cancelled = true;
        r.elapsedMs = static_cast<float>(juce::Time::getMillisecondCounterHiRes() - tStart);
        juce::Logger::writeToLog(juce::String("[Hybrid] CANCELLED ") + where + " after "
            + juce::String(r.elapsedMs, 0) + " ms");
        return r;
    };
    if (cancel.requested())
        return cancelledResult("before start");

    // ==========================================
    // 1. Beat This! FIRST (C12). The neural tracker is authoritative when
    //    confident: it already resolves octave / metric ambiguity and gives
    //    beats + downbeats. DSP detectors only run as a fallback.
    // ==========================================
#if WITH_BEAT_THIS
    if (config.useBeatThis)
    {
        juce::Logger::writeToLog("[Hybrid] Step 1: BeatThis neural detector");
        const auto bt = beatThisDetector_.detect(buffer, sampleRate, cancel);
        if (bt.cancelled || cancel.requested())
            return cancelledResult("during BeatThis");
        if (bt.valid)
        {
            result.beatThisBPM        = static_cast<float>(bt.bpm);
            result.beatThisConfidence = static_cast<float>(bt.confidence);
            result.beatTimesSec       = bt.beatTimesSec;
            result.downbeatTimesSec   = bt.downbeatTimesSec;
            result.beatsPerBar        = bt.beatsPerBar;
        }
        const bool confident = bt.valid && bt.confidence >= config.beatThisConfidenceFloor;
        juce::Logger::writeToLog("[Hybrid] BeatThis: valid=" + juce::String(bt.valid ? 1 : 0)
            + " bpm=" + juce::String(bt.bpm, 3) + " conf=" + juce::String(bt.confidence, 2)
            + " inliers=" + juce::String(bt.tempoInliers)
            + " -> " + (confident ? "ACCEPT (DSP skipped)" : "REJECT (DSP fallback)"));

        if (confident)
        {
            result.bpm        = static_cast<float>(bt.bpm);
            result.confidence = static_cast<float>(bt.confidence);
            result.source     = Result::Source::BeatThis;

            // The IBI fit over beat numbers (C16) is sub-0.1 BPM precise when
            // it has enough beats; the ODF refiner is only needed for short
            // material where the fit has few inliers.
            if (bt.tempoInliers < kMinInliersForFit)
            {
                auto refined = samplar::refineTempoNearBPM(buffer, sampleRate, bt.bpm, 0.05);
                if (refined.improved && refined.confidence > 0.15)
                {
                    juce::Logger::writeToLog("[Hybrid] few inliers (" + juce::String(bt.tempoInliers)
                        + ") -> refined " + juce::String(bt.bpm, 3) + " -> " + juce::String(refined.refinedBPM, 3));
                    result.bpm = static_cast<float>(refined.refinedBPM);
                }
                else
                {
                    juce::Logger::writeToLog("[Hybrid] refinement skipped (improved="
                        + juce::String(refined.improved ? 1 : 0) + " conf=" + juce::String(refined.confidence, 3) + ")");
                }
            }
            else
            {
                juce::Logger::writeToLog("[Hybrid] IBI fit has " + juce::String(bt.tempoInliers)
                    + " inliers -> ODF refiner not needed");
            }

            result.preCorrectionBPM = result.bpm;
            result.correctedBPM     = result.bpm;

            if (! config.alwaysRunDsp)
            {
                result.elapsedMs = static_cast<float>(juce::Time::getMillisecondCounterHiRes() - tStart);
                juce::Logger::writeToLog("[Hybrid] END bpm=" + juce::String(result.bpm, 3)
                    + " conf=" + juce::String(result.confidence, 2) + " source=BeatThis elapsed="
                    + juce::String(result.elapsedMs, 0) + " ms");
                return result;
            }
            juce::Logger::writeToLog("[Hybrid] alwaysRunDsp=1 -> running DSP detectors for diagnostics only");
        }
    }
#endif

    // ==========================================
    // 2. DSP fallback: FFT spectral flux + DWT on a mono downmix (the old
    //    code analysed channel 0 only), merged, then octave correction.
    // ==========================================
    const bool beatThisAccepted = (result.source == Result::Source::BeatThis);
    const Result beatThisPart = result;
    result.dspFallbackUsed = ! beatThisAccepted;
    juce::Logger::writeToLog("[Hybrid] Step 2: DSP detectors (FFT + DWT)");

    if (cancel.requested())
        return cancelledResult("before DSP fallback");
    const juce::AudioBuffer<float> mono = analysis::downmixToMonoBuffer(buffer);

    TempoAnalyzer::Config fftConfig;
    fftConfig.fftSize      = config.fftSize;
    fftConfig.hopSize      = config.hopSize;
    fftConfig.minBPM       = config.minBPM;
    fftConfig.maxBPM       = config.maxBPM;
    fftConfig.preferredBPM = config.preferredBPM;
    const auto fftResult = fftDetector_.analyze(mono, sampleRate, fftConfig);
    juce::Logger::writeToLog("[Hybrid] FFT Result: " + juce::String(fftResult.bpm, 2)
        + " BPM (conf: " + juce::String(fftResult.confidence, 2) + ")");

    if (cancel.requested())
        return cancelledResult("after FFT detector");
    DWTTempoDetector::Config dwtConfig;
    dwtConfig.numLevels     = config.dwtLevels;
    dwtConfig.minBPM        = config.minBPM;
    dwtConfig.maxBPM        = config.maxBPM;
    dwtConfig.windowSeconds = config.windowSeconds;
    dwtConfig.hopSeconds    = config.windowHopSeconds;
    const auto dwtResult = dwtDetector_.detect(mono, sampleRate, dwtConfig);
    juce::Logger::writeToLog("[Hybrid] DWT Result: " + juce::String(dwtResult.bpm, 2)
        + " BPM (conf: " + juce::String(dwtResult.confidence, 2) + ")");

    if (cancel.requested())
        return cancelledResult("after DWT detector");
    Result dsp = mergeResults(fftResult.bpm, fftResult.confidence,
                              dwtResult.bpm, dwtResult.confidence, config);

    if (beatThisAccepted)
    {
        // Diagnostics-only DSP run: keep the BeatThis answer.
        result = beatThisPart;
        result.fftBPM = dsp.fftBPM; result.fftConfidence = dsp.fftConfidence;
        result.dwtBPM = dsp.dwtBPM; result.dwtConfidence = dsp.dwtConfidence;
        result.elapsedMs = static_cast<float>(juce::Time::getMillisecondCounterHiRes() - tStart);
        juce::Logger::writeToLog("[Hybrid] END bpm=" + juce::String(result.bpm, 3)
            + " source=BeatThis (DSP diag fft=" + juce::String(dsp.fftBPM, 2) + " dwt=" + juce::String(dsp.dwtBPM, 2)
            + ") elapsed=" + juce::String(result.elapsedMs, 0) + " ms");
        return result;
    }

    // Keep whatever BeatThis produced (low-confidence beats) for callers.
    dsp.beatThisBPM        = beatThisPart.beatThisBPM;
    dsp.beatThisConfidence = beatThisPart.beatThisConfidence;
    dsp.beatTimesSec       = beatThisPart.beatTimesSec;
    dsp.downbeatTimesSec   = beatThisPart.downbeatTimesSec;
    dsp.beatsPerBar        = beatThisPart.beatsPerBar;
    dsp.dspFallbackUsed    = true;
    result = dsp;

    // ==========================================
    // 3. Octave Correction (Schreiber & Mueller 2014) on the DSP candidate.
    // ==========================================
    result.preCorrectionBPM = result.bpm;
    if (config.enableOctaveCorrection && result.bpm > 0.0f)
    {
        auto oc = correctOctaveFromBuffer(static_cast<double>(result.bpm), mono, sampleRate);
        result.onsetDensityHz = static_cast<float>(oc.onsetDensityHz);
        result.octaveCorrected = oc.wasCorrected;
        result.correctedBPM    = static_cast<float>(oc.wasCorrected ? oc.correctedBPM : result.bpm);
        if (oc.wasCorrected)
            result.bpm = static_cast<float>(oc.correctedBPM);
        juce::Logger::writeToLog("[Hybrid] Octave correction: " + juce::String(result.preCorrectionBPM, 2)
            + " -> " + juce::String(result.bpm, 2) + " | " + oc.reasoning);
    }
    else
    {
        result.correctedBPM = result.bpm;
        if (! config.enableOctaveCorrection)
            juce::Logger::writeToLog("[Hybrid] Octave correction DISABLED via config");
    }

    if (cancel.requested())
        return cancelledResult("after octave correction");
    // Precision pass on the DSP answer (same narrow-band refiner as before).
    if (result.bpm > 0.0f)
    {
        auto refined = samplar::refineTempoNearBPM(buffer, sampleRate, result.bpm, 0.03);
        if (refined.improved && refined.confidence > 0.15)
        {
            juce::Logger::writeToLog("[Hybrid] DSP refined " + juce::String(result.bpm, 3)
                + " -> " + juce::String(refined.refinedBPM, 3));
            result.bpm = static_cast<float>(refined.refinedBPM);
        }
    }

    result.elapsedMs = static_cast<float>(juce::Time::getMillisecondCounterHiRes() - tStart);
    juce::Logger::writeToLog("=== HYBRID RESULT ===");
    juce::Logger::writeToLog("[Hybrid] END bpm=" + juce::String(result.bpm, 3)
        + " conf=" + juce::String(result.confidence, 2) + " source=" + result.getSourceName()
        + " (DSP fallback) elapsed=" + juce::String(result.elapsedMs, 0) + " ms");
    return result;
}

HybridTempoDetector::Result HybridTempoDetector::detect(
    const juce::File& audioFile,
    const Config& config,
    AnalysisCancel cancel)
{
    if (!formatManagerInitialized_)
    {
        formatManager_.registerBasicFormats();
        formatManagerInitialized_ = true;
    }

    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager_.createReaderFor(audioFile));

    if (!reader)
    {
        juce::Logger::writeToLog("[Hybrid] Failed to load: " + audioFile.getFullPathName());
        return Result();
    }

    // Read and mix to mono
    const int numSamples = static_cast<int>(reader->lengthInSamples);
    juce::AudioBuffer<float> buffer(1, numSamples);
    buffer.clear();

    if (reader->numChannels == 1)
    {
        reader->read(&buffer, 0, numSamples, 0, true, false);
    }
    else
    {
        juce::AudioBuffer<float> stereo(2, numSamples);
        reader->read(&stereo, 0, numSamples, 0, true, true);
        for (int i = 0; i < numSamples; ++i)
        {
            buffer.setSample(0, i, (stereo.getSample(0, i) + stereo.getSample(1, i)) * 0.5f);
        }
    }

    return detect(buffer, reader->sampleRate, config, cancel);
}

//==============================================================================
// Merge Logic
//==============================================================================

float HybridTempoDetector::getHarmonicRelation(float bpm1, float bpm2)
{
    if (bpm1 <= 0 || bpm2 <= 0)
        return 0.0f;

    float ratio = bpm1 / bpm2;

    // Check common harmonic relationships
    const float tolerance = 0.05f; // 5% tolerance

    // 1:1 (same tempo)
    if (std::abs(ratio - 1.0f) < tolerance)
        return 1.0f;

    // 2:1 (double time)
    if (std::abs(ratio - 2.0f) < tolerance)
        return 2.0f;

    // 1:2 (half time)
    if (std::abs(ratio - 0.5f) < tolerance)
        return 0.5f;

    // 3:1 (triple time)
    if (std::abs(ratio - 3.0f) < tolerance)
        return 3.0f;

    // 1:3
    if (std::abs(ratio - 0.333f) < tolerance)
        return 0.333f;

    // 3:2
    if (std::abs(ratio - 1.5f) < tolerance)
        return 1.5f;

    // 2:3
    if (std::abs(ratio - 0.667f) < tolerance)
        return 0.667f;

    return 0.0f; // Not harmonically related
}

float HybridTempoDetector::resolveHarmonic(float bpm1, float conf1,
                                            float bpm2, float conf2,
                                            float harmonicFactor,
                                            const Config& config)
{
    // Prefer tempos in the "sweet spot" (80-160 BPM)
    auto sweetSpotScore = [](float bpm) -> float {
        if (bpm >= 80.0f && bpm <= 160.0f)
            return 1.0f;
        else if (bpm >= 60.0f && bpm <= 180.0f)
            return 0.7f;
        else
            return 0.3f;
    };

    float score1 = conf1 * sweetSpotScore(bpm1);
    float score2 = conf2 * sweetSpotScore(bpm2);

    // If one tempo is in sweet spot and other isn't, prefer sweet spot
    if (score1 > score2 * 1.2f)
        return bpm1;
    if (score2 > score1 * 1.2f)
        return bpm2;

    // If both similar, prefer the one closer to preferred BPM
    float dist1 = std::abs(bpm1 - config.preferredBPM);
    float dist2 = std::abs(bpm2 - config.preferredBPM);

    return (dist1 < dist2) ? bpm1 : bpm2;
}

HybridTempoDetector::Result HybridTempoDetector::mergeResults(
    float fftBPM, float fftConf,
    float dwtBPM, float dwtConf,
    const Config& config)
{
    Result result;
    result.fftBPM = fftBPM;
    result.fftConfidence = fftConf;
    result.dwtBPM = dwtBPM;
    result.dwtConfidence = dwtConf;

    juce::Logger::writeToLog("[Hybrid] === MERGING RESULTS ===");

    // Handle edge cases
    if (fftBPM <= 0 && dwtBPM <= 0)
    {
        juce::Logger::writeToLog("[Hybrid] Both detectors failed");
        result.source = Result::Source::None;
        return result;
    }

    if (fftBPM <= 0)
    {
        juce::Logger::writeToLog("[Hybrid] Using DWT only (FFT failed)");
        result.bpm = dwtBPM;
        result.confidence = dwtConf;
        result.source = Result::Source::DWT;
        return result;
    }

    if (dwtBPM <= 0)
    {
        juce::Logger::writeToLog("[Hybrid] Using FFT only (DWT failed)");
        result.bpm = fftBPM;
        result.confidence = fftConf;
        result.source = Result::Source::FFT;
        return result;
    }

    // Both detectors produced results
    float ratio = fftBPM / dwtBPM;
    float percentDiff = std::abs(ratio - 1.0f);

    juce::Logger::writeToLog("[Hybrid] BPM ratio: " + juce::String(ratio, 3));
    juce::Logger::writeToLog("[Hybrid] Percent difference: " + juce::String(percentDiff * 100, 1) + "%");

    // Case 1: Results agree (within threshold)
    if (percentDiff <= config.agreementThreshold)
    {
        juce::Logger::writeToLog("[Hybrid] AGREEMENT - Confidence weighted average");

        // Weighted average based on confidence
        float totalConf = fftConf + dwtConf;
        if (totalConf > 0)
        {
            result.bpm = (fftBPM * fftConf + dwtBPM * dwtConf) / totalConf;
            result.confidence = (fftConf + dwtConf) / 2.0f * 1.2f; // Boost confidence for agreement
            result.confidence = std::min(result.confidence, 1.0f);
        }
        else
        {
            result.bpm = (fftBPM + dwtBPM) / 2.0f;
            result.confidence = 0.5f;
        }

        result.source = Result::Source::Merged;
        return result;
    }

    // Case 2: Check for harmonic relationship
    float harmonicFactor = getHarmonicRelation(fftBPM, dwtBPM);

    if (harmonicFactor > 0.0f)
    {
        juce::Logger::writeToLog("[Hybrid] HARMONIC RELATIONSHIP detected: " +
            juce::String(harmonicFactor, 2) + "x");

        // Resolve which tempo is "correct"
        result.bpm = resolveHarmonic(fftBPM, fftConf, dwtBPM, dwtConf,
                                     harmonicFactor, config);

        // Confidence based on agreement after harmonic resolution
        result.confidence = std::max(fftConf, dwtConf) * config.harmonicWeight;
        result.source = Result::Source::Merged;

        juce::Logger::writeToLog("[Hybrid] Resolved to: " + juce::String(result.bpm, 2) + " BPM");
        return result;
    }

    // Case 3: Disagreement - use higher confidence
    juce::Logger::writeToLog("[Hybrid] DISAGREEMENT - Using higher confidence detector");

    if (fftConf > dwtConf)
    {
        result.bpm = fftBPM;
        result.confidence = fftConf * 0.8f; // Reduce confidence due to disagreement
        result.source = Result::Source::FFT;
        juce::Logger::writeToLog("[Hybrid] Choosing FFT (higher confidence)");
    }
    else
    {
        result.bpm = dwtBPM;
        result.confidence = dwtConf * 0.8f;
        result.source = Result::Source::DWT;
        juce::Logger::writeToLog("[Hybrid] Choosing DWT (higher confidence)");
    }

    return result;
}

} // namespace samplar
