#include "LoopDivider.h"

namespace samplar {

//==============================================================================
// Constructor
//==============================================================================

LoopDivider::LoopDivider()
{
    formatManager_.registerBasicFormats();
    juce::Logger::writeToLog("🔄 [LoopDivider] Created");
}

//==============================================================================
// Public API
//==============================================================================

LoopDivider::Result LoopDivider::divide(const juce::AudioBuffer<float>& buffer,
                                         double sampleRate,
                                         const TempoAnalyzer::Result& tempoResult,
                                         const Config& config)
{
    juce::Logger::writeToLog("🔄 [LoopDivider] === DIVIDING LOOP ===");
    juce::Logger::writeToLog("🔄 [LoopDivider] BPM: " + juce::String(tempoResult.bpm, 1) +
                            " Confidence: " + juce::String(tempoResult.confidence, 2));
    juce::Logger::writeToLog("🔄 [LoopDivider] Beat positions: " +
                            juce::String(static_cast<int>(tempoResult.beatPositions.size())));

    Result result;
    result.bpm = tempoResult.bpm;
    result.confidence = tempoResult.confidence;

    // Validate tempo detection
    if (tempoResult.bpm <= 0 || tempoResult.confidence < config.minConfidence)
    {
        juce::Logger::writeToLog("⚠️ [LoopDivider] Tempo detection too weak: " +
                                juce::String(tempoResult.confidence, 2) +
                                " < " + juce::String(config.minConfidence, 2));
        result.error = "Tempo detection confidence too low";
        return result;
    }

    if (tempoResult.beatPositions.empty())
    {
        juce::Logger::writeToLog("⚠️ [LoopDivider] No beat positions detected");
        result.error = "No beat positions detected";
        return result;
    }

    // Calculate segments from beat grid
    result.segments = calculateSegments(tempoResult.beatPositions,
                                        sampleRate,
                                        buffer.getNumSamples(),
                                        config);

    // Apply zero-crossing smoothing if enabled
    if (config.snapToZeroCrossing && buffer.getNumChannels() > 0)
    {
        const float* samples = buffer.getReadPointer(0);
        const int numSamples = buffer.getNumSamples();
        const int searchSamples = static_cast<int>(sampleRate * config.zeroCrossingSearchMs / 1000.0);

        for (auto& segment : result.segments)
        {
            segment.startSample = snapToZeroCrossing(samples, numSamples,
                                                      segment.startSample, searchSamples);
            segment.endSample = snapToZeroCrossing(samples, numSamples,
                                                    segment.endSample, searchSamples);
        }
    }

    // Calculate totals
    result.totalBeats = tempoResult.loopLengthBeats;
    result.totalBars = static_cast<int>(result.totalBeats / config.beatsPerBar);
    result.isValidLoop = tempoResult.isLoop;

    juce::Logger::writeToLog("✅ [LoopDivider] Created " +
                            juce::String(static_cast<int>(result.segments.size())) +
                            " segments (" + juce::String(result.totalBars) + " bars)");

    return result;
}

LoopDivider::Result LoopDivider::divideWithAnalysis(const juce::AudioBuffer<float>& buffer,
                                                     double sampleRate,
                                                     const Config& config,
                                                     const TempoAnalyzer::Config& tempoConfig)
{
    juce::Logger::writeToLog("🔄 [LoopDivider] Running tempo analysis first...");

    // Run tempo analysis
    auto tempoResult = tempoAnalyzer_.analyze(buffer, sampleRate, tempoConfig);

    if (tempoResult.error.isNotEmpty())
    {
        Result result;
        result.error = tempoResult.error;
        return result;
    }

    // Now divide using the tempo result
    return divide(buffer, sampleRate, tempoResult, config);
}

LoopDivider::Result LoopDivider::divideFile(const juce::File& audioFile,
                                             const Config& config,
                                             const TempoAnalyzer::Config& tempoConfig)
{
    juce::Logger::writeToLog("🔄 [LoopDivider] Processing file: " + audioFile.getFileName());

    Result result;

    // Load audio file
    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager_.createReaderFor(audioFile));

    if (!reader)
    {
        result.error = "Failed to load audio file";
        juce::Logger::writeToLog("❌ [LoopDivider] Failed to load: " + audioFile.getFileName());
        return result;
    }

