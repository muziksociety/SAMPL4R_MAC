#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <map>
#include <vector>

#ifdef WITH_LTE
#include <LoopTempoEstimator/LteTypes.h>
#include <LoopTempoEstimator/LoopTempoEstimator.h>
#endif

namespace samplar {

#ifdef WITH_LTE

/**
 * JUCE AudioBuffer wrapper for Loop Tempo Estimator library.
 * Implements LteAudioReader interface to provide audio data to LTE.
 */
class JuceAudioReaderWrapper : public LTE::LteAudioReader
{
public:
    /**
     * Create wrapper from JUCE AudioBuffer
     * @param buffer Source audio buffer (mono or stereo - will use first channel)
     * @param sampleRate Sample rate of the audio
     */
    JuceAudioReaderWrapper(const juce::AudioBuffer<float>& buffer, double sampleRate)
        : buffer_(buffer)
        , sampleRate_(sampleRate)
    {
    }

    double GetSampleRate() const override
    {
        return sampleRate_;
    }

    long long GetNumSamples() const override
    {
        return static_cast<long long>(buffer_.getNumSamples());
    }

    void ReadFloats(float* output, long long where, size_t numFrames) const override
    {
        if (where < 0 || output == nullptr)
            return;

        const float* source = buffer_.getReadPointer(0);
        const int totalSamples = buffer_.getNumSamples();

        for (size_t i = 0; i < numFrames; ++i)
        {
            long long srcIdx = where + static_cast<long long>(i);
            if (srcIdx >= 0 && srcIdx < totalSamples)
            {
                output[i] = source[srcIdx];
            }
            else
            {
                output[i] = 0.0f; // Zero-pad outside buffer range
            }
        }
    }

private:
    const juce::AudioBuffer<float>& buffer_;
    double sampleRate_;
};

/**
 * Result from Loop Tempo Estimator
 */
struct LteResult
{
    bool detected = false;     // True if LTE believes this is a loop
    float bpm = 0.0f;          // Detected BPM (0 if not a loop)
    float score = 0.0f;        // Loop classification score (0-1)
    bool tooLong = false;      // True if audio was too long for loop analysis

    // ODF data for auto-loop detection
    std::vector<float> odf;           // Onset Detection Function
    double odfSampleRate = 0.0;       // ODF sample rate (frames/sec)
    std::vector<int> odfPeakIndices;  // Peak indices in ODF
    double audioFileDuration = 0.0;   // Duration of analyzed audio
};

// Maximum duration for LTE analysis
// Set high (10 minutes) to capture ODF for full song autocorrelation analysis
// The ODF is needed for pattern detection across the ENTIRE song
static constexpr double kLteMaxDurationSeconds = 600.0;

/**
 * Run Loop Tempo Estimator on audio buffer
 * @param buffer Audio to analyze
 * @param sampleRate Sample rate
 * @param progressCallback Optional progress callback (0.0 - 1.0)
 * @return LTE detection result
 */
inline LteResult runLoopTempoEstimator(
    const juce::AudioBuffer<float>& buffer,
    double sampleRate,
    std::function<void(double)> progressCallback = nullptr)
{
    LteResult result;

    juce::Logger::writeToLog("🎵 [LTE] === REAL LTE LIBRARY ACTIVE (WITH_LTE=1) ===");

    if (buffer.getNumSamples() == 0 || sampleRate <= 0)
    {
        juce::Logger::writeToLog("❌ [LTE] Invalid buffer or sample rate");
        return result;
    }

    double durationSeconds = buffer.getNumSamples() / sampleRate;

    // For very long audio (>10min), extract a segment from the middle
    juce::AudioBuffer<float> analysisBuffer;
    const juce::AudioBuffer<float>* bufferToAnalyze = &buffer;

    if (durationSeconds > kLteMaxDurationSeconds)
    {
        juce::Logger::writeToLog("🎵 [LTE] Audio very long (" + juce::String(durationSeconds, 1) +
                                 "s > 10min limit), extracting " + juce::String(kLteMaxDurationSeconds, 0) + "s segment from middle");

        // Extract segment from the middle of the audio (up to 10 minutes)
        int segmentSamples = static_cast<int>(kLteMaxDurationSeconds * sampleRate);
        int startSample = (buffer.getNumSamples() - segmentSamples) / 2;

        analysisBuffer.setSize(buffer.getNumChannels(), segmentSamples);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            analysisBuffer.copyFrom(ch, 0, buffer, ch, startSample, segmentSamples);
        }
        bufferToAnalyze = &analysisBuffer;

        juce::Logger::writeToLog("🎵 [LTE] Extracted segment: start=" + juce::String(startSample) +
                                 " samples, length=" + juce::String(segmentSamples) + " samples");
    }

