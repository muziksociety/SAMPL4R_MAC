#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

#include "../../services/LicenseSystemService.h"
#include "../../app/EventBus.h"

namespace samplar {

/**
 * SAMPLAR sign-in panel (browser OAuth). Shown INSIDE the plugin / app window
 * by LicenseGateOverlay (no modal loop in the host), or inside a DialogWindow
 * by the settings page.
 *
 *   +------------------------------------------------+
 *   |                    SAMPLAR                     |
 *   |          Sign in to activate SAMPLAR           |
 *   |  Sign in with your account in your browser...  |
 *   |  [        Sign in with browser          ]      |
 *   |  [ Try again ]                                 |
 *   |  <status / reason from the licence service>    |
 *   |  Create account  Forgot password  Buy  Devices |
 *   |                                     [ Quit ]   |
 *   +------------------------------------------------+
 *
 * While the browser sign-in runs the primary button becomes "Cancel sign-in".
 * Message thread only. Async callbacks are guarded by SafePointer.
 */
class LicenseActivationDialog : public juce::Component,
                                private juce::Timer
{
public:
    LicenseActivationDialog(LicenseSystemService& service, EventBus& bus);
    ~LicenseActivationDialog() override;

    /** Standalone app startup only (JUCE_MODAL_LOOPS_PERMITTED). Never call it
     *  from a plugin: use showInWindow. Returns 1 when licensed, 0 on Quit. */
    static int showModal(LicenseSystemService& service, EventBus& bus);

    /** In-window sign-in panel over the window that contains `from`
     *  (non-blocking). onComplete(1) once licensed, onComplete(0) on Quit /
     *  Close. allowClose=false makes it a gate with a Quit button. */
    static void showInWindow(juce::Component* from, LicenseSystemService& service, EventBus& bus,
                             bool allowClose, std::function<void(int result)> onComplete);

    void resized() override;
    void paint(juce::Graphics& g) override;

    /** Fired once when the licence becomes valid. May delete this dialog. */
    std::function<void()> onValidated;
    /** Fired by the dismiss button (any OAuth in flight is cancelled first). May delete this dialog. */
    std::function<void()> onQuit;

    /** Text of the dismiss button ("Quit", "Close"); empty hides it (default). */
    void setDismissButtonText(const juce::String& text);

    bool isOAuthInProgress() const { return oauthInProgress_; }

private:
    void timerCallback() override;

    void handleStatusEvent(const juce::var& payload);
    void updateStatusView(LicenseSystemService::LicenseStatus status, const juce::String& message);
    juce::String statusToFriendlyString(LicenseSystemService::LicenseStatus status) const;
    void setInProgress(bool inProgress);
    void notifyValidated();

    void onPrimaryClicked();
    void onTryAgainClicked();
    void onDismissClicked();

    LicenseSystemService& service_;
    EventBus& bus_;
    EventBus::HandlerId statusSubscription_ { 0 };

    juce::Label titleLabel_;
    juce::Label subtitleLabel_;
    juce::Label helpLabel_;
    juce::Label statusLabel_;
    juce::Label progressLabel_;

    juce::TextButton signInBtn_   { "Sign in with browser" };
    juce::TextButton tryAgainBtn_ { "Try again" };
    juce::TextButton dismissBtn_;
    juce::HyperlinkButton createAccountLink_ { "Create account", juce::URL(LicenseSystemService::kCreateAccountUrl) };
    juce::HyperlinkButton forgotLink_        { "Forgot password", juce::URL(LicenseSystemService::kForgotPasswordUrl) };
    juce::HyperlinkButton buyLink_           { "Buy SAMPLAR", juce::URL(LicenseSystemService::kBuyUrl) };
    juce::HyperlinkButton devicesLink_       { "Manage devices", juce::URL(LicenseSystemService::kAccountLicensesUrl) };

    bool oauthInProgress_ { false };
    bool validatedNotified_ { false };
    int dotsIndex_ { 0 };
    int signInRun_ { 0 };   // results of a cancelled / older sign-in are ignored

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LicenseActivationDialog)
};

} // namespace samplar
