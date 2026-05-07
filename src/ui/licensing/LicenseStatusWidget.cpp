#include "LicenseStatusWidget.h"

#include "LicenseActivationDialog.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace samplar {

namespace {
constexpr auto kSupportUrl = "https://support.samplemonsta.com/licensing";
}

LicenseStatusWidget::LicenseStatusWidget(LicenseSystemService* service, EventBus* bus)
    : licenseService_(service),
      eventBus_(bus),
      graceProgressBar_(graceProgressValue_)
{
    setOpaque(false);

    headingLabel_.setText("License", juce::dontSendNotification);
    headingLabel_.setFont(juce::Font(15.0f, juce::Font::bold));
    headingLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(headingLabel_);

    statusLabel_.setFont(juce::Font(20.0f, juce::Font::bold));
    statusLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    statusLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(statusLabel_);

    licenseTypeLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    licenseTypeLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(licenseTypeLabel_);

    expirationLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    expirationLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(expirationLabel_);

    hardwareIdLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    hardwareIdLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(hardwareIdLabel_);

    messageLabel_.setColour(juce::Label::textColourId, juce::Colours::lightsteelblue);
    messageLabel_.setJustificationType(juce::Justification::centredLeft);
    messageLabel_.setMinimumHorizontalScale(0.7f);
    addAndMakeVisible(messageLabel_);

    graceProgressBar_.setVisible(false);
    addAndMakeVisible(graceProgressBar_);

    auto configureButton = [this](juce::TextButton& button, const juce::Colour& colour)
    {
        button.addListener(this);
        button.setColour(juce::TextButton::buttonColourId, colour);
        button.setColour(juce::TextButton::buttonOnColourId, colour.brighter(0.2f));
        button.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        button.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        addAndMakeVisible(button);
    };

    configureButton(activateButton_, juce::Colour::fromString("ff3b82f6"));
    configureButton(renewButton_, juce::Colour::fromString("ffef4444"));
    configureButton(supportButton_, juce::Colour::fromString("ff0ea5e9"));

    renewButton_.setVisible(false);

    updateLicenseStatus();
    subscribe();
    startTimer(updateIntervalMs);
}

LicenseStatusWidget::~LicenseStatusWidget()
{
    stopTimer();
    unsubscribe();
    for (auto* button : { &activateButton_, &renewButton_, &supportButton_ })
        button->removeListener(this);
}

void LicenseStatusWidget::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    auto background = juce::Colour::fromString("ff1f2933");
    auto outline = juce::Colour::fromString("ff334155");
    g.setColour(background);
    g.fillRoundedRectangle(bounds, 10.0f);
    g.setColour(outline);
    g.drawRoundedRectangle(bounds, 10.0f, 1.0f);
}

void LicenseStatusWidget::resized()
{
    auto area = getLocalBounds().reduced(12);

    headingLabel_.setBounds(area.removeFromTop(18));
    statusLabel_.setBounds(area.removeFromTop(28));
    messageLabel_.setBounds(area.removeFromTop(20));
    area.removeFromTop(4);

    licenseTypeLabel_.setBounds(area.removeFromTop(18));
    expirationLabel_.setBounds(area.removeFromTop(18));
    hardwareIdLabel_.setBounds(area.removeFromTop(18));
    area.removeFromTop(4);

    if (graceProgressBar_.isVisible())
    {
        graceProgressBar_.setBounds(area.removeFromTop(18));
        area.removeFromTop(6);
    }
    else
    {
        graceProgressBar_.setBounds(0, 0, 0, 0);
    }

    auto buttonArea = area.removeFromBottom(30);
    auto buttonWidth = juce::jmax(80, buttonArea.getWidth() / 3 - 6);
    activateButton_.setBounds(buttonArea.removeFromLeft(buttonWidth).reduced(0, 2));
    buttonArea.removeFromLeft(6);
    renewButton_.setBounds(buttonArea.removeFromLeft(buttonWidth).reduced(0, 2));
    buttonArea.removeFromLeft(6);
    supportButton_.setBounds(buttonArea.removeFromLeft(buttonWidth).reduced(0, 2));
}