    // Load into buffer
    const int numSamples = static_cast<int>(reader->lengthInSamples);
    const int numChannels = static_cast<int>(reader->numChannels);
    juce::AudioBuffer<float> buffer(numChannels, numSamples);
    buffer.clear();
    reader->read(&buffer, 0, numSamples, 0, true, true);

    // Process
    return divideWithAnalysis(buffer, reader->sampleRate, config, tempoConfig);
}

juce::AudioBuffer<float> LoopDivider::extractSegment(const juce::AudioBuffer<float>& source,
                                                      const LoopSegment& segment)
{
    const int numChannels = source.getNumChannels();
    const int segmentLength = segment.endSample - segment.startSample;

    if (segmentLength <= 0 || segment.startSample < 0 ||
        segment.endSample > source.getNumSamples())
    {
        juce::Logger::writeToLog("⚠️ [LoopDivider] Invalid segment bounds");
        return juce::AudioBuffer<float>(numChannels, 0);
    }

    juce::AudioBuffer<float> output(numChannels, segmentLength);
    output.clear();

    for (int ch = 0; ch < numChannels; ++ch)
    {
        output.copyFrom(ch, 0, source, ch, segment.startSample, segmentLength);
    }

    return output;
}

juce::Array<juce::File> LoopDivider::exportSegments(const juce::AudioBuffer<float>& source,
                                                     double sampleRate,
                                                     const std::vector<LoopSegment>& segments,
                                                     const juce::File& outputDir,
                                                     const juce::String& prefix)
{
    juce::Logger::writeToLog("💾 [LoopDivider] Exporting " +
                            juce::String(static_cast<int>(segments.size())) +
                            " segments to: " + outputDir.getFullPathName());

    juce::Array<juce::File> outputFiles;

    if (!outputDir.exists())
        outputDir.createDirectory();

    juce::WavAudioFormat wavFormat;

    for (size_t i = 0; i < segments.size(); ++i)
    {
        const auto& segment = segments[i];
        auto segmentBuffer = extractSegment(source, segment);

        if (segmentBuffer.getNumSamples() == 0)
            continue;

        // Create filename with bar info
        juce::String filename = prefix +
                               juce::String::formatted("bar%02d", segment.barIndex + 1) +
                               ".wav";
        juce::File outputFile = outputDir.getChildFile(filename);

        // Create writer
        std::unique_ptr<juce::FileOutputStream> stream(outputFile.createOutputStream());
        if (!stream)
        {
            juce::Logger::writeToLog("❌ [LoopDivider] Failed to create: " + filename);
            continue;
        }

        std::unique_ptr<juce::AudioFormatWriter> writer(
            wavFormat.createWriterFor(stream.get(),
                                       sampleRate,
                                       static_cast<unsigned int>(segmentBuffer.getNumChannels()),
                                       24, {}, 0));

        if (!writer)
        {
            juce::Logger::writeToLog("❌ [LoopDivider] Failed to create writer for: " + filename);
            continue;
        }

        stream.release(); // Writer takes ownership

        writer->writeFromAudioSampleBuffer(segmentBuffer, 0, segmentBuffer.getNumSamples());
        outputFiles.add(outputFile);

        juce::Logger::writeToLog("💾 [LoopDivider] Exported: " + filename +
                                " (" + juce::String(segment.numBeats, 1) + " beats)");
    }

    juce::Logger::writeToLog("✅ [LoopDivider] Exported " +
                            juce::String(outputFiles.size()) + " segment files");

    return outputFiles;
}

//==============================================================================
// Internal Methods
//==============================================================================

