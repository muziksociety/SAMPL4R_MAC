#include "LicenseActivationDialog.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace samplar {

namespace {
constexpr int kDialogWidth  = 480;
constexpr int kDialogHeight = 420;
constexpr int kButtonHeight = 52;
constexpr int kMargin       = 40;
constexpr int kSpacing      = 14;
}

// Custom DialogWindow — close button is disabled while OAuth is in flight so a
// stray click during the browser round-trip doesn't kill the modal.
class LicenseDialogWindow : public juce::DialogWindow
{
public:
    LicenseDialogWindow(const juce::String& title, juce::Colour backgroundColour)
        : juce::DialogWindow(title, backgroundColour, true)
    {
    }

    void closeButtonPressed() override
    {
        if (auto* content = dynamic_cast<LicenseActivationDialog*>(getContentComponent())) {
            if (content->isOAuthInProgress())
                return; // ignore close while OAuth is mid-flight
        }
        exitModalState(0);
    }
};

LicenseActivationDialog::LicenseActivationDialog(LicenseSystemService& service, EventBus& bus)
    : service_(service), bus_(bus)
{
    setOpaque(true);

    // ─── Title ────────────────────────────────────────────────────────────
    titleLabel_.setText("SAMPLAR", juce::dontSendNotification);
    titleLabel_.setJustificationType(juce::Justification::centred);
    titleLabel_.setFont(juce::Font(32.0f, juce::Font::bold));
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel_);

    // ─── Subtitle ─────────────────────────────────────────────────────────
    subtitleLabel_.setText("Sign in with your SampleMonsta account",
                           juce::dontSendNotification);
    subtitleLabel_.setJustificationType(juce::Justification::centred);
    subtitleLabel_.setFont(juce::Font(16.0f));
    subtitleLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(subtitleLabel_);

    // ─── Help text ────────────────────────────────────────────────────────
    helpLabel_.setText("Click below to authenticate with your SampleMonsta account "
                       "in your browser. You'll be redirected back to SAMPLAR when done.",
                       juce::dontSendNotification);
    helpLabel_.setJustificationType(juce::Justification::centred);
    helpLabel_.setFont(juce::Font(13.0f));
    helpLabel_.setColour(juce::Label::textColourId, juce::Colour::fromString("ff94a3b8"));
    helpLabel_.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(helpLabel_);

    // ─── Status / progress ────────────────────────────────────────────────
    statusLabel_.setJustificationType(juce::Justification::centred);
    statusLabel_.setColour(juce::Label::textColourId, juce::Colour::fromString("fffacc15"));
    statusLabel_.setFont(juce::Font(14.0f));
    addAndMakeVisible(statusLabel_);

    progressLabel_.setJustificationType(juce::Justification::centred);
    progressLabel_.setColour(juce::Label::textColourId, juce::Colour::fromString("ff94a3b8"));
    progressLabel_.setFont(juce::Font(13.0f));
    addAndMakeVisible(progressLabel_);

    // ─── Primary OAuth button ─────────────────────────────────────────────
    signInBtn_.setButtonText("Sign in with SampleMonsta");
    signInBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour::fromString("ff3b82f6"));
    signInBtn_.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromString("ff3367d6"));
    signInBtn_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    signInBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    signInBtn_.addListener(this);
    addAndMakeVisible(signInBtn_);

    // ─── Forgot / signup links ────────────────────────────────────────────
    forgotLink_.setFont(juce::Font(12.0f), false, juce::Justification::centred);
    forgotLink_.setColour(juce::HyperlinkButton::textColourId, juce::Colour::fromString("ff60a5fa"));
    addAndMakeVisible(forgotLink_);

    signupLink_.setFont(juce::Font(12.0f), false, juce::Justification::centred);
    signupLink_.setColour(juce::HyperlinkButton::textColourId, juce::Colour::fromString("ff60a5fa"));
    addAndMakeVisible(signupLink_);

    // ─── Quit button (bottom-right) ───────────────────────────────────────
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
        "Sign in to SAMPLAR",
        juce::Colours::darkgrey.darker(0.4f));

    dialogWindow->setUsingNativeTitleBar(false);
    dialogWindow->setResizable(false, false);
    dialogWindow->setContentOwned(content, true);
    dialogWindow->centreWithSize(dialogWindow->getWidth(), dialogWindow->getHeight());

    content->onValidated = [&dialogWindow]() {
        juce::Logger::writeToLog("[LicenseDialog] onValidated callback");
        if (dialogWindow) dialogWindow->exitModalState(1);
    };

    content->onQuit = [&dialogWindow]() {
        juce::Logger::writeToLog("[LicenseDialog] onQuit callback");
        if (dialogWindow) dialogWindow->exitModalState(0);
    };

   #if JUCE_MODAL_LOOPS_PERMITTED
    dialogWindow->setVisible(true);
    return dialogWindow->runModalLoop();
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

    if (!onComplete) {
        juce::Logger::writeToLog("[LicenseDialog] ERROR: showAsync null callback");
        return;
    }

    auto* dialogWindow = new LicenseDialogWindow(
        "Sign in to SAMPLAR",
        juce::Colours::darkgrey.darker(0.4f));

    auto* content = new LicenseActivationDialog(service, bus);

    dialogWindow->setUsingNativeTitleBar(false);
    dialogWindow->setResizable(false, false);
    dialogWindow->setContentOwned(content, true);
    dialogWindow->centreWithSize(dialogWindow->getWidth(), dialogWindow->getHeight());

    content->onValidated = [dialogWindow]() {
        if (dialogWindow != nullptr) dialogWindow->exitModalState(1);
    };
    content->onQuit = [dialogWindow]() {
        if (dialogWindow != nullptr) dialogWindow->exitModalState(0);
    };

    dialogWindow->setVisible(true);
    dialogWindow->enterModalState(true,
        juce::ModalCallbackFunction::create([onComplete](int result) {
            if (onComplete) onComplete(result);
        }),
        true /* deleteWhenDismissed */);
}