void LicenseStatusWidget::setVisible(bool shouldBeVisible)
{
    const auto wasVisible = isVisible();
    juce::Component::setVisible(shouldBeVisible);

    if (shouldBeVisible && !wasVisible)
    {
        updateLicenseStatus();
        if (!isTimerRunning())
            startTimer(updateIntervalMs);
    }
    else if (!shouldBeVisible && wasVisible)
    {
        stopTimer();
    }
}

void LicenseStatusWidget::updateLicenseStatus()
{
    if (licenseService_ == nullptr)
    {
        statusLabel_.setText("License service unavailable", juce::dontSendNotification);
        statusLabel_.setColour(juce::Label::textColourId, juce::Colours::orange);
        licenseTypeLabel_.setText("Type: ?", juce::dontSendNotification);
        expirationLabel_.setText("Expires: ?", juce::dontSendNotification);
        hardwareIdLabel_.setText("Hardware: ?", juce::dontSendNotification);
        messageLabel_.setText({}, juce::dontSendNotification);
        graceProgressValue_ = 0.0;
        graceProgressBar_.setVisible(false);
        renewButton_.setVisible(false);
        repaint();
        return;
    }

    const auto previousStatus = lastStatus_;
    const auto status = licenseService_->getCurrentStatus();
    lastStatus_ = status;

    statusLabel_.setText(getStatusText(status), juce::dontSendNotification);
    statusLabel_.setColour(juce::Label::textColourId, getStatusColour(status));

    const auto info = licenseService_->getCurrentLicenseInfo();
    licenseTypeLabel_.setText("Type: " + juce::String(info.licenseType.empty() ? "?" : info.licenseType.c_str()), juce::dontSendNotification);

    if (info.expiresAt.time_since_epoch().count() != 0)
    {
        const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(info.expiresAt.time_since_epoch()).count();
        juce::Time expiry(millis);
        expirationLabel_.setText("Expires: " + expiry.toString(true, true), juce::dontSendNotification);
    }
    else
    {
        expirationLabel_.setText("Expires: ?", juce::dontSendNotification);
    }

    auto hardwareId = juce::String(licenseService_->generateHardwareId());
    if (hardwareId.length() > 24)
        hardwareId = hardwareId.substring(0, 10) + "?" + hardwareId.substring(hardwareId.length() - 8);
    hardwareIdLabel_.setText("Hardware: " + (hardwareId.isNotEmpty() ? hardwareId : juce::String("?")), juce::dontSendNotification);

    auto lastMessage = juce::String(licenseService_->getLastResultMessage());
    messageLabel_.setText(lastMessage, juce::dontSendNotification);

    if (status == LicenseSystemService::LicenseStatus::GracePeriodActive)
    {
        const auto remaining = licenseService_->getRemainingGraceTime();
        const auto remainingMinutes = static_cast<double>(remaining.count());
        graceProgressValue_ = juce::jlimit(0.0, 1.0, gracePeriodTotalMinutes > 0.0 ? 1.0 - (remainingMinutes / gracePeriodTotalMinutes) : 0.0);
        graceProgressBar_.setVisible(true);
        if (previousStatus != status)
            showGracePeriodWarning();
    }
    else
    {
        graceProgressValue_ = 0.0;
        graceProgressBar_.setVisible(false);
    }

    renewButton_.setVisible(status == LicenseSystemService::LicenseStatus::Expired
                             || status == LicenseSystemService::LicenseStatus::GracePeriodExpired);
    activateButton_.setButtonText(status == LicenseSystemService::LicenseStatus::Valid ? "Revalidate" : "Activate");

    if (previousStatus != status)
    {
        if (status == LicenseSystemService::LicenseStatus::Expired)
            showLicenseExpiredDialog();
        else if (status == LicenseSystemService::LicenseStatus::HardwareMismatch)
            showHardwareMismatchDialog();
    }

    repaint();
}

