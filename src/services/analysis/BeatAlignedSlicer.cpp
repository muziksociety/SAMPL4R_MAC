#include "BeatAlignedSlicer.h"
#include "../../utils/OnsetDetector.h"
#include <cmath>

namespace samplar {

//==============================================================================
// Constructor
//==============================================================================

BeatAlignedSlicer::BeatAlignedSlicer()
{
    formatManager_.registerBasicFormats();
    juce::Logger::writeToLog("✂️ [BeatAlignedSlicer] Created");
}

void BeatAlignedSlicer::ensureFFTSize(int size)
{
    if (currentFFTSize_ != size)
    {
        int order = static_cast<int>(std::log2(size));
        fft_ = std::make_unique<juce::dsp::FFT>(order);
        currentFFTSize_ = size;
    }
}

//==============================================================================
// Public API
//==============================================================================

BeatAlignedSlicer::Result BeatAlignedSlicer::slice(const juce::AudioBuffer<float>& buffer,
                                                    double sampleRate,
                                                    const TempoAnalyzer::Result& tempoResult,
                                                    const Config& config)
{
    juce::Logger::writeToLog("✂️ [BeatAlignedSlicer] === SLICING WITH BEAT GRID ===");
    juce::Logger::writeToLog("✂️ [BeatAlignedSlicer] BPM: " + juce::String(tempoResult.bpm, 1) +
                            " Beats: " + juce::String(static_cast<int>(tempoResult.beatPositions.size())));

    Result result;
    result.bpm = tempoResult.bpm;
    result.confidence = tempoResult.confidence;

    if (tempoResult.beatPositions.empty())
    {
        juce::Logger::writeToLog("⚠️ [BeatAlignedSlicer] No beat grid available, using pure onset detection");
    }

    // Step 1: Detect onsets
    std::vector<float> onsetStrengths;
    auto onsetTimes = detectOnsets(buffer, sampleRate, config, onsetStrengths);

    juce::Logger::writeToLog("✂️ [BeatAlignedSlicer] Detected " +
                            juce::String(static_cast<int>(onsetTimes.size())) + " onsets");

    if (onsetTimes.empty())
    {
        result.error = "No onsets detected";
        juce::Logger::writeToLog("⚠️ [BeatAlignedSlicer] No onsets detected");
        return result;
    }

    // Step 2: Align onsets to beat grid
    result.slices = alignOnsetsToBeatGrid(onsetTimes, onsetStrengths,
                                           tempoResult.beatPositions,
                                           sampleRate,
                                           buffer.getNumSamples(),
                                           config);

    // Count slice types
    for (const auto& slice : result.slices)
    {
        switch (slice.type)
        {
            case SliceType::OnBeat: ++result.onBeatCount; break;
            case SliceType::OffBeat: ++result.offBeatCount; break;
            case SliceType::GhostNote: ++result.ghostNoteCount; break;
            case SliceType::Quantized: ++result.quantizedCount; break;
        }
    }

    juce::Logger::writeToLog("✅ [BeatAlignedSlicer] Created " +
                            juce::String(static_cast<int>(result.slices.size())) + " slices:");
    juce::Logger::writeToLog("✅ [BeatAlignedSlicer]   On-beat: " + juce::String(result.onBeatCount));
    juce::Logger::writeToLog("✅ [BeatAlignedSlicer]   Off-beat: " + juce::String(result.offBeatCount));
    juce::Logger::writeToLog("✅ [BeatAlignedSlicer]   Ghost notes: " + juce::String(result.ghostNoteCount));
    juce::Logger::writeToLog("✅ [BeatAlignedSlicer]   Quantized: " + juce::String(result.quantizedCount));

    return result;
}

BeatAlignedSlicer::Result BeatAlignedSlicer::sliceWithAnalysis(const juce::AudioBuffer<float>& buffer,
                                                                double sampleRate,
                                                                const Config& config,
                                                                const TempoAnalyzer::Config& tempoConfig)
{
    juce::Logger::writeToLog("✂️ [BeatAlignedSlicer] Running tempo analysis first...");

    auto tempoResult = tempoAnalyzer_.analyze(buffer, sampleRate, tempoConfig);

    if (tempoResult.error.isNotEmpty())
    {
        Result result;
        result.error = tempoResult.error;
        return result;
    }

    return slice(buffer, sampleRate, tempoResult, config);
}

BeatAlignedSlicer::Result BeatAlignedSlicer::sliceFile(const juce::File& audioFile,
                                                        const Config& config,
                                                        const TempoAnalyzer::Config& tempoConfig)
{
    juce::Logger::writeToLog("✂️ [BeatAlignedSlicer] Processing file: " + audioFile.getFileName());

    Result result;

    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager_.createReaderFor(audioFile));