void LicenseActivationDialog::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    juce::ColourGradient gradient(
        juce::Colour::fromString("ff0f172a"), bounds.getTopLeft(),
        juce::Colour::fromString("ff1e293b"), bounds.getBottomRight(), false);
    g.setGradientFill(gradient);
    g.fillRect(bounds);

    auto card = bounds.reduced(20.0f);
    g.setColour(juce::Colour::fromString("ff161b2e"));
    g.fillRoundedRectangle(card, 16.0f);
    g.setColour(juce::Colour::fromString("ff3b82f6").withAlpha(0.25f));
    g.drawRoundedRectangle(card, 16.0f, 1.5f);
}

void LicenseActivationDialog::resized()
{
    auto bounds = getLocalBounds().reduced(kMargin);

    titleLabel_.setBounds(bounds.removeFromTop(36));
    bounds.removeFromTop(kSpacing / 2);

    subtitleLabel_.setBounds(bounds.removeFromTop(22));
    bounds.removeFromTop(kSpacing);

    // Help text (two lines)
    helpLabel_.setBounds(bounds.removeFromTop(48));
    bounds.removeFromTop(kSpacing);

    // Primary OAuth button — full-width, tall
    auto signInArea = bounds.removeFromTop(kButtonHeight);
    signInBtn_.setBounds(signInArea);
    bounds.removeFromTop(kSpacing);

    // Status + progress
    statusLabel_.setBounds(bounds.removeFromTop(22));
    progressLabel_.setBounds(bounds.removeFromTop(18));
    bounds.removeFromTop(kSpacing);

    // Forgot + signup links side-by-side
    auto linksRow = bounds.removeFromTop(20);
    const int half = linksRow.getWidth() / 2;
    forgotLink_.setBounds(linksRow.removeFromLeft(half).reduced(4, 0));
    signupLink_.setBounds(linksRow.reduced(4, 0));

    // Quit button anchored to bottom-right
    auto allBounds = getLocalBounds().reduced(kMargin);
    auto quitArea = allBounds.removeFromBottom(32);
    quitButton_.setBounds(quitArea.removeFromRight(100));
}