void LicenseStatusWidget::timerCallback()
{
    updateLicenseStatus();
}

void LicenseStatusWidget::buttonClicked(juce::Button* button)
{
    if (button == &activateButton_)
    {
        if (licenseService_ != nullptr && eventBus_ != nullptr)
        {
            LicenseActivationDialog::showModal(*licenseService_, *eventBus_);
            updateLicenseStatus();
        }
    }
    else if (button == &renewButton_)
    {
        showLicenseExpiredDialog();
    }
    else if (button == &supportButton_)
    {
        juce::URL(kSupportUrl).launchInDefaultBrowser();
    }
}

void LicenseStatusWidget::subscribe()
{
    if (eventBus_ == nullptr || subscriptionId_ != 0)
        return;

    juce::Component::SafePointer<LicenseStatusWidget> weak(this);
    subscriptionId_ = eventBus_->subscribe("license.status", [weak](const juce::var& payload) {
        if (auto* self = weak.getComponent())
        {
            juce::MessageManager::callAsync([weak, payload]() {
                if (auto* target = weak.getComponent())
                    target->handleStatusPayload(payload);
            });
        }
    });
}

void LicenseStatusWidget::unsubscribe()
{
    if (eventBus_ != nullptr && subscriptionId_ != 0)
    {
        eventBus_->unsubscribe("license.status", subscriptionId_);
        subscriptionId_ = 0;
    }
}

void LicenseStatusWidget::handleStatusPayload(const juce::var& payload)
{
    if (payload.isObject())
    {
        if (auto* object = payload.getDynamicObject())
        {
            auto message = object->getProperty(juce::Identifier("message")).toString();
            messageLabel_.setText(message, juce::dontSendNotification);
        }
    }

    updateLicenseStatus();
}

juce::Colour LicenseStatusWidget::getStatusColour(LicenseSystemService::LicenseStatus status) const
{
    using Status = LicenseSystemService::LicenseStatus;
    switch (status)
    {
        case Status::Valid: return juce::Colour::fromString("ff22c55e");
        case Status::GracePeriodActive: return juce::Colour::fromString("fffacc15");
        case Status::ActivationLimitExceeded:
        case Status::Expired:
        case Status::GracePeriodExpired:
        case Status::InvalidKey:
        case Status::HardwareMismatch:
        case Status::UserLockedOut: return juce::Colour::fromString("fff87171");
        case Status::NetworkError: return juce::Colour::fromString("ff38bdf8");
        case Status::NotInitialised:
        default: return juce::Colours::lightgrey;
    }
}

juce::String LicenseStatusWidget::getStatusText(LicenseSystemService::LicenseStatus status) const
{
    using Status = LicenseSystemService::LicenseStatus;
    switch (status)
    {
        case Status::Valid: return "License valid";
        case Status::Expired: return "License expired";
        case Status::GracePeriodActive: return "Grace period active";
        case Status::GracePeriodExpired: return "Grace period expired";
        case Status::ActivationLimitExceeded: return "Activation limit exceeded";
        case Status::HardwareMismatch: return "Hardware mismatch";
        case Status::InvalidKey: return "Invalid license key";
        case Status::UserLockedOut: return "User locked out";
        case Status::NetworkError: return "Network error";
        case Status::NotInitialised:
        default: return "Awaiting validation";
    }
}

void LicenseStatusWidget::showLicenseExpiredDialog()
{
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                           "License expired",
                                           "Your license has expired. Renew to restore full access.");
}

void LicenseStatusWidget::showHardwareMismatchDialog()
{
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                           "Hardware mismatch",
                                           "This license is bound to a different device. Please reactivate to continue.");
}

void LicenseStatusWidget::showGracePeriodWarning()
{
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                           "Offline grace period",
                                           "You are currently using an offline grace period. Connect to the internet to refresh your license before it expires.");
}

} // namespace samplar

