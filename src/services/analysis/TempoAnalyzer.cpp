#include "TempoAnalyzer.h"
#include "AnalysisDsp.h"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace samplar {

//==============================================================================
// Constructor
//==============================================================================

TempoAnalyzer::TempoAnalyzer()
{
    formatManager_.registerBasicFormats();
    juce::Logger::writeToLog("🎵 [TempoAnalyzer] Initialized");
}

//==============================================================================
// Public API - File-based analysis
//==============================================================================

TempoAnalyzer::Result TempoAnalyzer::analyze(const juce::File& audioFile,
                                              const Config& config)
{
    juce::Logger::writeToLog("🎵 [TempoAnalyzer] === ANALYZING FILE ===");
    juce::Logger::writeToLog("📁 [TempoAnalyzer] File: " + audioFile.getFullPathName());

    Result result;

    // Load audio file
    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager_.createReaderFor(audioFile));

    if (!reader)
    {
        result.error = "Failed to load audio file";
        juce::Logger::writeToLog("❌ [TempoAnalyzer] " + result.error);
        return result;
    }

    // Read into buffer
    const int numSamples = static_cast<int>(reader->lengthInSamples);
    const double sampleRate = reader->sampleRate;

    juce::AudioBuffer<float> buffer(1, numSamples);
    buffer.clear();

    // Mix to mono
    if (reader->numChannels == 1)
    {
        reader->read(&buffer, 0, numSamples, 0, true, false);
    }
    else
    {
        juce::AudioBuffer<float> stereo(2, numSamples);
        reader->read(&stereo, 0, numSamples, 0, true, true);

        // Mix to mono
        for (int i = 0; i < numSamples; ++i)
        {
            buffer.setSample(0, i, (stereo.getSample(0, i) + stereo.getSample(1, i)) * 0.5f);
        }
    }

    juce::Logger::writeToLog("🎵 [TempoAnalyzer] Loaded " + juce::String(numSamples) +
                            " samples at " + juce::String(sampleRate) + " Hz");

    return analyze(buffer, sampleRate, config);
}

//==============================================================================
// Public API - Buffer-based analysis
//==============================================================================

TempoAnalyzer::Result TempoAnalyzer::analyze(const juce::AudioBuffer<float>& buffer,
                                              double sampleRate,
                                              const Config& config)
{
    juce::Logger::writeToLog("🎵 [TempoAnalyzer] === BUFFER ANALYSIS ===");

    Result result;
    const int numSamples = buffer.getNumSamples();

    // Minimum duration check (need at least 0.5 seconds for tempo detection)
    const float duration = static_cast<float>(numSamples) / static_cast<float>(sampleRate);
    if (duration < 0.5f)
    {
        juce::Logger::writeToLog("🎵 [TempoAnalyzer] Sample too short for tempo detection: " +
                                juce::String(duration, 2) + "s");
        result.isLoop = false;
        return result;
    }

    // Get mono samples
    const float* samples = buffer.getReadPointer(0);

    // Step 1: Compute onset envelope using spectral flux
    juce::Logger::writeToLog("🎵 [TempoAnalyzer] Step 1: Computing onset envelope...");
    auto onsetEnvelope = computeOnsetEnvelope(samples, numSamples, sampleRate, config);

    if (onsetEnvelope.empty())
    {
        result.error = "Failed to compute onset envelope";
        juce::Logger::writeToLog("❌ [TempoAnalyzer] " + result.error);
        return result;
    }

    juce::Logger::writeToLog("🎵 [TempoAnalyzer] Onset envelope: " +
                            juce::String(onsetEnvelope.size()) + " frames");

    // Step 2: Compute autocorrelation of onset envelope
    juce::Logger::writeToLog("🎵 [TempoAnalyzer] Step 2: Computing autocorrelation...");
    auto autocorr = computeAutocorrelation(onsetEnvelope);

    // Step 3: Find tempo from autocorrelation peaks
    juce::Logger::writeToLog("🎵 [TempoAnalyzer] Step 3: Finding tempo from peaks...");
    const double hopRate = sampleRate / static_cast<double>(config.hopSize);
    result.bpm = findTempoFromAutocorr(autocorr, hopRate, config, result.confidence);

    juce::Logger::writeToLog("🎵 [TempoAnalyzer] Detected BPM: " + juce::String(result.bpm, 1) +
                            " (confidence: " + juce::String(result.confidence, 2) + ")");

    // Step 4: Generate beat grid if tempo was found
    if (result.bpm > 0 && result.confidence > 0.3f)
    {
        juce::Logger::writeToLog("🎵 [TempoAnalyzer] Step 4: Generating beat grid...");
        result.beatPositions = generateBeatGrid(result.bpm, 0.0f, duration, sampleRate);

        juce::Logger::writeToLog("🎵 [TempoAnalyzer] Generated " +
                                juce::String(result.beatPositions.size()) + " beat positions");

        // Step 5: Find downbeat if requested
        if (config.detectDownbeat && !result.beatPositions.empty())
        {
            juce::Logger::writeToLog("🎵 [TempoAnalyzer] Step 5: Detecting downbeat...");
            result.downbeatIndex = findDownbeat(samples, numSamples, sampleRate,
                                                result.beatPositions, result.downbeatConfidence);

            juce::Logger::writeToLog("🎵 [TempoAnalyzer] Downbeat at index " +
                                    juce::String(result.downbeatIndex) +
                                    " (confidence: " + juce::String(result.downbeatConfidence, 2) + ")");
        }

        // Step 6: Analyze loop characteristics
        result.isLoop = analyzeLoopCharacteristics(result.bpm, duration, result.loopLengthBeats);

        if (result.isLoop)
        {
            juce::Logger::writeToLog("✅ [TempoAnalyzer] Detected as LOOP: " +
                                    juce::String(result.loopLengthBeats, 1) + " beats at " +
                                    juce::String(result.bpm, 1) + " BPM");
        }
    }
    else
    {
        juce::Logger::writeToLog("🎵 [TempoAnalyzer] Low confidence - likely a one-shot sample");
        result.isLoop = false;
    }

    juce::Logger::writeToLog("✅ [TempoAnalyzer] Analysis complete");
    return result;
}