    juce::Logger::writeToLog("🎵 [LTE] === STARTING LOOP TEMPO ESTIMATION ===");
    juce::Logger::writeToLog("🎵 [LTE] Analysis duration: " + juce::String(bufferToAnalyze->getNumSamples() / sampleRate, 2) + "s");
    juce::Logger::writeToLog("🎵 [LTE] Sample rate: " + juce::String(sampleRate) + " Hz");
    juce::Logger::writeToLog("🎵 [LTE] Samples: " + juce::String(bufferToAnalyze->getNumSamples()));

    // Create audio reader wrapper
    JuceAudioReaderWrapper audioReader(*bufferToAnalyze, sampleRate);

    // Progress callback wrapper
    auto lteProgressCallback = [&progressCallback](double progress) {
        if (progressCallback)
            progressCallback(progress);
    };

    // Get debug output for analysis details
    LTE::QuantizationFitDebugOutput debugOutput;

    // Run LTE with Lenient tolerance (10% false positive rate)
    // This gives us more detections - user can see both DWT and LTE results
    // Timeout: 10 minutes to allow full song ODF generation (needed for autocorrelation)
    auto bpmOpt = LTE::GetBpm(
        audioReader,
        LTE::FalsePositiveTolerance::Lenient,
        lteProgressCallback,
        &debugOutput,
        std::chrono::seconds{600}  // 10-minute limit for full song analysis
    );

    // Always capture the score
    result.score = static_cast<float>(debugOutput.score);

    if (bpmOpt.has_value())
    {
        result.detected = true;
        result.bpm = static_cast<float>(bpmOpt.value());

        juce::Logger::writeToLog("✅ [LTE] Loop detected!");
        juce::Logger::writeToLog("   BPM: " + juce::String(result.bpm, 2));
        juce::Logger::writeToLog("   Score: " + juce::String(result.score, 3));
        juce::Logger::writeToLog("   Kurtosis: " + juce::String(debugOutput.kurtosis, 3));
        juce::Logger::writeToLog("   Is Single Event: " + juce::String(debugOutput.isSingleEvent ? "Yes" : "No"));
    }
    else
    {
        result.detected = false;
        // Use debugOutput.bpm even when not classified as loop - still useful for comparison
        result.bpm = static_cast<float>(debugOutput.bpm);

        juce::Logger::writeToLog("⚠️ [LTE] Not classified as a loop");
        juce::Logger::writeToLog("   Estimated BPM: " + juce::String(result.bpm, 2));
        juce::Logger::writeToLog("   Score: " + juce::String(result.score, 3) + " (below threshold)");
    }

    // Copy ODF data for auto-loop detection
    result.odf = debugOutput.odf;
    result.odfSampleRate = debugOutput.odfSr;
    result.odfPeakIndices = debugOutput.odfPeakIndices;
    result.audioFileDuration = debugOutput.audioFileDuration;

    juce::Logger::writeToLog("🎵 [LTE] ODF data captured: " + juce::String(result.odf.size()) +
                             " frames at " + juce::String(result.odfSampleRate, 1) + " fps, " +
                             juce::String(result.odfPeakIndices.size()) + " peaks");
    juce::Logger::writeToLog("🎵 [LTE] === LOOP TEMPO ESTIMATION COMPLETE ===");

    return result;
}

/**
 * Result from running LTE on multiple loop regions with voting
 */