    if (!reader)
    {
        result.error = "Failed to load audio file";
        return result;
    }

    const int numSamples = static_cast<int>(reader->lengthInSamples);
    const int numChannels = static_cast<int>(reader->numChannels);
    juce::AudioBuffer<float> buffer(numChannels, numSamples);
    buffer.clear();
    reader->read(&buffer, 0, numSamples, 0, true, true);

    return sliceWithAnalysis(buffer, reader->sampleRate, config, tempoConfig);
}

juce::AudioBuffer<float> BeatAlignedSlicer::extractSlice(const juce::AudioBuffer<float>& source,
                                                          const Slice& slice,
                                                          bool applyFadeFlag,
                                                          int fadeSamples)
{
    const int numChannels = source.getNumChannels();
    const int sliceLength = slice.endSample - slice.startSample;

    if (sliceLength <= 0 || slice.startSample < 0 ||
        slice.endSample > source.getNumSamples())
    {
        return juce::AudioBuffer<float>(numChannels, 0);
    }

    juce::AudioBuffer<float> output(numChannels, sliceLength);
    output.clear();

    for (int ch = 0; ch < numChannels; ++ch)
    {
        output.copyFrom(ch, 0, source, ch, slice.startSample, sliceLength);
    }

    if (applyFadeFlag && fadeSamples > 0)
    {
        applyFade(output, fadeSamples);
    }

    return output;
}

juce::Array<juce::File> BeatAlignedSlicer::exportSlices(const juce::AudioBuffer<float>& source,
                                                         double sampleRate,
                                                         const std::vector<Slice>& slices,
                                                         const juce::File& outputDir,
                                                         const juce::String& prefix)
{
    juce::Logger::writeToLog("💾 [BeatAlignedSlicer] Exporting " +
                            juce::String(static_cast<int>(slices.size())) +
                            " slices to: " + outputDir.getFullPathName());

    juce::Array<juce::File> outputFiles;

    if (!outputDir.exists())
        outputDir.createDirectory();

    juce::WavAudioFormat wavFormat;

    for (size_t i = 0; i < slices.size(); ++i)
    {
        const auto& slice = slices[i];
        auto sliceBuffer = extractSlice(source, slice, true, 64);

        if (sliceBuffer.getNumSamples() == 0)
            continue;

        // Create filename with type indicator
        juce::String typeStr;
        switch (slice.type)
        {
            case SliceType::OnBeat: typeStr = "beat"; break;
            case SliceType::OffBeat: typeStr = "off"; break;
            case SliceType::GhostNote: typeStr = "ghost"; break;
            case SliceType::Quantized: typeStr = "quant"; break;
        }

        juce::String filename = prefix +
                               juce::String::formatted("%03d", static_cast<int>(i + 1)) +
                               "_" + typeStr + ".wav";
        juce::File outputFile = outputDir.getChildFile(filename);

        std::unique_ptr<juce::FileOutputStream> stream(outputFile.createOutputStream());
        if (!stream) continue;

        std::unique_ptr<juce::AudioFormatWriter> writer(
            wavFormat.createWriterFor(stream.get(),
                                       sampleRate,
                                       static_cast<unsigned int>(sliceBuffer.getNumChannels()),
                                       24, {}, 0));

        if (!writer) continue;
        stream.release();

        writer->writeFromAudioSampleBuffer(sliceBuffer, 0, sliceBuffer.getNumSamples());
        outputFiles.add(outputFile);
    }

    juce::Logger::writeToLog("✅ [BeatAlignedSlicer] Exported " +
                            juce::String(outputFiles.size()) + " slice files");

    return outputFiles;
}

