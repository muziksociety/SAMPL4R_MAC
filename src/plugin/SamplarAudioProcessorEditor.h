#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace samplar { class SamplarAudioProcessor; }

namespace samplar {

/**
 * SamplarAudioProcessorEditor — Plugin editor that hosts the SAMPLAR UI.
 *
 * In standalone mode JUCE's StandaloneFilterWindow embeds this editor.
 * In VST3 mode the DAW hosts it as the plugin window.
 *
 * Phase 1: Creates the same RootComponent that MainWindow used to create,
 * so the UI is identical in both modes.
 */
class SamplarAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit SamplarAudioProcessorEditor(SamplarAudioProcessor& processor);
    ~SamplarAudioProcessorEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /** Re-claim ServiceLocator when this editor becomes visible/focused,
     *  so UI events go to THIS instance's bus (multi-instance isolation). */
    void visibilityChanged() override;
    void broughtToFront()    override;

private:
    SamplarAudioProcessor& processor_;

    // The existing SAMPLAR UI content component
    std::unique_ptr<juce::Component> rootComponent_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplarAudioProcessorEditor)
};

} // namespace samplar