//==============================================================================
// Quick BPM detection
//==============================================================================

float TempoAnalyzer::detectBPM(const juce::AudioBuffer<float>& buffer,
                                double sampleRate,
                                const Config& config)
{
    Result result = analyze(buffer, sampleRate, config);
    return result.bpm;
}

//==============================================================================
// Onset Detection (Spectral Flux)
//==============================================================================

std::vector<float> TempoAnalyzer::computeOnsetEnvelope(const float* samples, int numSamples,
                                                        double sampleRate, const Config& config)
{
    ensureFFTSize(config.fftSize);

    const int fftSize = config.fftSize;
    const int hopSize = config.hopSize;
    const int numFrames = (numSamples - fftSize) / hopSize + 1;

    if (numFrames < 2)
        return {};

    std::vector<float> envelope(numFrames, 0.0f);
    std::vector<float> prevMagnitudes(fftSize / 2 + 1, 0.0f);
    std::vector<float> fftData(fftSize * 2, 0.0f);
    std::vector<float> magnitudes(fftSize / 2 + 1, 0.0f);

    // Hann window
    std::vector<float> window(fftSize);
    for (int i = 0; i < fftSize; ++i)
    {
        window[i] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi *
                                            static_cast<float>(i) / static_cast<float>(fftSize - 1)));
    }

    for (int frame = 0; frame < numFrames; ++frame)
    {
        const int startSample = frame * hopSize;

        // Apply window and prepare FFT input
        std::fill(fftData.begin(), fftData.end(), 0.0f);
        for (int i = 0; i < fftSize && (startSample + i) < numSamples; ++i)
        {
            fftData[i] = samples[startSample + i] * window[i];
        }

        // Perform FFT
        fft_->performRealOnlyForwardTransform(fftData.data(), true);

        // Compute magnitudes
        for (int bin = 0; bin <= fftSize / 2; ++bin)
        {
            const float real = fftData[bin * 2];
            const float imag = fftData[bin * 2 + 1];
            magnitudes[bin] = std::sqrt(real * real + imag * imag);
        }

        // Spectral flux (sum of positive differences)
        float flux = 0.0f;
        for (int bin = 0; bin <= fftSize / 2; ++bin)
        {
            const float diff = magnitudes[bin] - prevMagnitudes[bin];
            if (diff > 0)
                flux += diff;
        }

        envelope[frame] = flux;
        prevMagnitudes = magnitudes;
    }

    // Normalize envelope
    const float maxVal = *std::max_element(envelope.begin(), envelope.end());
    if (maxVal > 0.0f)
    {
        for (auto& val : envelope)
            val /= maxVal;
    }

    return envelope;
}

