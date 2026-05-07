#include "LicenseActivationDialog.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace samplar {

namespace {
constexpr int kDialogWidth = 480;
constexpr int kDialogHeight = 320;
constexpr int kButtonHeight = 44;
constexpr int kMargin = 40;
constexpr int kSpacing = 16;
}

// Custom DialogWindow that prevents closing during OAuth authentication
class LicenseDialogWindow : public juce::DialogWindow
{
public:
    LicenseDialogWindow(const juce::String& title, juce::Colour backgroundColour)
        : juce::DialogWindow(title, backgroundColour, true)
    {
    }

    void closeButtonPressed() override
    {
        if (auto* content = dynamic_cast<LicenseActivationDialog*>(getContentComponent()))
        {
            if (content->isOAuthInProgress())
            {
                juce::Logger::writeToLog("[LicenseDialog] Close blocked - OAuth in progress");
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::InfoIcon,
                    "Please Wait",
                    "Browser authentication is in progress.\n\nPlease complete the login in your browser.",
                    "OK");
                return;
            }
        }
        exitModalState(0);
    }
};

LicenseActivationDialog::LicenseActivationDialog(LicenseSystemService& service, EventBus& bus)
    : service_(service), bus_(bus)
{
    setOpaque(true);

    // Title
    titleLabel_.setText("SAMPLAR", juce::dontSendNotification);
    titleLabel_.setJustificationType(juce::Justification::centred);
    titleLabel_.setFont(juce::Font(32.0f, juce::Font::bold));
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel_);

    // Subtitle
    subtitleLabel_.setText("Sign in to activate your license", juce::dontSendNotification);
    subtitleLabel_.setJustificationType(juce::Justification::centred);
    subtitleLabel_.setFont(juce::Font(16.0f));
    subtitleLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(subtitleLabel_);

    // Status label (for errors/success)
    statusLabel_.setJustificationType(juce::Justification::centred);
    statusLabel_.setColour(juce::Label::textColourId, juce::Colour::fromString("fffacc15"));
    statusLabel_.setFont(juce::Font(14.0f));
    addAndMakeVisible(statusLabel_);

    // Progress label
    progressLabel_.setJustificationType(juce::Justification::centred);
    progressLabel_.setColour(juce::Label::textColourId, juce::Colour::fromString("ff94a3b8"));
    progressLabel_.setFont(juce::Font(13.0f));
    addAndMakeVisible(progressLabel_);

    // Sign-in button - large and prominent
    loginButton_.setButtonText("Sign in with browser");
    loginButton_.setColour(juce::TextButton::buttonColourId, juce::Colour::fromString("ff3b82f6")); // Blue
    loginButton_.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromString("ff3367d6"));
    loginButton_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    loginButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    loginButton_.addListener(this);
    addAndMakeVisible(loginButton_);

    // Quit button - subtle
    quitButton_.setButtonText("Quit");
    quitButton_.setColour(juce::TextButton::buttonColourId, juce::Colour::fromString("ff374151"));
    quitButton_.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromString("ff4b5563"));
    quitButton_.setColour(juce::TextButton::textColourOnId, juce::Colours::lightgrey);
    quitButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::lightgrey);
    quitButton_.addListener(this);
    addAndMakeVisible(quitButton_);

    statusSubscription_ = bus_.subscribe("license.status", [this](const juce::var& payload) {
        juce::MessageManager::callAsync([safePayload = payload, this]() {
            handleStatusEvent(safePayload);
        });
    });

    refreshStatusFromService();
    bus_.publish("license.status_request", {});

    setSize(kDialogWidth, kDialogHeight);
}

LicenseActivationDialog::~LicenseActivationDialog()
{
    if (statusSubscription_ != 0)
        bus_.unsubscribe("license.status", statusSubscription_);

    stopTimer();
}

int LicenseActivationDialog::showModal(LicenseSystemService& service, EventBus& bus)
{
    juce::Logger::writeToLog("[LicenseDialog] showModal() starting");

    auto* content = new LicenseActivationDialog(service, bus);

    auto dialogWindow = std::make_unique<LicenseDialogWindow>(
        "License Activation",
        juce::Colours::darkgrey.darker(0.4f));

    dialogWindow->setUsingNativeTitleBar(false);
    dialogWindow->setResizable(false, false);
    dialogWindow->setContentOwned(content, true);
    dialogWindow->centreWithSize(dialogWindow->getWidth(), dialogWindow->getHeight());

    content->onValidated = [&dialogWindow]() {
        juce::Logger::writeToLog("[LicenseDialog] onValidated callback - closing dialog with success");
        if (dialogWindow)
            dialogWindow->exitModalState(1);
    };

    content->onQuit = [&dialogWindow]() {
        juce::Logger::writeToLog("[LicenseDialog] onQuit callback - closing dialog with failure");
        if (dialogWindow)
            dialogWindow->exitModalState(0);
    };

   #if JUCE_MODAL_LOOPS_PERMITTED
    juce::Logger::writeToLog("[LicenseDialog] Running modal dialog");
    dialogWindow->setVisible(true);
    int result = dialogWindow->runModalLoop();
    juce::Logger::writeToLog("[LicenseDialog] Modal dialog returned: " + juce::String(result));
    return result;
   #else
    juce::Logger::writeToLog("[LicenseDialog] WARNING: Modal loops not permitted");
    dialogWindow->setVisible(true);
    return 0;
   #endif
}

