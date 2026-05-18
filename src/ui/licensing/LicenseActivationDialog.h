#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

#include "../../services/LicenseSystemService.h"
#include "../../app/EventBus.h"

namespace samplar {

/**
 * OAuth browser sign-in dialog.
 *
 * Primary (and only) sign-in path: clicking the big "Sign in with SampleMonsta"
 * button opens the user's default browser to the server's /auth/app-login
 * endpoint. The server redirects back to a local loopback callback caught by
 * OAuthBrowserAuthenticator, which delivers a signed_token + license info.
 *
 * No credentials are ever entered in-app — the server-side account auth is
 * browser-OAuth, not in-app email/password.
 */
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

    /** LicenseDialogWindow's modal guard depends on this — true while the
     *  user has an OAuth browser flow in progress so the window won't close. */
    bool isOAuthInProgress() const { return oauthInProgress_; }

private:
    void buttonClicked(juce::Button* button) override;
    void timerCallback() override;

    void handleStatusEvent(const juce::var& payload);
    void refreshStatusFromService();
    void updateStatusView(LicenseSystemService::LicenseStatus status, const juce::String& message);
    void updateProgressState(LicenseSystemService::LicenseStatus status);
    juce::String statusToFriendlyString(LicenseSystemService::LicenseStatus status) const;

    void startBrowserSignIn();
    static void openUrl(const juce::String& url);

    LicenseSystemService& service_;
    EventBus& bus_;
    EventBus::HandlerId statusSubscription_ { 0 };

    juce::Label titleLabel_;
    juce::Label subtitleLabel_;
    juce::Label helpLabel_;
    juce::Label statusLabel_;
    juce::Label progressLabel_;

    juce::TextButton  signInBtn_   { "Sign in with SampleMonsta" };
    juce::HyperlinkButton forgotLink_ { "Forgot password?",
                                        juce::URL("https://samplemonsta.com/forgot-password") };
    juce::HyperlinkButton signupLink_ { "Create account",
                                        juce::URL("https://samplemonsta.com/signup") };
    juce::TextButton  quitButton_   { "Quit" };

    juce::String lastStatusMessage_;
    LicenseSystemService::LicenseStatus lastStatus_ { LicenseSystemService::LicenseStatus::NotInitialised };
    bool isBusy_ { false };
    bool oauthInProgress_ { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LicenseActivationDialog)
};

} // namespace samplar
