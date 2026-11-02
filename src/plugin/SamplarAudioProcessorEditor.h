#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../app/EventBus.h"

namespace samplar { class SamplarAudioProcessor; }

namespace samplar {

class LicenseSystemService;
class LicenseGateOverlay;

/**
 * SamplarAudioProcessorEditor - Plugin editor that hosts the SAMPLAR UI.
 *
 * In standalone mode JUCE's StandaloneFilterWindow embeds this editor.
 * In VST3 mode the DAW hosts it as the plugin window.
 *
 * Creates the same RootComponent that MainWindow used to create, so the UI is
 * identical in both modes. While SAMPLAR is not licensed, an in-window sign-in
 * panel (LicenseGateOverlay) covers it - never a modal loop inside the host.
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
    /** Show the sign-in panel while not licensed, remove it once licensed. Message thread. */
    void updateLicenseGate();

    SamplarAudioProcessor& processor_;

    // The existing SAMPLAR UI content component
    std::unique_ptr<juce::Component> rootComponent_;

    LicenseSystemService* licenseService_ = nullptr;   // owned by the processor's registry (outlives the editor)
    EventBus::HandlerId licenseSubscription_ = 0;
    std::unique_ptr<LicenseGateOverlay> licenseGate_;
    // Fail closed: shown (and the UI disabled) when no licence service exists.
    std::unique_ptr<juce::Component> licenceUnavailablePanel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplarAudioProcessorEditor)
};

} // namespace samplar
