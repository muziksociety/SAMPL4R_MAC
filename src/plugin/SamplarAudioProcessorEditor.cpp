#include "SamplarAudioProcessorEditor.h"
#include "SamplarAudioProcessor.h"
#include "../ui/MainWindow.h"
#include "../ui/licensing/LicenseGateOverlay.h"
#include "../services/LicenseSystemService.h"
#include "../app/ServiceLocator.h"
#include "../utils/Tracing.h"
#include "GpuTeardownOffload.h"

namespace samplar {

SamplarAudioProcessorEditor::SamplarAudioProcessorEditor(SamplarAudioProcessor& p)
    : AudioProcessorEditor(p), processor_(p)
{
    SAMPLAR_TRACE_SCOPE("editor", "SamplarAudioProcessorEditor::ctor");
    juce::Logger::writeToLog("[SamplarAudioProcessorEditor] === CONSTRUCTOR START ===");

    // Claim ServiceLocator BEFORE creating any UI so all components
    // subscribe to THIS processor's bus (critical for multi-instance isolation).
    p.claimServiceLocator();

    // Last-instance unload: JUCE's D3D11/D2D release runs off the host's message thread.
    samplar::GpuTeardownOffload::retainGpuState();

    // Licence check: non-blocking. The UI is built first; while unlicensed an
    // in-window sign-in panel covers it (1.0.0 ran a modal loop right here,
    // inside the host's createEditor()).
    licenseService_ = dynamic_cast<LicenseSystemService*>(processor_.getServiceRegistry().get("license"));
    juce::Logger::writeToLog("[SamplarAudioProcessorEditor] licence service: " +
        juce::String(licenseService_ != nullptr ? "found" : "MISSING"));

    // Create the full SAMPLAR UI (same RootComponent used by MainWindow)
    {
        SAMPLAR_TRACE_SCOPE("editor", "createRootComponent");
        rootComponent_ = samplar::createRootComponent();
    }
    {
        SAMPLAR_TRACE_SCOPE("editor", "addAndMakeVisible(root)");
        addAndMakeVisible(*rootComponent_);
    }

    setSize(1060, 724);
    setResizable(true, true);
    setResizeLimits(850, 500, 1810, 1130);

    if (licenseService_ != nullptr)
    {
        // license.status can arrive on any thread: hop to the message thread,
        // and only while this editor exists.
        juce::Component::SafePointer<SamplarAudioProcessorEditor> safe(this);
        licenseSubscription_ = processor_.getEventBus().subscribe("license.status", [safe](const juce::var&) {
            juce::MessageManager::callAsync([safe]() {
                if (safe != nullptr)
                    safe->updateLicenseGate();
            });
        });
    }
    updateLicenseGate();   // also when the service is missing: fail CLOSED (review L-M5)

    juce::Logger::writeToLog("[SamplarAudioProcessorEditor] === CONSTRUCTOR END ===");
}

SamplarAudioProcessorEditor::~SamplarAudioProcessorEditor()
{
    SAMPLAR_TRACE_SCOPE("editor", "SamplarAudioProcessorEditor::dtor");
    juce::Logger::writeToLog("[SamplarAudioProcessorEditor] Destructor START");

    if (licenseSubscription_ != 0)
        processor_.getEventBus().unsubscribe("license.status", licenseSubscription_);
    licenseGate_.reset();   // before the UI root; a browser sign-in in flight keeps running in the service

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

void SamplarAudioProcessorEditor::updateLicenseGate()
{
    if (licenseService_ == nullptr)
    {
        // Fail closed (review L-M5): without a licence service nothing can be
        // verified, so the UI stays disabled behind a notice (the audio gate in
        // processBlock is already closed: its flag pointer is null).
        if (licenceUnavailablePanel_ == nullptr)
        {
            juce::Logger::writeToLog("[SamplarAudioProcessorEditor] ERROR licence service missing -> UI locked (fail closed)");
            struct Panel : juce::Component
            {
                void paint(juce::Graphics& g) override
                {
                    g.fillAll(juce::Colour(0xf0101018));
                    g.setColour(juce::Colours::white);
                    g.setFont(juce::FontOptions(16.0f, juce::Font::bold));
                    g.drawFittedText("SAMPLAR could not start its licence check. Please reinstall SAMPLAR.",
                                     getLocalBounds().reduced(24), juce::Justification::centred, 3);
                }
            };
            licenceUnavailablePanel_ = std::make_unique<Panel>();
            addAndMakeVisible(*licenceUnavailablePanel_);
            licenceUnavailablePanel_->setBounds(getLocalBounds());
            licenceUnavailablePanel_->toFront(false);
        }
        if (rootComponent_ != nullptr)
            rootComponent_->setEnabled(false);
        return;
    }

    const bool licensed = licenseService_->getCurrentStatus() == LicenseSystemService::LicenseStatus::Valid;

    if (!licensed && licenseGate_ == nullptr)
    {
        juce::Logger::writeToLog("[SamplarAudioProcessorEditor] Not licensed -> in-window sign-in panel");
        // No dismiss button: signing in (or a successful re-check) is the way out.
        licenseGate_ = std::make_unique<LicenseGateOverlay>(*licenseService_, processor_.getEventBus(), juce::String());
        juce::Component::SafePointer<SamplarAudioProcessorEditor> safe(this);
        licenseGate_->onFinished = [safe](int result) {
            juce::Logger::writeToLog("[SamplarAudioProcessorEditor] sign-in panel finished: " + juce::String(result));
            if (safe != nullptr)
                safe->updateLicenseGate();
        };
        addAndMakeVisible(*licenseGate_);
        licenseGate_->setBounds(getLocalBounds());
        licenseGate_->toFront(true);
    }
    else if (licensed && licenseGate_ != nullptr)
    {
        juce::Logger::writeToLog("[SamplarAudioProcessorEditor] Licensed -> sign-in panel removed");
        licenseGate_.reset();
    }

    if (rootComponent_ != nullptr)
        rootComponent_->setEnabled(licensed);   // no keyboard / MIDI-learn input behind the panel
}

void SamplarAudioProcessorEditor::paint(juce::Graphics& g)
{
    juce::ignoreUnused(g);   // the root component (and the sign-in panel) paint everything
}

void SamplarAudioProcessorEditor::resized()
{
    if (rootComponent_)
        rootComponent_->setBounds(getLocalBounds());
    if (licenseGate_)
        licenseGate_->setBounds(getLocalBounds());
    if (licenceUnavailablePanel_)
        licenceUnavailablePanel_->setBounds(getLocalBounds());
}

void SamplarAudioProcessorEditor::visibilityChanged()
{
    if (isVisible() && rootComponent_)
    {
        // Re-claim ServiceLocator for THIS instance so all UI event-bus calls
        // go to our processor's bus, not another instance's.
        juce::Logger::writeToLog("[SamplarAudioProcessorEditor] visibilityChanged -> re-claim ServiceLocator");
        processor_.claimServiceLocator();
    }
}

void SamplarAudioProcessorEditor::broughtToFront()
{
    if (rootComponent_)
    {
        juce::Logger::writeToLog("[SamplarAudioProcessorEditor] broughtToFront -> re-claim ServiceLocator");
        processor_.claimServiceLocator();
    }
}

} // namespace samplar