void LicenseActivationDialog::showAsync(LicenseSystemService& service, EventBus& bus,
                                        std::function<void(int result)> onComplete)
{
    juce::Logger::writeToLog("[LicenseDialog] === showAsync START ===");

    if (!onComplete)
    {
        juce::Logger::writeToLog("[LicenseDialog] ERROR: showAsync called with null callback");
        return;
    }

    // The dialog window must be a raw pointer here because enterModalState()
    // takes ownership via the ModalComponentManager callback mechanism.
    // JUCE deletes it after the callback fires when deleteWhenDismissed=true.
    auto* dialogWindow = new LicenseDialogWindow(
        "License Activation",
        juce::Colours::darkgrey.darker(0.4f));

    auto* content = new LicenseActivationDialog(service, bus);

    dialogWindow->setUsingNativeTitleBar(false);
    dialogWindow->setResizable(false, false);
    dialogWindow->setContentOwned(content, true);
    dialogWindow->centreWithSize(dialogWindow->getWidth(), dialogWindow->getHeight());

    content->onValidated = [dialogWindow]() {
        juce::Logger::writeToLog("[LicenseDialog] showAsync onValidated - exitModalState(1)");
        if (dialogWindow != nullptr)
            dialogWindow->exitModalState(1);
    };

    content->onQuit = [dialogWindow]() {
        juce::Logger::writeToLog("[LicenseDialog] showAsync onQuit - exitModalState(0)");
        if (dialogWindow != nullptr)
            dialogWindow->exitModalState(0);
    };

    juce::Logger::writeToLog("[LicenseDialog] Entering async modal state");
    dialogWindow->setVisible(true);

    // enterModalState with deleteWhenDismissed=true: JUCE owns the window after this call.
    // The ModalComponentManager::Callback fires on the message thread with the result.
    dialogWindow->enterModalState(true,
        juce::ModalCallbackFunction::create([onComplete](int result) {
            juce::Logger::writeToLog("[LicenseDialog] === showAsync callback fired === result: "
                                     + juce::String(result));
            if (onComplete)
                onComplete(result);
        }),
        true /* deleteWhenDismissed */);

    juce::Logger::writeToLog("[LicenseDialog] === showAsync END (dialog now running asynchronously) ===");
}

void LicenseActivationDialog::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Dark gradient background
    juce::ColourGradient gradient(
        juce::Colour::fromString("ff0f172a"), bounds.getTopLeft(),
        juce::Colour::fromString("ff1e293b"), bounds.getBottomRight(), false);
    g.setGradientFill(gradient);
    g.fillRect(bounds);

    // Subtle card effect
    auto card = bounds.reduced(20.0f);
    g.setColour(juce::Colour::fromString("ff161b2e"));
    g.fillRoundedRectangle(card, 16.0f);
    g.setColour(juce::Colour::fromString("ff3b82f6").withAlpha(0.25f));
    g.drawRoundedRectangle(card, 16.0f, 1.5f);
}

void LicenseActivationDialog::resized()
{
    auto bounds = getLocalBounds().reduced(kMargin);

    // Center everything vertically
    auto contentHeight = 32 + kSpacing + 20 + kSpacing * 2 + 24 + 20 + kSpacing * 2 + kButtonHeight + kSpacing + 32;
    auto yOffset = (bounds.getHeight() - contentHeight) / 2;
    bounds.removeFromTop(yOffset);

    // Title
    titleLabel_.setBounds(bounds.removeFromTop(36));
    bounds.removeFromTop(kSpacing / 2);

    // Subtitle
    subtitleLabel_.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(kSpacing * 2);

    // Status and progress
    statusLabel_.setBounds(bounds.removeFromTop(24));
    progressLabel_.setBounds(bounds.removeFromTop(20));
    bounds.removeFromTop(kSpacing * 2);

    // Login button - centered and prominent
    auto buttonArea = bounds.removeFromTop(kButtonHeight);
    auto loginWidth = juce::jmin(280, buttonArea.getWidth());
    loginButton_.setBounds(buttonArea.withSizeKeepingCentre(loginWidth, kButtonHeight));
    bounds.removeFromTop(kSpacing);

    // Quit button - smaller, centered below
    auto quitArea = bounds.removeFromTop(32);
    quitButton_.setBounds(quitArea.withSizeKeepingCentre(100, 32));
}

void LicenseActivationDialog::buttonClicked(juce::Button* button)
{
    if (button == &loginButton_)
        performBrowserLogin();
    else if (button == &quitButton_)
    {
        if (onQuit)
            onQuit();
    }
}

