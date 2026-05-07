#include "DWTTempoDetector.h"
#include <algorithm>
#include <numeric>

namespace samplar {

// Static constexpr definitions
constexpr float DWTTempoDetector::db4Lo[8];
constexpr float DWTTempoDetector::db4Hi[8];

//==============================================================================
// Public API
//==============================================================================

DWTTempoDetector::Result DWTTempoDetector::detect(const juce::AudioBuffer<float>& buffer,
                                                   double sampleRate,
                                                   const Config& config)
{
    juce::Logger::writeToLog("[DWTDetector] === STARTING DWT TEMPO DETECTION ===");
    juce::Logger::writeToLog("[DWTDetector]   Samples: " + juce::String(buffer.getNumSamples()));
    juce::Logger::writeToLog("[DWTDetector]   Sample rate: " + juce::String(sampleRate));
    juce::Logger::writeToLog("[DWTDetector]   Window: " + juce::String(config.windowSeconds) + "s");

    Result result;
    const int numSamples = buffer.getNumSamples();
    const float duration = static_cast<float>(numSamples) / static_cast<float>(sampleRate);

    if (duration < config.windowSeconds)
    {
        juce::Logger::writeToLog("[DWTDetector] Audio too short for windowed analysis");
        // Process entire buffer as single window
        float conf = 0.0f;
        const float* samples = buffer.getReadPointer(0);
        result.bpm = processWindow(samples, numSamples, sampleRate, config, conf);
        result.confidence = conf;
        result.windowBPMs.push_back(result.bpm);
        return result;
    }

    // Process overlapping windows
    const int windowSamples = static_cast<int>(config.windowSeconds * sampleRate);
    const int hopSamples = static_cast<int>(config.hopSeconds * sampleRate);
    const float* samples = buffer.getReadPointer(0);

    std::vector<float> windowBPMs;
    std::vector<float> windowConfs;

    for (int start = 0; start + windowSamples <= numSamples; start += hopSamples)
    {
        float conf = 0.0f;
        float bpm = processWindow(samples + start, windowSamples, sampleRate, config, conf);

        if (bpm > 0 && conf > 0.1f)
        {
            windowBPMs.push_back(bpm);
            windowConfs.push_back(conf);
            juce::Logger::writeToLog("[DWTDetector]   Window " +
                juce::String(windowBPMs.size()) + ": " +
                juce::String(bpm, 1) + " BPM (conf: " + juce::String(conf, 2) + ")");
        }
    }

    result.windowBPMs = windowBPMs;

    if (windowBPMs.empty())
    {
        juce::Logger::writeToLog("[DWTDetector] No valid BPM estimates from windows");
        return result;
    }

    // Take weighted median based on confidence
    // First, sort by BPM while keeping confidence pairs
    std::vector<std::pair<float, float>> bpmConfPairs;
    for (size_t i = 0; i < windowBPMs.size(); ++i)
        bpmConfPairs.emplace_back(windowBPMs[i], windowConfs[i]);

    std::sort(bpmConfPairs.begin(), bpmConfPairs.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    // Find weighted median
    float totalWeight = 0.0f;
    for (const auto& p : bpmConfPairs)
        totalWeight += p.second;

    float halfWeight = totalWeight / 2.0f;
    float cumWeight = 0.0f;

    for (const auto& p : bpmConfPairs)
    {
        cumWeight += p.second;
        if (cumWeight >= halfWeight)
        {
            result.bpm = p.first;
            break;
        }
    }

    // Average confidence
    result.confidence = totalWeight / static_cast<float>(windowConfs.size());

    juce::Logger::writeToLog("[DWTDetector] === RESULT ===");
    juce::Logger::writeToLog("[DWTDetector]   Final BPM: " + juce::String(result.bpm, 2));
    juce::Logger::writeToLog("[DWTDetector]   Confidence: " + juce::String(result.confidence, 2));
    juce::Logger::writeToLog("[DWTDetector]   Windows processed: " + juce::String(windowBPMs.size()));

    return result;
}

DWTTempoDetector::Result DWTTempoDetector::detect(const juce::File& audioFile,
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
        juce::Logger::writeToLog("[DWTDetector] Failed to load: " + audioFile.getFullPathName());
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
// DWT Implementation
//==============================================================================

void DWTTempoDetector::dwtDecompose(const std::vector<float>& input,
                                     std::vector<float>& approx,
                                     std::vector<float>& detail)
{
    const int n = static_cast<int>(input.size());
    const int outputLen = (n + db4Length - 1) / 2;

    approx.resize(outputLen, 0.0f);
    detail.resize(outputLen, 0.0f);

    for (int i = 0; i < outputLen; ++i)
    {
        float lo = 0.0f;
        float hi = 0.0f;

        for (int j = 0; j < db4Length; ++j)
        {
            int idx = 2 * i + j;
            if (idx < n)
            {
                lo += db4Lo[j] * input[idx];
                hi += db4Hi[j] * input[idx];
            }
        }

        approx[i] = lo;
        detail[i] = hi;
    }
}

void DWTTempoDetector::envelopeFilter(std::vector<float>& signal)
{
    // Low-pass IIR filter: y[n] = 0.01*x[n] + 0.99*y[n-1]
    const float a = 0.01f;
    const float b = 0.99f;

    float prev = 0.0f;
    for (auto& s : signal)
    {
        s = a * std::abs(s) + b * prev;
        prev = s;
    }
}

void DWTTempoDetector::rectifyAndNormalize(std::vector<float>& signal)
{
    if (signal.empty())
        return;

    // Full-wave rectification
    for (auto& s : signal)
        s = std::abs(s);

    // Mean subtraction
    float mean = std::accumulate(signal.begin(), signal.end(), 0.0f) /
                 static_cast<float>(signal.size());

    for (auto& s : signal)
        s = std::max(0.0f, s - mean);
}

std::vector<float> DWTTempoDetector::downsample(const std::vector<float>& signal, int factor)
{
    if (factor <= 1)
        return signal;

    std::vector<float> result;
    result.reserve(signal.size() / factor + 1);

    for (size_t i = 0; i < signal.size(); i += factor)
        result.push_back(signal[i]);

    return result;
}

std::vector<float> DWTTempoDetector::autocorrelate(const std::vector<float>& signal)
{
    const int n = static_cast<int>(signal.size());
    std::vector<float> result(n, 0.0f);

    // Compute mean
    float mean = std::accumulate(signal.begin(), signal.end(), 0.0f) /
                 static_cast<float>(n);

    // Autocorrelation (positive lags only)
    for (int lag = 0; lag < n; ++lag)
    {
        float sum = 0.0f;
        for (int i = 0; i < n - lag; ++i)
        {
            sum += (signal[i] - mean) * (signal[i + lag] - mean);
        }
        result[lag] = sum;
    }

    // Normalize by zero-lag
    if (result[0] > 0.0f)
    {
        float norm = result[0];
        for (auto& r : result)
            r /= norm;
    }

    return result;
}

float DWTTempoDetector::findBPMFromAutocorr(const std::vector<float>& autocorr,
                                             double sampleRate,
                                             int downsampleFactor,
                                             const Config& config,
                                             float& confidence)
{
    confidence = 0.0f;

    // Effective sample rate after all downsampling
    const double effectiveRate = sampleRate / static_cast<double>(downsampleFactor);

    // Convert BPM range to lag range
    // lag = 60 * effectiveRate / BPM
    const int minLag = static_cast<int>(60.0 * effectiveRate / config.maxBPM);
    const int maxLag = static_cast<int>(60.0 * effectiveRate / config.minBPM);

    if (maxLag >= static_cast<int>(autocorr.size()) || minLag < 1)
        return 0.0f;

    int bestLag = 0;
    float bestScore = 0.0f;

    // Phase 1: Find best integer lag
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        // Check for local maximum
        if (lag > 0 && lag < static_cast<int>(autocorr.size()) - 1)
        {
            if (autocorr[lag] > autocorr[lag - 1] && autocorr[lag] > autocorr[lag + 1])
            {
                float bpm = static_cast<float>(60.0 * effectiveRate / lag);

                // Weight towards common musical tempos (80-160 BPM)
                float weight = 1.0f;
                if (bpm >= 80.0f && bpm <= 160.0f)
                    weight = 1.5f;
                else if (bpm >= 60.0f && bpm <= 180.0f)
                    weight = 1.2f;

                float score = autocorr[lag] * weight;

                if (score > bestScore)
                {
                    bestScore = score;
                    bestLag = lag;
                    confidence = autocorr[lag];
                }
            }
        }
    }

    if (bestLag == 0)
        return 0.0f;

    // Phase 2: PARABOLIC PEAK INTERPOLATION for sub-sample precision
    // Fits a parabola through the peak and its neighbors to find the true maximum
    //
    //     y0 ●           ● y2
    //          \       /
    //           \     /
    //            ●───●  ← true peak (fractional)
    //           y1
    //
    // Formula: offset = (y0 - y2) / (2 * (y0 - 2*y1 + y2))

    double refinedLag = static_cast<double>(bestLag);

    if (bestLag > 0 && bestLag < static_cast<int>(autocorr.size()) - 1)
    {
        float y0 = autocorr[bestLag - 1];
        float y1 = autocorr[bestLag];      // peak
        float y2 = autocorr[bestLag + 1];

        float denominator = y0 - 2.0f * y1 + y2;

        // Only interpolate if denominator is significant (parabola is well-defined)
        if (std::abs(denominator) > 1e-10f)
        {
            float offset = (y0 - y2) / (2.0f * denominator);

            // Clamp offset to reasonable range (-0.5 to 0.5)
            offset = std::max(-0.5f, std::min(0.5f, offset));

            refinedLag = static_cast<double>(bestLag) + static_cast<double>(offset);

            juce::Logger::writeToLog("[DWTDetector] Parabolic refinement: lag " +
                juce::String(bestLag) + " -> " + juce::String(refinedLag, 4) +
                " (offset: " + juce::String(offset, 4) + ")");
        }
    }

    // Calculate BPM from refined (fractional) lag
    float bestBPM = static_cast<float>(60.0 * effectiveRate / refinedLag);

    juce::Logger::writeToLog("[DWTDetector] Final BPM: " + juce::String(bestBPM, 3) +
        " (from lag " + juce::String(refinedLag, 4) + " at " +
        juce::String(effectiveRate, 1) + " Hz effective rate)");

    return bestBPM;
}

float DWTTempoDetector::processWindow(const float* samples,
                                       int numSamples,
                                       double sampleRate,
                                       const Config& config,
                                       float& confidence)
{
    confidence = 0.0f;

    // Copy to vector
    std::vector<float> signal(samples, samples + numSamples);

    // 4-level DWT decomposition
    std::vector<std::vector<float>> details(config.numLevels);
    std::vector<float> approx = signal;

    for (int level = 0; level < config.numLevels; ++level)
    {
        std::vector<float> newApprox, detail;
        dwtDecompose(approx, newApprox, detail);

        // Apply envelope filter to detail coefficients
        envelopeFilter(detail);

        // Rectify and normalize
        rectifyAndNormalize(detail);

        details[level] = detail;
        approx = newApprox;
    }

    // Downsample each level to common length
    // Level 0: downsample by 1 (relative to level 0)
    // Level 1: downsample by 2
    // Level 2: downsample by 4
    // Level 3: downsample by 8
    // But we want them all at the same rate, so:
    // Level 0 has 2^1 downsampling from original
    // Level 1 has 2^2 downsampling from original
    // etc.
    // To align, downsample higher levels less

    int totalDownsample = 1 << config.numLevels; // 2^numLevels (e.g., 8 for 3 levels)

    std::vector<std::vector<float>> alignedDetails(config.numLevels);
    for (int level = 0; level < config.numLevels; ++level)
    {
        int levelDownsample = 1 << (config.numLevels - level - 1);
        alignedDetails[level] = downsample(details[level], levelDownsample);
    }

    // Sum all detail levels
    size_t minLen = alignedDetails[0].size();
    for (const auto& d : alignedDetails)
        minLen = std::min(minLen, d.size());

    std::vector<float> summed(minLen, 0.0f);
    for (int level = 0; level < config.numLevels; ++level)
    {
        for (size_t i = 0; i < minLen; ++i)
        {
            summed[i] += alignedDetails[level][i];
        }
    }

    // Autocorrelation
    auto autocorr = autocorrelate(summed);

    // Find BPM from peaks
    float bpm = findBPMFromAutocorr(autocorr, sampleRate, totalDownsample, config, confidence);

    return bpm;
}

} // namespace samplar