struct LteVotingResult
{
    float votedBPM = 0.0f;         // BPM with most votes
    float confidence = 0.0f;       // Voting confidence (votes for winner / total loops)
    int totalLoops = 0;            // Total loops analyzed
    int loopsDetected = 0;         // Loops that LTE classified as loops
    int votesForWinner = 0;        // Number of loops that agreed on votedBPM
    float averageScore = 0.0f;     // Average LTE score across all loops
};

/**
 * Run LTE on each loop region and vote for the best BPM
 * @param buffer Full audio buffer
 * @param sampleRate Sample rate
 * @param loopRegions Vector of (startTime, endTime) pairs for each loop
 * @return Voting result with best BPM
 */
inline LteVotingResult runLteOnLoops(
    const juce::AudioBuffer<float>& buffer,
    double sampleRate,
    const std::vector<std::pair<double, double>>& loopRegions)
{
    LteVotingResult result;

    if (loopRegions.empty() || buffer.getNumSamples() == 0 || sampleRate <= 0)
    {
        juce::Logger::writeToLog("❌ [LTE-Vote] No loops to analyze");
        return result;
    }

    juce::Logger::writeToLog("🎵 [LTE-Vote] === STARTING LTE VOTING ON " +
                             juce::String(loopRegions.size()) + " LOOPS ===");

    // Collect BPM results from each loop
    std::vector<float> bpmResults;
    std::vector<float> scores;
    int loopsClassifiedAsLoop = 0;

    for (size_t i = 0; i < loopRegions.size(); ++i)
    {
        double startTime = loopRegions[i].first;
        double endTime = loopRegions[i].second;

        // Convert to samples
        int startSample = static_cast<int>(startTime * sampleRate);
        int endSample = static_cast<int>(endTime * sampleRate);
        int numSamples = endSample - startSample;

        // Bounds check
        if (startSample < 0) startSample = 0;
        if (endSample > buffer.getNumSamples()) endSample = buffer.getNumSamples();
        numSamples = endSample - startSample;

        if (numSamples < static_cast<int>(sampleRate * 0.5))  // Skip loops shorter than 0.5s
        {
            juce::Logger::writeToLog("   Loop " + juce::String(i) + ": Too short, skipping");
            continue;
        }

        // Extract loop audio
        juce::AudioBuffer<float> loopBuffer(buffer.getNumChannels(), numSamples);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            loopBuffer.copyFrom(ch, 0, buffer, ch, startSample, numSamples);
        }

        // Run LTE on this loop (no progress callback, quick analysis)
        auto lteResult = runLoopTempoEstimator(loopBuffer, sampleRate, nullptr);

        if (lteResult.bpm > 0)
        {
            bpmResults.push_back(lteResult.bpm);
            scores.push_back(lteResult.score);

            if (lteResult.detected)
                loopsClassifiedAsLoop++;

            juce::Logger::writeToLog("   Loop " + juce::String(i) + ": " +
                                     juce::String(lteResult.bpm, 2) + " BPM" +
                                     (lteResult.detected ? " (LOOP)" : "") +
                                     " score=" + juce::String(lteResult.score, 2));
        }
        else
        {
            juce::Logger::writeToLog("   Loop " + juce::String(i) + ": No BPM detected");
        }
    }

    result.totalLoops = static_cast<int>(loopRegions.size());
    result.loopsDetected = loopsClassifiedAsLoop;

    if (bpmResults.empty())
    {
        juce::Logger::writeToLog("❌ [LTE-Vote] No valid BPM results from any loop");
        return result;
    }

    // Calculate average score
    float totalScore = 0.0f;
    for (float s : scores) totalScore += s;
    result.averageScore = totalScore / scores.size();

    // Voting: Group BPMs within ±2 BPM tolerance and find most common
    std::map<int, std::vector<float>> bpmBuckets;  // bucket key -> actual BPM values

    for (float bpm : bpmResults)
    {
        // Normalize to 60-200 range first
        while (bpm > 200.0f) bpm /= 2.0f;
        while (bpm < 60.0f && bpm > 0) bpm *= 2.0f;

        int bucket = static_cast<int>(std::round(bpm));  // Round to nearest integer
        bpmBuckets[bucket].push_back(bpm);
    }

    // Find bucket with most votes (considering adjacent buckets as same)
    int bestBucket = 0;
    int bestVotes = 0;

    for (auto& [bucket, values] : bpmBuckets)
    {
        // Count votes including adjacent buckets (±1 BPM tolerance)
        int votes = static_cast<int>(values.size());

        // Add adjacent bucket votes
        if (bpmBuckets.count(bucket - 1)) votes += static_cast<int>(bpmBuckets[bucket - 1].size());
        if (bpmBuckets.count(bucket + 1)) votes += static_cast<int>(bpmBuckets[bucket + 1].size());

        if (votes > bestVotes)
        {
            bestVotes = votes;
            bestBucket = bucket;
        }
    }

    // Calculate average BPM from winning bucket and neighbors
    std::vector<float> winningBPMs;
    if (bpmBuckets.count(bestBucket - 1))
        winningBPMs.insert(winningBPMs.end(), bpmBuckets[bestBucket - 1].begin(), bpmBuckets[bestBucket - 1].end());
    if (bpmBuckets.count(bestBucket))
        winningBPMs.insert(winningBPMs.end(), bpmBuckets[bestBucket].begin(), bpmBuckets[bestBucket].end());
    if (bpmBuckets.count(bestBucket + 1))
        winningBPMs.insert(winningBPMs.end(), bpmBuckets[bestBucket + 1].begin(), bpmBuckets[bestBucket + 1].end());

    float avgBPM = 0.0f;
    for (float b : winningBPMs) avgBPM += b;
    avgBPM /= winningBPMs.size();

    result.votedBPM = avgBPM;
    result.votesForWinner = static_cast<int>(winningBPMs.size());
    result.confidence = static_cast<float>(winningBPMs.size()) / static_cast<float>(bpmResults.size());

    juce::Logger::writeToLog("✅ [LTE-Vote] Voting Complete:");
    juce::Logger::writeToLog("   Voted BPM: " + juce::String(result.votedBPM, 2));
    juce::Logger::writeToLog("   Votes: " + juce::String(result.votesForWinner) + "/" +
                             juce::String(bpmResults.size()) + " (" +
                             juce::String(static_cast<int>(result.confidence * 100)) + "%)");
    juce::Logger::writeToLog("   Loops classified as loops: " + juce::String(result.loopsDetected) +
                             "/" + juce::String(result.totalLoops));
    juce::Logger::writeToLog("   Average score: " + juce::String(result.averageScore, 2));

    return result;
}

