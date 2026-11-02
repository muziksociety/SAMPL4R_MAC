#include "LicenseActivationDialog.h"
#include "LicenseGateOverlay.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace samplar {

namespace {
constexpr int kDialogWidth  = 480;
constexpr int kDialogHeight = 440;
constexpr int kButtonHeight = 48;
constexpr int kMargin       = 36;
constexpr int kSpacing      = 12;

const juce::Colour kAccent      { 0xff3b82f6 };
const juce::Colour kAccentDown  { 0xff3367d6 };
const juce::Colour kNeutral     { 0xff374151 };
const juce::Colour kOk          { 0xff22c55e };
const juce::Colour kError       { 0xfff87171 };
const juce::Colour kMuted       { 0xff94a3b8 };
const juce::Colour kLink        { 0xff60a5fa };
}

// Standalone-app DialogWindow: closing is refused while the browser sign-in runs.
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
            if (content->isOAuthInProgress())
                return;   // finish or cancel the sign-in first
        exitModalState(0);
    }
};

LicenseActivationDialog::LicenseActivationDialog(LicenseSystemService& service, EventBus& bus)
    : service_(service), bus_(bus)
{
    juce::Logger::writeToLog("[LicenseDialog] constructed");
    setOpaque(true);

    titleLabel_.setText("SAMPLAR", juce::dontSendNotification);
    titleLabel_.setJustificationType(juce::Justification::centred);
    titleLabel_.setFont(juce::Font(32.0f, juce::Font::bold));
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel_);

    subtitleLabel_.setText("Sign in to activate SAMPLAR", juce::dontSendNotification);
    subtitleLabel_.setJustificationType(juce::Justification::centred);
    subtitleLabel_.setFont(juce::Font(16.0f));
    subtitleLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(subtitleLabel_);

    helpLabel_.setText("Sign in with your account in your web browser. "
                       "SAMPLAR unlocks by itself when you are done.",
                       juce::dontSendNotification);
    helpLabel_.setJustificationType(juce::Justification::centred);
    helpLabel_.setFont(juce::Font(13.0f));
    helpLabel_.setColour(juce::Label::textColourId, kMuted);
    addAndMakeVisible(helpLabel_);

    statusLabel_.setJustificationType(juce::Justification::centred);
    statusLabel_.setFont(juce::Font(14.0f));
    statusLabel_.setMinimumHorizontalScale(0.8f);
    addAndMakeVisible(statusLabel_);

    progressLabel_.setJustificationType(juce::Justification::centred);
    progressLabel_.setColour(juce::Label::textColourId, kMuted);
    progressLabel_.setFont(juce::Font(13.0f));
    addAndMakeVisible(progressLabel_);

    signInBtn_.setColour(juce::TextButton::buttonColourId, kAccent);
    signInBtn_.setColour(juce::TextButton::buttonOnColourId, kAccentDown);
    signInBtn_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    signInBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    signInBtn_.onClick = [this] { onPrimaryClicked(); };
    addAndMakeVisible(signInBtn_);

    for (auto* b : { &tryAgainBtn_, &dismissBtn_ })
    {
        b->setColour(juce::TextButton::buttonColourId, kNeutral);
        b->setColour(juce::TextButton::buttonOnColourId, kNeutral.brighter(0.15f));
        b->setColour(juce::TextButton::textColourOnId, juce::Colours::lightgrey);
        b->setColour(juce::TextButton::textColourOffId, juce::Colours::lightgrey);
        addAndMakeVisible(*b);
    }
    tryAgainBtn_.setTooltip("Check your license online again (after reconnecting to the internet)");
    tryAgainBtn_.onClick = [this] { onTryAgainClicked(); };
    dismissBtn_.onClick = [this] { onDismissClicked(); };
    dismissBtn_.setVisible(false);   // shown only when the host gives it a meaning

    for (auto* link : { &createAccountLink_, &forgotLink_, &buyLink_, &devicesLink_ })
    {
        link->setFont(juce::Font(12.0f), false, juce::Justification::centred);
        link->setColour(juce::HyperlinkButton::textColourId, kLink);
        addAndMakeVisible(*link);
    }

    // license.status may be published from any thread: hop to the message
    // thread, and only if this dialog still exists.
    juce::Component::SafePointer<LicenseActivationDialog> safe(this);
    statusSubscription_ = bus_.subscribe("license.status", [safe](const juce::var& payload) {
        juce::MessageManager::callAsync([safe, payload]() {
            if (safe != nullptr)
                safe->handleStatusEvent(payload);
        });
    });

    updateStatusView(service_.getCurrentStatus(), juce::String(service_.getLastResultMessage()));
    setSize(kDialogWidth, kDialogHeight);
}