void LicenseActivationDialog::timerCallback()
{
    if (!isBusy_)
    {
        stopTimer();
        return;
    }

    static const juce::String dots[] { ".", "..", "..." };
    static int index = 0;
    progressLabel_.setText("Waiting for browser" + dots[index], juce::dontSendNotification);
    index = (index + 1) % 3;
}

void LicenseActivationDialog::handleStatusEvent(const juce::var& payload)
{
    auto statusVar = payload.getProperty("status", 0);
    auto messageVar = payload.getProperty("message", juce::var());
    auto status = static_cast<LicenseSystemService::LicenseStatus>(static_cast<int>(statusVar));
    const auto message = messageVar.toString();
    updateStatusView(status, message);

    if (status == LicenseSystemService::LicenseStatus::Valid && onValidated)
        onValidated();
}

void LicenseActivationDialog::refreshStatusFromService()
{
    updateStatusView(service_.getCurrentStatus(), juce::String(service_.getLastResultMessage()));
}

void LicenseActivationDialog::updateStatusView(LicenseSystemService::LicenseStatus status, const juce::String& message)
{
    lastStatus_ = status;
    lastStatusMessage_ = message;

    juce::String statusText = statusToFriendlyString(status);
    if (message.isNotEmpty() && status != LicenseSystemService::LicenseStatus::NotInitialised)
        statusText = message;

    statusLabel_.setText(statusText, juce::dontSendNotification);

    auto colour = juce::Colours::orange;
    if (status == LicenseSystemService::LicenseStatus::Valid)
        colour = juce::Colour::fromString("ff22c55e");
    else if (status == LicenseSystemService::LicenseStatus::NetworkError)
        colour = juce::Colour::fromString("ff3b82f6");
    else if (status == LicenseSystemService::LicenseStatus::InvalidKey ||
             status == LicenseSystemService::LicenseStatus::HardwareMismatch)
        colour = juce::Colour::fromString("fff87171");
    else if (status == LicenseSystemService::LicenseStatus::NotInitialised)
        colour = juce::Colours::lightgrey;

    statusLabel_.setColour(juce::Label::textColourId, colour);
    updateProgressState(status);
}

void LicenseActivationDialog::updateProgressState(LicenseSystemService::LicenseStatus status)
{
    switch (status)
    {
        case LicenseSystemService::LicenseStatus::Valid:
            isBusy_ = false;
            progressLabel_.setText("", juce::dontSendNotification);
            stopTimer();
            if (onValidated)
                onValidated();
            break;
        case LicenseSystemService::LicenseStatus::NetworkError:
            isBusy_ = false;
            progressLabel_.setText("Check your internet connection", juce::dontSendNotification);
            stopTimer();
            break;
        case LicenseSystemService::LicenseStatus::NotInitialised:
            isBusy_ = false;
            progressLabel_.setText("", juce::dontSendNotification);
            stopTimer();
            break;
        default:
            break;
    }
}

juce::String LicenseActivationDialog::statusToFriendlyString(LicenseSystemService::LicenseStatus status) const
{
    using Status = LicenseSystemService::LicenseStatus;
    switch (status)
    {
        case Status::Valid: return "License activated";
        case Status::Expired: return "License expired";
        case Status::HardwareMismatch: return "Hardware mismatch";
        case Status::ActivationLimitExceeded: return "Activation limit exceeded";
        case Status::InvalidKey: return "Invalid license";
        case Status::NetworkError: return "Network error";
        case Status::GracePeriodActive: return "Grace period active";
        case Status::GracePeriodExpired: return "Grace period expired";
        case Status::UserLockedOut: return "Account locked";
        case Status::NotInitialised: default: return "";
    }
}

void LicenseActivationDialog::performBrowserLogin()
{
    juce::Logger::writeToLog("[LicenseDialog] performBrowserLogin() - starting OAuth flow");

    isOAuthInProgress_ = true;
    juce::Logger::writeToLog("[LicenseDialog] isOAuthInProgress_ = TRUE");

    loginButton_.setEnabled(false);
    quitButton_.setEnabled(false);

    statusLabel_.setText("Opening browser...", juce::dontSendNotification);
    statusLabel_.setColour(juce::Label::textColourId, juce::Colour::fromString("ff3b82f6"));
    progressLabel_.setText("Complete sign-in in your browser", juce::dontSendNotification);
    isBusy_ = true;
    startTimer(400);

    service_.startBrowserAuthentication([this](bool success, const std::string& message) {
        juce::Logger::writeToLog("[LicenseDialog] OAuth callback - success: " +
            juce::String(success ? "YES" : "NO"));

        isOAuthInProgress_ = false;
        loginButton_.setEnabled(true);
        quitButton_.setEnabled(true);
        isBusy_ = false;
        stopTimer();

        if (success)
        {
            juce::Logger::writeToLog("[LicenseDialog] OAuth success");
            updateStatusView(LicenseSystemService::LicenseStatus::Valid, juce::String(message));
            if (onValidated)
                onValidated();
        }
        else
        {
            juce::Logger::writeToLog("[LicenseDialog] OAuth failed: " + juce::String(message));
            updateStatusView(service_.getCurrentStatus(), juce::String(message));
        }
    });
}

} // namespace samplar