std::vector<int> TempoAnalyzer::pickOnsetPeaks(const std::vector<float>& envelope,
                                                float threshold)
{
    std::vector<int> peaks;
    const int windowSize = 3;

    for (size_t i = windowSize; i < envelope.size() - windowSize; ++i)
    {
        if (envelope[i] < threshold)
            continue;

        bool isPeak = true;
        for (int j = -windowSize; j <= windowSize; ++j)
        {
            if (j != 0 && envelope[i + j] >= envelope[i])
            {
                isPeak = false;
                break;
            }
        }

        if (isPeak)
            peaks.push_back(static_cast<int>(i));
    }

    return peaks;
}

//==============================================================================
// Tempo Detection (Autocorrelation)
//==============================================================================

std::vector<float> TempoAnalyzer::computeAutocorrelation(const std::vector<float>& envelope)
{
    // C12: FFT autocorrelation (O(n log n)) instead of the O(n^2) loop.
    // Same normalisation as before: unbiased (sum / (n - lag)), then / r[0].
    const int n = static_cast<int>(envelope.size());
    if (n <= 0) return {};
    std::vector<float> autocorr = analysis::autocorrelationFFT(envelope, n - 1); // biased, r[0] = 1
    for (int lag = 0; lag < n; ++lag)
        autocorr[static_cast<size_t>(lag)] *= static_cast<float>(n) / static_cast<float>(n - lag);
    return autocorr;
}

float TempoAnalyzer::findTempoFromAutocorr(const std::vector<float>& autocorr,
                                            double hopRate, const Config& config,
                                            float& outConfidence)
{
    outConfidence = 0.0f;

    // Convert BPM range to lag range
    const int minLag = static_cast<int>(hopRate * 60.0 / config.maxBPM);
    const int maxLag = static_cast<int>(hopRate * 60.0 / config.minBPM);

    if (maxLag >= static_cast<int>(autocorr.size()) || minLag < 1)
    {
        juce::Logger::writeToLog("⚠️ [TempoAnalyzer] Lag range out of bounds");
        return 0.0f;
    }

    // Find peaks in the valid lag range
    float bestBPM = 0.0f;
    float bestScore = 0.0f;

    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        // Check if this is a local maximum
        if (lag > 0 && lag < static_cast<int>(autocorr.size()) - 1)
        {
            if (autocorr[lag] > autocorr[lag - 1] && autocorr[lag] > autocorr[lag + 1])
            {
                const float bpm = static_cast<float>(hopRate * 60.0 / lag);
                const float weight = applyTempoWeighting(bpm, config.preferredBPM);
                const float score = autocorr[lag] * weight;

                if (score > bestScore)
                {
                    bestScore = score;
                    bestBPM = bpm;
                    outConfidence = autocorr[lag];
                }
            }
        }
    }

    // Also check for tempo at half and double the best tempo
    if (bestBPM > 0)
    {
        // Check half tempo
        const float halfBPM = bestBPM / 2.0f;
        if (halfBPM >= config.minBPM)
        {
            const int halfLag = static_cast<int>(hopRate * 60.0 / halfBPM);
            if (halfLag < static_cast<int>(autocorr.size()))
            {
                const float halfWeight = applyTempoWeighting(halfBPM, config.preferredBPM);
                const float halfScore = autocorr[halfLag] * halfWeight;
                if (halfScore > bestScore * 0.8f)  // Prefer slower tempo if close
                {
                    bestScore = halfScore;
                    bestBPM = halfBPM;
                    outConfidence = autocorr[halfLag];
                }
            }
        }
    }

    return bestBPM;
}

float TempoAnalyzer::applyTempoWeighting(float bpm, float preferredBPM)
{
    // Gaussian weighting centered on preferred BPM
    const float sigma = 60.0f;  // Standard deviation in BPM
    const float diff = bpm - preferredBPM;
    return std::exp(-(diff * diff) / (2.0f * sigma * sigma));
}

//==============================================================================
// Downbeat Detection
//==============================================================================

