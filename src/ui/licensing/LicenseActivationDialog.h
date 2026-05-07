#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

#include "../../services/LicenseSystemService.h"
#include "../../app/EventBus.h"

namespace samplar {

class LicenseActivationDialog : public juce::Component,
                                private juce::Button::Listener,
                                private juce::Timer
{
public:
    LicenseActivationDialog(LicenseSystemService& service, EventBus& bus);
    ~LicenseActivationDialog() override;

    static int showModal(LicenseSystemService& service, EventBus& bus);

    /** Async-compatible modal display for VST3 hosts where runModalLoop() is forbidden.
     *  Uses enterModalState() + ModalComponentManager::Callback instead.
     *  The callback receives 1 on success (license validated), 0 on quit/cancel. */
    static void showAsync(LicenseSystemService& service, EventBus& bus,
                          std::function<void(int result)> onComplete);

    void resized() override;
    void paint(juce::Graphics& g) override;

    std::function<void()> onValidated;
    std::function<void()> onQuit;

private:
    void buttonClicked(juce::Button* button) override;
    void timerCallback() override;

    void handleStatusEvent(const juce::var& payload);
    void refreshStatusFromService();
    void updateStatusView(LicenseSystemService::LicenseStatus status, const juce::String& message);
    void updateProgressState(LicenseSystemService::LicenseStatus status);
    juce::String statusToFriendlyString(LicenseSystemService::LicenseStatus status) const;
    void performBrowserLogin();

    LicenseSystemService& service_;
    EventBus& bus_;
    EventBus::HandlerId statusSubscription_ { 0 };

    juce::Label titleLabel_;
    juce::Label subtitleLabel_;
    juce::Label statusLabel_;
    juce::Label progressLabel_;
    juce::TextButton loginButton_ { "Sign in with browser" };
    juce::TextButton quitButton_ { "Quit" };

    juce::String lastStatusMessage_;
    LicenseSystemService::LicenseStatus lastStatus_ { LicenseSystemService::LicenseStatus::NotInitialised };
    bool isBusy_ { false };
    bool isOAuthInProgress_ { false };

public:
    bool isOAuthInProgress() const { return isOAuthInProgress_; }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LicenseActivationDialog)
};

} // namespace samplar