//==============================================================================
// Onset Detection - Delegates to shared OnsetDetector utility
//==============================================================================

std::vector<float> BeatAlignedSlicer::detectOnsets(const juce::AudioBuffer<float>& buffer,
                                                    double sampleRate,
                                                    const Config& config,
                                                    std::vector<float>& onsetStrengths)
{
    juce::Logger::writeToLog("✂️ [BeatAlignedSlicer] === detectOnsets START ===");
    juce::Logger::writeToLog("✂️ [BeatAlignedSlicer]   Delegating to OnsetDetector");

    // Delegate to shared OnsetDetector utility
    auto result = OnsetDetector::detect(
        buffer,
        sampleRate,
        config.fftSize,
        config.hopSize,
        config.onsetThreshold,
        50.0f  // 50ms minimum gap (preserved from original)
    );

    // Convert sample positions to times and copy strengths
    std::vector<float> onsetTimes = OnsetDetector::samplesToTimes(result.samplePositions, sampleRate);
    onsetStrengths = result.strengths;

    juce::Logger::writeToLog("✂️ [BeatAlignedSlicer]   Detected " +
                            juce::String(static_cast<int>(onsetTimes.size())) + " onsets");
    juce::Logger::writeToLog("✂️ [BeatAlignedSlicer] === detectOnsets END ===");

    return onsetTimes;
}

std::vector<int> BeatAlignedSlicer::pickPeaks(const std::vector<float>& envelope,
                                               float threshold,
                                               int minDistance)
{
    std::vector<int> peaks;

    for (size_t i = 1; i + 1 < envelope.size(); ++i)
    {
        if (envelope[i] <= threshold)
            continue;

        // Local maximum check
        if (envelope[i] > envelope[i - 1] && envelope[i] >= envelope[i + 1])
        {
            // Check distance from last peak
            if (peaks.empty() ||
                static_cast<int>(i) - peaks.back() >= minDistance)
            {
                peaks.push_back(static_cast<int>(i));
            }
            else if (envelope[i] > envelope[static_cast<size_t>(peaks.back())])
            {
                // Replace weaker peak
                peaks.back() = static_cast<int>(i);
            }
        }
    }

    return peaks;
}

//==============================================================================
// Beat Alignment
//==============================================================================

std::vector<BeatAlignedSlicer::Slice> BeatAlignedSlicer::alignOnsetsToBeatGrid(
    const std::vector<float>& onsetTimes,
    const std::vector<float>& onsetStrengths,
    const std::vector<float>& beatPositions,
    double sampleRate,
    int totalSamples,
    const Config& config)
{
    std::vector<Slice> slices;

    const float beatSnapThreshold = config.beatSnapThresholdMs / 1000.0f;
    const float duration = static_cast<float>(totalSamples) / static_cast<float>(sampleRate);
    const int minSliceSamples = static_cast<int>(sampleRate * config.minSliceLengthMs / 1000.0);
    const int maxSliceSamples = static_cast<int>(sampleRate * config.maxSliceLengthMs / 1000.0);

    for (size_t i = 0; i < onsetTimes.size(); ++i)
    {
        Slice slice;
        float onsetTime = onsetTimes[i];
        slice.onsetStrength = onsetStrengths[i];

        // Find nearest beat
        float beatDistance = 0.0f;
        int nearestBeat = findNearestBeat(onsetTime, beatPositions, beatDistance);
        slice.beatIndex = nearestBeat;
        slice.beatDistance = beatDistance;

        // Determine start sample - potentially snap to beat
        float startTime = onsetTime;

        // Snap to beat if close enough and onset is weak
        if (std::abs(beatDistance) <= beatSnapThreshold)
        {
            if (nearestBeat >= 0 && static_cast<size_t>(nearestBeat) < beatPositions.size())
            {
                startTime = beatPositions[static_cast<size_t>(nearestBeat)];
            }
        }
        else if (config.quantizeWeakOnsets && slice.onsetStrength < 0.3f)
        {
            // Quantize weak onsets to nearest beat
            if (nearestBeat >= 0 && static_cast<size_t>(nearestBeat) < beatPositions.size())
            {
                startTime = beatPositions[static_cast<size_t>(nearestBeat)];
            }
        }

        slice.startTime = startTime;
        slice.startSample = static_cast<int>(startTime * sampleRate);
        slice.startSample = juce::jmax(0, slice.startSample);

        // Determine end sample (next onset or limit)
        if (i + 1 < onsetTimes.size())
        {
            slice.endTime = onsetTimes[i + 1];
        }
        else
        {
            slice.endTime = duration;
        }

        slice.endSample = static_cast<int>(slice.endTime * sampleRate);
        slice.endSample = juce::jmin(slice.endSample, totalSamples);

        // Apply length limits
        int sliceLength = slice.endSample - slice.startSample;
        if (sliceLength < minSliceSamples)
        {
            slice.endSample = juce::jmin(slice.startSample + minSliceSamples, totalSamples);
        }
        else if (sliceLength > maxSliceSamples)
        {
            slice.endSample = slice.startSample + maxSliceSamples;
        }

        // Calculate peak level (estimate)
        slice.peakLevel = slice.onsetStrength > 0 ?
                          20.0f * std::log10(slice.onsetStrength) : -60.0f;

        // Classify slice type
        slice.type = classifySlice(beatDistance, slice.peakLevel,
                                    beatSnapThreshold, config.ghostNoteThresholdDb);

        // Filter out ghost notes if not wanted
        if (slice.type == SliceType::GhostNote && !config.keepGhostNotes)
            continue;

        slices.push_back(slice);
    }

    return slices;
}