LicenseActivationDialog::~LicenseActivationDialog()
{
    // A browser sign-in still running is NOT cancelled: the service owns it and
    // still activates SAMPLAR if the user finishes in the browser.
    stopTimer();
    if (statusSubscription_ != 0)
        bus_.unsubscribe("license.status", statusSubscription_);
    juce::Logger::writeToLog("[LicenseDialog] destroyed");
}

void LicenseActivationDialog::setDismissButtonText(const juce::String& text)
{
    dismissBtn_.setButtonText(text);
    dismissBtn_.setVisible(text.isNotEmpty());
    resized();
}

int LicenseActivationDialog::showModal(LicenseSystemService& service, EventBus& bus)
{
    juce::Logger::writeToLog("[LicenseDialog] === showModal START (standalone app) ===");
    if (service.getCurrentStatus() == LicenseSystemService::LicenseStatus::Valid)
    {
        juce::Logger::writeToLog("[LicenseDialog] showModal: already licensed - nothing to show");
        return 1;
    }

    auto* content = new LicenseActivationDialog(service, bus);
    content->setDismissButtonText("Quit");

    auto dialogWindow = std::make_unique<LicenseDialogWindow>("Sign in to SAMPLAR",
                                                              juce::Colours::darkgrey.darker(0.4f));
    dialogWindow->setUsingNativeTitleBar(false);
    dialogWindow->setResizable(false, false);
    dialogWindow->setContentOwned(content, true);
    dialogWindow->centreWithSize(dialogWindow->getWidth(), dialogWindow->getHeight());

    auto* window = dialogWindow.get();
    content->onValidated = [window]() { window->exitModalState(1); };
    content->onQuit      = [window]() { window->exitModalState(0); };

   #if JUCE_MODAL_LOOPS_PERMITTED
    dialogWindow->setVisible(true);
    const int result = dialogWindow->runModalLoop();
    juce::Logger::writeToLog("[LicenseDialog] === showModal END === result " + juce::String(result));
    return result;
   #else
    juce::Logger::writeToLog("[LicenseDialog] showModal: modal loops not permitted in this build - use showInWindow");
    return 0;
   #endif
}

void LicenseActivationDialog::showInWindow(juce::Component* from, LicenseSystemService& service, EventBus& bus,
                                           bool allowClose, std::function<void(int result)> onComplete)
{
    LicenseGateOverlay::showInWindow(from, service, bus, allowClose, std::move(onComplete));
}

void LicenseActivationDialog::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    juce::ColourGradient gradient(juce::Colour(0xff0f172a), bounds.getTopLeft(),
                                  juce::Colour(0xff1e293b), bounds.getBottomRight(), false);
    g.setGradientFill(gradient);
    g.fillRect(bounds);

    auto card = bounds.reduced(14.0f);
    g.setColour(juce::Colour(0xff161b2e));
    g.fillRoundedRectangle(card, 14.0f);
    g.setColour(kAccent.withAlpha(0.25f));
    g.drawRoundedRectangle(card, 14.0f, 1.5f);
}

