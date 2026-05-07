#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "../app/Service.h"

namespace samplar {

/**
 * PluginPreviewService — Routes sample browser preview audio through
 * the VST3 processBlock instead of a standalone AudioDeviceManager.
 *
 * Registered under "preview_audio" (plugin mode only).
 * SamplarAudioProcessor::processBlock calls getNextAudioBlock() each cycle.
 * SampleBrowserPage adds its masterMixer_ as an input when ensureAudioReady() runs.
 */
class PluginPreviewService : public IService
{
public:
    void initialise() override {}
    void shutdown() override { mixer_.removeAllInputs(); }

    juce::MixerAudioSource& getMixer() { return mixer_; }

    void prepareToPlay(int blockSize, double sampleRate)
    {
        currentSampleRate_ = sampleRate;
        mixer_.prepareToPlay(blockSize, sampleRate);
    }

    /** Returns the DAW/host sample rate (set via prepareToPlay). */
    double getSampleRate() const { return currentSampleRate_; }

    void releaseResources()
    {
        mixer_.releaseResources();
    }

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info)
    {
        mixer_.getNextAudioBlock(info);
    }

private:
    juce::MixerAudioSource mixer_;
    double currentSampleRate_ = 0.0;
};

} // namespace samplar