#else

// Stub when LTE not available
struct LteResult
{
    bool detected = false;
    float bpm = 0.0f;
    float score = 0.0f;
    bool tooLong = false;

    // ODF data (empty in stub)
    std::vector<float> odf;
    double odfSampleRate = 0.0;
    std::vector<int> odfPeakIndices;
    double audioFileDuration = 0.0;
};

inline LteResult runLoopTempoEstimator(
    const juce::AudioBuffer<float>&,
    double,
    std::function<void(double)> = nullptr)
{
    juce::Logger::writeToLog("❌❌❌ [LTE] STUB VERSION - WITH_LTE=OFF - Library not linked! ❌❌❌");
    return LteResult{};
}

// Stub for voting result
struct LteVotingResult
{
    float votedBPM = 0.0f;
    float confidence = 0.0f;
    int totalLoops = 0;
    int loopsDetected = 0;
    int votesForWinner = 0;
    float averageScore = 0.0f;
};

inline LteVotingResult runLteOnLoops(
    const juce::AudioBuffer<float>&,
    double,
    const std::vector<std::pair<double, double>>&)
{
    juce::Logger::writeToLog("❌❌❌ [LTE-Vote] STUB VERSION - WITH_LTE=OFF ❌❌❌");
    return LteVotingResult{};
}

#endif // WITH_LTE

} // namespace samplar