void LicenseActivationDialog::resized()
{
    auto bounds = getLocalBounds().reduced(kMargin, kMargin - 8);

    titleLabel_.setBounds(bounds.removeFromTop(36));
    bounds.removeFromTop(kSpacing / 2);
    subtitleLabel_.setBounds(bounds.removeFromTop(22));
    bounds.removeFromTop(kSpacing / 2);
    helpLabel_.setBounds(bounds.removeFromTop(36));
    bounds.removeFromTop(kSpacing);

    signInBtn_.setBounds(bounds.removeFromTop(kButtonHeight));
    bounds.removeFromTop(kSpacing);
    tryAgainBtn_.setBounds(bounds.removeFromTop(30).withSizeKeepingCentre(140, 30));
    bounds.removeFromTop(kSpacing);

    statusLabel_.setBounds(bounds.removeFromTop(40));
    progressLabel_.setBounds(bounds.removeFromTop(18));
    bounds.removeFromTop(kSpacing);

    auto linksRow = bounds.removeFromTop(20);
    const int quarter = linksRow.getWidth() / 4;
    createAccountLink_.setBounds(linksRow.removeFromLeft(quarter));
    forgotLink_.setBounds(linksRow.removeFromLeft(quarter));
    buyLink_.setBounds(linksRow.removeFromLeft(quarter));
    devicesLink_.setBounds(linksRow);

    if (dismissBtn_.isVisible())
        dismissBtn_.setBounds(bounds.removeFromBottom(30).removeFromRight(100));
}

void LicenseActivationDialog::onPrimaryClicked()
{
    if (oauthInProgress_)
    {
        juce::Logger::writeToLog("[LicenseDialog] Cancel sign-in clicked");
        ++signInRun_;                              // its late "cancelled" result is ignored
        service_.cancelBrowserAuthentication();    // non-blocking
        setInProgress(false);
        updateStatusView(service_.getCurrentStatus(), "Sign-in cancelled.");
        return;
    }

    juce::Logger::writeToLog("[LicenseDialog] Sign in with browser clicked");
    const int run = ++signInRun_;
    setInProgress(true);
    statusLabel_.setText("Waiting for you to sign in in your browser...", juce::dontSendNotification);
    statusLabel_.setColour(juce::Label::textColourId, kAccent);

    // The service calls back on the message thread; this dialog may be gone.
    juce::Component::SafePointer<LicenseActivationDialog> safe(this);
    service_.startBrowserAuthentication([safe, run](bool success, const std::string& message) {
        if (safe == nullptr)
            return;
        juce::Logger::writeToLog(juce::String("[LicenseDialog] Sign-in finished - success: ") + (success ? "yes" : "no"));
        if (run != safe->signInRun_ && !success)
            return;   // an older / cancelled sign-in; the current state stays
        if (success)
            safe->signInRun_++;   // no other run is pending any more
        safe->setInProgress(false);
        if (success)
        {
            safe->updateStatusView(LicenseSystemService::LicenseStatus::Valid, juce::String(message));
            safe->notifyValidated();   // may delete the dialog: nothing after this
            return;
        }
        safe->updateStatusView(safe->service_.getCurrentStatus(),
                               message.empty() ? juce::String("Sign-in failed or was cancelled.") : juce::String(message));
    });
}

void LicenseActivationDialog::onTryAgainClicked()
{
    juce::Logger::writeToLog("[LicenseDialog] Try again clicked");
    statusLabel_.setText("Checking your license...", juce::dontSendNotification);
    statusLabel_.setColour(juce::Label::textColourId, kAccent);
    service_.recheckNow();   // background; the result arrives as license.status
}

void LicenseActivationDialog::onDismissClicked()
{
    juce::Logger::writeToLog("[LicenseDialog] Dismiss (" + dismissBtn_.getButtonText() + ") clicked");
    if (oauthInProgress_)
    {
        service_.cancelBrowserAuthentication();
        setInProgress(false);
    }
    if (auto cb = onQuit)   // copy: the handler may delete this dialog
        cb();
}

void LicenseActivationDialog::notifyValidated()
{
    if (validatedNotified_)
        return;
    validatedNotified_ = true;
    juce::Logger::writeToLog("[LicenseDialog] licence valid -> onValidated");
    if (auto cb = onValidated)   // copy: the handler may delete this dialog
        cb();
}

