#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../services/LicenseSystemService.h"
#include "../../app/EventBus.h"

namespace samplar {

class LicenseStatusWidget : public juce::Component,
                            private juce::Timer,
                            private juce::Button::Listener
{
public:
    LicenseStatusWidget(LicenseSystemService* service, EventBus* bus);
    ~LicenseStatusWidget() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void setVisible(bool shouldBeVisible) override;

    void updateLicenseStatus();

private:
    void timerCallback() override;
    void buttonClicked(juce::Button* button) override;

    void subscribe();
    void unsubscribe();
    void handleStatusPayload(const juce::var& payload);

    juce::Colour getStatusColour(LicenseSystemService::LicenseStatus status) const;
    juce::String getStatusText(LicenseSystemService::LicenseStatus status) const;

    void showLicenseExpiredDialog();
    void showHardwareMismatchDialog();
    void showGracePeriodWarning();

    LicenseSystemService* licenseService_{ nullptr };
    EventBus* eventBus_{ nullptr };
    EventBus::HandlerId subscriptionId_{ 0 };

    juce::Label headingLabel_;
    juce::Label statusLabel_;
    juce::Label licenseTypeLabel_;
    juce::Label expirationLabel_;
    juce::Label hardwareIdLabel_;
    juce::Label messageLabel_;

    double graceProgressValue_{ 0.0 };
    juce::ProgressBar graceProgressBar_;
    juce::TextButton activateButton_{ "Activate" };
    juce::TextButton renewButton_{ "Renew" };
    juce::TextButton supportButton_{ "Account" };   // opens the account portal (licences, devices)

    LicenseSystemService::LicenseStatus lastStatus_{ LicenseSystemService::LicenseStatus::NotInitialised };

    static constexpr int updateIntervalMs = 30000;
    static constexpr double gracePeriodTotalMinutes = 48.0 * 60.0;
};

} // namespace samplar