int TempoAnalyzer::findDownbeat(const float* samples, int numSamples, double sampleRate,
                                 const std::vector<float>& beatPositions, float& confidence)
{
    if (beatPositions.empty())
    {
        confidence = 0.0f;
        return 0;
    }

    // Analyze low-frequency energy at each beat position
    // Downbeats typically have more low-frequency (kick) energy
    std::vector<float> beatEnergies;
    const int windowSamples = static_cast<int>(sampleRate * 0.05);  // 50ms window

    for (const auto& beatTime : beatPositions)
    {
        const int samplePos = static_cast<int>(beatTime * sampleRate);
        const float energy = computeLowFreqEnergy(samples, numSamples, sampleRate,
                                                   samplePos, windowSamples);
        beatEnergies.push_back(energy);
    }

    // Find beat with maximum low-frequency energy in first few beats
    const int searchBeats = std::min(8, static_cast<int>(beatEnergies.size()));
    int bestBeat = 0;
    float bestEnergy = 0.0f;

    for (int i = 0; i < searchBeats; ++i)
    {
        if (beatEnergies[i] > bestEnergy)
        {
            bestEnergy = beatEnergies[i];
            bestBeat = i;
        }
    }

    // Calculate confidence based on how much stronger the downbeat is
    if (searchBeats > 1)
    {
        float avgOtherEnergy = 0.0f;
        for (int i = 0; i < searchBeats; ++i)
        {
            if (i != bestBeat)
                avgOtherEnergy += beatEnergies[i];
        }
        avgOtherEnergy /= static_cast<float>(searchBeats - 1);

        if (avgOtherEnergy > 0)
        {
            confidence = std::min(1.0f, (bestEnergy - avgOtherEnergy) / avgOtherEnergy);
        }
        else
        {
            confidence = 1.0f;
        }
    }
    else
    {
        confidence = 0.5f;
    }

    return bestBeat;
}

float TempoAnalyzer::computeLowFreqEnergy(const float* samples, int numSamples,
                                           double sampleRate, int position, int windowSize)
{
    // Simple low-pass filter and energy calculation
    // Focus on frequencies below 200 Hz (typical kick drum range)
    const float cutoff = 200.0f;
    const float rc = 1.0f / (2.0f * juce::MathConstants<float>::pi * cutoff);
    const float dt = 1.0f / static_cast<float>(sampleRate);
    const float alpha = dt / (rc + dt);

    float filtered = 0.0f;
    float energy = 0.0f;
    int count = 0;

    const int start = std::max(0, position - windowSize / 2);
    const int end = std::min(numSamples, position + windowSize / 2);

    for (int i = start; i < end; ++i)
    {
        filtered = filtered + alpha * (samples[i] - filtered);
        energy += filtered * filtered;
        ++count;
    }

    return count > 0 ? energy / static_cast<float>(count) : 0.0f;
}

//==============================================================================
// Beat Grid
//==============================================================================

std::vector<float> TempoAnalyzer::generateBeatGrid(float bpm, float startTime,
                                                    float duration, double /*sampleRate*/)
{
    std::vector<float> beats;

    if (bpm <= 0)
        return beats;

    const float beatDuration = 60.0f / bpm;
    float t = startTime;

    while (t < duration)
    {
        beats.push_back(t);
        t += beatDuration;
    }

    return beats;
}

//==============================================================================
// Loop Analysis
//==============================================================================

bool TempoAnalyzer::analyzeLoopCharacteristics(float bpm, float duration, float& outLoopBeats)
{
    if (bpm <= 0)
    {
        outLoopBeats = 0.0f;
        return false;
    }

    const float beatDuration = 60.0f / bpm;
    const float exactBeats = duration / beatDuration;

    // Check if duration aligns with common loop lengths (1, 2, 4, 8, 16 beats)
    const float commonLoopLengths[] = {1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f};

    for (float loopLen : commonLoopLengths)
    {
        const float diff = std::abs(exactBeats - loopLen);
        if (diff < 0.1f)  // Within 10% of a common loop length
        {
            outLoopBeats = loopLen;
            return true;
        }
    }

    // Also check if it's close to any integer number of beats
    const float roundedBeats = std::round(exactBeats);
    if (roundedBeats > 0 && std::abs(exactBeats - roundedBeats) < 0.15f)
    {
        outLoopBeats = roundedBeats;
        return true;
    }

    outLoopBeats = exactBeats;
    return false;
}

//==============================================================================
// Utilities
//==============================================================================

void TempoAnalyzer::ensureFFTSize(int size)
{
    if (currentFFTSize_ != size)
    {
        const int order = static_cast<int>(std::log2(size));
        fft_ = std::make_unique<juce::dsp::FFT>(order);
        currentFFTSize_ = size;
    }
}

} // namespace samplar
