#include "SamplarAudioProcessorEditor.h"
#include "SamplarAudioProcessor.h"
#include "../ui/MainWindow.h"
#include "../ui/licensing/LicenseActivationDialog.h"
#include "../services/LicenseSystemService.h"
#include "../app/ServiceLocator.h"
#include "../utils/Tracing.h"

namespace samplar {

SamplarAudioProcessorEditor::SamplarAudioProcessorEditor(SamplarAudioProcessor& p)
    : AudioProcessorEditor(p), processor_(p)
{
    SAMPLAR_TRACE_SCOPE("editor", "SamplarAudioProcessorEditor::ctor");
    juce::Logger::writeToLog("[SamplarAudioProcessorEditor] === CONSTRUCTOR START ===");

    // Claim ServiceLocator BEFORE creating any UI so all components
    // subscribe to THIS processor's bus (critical for multi-instance isolation).
    ServiceLocator::initialise(&p.getServiceRegistry(), &p.getEventBus());
    ServiceLocator::setUIState(&p.getUIState());

    // License check (blocking for standalone, async for VST3 later)
    if (auto* licenseService = dynamic_cast<LicenseSystemService*>(
            processor_.getServiceRegistry().get("license")))
    {
        if (licenseService->getCurrentStatus() != LicenseSystemService::LicenseStatus::Valid)
        {
            juce::Logger::writeToLog("[SamplarAudioProcessorEditor] Showing license dialog");
            const int result = LicenseActivationDialog::showModal(
                *licenseService, processor_.getEventBus());

            if (result != 1 || licenseService->getCurrentStatus()
                    != LicenseSystemService::LicenseStatus::Valid)
            {
                juce::Logger::writeToLog("[SamplarAudioProcessorEditor] License invalid - UI disabled");
                setSize(400, 200);
                return;
            }
        }
    }

    // Create the full SAMPLAR UI (same RootComponent used by MainWindow)
    {
        SAMPLAR_TRACE_SCOPE("editor", "createRootComponent");
        rootComponent_ = samplar::createRootComponent();
    }
    {
        SAMPLAR_TRACE_SCOPE("editor", "addAndMakeVisible(root)");
        addAndMakeVisible(*rootComponent_);
    }

    setSize(1500, 1024);
    setResizable(true, true);
    setResizeLimits(1200, 700, 2560, 1600);

    juce::Logger::writeToLog("[SamplarAudioProcessorEditor] === CONSTRUCTOR END ===");
}

SamplarAudioProcessorEditor::~SamplarAudioProcessorEditor()
{
    SAMPLAR_TRACE_SCOPE("editor", "SamplarAudioProcessorEditor::dtor");
    juce::Logger::writeToLog("[SamplarAudioProcessorEditor] Destructor START");

    // CRITICAL: snapshot every AppState binding into root_ BEFORE children destroy.
    // Without this, closing the plugin window within FL discards any UI edits
    // (step seq pattern, expanded variation, selected tiles, etc.) because
    // setStateInformation only fires on project save, not on editor close.
    // After snapshotting, when the editor is reopened the newly-constructed pages'
    // bindings late-drain from root_ and restore their state.
    processor_.getAppState().snapshotAllBindings();

    juce::PopupMenu::dismissAllActiveMenus();
    setLookAndFeel(nullptr);
    juce::Logger::writeToLog("[SamplarAudioProcessorEditor] Destructor END");
}

void SamplarAudioProcessorEditor::paint(juce::Graphics& g)
{
    if (!rootComponent_)
    {
        // License failed � show error
        g.fillAll(juce::Colour(0xff1a1a2e));
        g.setColour(juce::Colours::white);
        g.setFont(18.0f);
        g.drawFittedText("SAMPLAR\n\nLicense required.",
                         getLocalBounds(), juce::Justification::centred, 3);
    }
}

void SamplarAudioProcessorEditor::resized()
{
    if (rootComponent_)
        rootComponent_->setBounds(getLocalBounds());
}

void SamplarAudioProcessorEditor::visibilityChanged()
{
    if (isVisible() && rootComponent_)
    {
        // Re-claim ServiceLocator for THIS instance so all UI event-bus calls
        // go to our processor's bus, not another instance's.
        juce::Logger::writeToLog("[SamplarAudioProcessorEditor] visibilityChanged ? re-claim ServiceLocator");
        ServiceLocator::initialise(&processor_.getServiceRegistry(),
                                   &processor_.getEventBus());
    }
}

void SamplarAudioProcessorEditor::broughtToFront()
{
    if (rootComponent_)
    {
        juce::Logger::writeToLog("[SamplarAudioProcessorEditor] broughtToFront ? re-claim ServiceLocator");
        ServiceLocator::initialise(&processor_.getServiceRegistry(),
                                   &processor_.getEventBus());
    }
}

} // namespace samplar