std::vector<LoopDivider::LoopSegment> LoopDivider::calculateSegments(
    const std::vector<float>& beatPositions,
    double sampleRate,
    int totalSamples,
    const Config& config)
{
    std::vector<LoopSegment> segments;

    if (beatPositions.empty())
        return segments;

    const int beatsPerSegment = config.beatsPerBar * config.barsPerSegment;
    const float duration = static_cast<float>(totalSamples) / static_cast<float>(sampleRate);

    juce::Logger::writeToLog("🔄 [LoopDivider] Calculating segments:");
    juce::Logger::writeToLog("🔄 [LoopDivider]   Beats per bar: " + juce::String(config.beatsPerBar));
    juce::Logger::writeToLog("🔄 [LoopDivider]   Bars per segment: " + juce::String(config.barsPerSegment));
    juce::Logger::writeToLog("🔄 [LoopDivider]   Beats per segment: " + juce::String(beatsPerSegment));

    int barIndex = 0;
    size_t beatIdx = 0;

    while (beatIdx < beatPositions.size())
    {
        LoopSegment segment;
        segment.barIndex = barIndex;
        segment.beatIndex = static_cast<int>(beatIdx);
        segment.startTime = beatPositions[beatIdx];
        segment.startSample = static_cast<int>(segment.startTime * sampleRate);

        // Find end of segment (beatsPerSegment beats later)
        size_t endBeatIdx = beatIdx + static_cast<size_t>(beatsPerSegment);

        if (endBeatIdx < beatPositions.size())
        {
            segment.endTime = beatPositions[endBeatIdx];
            segment.numBeats = static_cast<float>(beatsPerSegment);
        }
        else
        {
            // Last segment goes to end of audio
            segment.endTime = duration;
            segment.numBeats = static_cast<float>(beatPositions.size() - beatIdx);
        }

        segment.endSample = static_cast<int>(segment.endTime * sampleRate);
        segment.endSample = juce::jmin(segment.endSample, totalSamples);
        segment.numBars = segment.numBeats / static_cast<float>(config.beatsPerBar);

        // Only add if segment meets minimum requirements
        if (segment.numBeats >= config.minSegmentBeats &&
            segment.endSample > segment.startSample)
        {
            segments.push_back(segment);
            juce::Logger::writeToLog("🔄 [LoopDivider]   Segment " +
                                    juce::String(barIndex + 1) + ": " +
                                    juce::String(segment.startTime, 3) + "s - " +
                                    juce::String(segment.endTime, 3) + "s (" +
                                    juce::String(segment.numBeats, 1) + " beats)");
        }

        beatIdx += static_cast<size_t>(beatsPerSegment);
        barIndex += config.barsPerSegment;
    }

    return segments;
}

int LoopDivider::snapToZeroCrossing(const float* samples, int numSamples,
                                     int targetSample, int searchSamples)
{
    if (targetSample <= 0 || targetSample >= numSamples)
        return targetSample;

    // Search for zero crossing in both directions
    int bestSample = targetSample;
    float minAbsValue = std::abs(samples[targetSample]);

    for (int offset = 1; offset <= searchSamples; ++offset)
    {
        // Search forward
        int fwdIdx = targetSample + offset;
        if (fwdIdx < numSamples)
        {
            float val = std::abs(samples[fwdIdx]);
            if (val < minAbsValue)
            {
                minAbsValue = val;
                bestSample = fwdIdx;
            }

            // Check for actual zero crossing
            if (fwdIdx > 0)
            {
                float prev = samples[fwdIdx - 1];
                float curr = samples[fwdIdx];
                if ((prev > 0 && curr <= 0) || (prev < 0 && curr >= 0))
                {
                    return fwdIdx;
                }
            }
        }

        // Search backward
        int bwdIdx = targetSample - offset;
        if (bwdIdx >= 0)
        {
            float val = std::abs(samples[bwdIdx]);
            if (val < minAbsValue)
            {
                minAbsValue = val;
                bestSample = bwdIdx;
            }

            // Check for actual zero crossing
            if (bwdIdx > 0)
            {
                float prev = samples[bwdIdx - 1];
                float curr = samples[bwdIdx];
                if ((prev > 0 && curr <= 0) || (prev < 0 && curr >= 0))
                {
                    return bwdIdx;
                }
            }
        }
    }

    return bestSample;
}

bool LoopDivider::validateLoop(float bpm, float confidence, int numBeats,
                                float duration, const Config& config)
{
    // Check confidence threshold
    if (confidence < config.minConfidence)
        return false;

    // Check if beat count is reasonable
    if (numBeats < static_cast<int>(config.minSegmentBeats))
        return false;

    // Check if duration matches expected for this BPM
    float expectedDuration = (static_cast<float>(numBeats) / bpm) * 60.0f;
    float durationError = std::abs(duration - expectedDuration) / expectedDuration;

    // Allow 5% deviation
    if (durationError > 0.05f)
        return false;

    return true;
}

} // namespace samplar