int BeatAlignedSlicer::findNearestBeat(float time, const std::vector<float>& beatPositions,
                                        float& outDistance)
{
    if (beatPositions.empty())
    {
        outDistance = std::numeric_limits<float>::max();
        return -1;
    }

    int nearestIdx = 0;
    float minDist = std::abs(time - beatPositions[0]);

    for (size_t i = 1; i < beatPositions.size(); ++i)
    {
        float dist = std::abs(time - beatPositions[i]);
        if (dist < minDist)
        {
            minDist = dist;
            nearestIdx = static_cast<int>(i);
        }
        else if (beatPositions[i] > time + minDist)
        {
            // Beats are sorted, no need to check further
            break;
        }
    }

    outDistance = time - beatPositions[static_cast<size_t>(nearestIdx)];
    return nearestIdx;
}

BeatAlignedSlicer::SliceType BeatAlignedSlicer::classifySlice(float beatDistance,
                                                               float peakLevel,
                                                               float beatSnapThreshold,
                                                               float ghostThreshold)
{
    // Check if it's a ghost note (quiet)
    if (peakLevel < ghostThreshold)
        return SliceType::GhostNote;

    // Check beat alignment
    if (std::abs(beatDistance) <= beatSnapThreshold)
        return SliceType::OnBeat;

    // Check if it was quantized (very close to beat but moved)
    if (std::abs(beatDistance) <= beatSnapThreshold * 2)
        return SliceType::Quantized;

    return SliceType::OffBeat;
}

//==============================================================================
// Audio Analysis
//==============================================================================

float BeatAlignedSlicer::calculatePeakLevel(const float* samples, int start, int length)
{
    float peak = 0.0f;
    for (int i = start; i < start + length && samples != nullptr; ++i)
    {
        peak = std::max(peak, std::abs(samples[i]));
    }

    if (peak <= 0.0f)
        return -96.0f;

    return 20.0f * std::log10(peak);
}

void BeatAlignedSlicer::applyFade(juce::AudioBuffer<float>& buffer, int fadeSamples)
{
    const int numSamples = buffer.getNumSamples();
    fadeSamples = juce::jmin(fadeSamples, numSamples / 4);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        float* data = buffer.getWritePointer(ch);

        // Fade in
        for (int i = 0; i < fadeSamples; ++i)
        {
            float gain = static_cast<float>(i) / static_cast<float>(fadeSamples);
            data[i] *= gain;
        }

        // Fade out
        for (int i = 0; i < fadeSamples; ++i)
        {
            float gain = static_cast<float>(i) / static_cast<float>(fadeSamples);
            data[numSamples - 1 - i] *= gain;
        }
    }
}

} // namespace samplar