void LicenseActivationDialog::buttonClicked(juce::Button* button)
{
    if (button == &signInBtn_)
        startBrowserSignIn();
    else if (button == &quitButton_) {
        if (oauthInProgress_) {
            // Cancel any in-flight OAuth before quitting so we don't leak the
            // callback server thread.
            service_.cancelBrowserAuthentication();
            oauthInProgress_ = false;
        }
        if (onQuit) onQuit();
    }
}

void LicenseActivationDialog::startBrowserSignIn()
{
    juce::Logger::writeToLog("[LicenseDialog] === startBrowserSignIn START ===");

    if (oauthInProgress_) {
        juce::Logger::writeToLog("[LicenseDialog] sign-in already in progress; ignoring click");
        return;
    }

    // Lock UI + show progress
    oauthInProgress_ = true;
    isBusy_ = true;
    signInBtn_.setEnabled(false);
    statusLabel_.setText("Waiting for browser sign-in...", juce::dontSendNotification);
    statusLabel_.setColour(juce::Label::textColourId, juce::Colour::fromString("ff3b82f6"));
    progressLabel_.setText("Check your default browser", juce::dontSendNotification);
    startTimer(400);

    service_.startBrowserAuthentication(
        [this](bool success, const std::string& message) {
            juce::String msg = juce::String(message);
            juce::MessageManager::callAsync([this, success, msg]() {
                juce::Logger::writeToLog(juce::String("[LicenseDialog] OAuth completed success=")
                                         + (success ? "true" : "false") + " msg=" + msg);

                oauthInProgress_ = false;
                isBusy_ = false;
                stopTimer();
                signInBtn_.setEnabled(true);
                progressLabel_.setText({}, juce::dontSendNotification);

                if (success) {
                    updateStatusView(LicenseSystemService::LicenseStatus::Valid,
                                     msg.isEmpty() ? juce::String("Signed in") : msg);
                    if (onValidated) onValidated();
                } else {
                    updateStatusView(service_.getCurrentStatus(),
                                     msg.isEmpty() ? juce::String("Sign-in failed or cancelled.") : msg);
                }
            });
        });
}

void LicenseActivationDialog::openUrl(const juce::String& url)
{
    juce::URL(url).launchInDefaultBrowser();
}

void LicenseActivationDialog::timerCallback()
{
    if (!isBusy_) {
        stopTimer();
        return;
    }
    static const juce::String dots[] { ".", "..", "..." };
    static int index = 0;
    progressLabel_.setText("Check your default browser" + dots[index],
                           juce::dontSendNotification);
    index = (index + 1) % 3;
}

void LicenseActivationDialog::handleStatusEvent(const juce::var& payload)
{
    auto statusVar  = payload.getProperty("status", 0);
    auto messageVar = payload.getProperty("message", juce::var());
    auto status     = static_cast<LicenseSystemService::LicenseStatus>(static_cast<int>(statusVar));
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
    switch (status) {
        case LicenseSystemService::LicenseStatus::Valid:
            isBusy_ = false;
            progressLabel_.setText("", juce::dontSendNotification);
            stopTimer();
            if (onValidated) onValidated();
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
    switch (status) {
        case Status::Valid:                   return "Signed in";
        case Status::Expired:                 return "License expired. Please renew at samplemonsta.com.";
        case Status::HardwareMismatch:        return "Registered to another machine. Contact support.";
        case Status::ActivationLimitExceeded: return "Activation limit exceeded. Contact support.";
        case Status::InvalidKey:              return "Sign-in failed. Please try again.";
        case Status::NetworkError:            return "Network error. Check your internet connection.";
        case Status::GracePeriodActive:       return "Offline — running in grace period.";
        case Status::GracePeriodExpired:      return "Offline grace expired. Please sign in again.";
        case Status::UserLockedOut:           return "Account locked. Contact support.";
        case Status::NotInitialised: default: return "";
    }
}

} // namespace samplar
