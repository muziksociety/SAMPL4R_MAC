#include "SModeService.h"

namespace samplar {

SModeService::SModeService(EventBus& bus)
    : bus_(bus)
    , separation_(bus)
{
}

void SModeService::initialise()
{
    juce::Logger::writeToLog("🎵 [SModeService] === initialise ===");

    separation_.initialise();

    // Subscribe to separation events
    separationCompleteSub_ = bus_.subscribe("smode.separation.complete",
        [this](const juce::var& v) { onSeparationComplete(v); });

    separationErrorSub_ = bus_.subscribe("smode.separation.error",
        [this](const juce::var& v) { onSeparationError(v); });

    juce::Logger::writeToLog("✅ [SModeService] Initialised, subscribed to separation events");
}

void SModeService::shutdown()
{
    juce::Logger::writeToLog("🎵 [SModeService] === shutdown ===");

    bus_.unsubscribe("smode.separation.complete", separationCompleteSub_);
    bus_.unsubscribe("smode.separation.error", separationErrorSub_);

    separation_.shutdown();
    model_.clear();
    active_ = false;
}

void SModeService::reset()
{
    juce::Logger::writeToLog("🎵 [SModeService] === reset ===");
    model_.clear();
    sourceFile_ = {};
    active_ = false;
}

void SModeService::startWorkflow(const juce::File& sourceFile,
                                  SModeSeparationService::StemCount stemCount)
{
    juce::Logger::writeToLog("🎵 [SModeService] === startWorkflow ===");
    juce::Logger::writeToLog("📁 [SModeService] Source: " + sourceFile.getFullPathName());
    juce::Logger::writeToLog("🎵 [SModeService] Stems: " + juce::String(static_cast<int>(stemCount)));

    sourceFile_ = sourceFile;
    model_.clear();
    active_ = false;

    separation_.setStemCount(stemCount);
    separation_.startSeparation(sourceFile);
}

void SModeService::onSeparationComplete(const juce::var& stemPaths)
{
    juce::Logger::writeToLog("🎵 [SModeService] === onSeparationComplete ===");

    std::vector<juce::File> files;
    if (stemPaths.isArray())
    {
        for (int i = 0; i < stemPaths.size(); ++i)
            files.emplace_back(stemPaths[i].toString());
    }

    if (files.empty())
    {
        juce::Logger::writeToLog("❌ [SModeService] No stem files in completion event");
        bus_.publish("smode.workflow.error", juce::var("No stems produced"));
        return;
    }

    loadStemAudio(files);
}

void SModeService::onSeparationError(const juce::var& errorMsg)
{
    juce::Logger::writeToLog("❌ [SModeService] Separation error: " + errorMsg.toString());
    active_ = false;
    bus_.publish("smode.workflow.error", errorMsg);
}

void SModeService::loadStemAudio(const std::vector<juce::File>& stemFiles)
{
    juce::Logger::writeToLog("🎵 [SModeService] === loadStemAudio ===");
    juce::Logger::writeToLog("🎵 [SModeService] Loading " + juce::String(static_cast<int>(stemFiles.size())) + " stems");

    auto stemNames = separation_.getStemNames();
    int stemCount = static_cast<int>(stemFiles.size());
    model_.setStemCount(stemCount);

    double sampleRate = 44100.0;

    for (int i = 0; i < stemCount; ++i)
    {
        auto* stem = model_.getStem(i);
        if (!stem) continue;

        // Set name from model config
        if (i < stemNames.size())
            stem->name = stemNames[i];
        else
            stem->name = "Stem " + juce::String(i + 1);

        // Load audio
        double sr = 0.0;
        auto buffer = loadAudioFile(stemFiles[static_cast<size_t>(i)], sr);

        if (buffer.getNumSamples() > 0)
        {
            stem->audioBuffer = std::move(buffer);
            if (sr > 0.0) sampleRate = sr;

            juce::Logger::writeToLog("  ✅ " + stem->name + ": "
                + juce::String(stem->audioBuffer.getNumSamples()) + " samples, "
                + juce::String(stem->audioBuffer.getNumChannels()) + " ch");
        }
        else
        {
            juce::Logger::writeToLog("  ❌ " + stem->name + ": failed to load "
                + stemFiles[static_cast<size_t>(i)].getFullPathName());
        }
    }

    model_.setSampleRate(sampleRate);
    active_ = true;

    juce::Logger::writeToLog("✅ [SModeService] All stems loaded, sampleRate=" + juce::String(sampleRate));
    bus_.publish("smode.workflow.ready", juce::var(stemCount));
}

juce::AudioBuffer<float> SModeService::loadAudioFile(const juce::File& file, double& outSampleRate)
{
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (!reader)
    {
        juce::Logger::writeToLog("❌ [SModeService] Cannot create reader for: " + file.getFullPathName());
        outSampleRate = 0.0;
        return {};
    }

    outSampleRate = reader->sampleRate;
    int numChannels = static_cast<int>(reader->numChannels);
    int numSamples = static_cast<int>(reader->lengthInSamples);

    juce::AudioBuffer<float> buffer(numChannels, numSamples);
    buffer.clear();
    reader->read(&buffer, 0, numSamples, 0, true, numChannels > 1);

    return buffer;
}

} // namespace samplar
