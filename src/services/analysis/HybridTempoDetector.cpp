#include "HybridTempoDetector.h"
#include <cmath>

namespace samplar {

//==============================================================================
// Public API
//==============================================================================

HybridTempoDetector::Result HybridTempoDetector::detect(
    const juce::AudioBuffer<float>& buffer,
    double sampleRate,
    const Config& config)
{
    juce::Logger::writeToLog("=== HYBRID TEMPO DETECTOR ===");
    juce::Logger::writeToLog("[Hybrid] Starting dual-detector analysis...");

    Result result;

    // ==========================================
    // 1. Run FFT Spectral Flux Detector
    // ==========================================
    juce::Logger::writeToLog("[Hybrid] Running FFT detector...");

    TempoAnalyzer::Config fftConfig;
    fftConfig.fftSize = config.fftSize;
    fftConfig.hopSize = config.hopSize;
    fftConfig.minBPM = config.minBPM;
    fftConfig.maxBPM = config.maxBPM;
    fftConfig.preferredBPM = config.preferredBPM;

    auto fftResult = fftDetector_.analyze(buffer, sampleRate, fftConfig);
    result.fftBPM = fftResult.bpm;
    result.fftConfidence = fftResult.confidence;

    juce::Logger::writeToLog("[Hybrid] FFT Result: " +
        juce::String(result.fftBPM, 2) + " BPM (conf: " +
        juce::String(result.fftConfidence, 2) + ")");

    // ==========================================
    // 2. Run DWT Detector
    // ==========================================
    juce::Logger::writeToLog("[Hybrid] Running DWT detector...");

    DWTTempoDetector::Config dwtConfig;
    dwtConfig.numLevels = config.dwtLevels;
    dwtConfig.minBPM = config.minBPM;
    dwtConfig.maxBPM = config.maxBPM;
    dwtConfig.windowSeconds = config.windowSeconds;
    dwtConfig.hopSeconds = config.windowHopSeconds;

    auto dwtResult = dwtDetector_.detect(buffer, sampleRate, dwtConfig);
    result.dwtBPM = dwtResult.bpm;
    result.dwtConfidence = dwtResult.confidence;

    juce::Logger::writeToLog("[Hybrid] DWT Result: " +
        juce::String(result.dwtBPM, 2) + " BPM (conf: " +
        juce::String(result.dwtConfidence, 2) + ")");

    // ==========================================
    // 3. Merge Results
    // ==========================================
    result = mergeResults(result.fftBPM, result.fftConfidence,
                          result.dwtBPM, result.dwtConfidence,
                          config);

    juce::Logger::writeToLog("=== HYBRID RESULT ===");
    juce::Logger::writeToLog("[Hybrid] Final BPM: " + juce::String(result.bpm, 2));
    juce::Logger::writeToLog("[Hybrid] Final Confidence: " + juce::String(result.confidence, 2));
    juce::Logger::writeToLog("[Hybrid] Source: " + result.getSourceName());

    return result;
}

HybridTempoDetector::Result HybridTempoDetector::detect(
    const juce::File& audioFile,
    const Config& config)
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

    return detect(buffer, reader->sampleRate, config);
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