void LicenseActivationDialog::setInProgress(bool inProgress)
{
    oauthInProgress_ = inProgress;
    signInBtn_.setButtonText(inProgress ? "Cancel sign-in" : "Sign in with browser");
    tryAgainBtn_.setEnabled(!inProgress);
    if (inProgress)
    {
        dotsIndex_ = 0;
        progressLabel_.setText("Check your web browser", juce::dontSendNotification);
        startTimer(400);
    }
    else
    {
        stopTimer();
        progressLabel_.setText({}, juce::dontSendNotification);
    }
}

void LicenseActivationDialog::timerCallback()
{
    if (!oauthInProgress_)
    {
        stopTimer();
        return;
    }
    static const char* const dots[] { ".", "..", "..." };
    progressLabel_.setText(juce::String("Check your web browser") + dots[dotsIndex_], juce::dontSendNotification);
    dotsIndex_ = (dotsIndex_ + 1) % 3;
}

void LicenseActivationDialog::handleStatusEvent(const juce::var& payload)
{
    const auto status = static_cast<LicenseSystemService::LicenseStatus>(static_cast<int>(payload.getProperty("status", 0)));
    const auto message = payload.getProperty("message", juce::var()).toString();
    juce::Logger::writeToLog("[LicenseDialog] license.status " + payload.getProperty("statusName", "").toString() +
        " (" + payload.getProperty("reason", "").toString() + ")");

    // Valid + signInRequired (token_* reply): the licence still plays offline,
    // but this panel exists to get a fresh sign-in - it is not "done" yet.
    const bool done = status == LicenseSystemService::LicenseStatus::Valid
                      && ! (bool) payload.getProperty("signInRequired", false);
    if (oauthInProgress_ && ! done)
        return;   // keep "waiting for browser"; the sign-in callback reports the outcome

    updateStatusView(status, message);
    if (done)
    {
        setInProgress(false);
        notifyValidated();   // may delete the dialog: nothing after this
    }
}

void LicenseActivationDialog::updateStatusView(LicenseSystemService::LicenseStatus status, const juce::String& message)
{
    juce::String statusText = statusToFriendlyString(status);
    if (message.isNotEmpty() && status != LicenseSystemService::LicenseStatus::NotInitialised)
        statusText = message;
    statusLabel_.setText(statusText, juce::dontSendNotification);

    auto colour = juce::Colours::orange;
    if (status == LicenseSystemService::LicenseStatus::Valid)
        colour = kOk;
    else if (status == LicenseSystemService::LicenseStatus::NetworkError)
        colour = kAccent;
    else if (status == LicenseSystemService::LicenseStatus::InvalidKey
             || status == LicenseSystemService::LicenseStatus::HardwareMismatch
             || status == LicenseSystemService::LicenseStatus::Expired)
        colour = kError;
    else if (status == LicenseSystemService::LicenseStatus::NotInitialised)
        colour = juce::Colours::lightgrey;
    statusLabel_.setColour(juce::Label::textColourId, colour);
}

juce::String LicenseActivationDialog::statusToFriendlyString(LicenseSystemService::LicenseStatus status) const
{
    using Status = LicenseSystemService::LicenseStatus;
    switch (status) {
        case Status::Valid:                   return "Signed in";
        case Status::Expired:                 return "Your SAMPLAR license has expired.";
        case Status::HardwareMismatch:        return "This license is registered to another computer. Use Manage devices.";
        case Status::ActivationLimitExceeded: return "All activations are in use. Use Manage devices to free one.";
        case Status::InvalidKey:              return "Sign in to activate SAMPLAR.";
        case Status::NetworkError:            return "Network error. Check your internet connection.";
        case Status::GracePeriodActive:       return "Offline - your license is still valid for now.";
        case Status::GracePeriodExpired:      return "Please connect to the internet and sign in again.";
        case Status::UserLockedOut:           return "Too many attempts. Try again later.";
        case Status::NotInitialised: default: return "";
    }
}

} // namespace samplar
