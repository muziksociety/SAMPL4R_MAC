#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <functional>
#include <memory>
#include <atomic>

namespace samplar {

/**
 * OAuth Browser Authenticator
 *
 * Implements secure OAuth-style browser authentication flow:
 * 1. Starts local HTTP server on random port
 * 2. Opens browser to server login page with callback URL
 * 3. Server redirects back to localhost with ONLY auth code (no sensitive data in URL)
 * 4. Client exchanges auth code for license data via secure server-to-server call
 * 5. Returns license info to the app
 *
 * Security: Sensitive data (tokens, license keys) never appear in browser URL.
 */
class OAuthBrowserAuthenticator : public juce::Thread
{
public:
    // Authentication result
    struct AuthResult
    {
        bool success = false;
        std::string errorMessage;

        // License data from server
        std::string accessToken;
        std::string licenseKey;
        std::string email;
        std::string licenseType;
        std::string expiresAt;
        std::string hardwareId;
        std::string signature;
        std::string issuedAt;

        // Full license info as juce::var for caching
        juce::var licenseInfo;
    };

    using CompletionCallback = std::function<void(const AuthResult&)>;

    OAuthBrowserAuthenticator();
    ~OAuthBrowserAuthenticator() override;

    /**
     * Start the OAuth authentication flow.
     *
     * @param serverBaseUrl  Base URL of the license server (e.g., "https://samplemonsta.com")
     * @param hardwareId     The machine's hardware ID for license binding
     * @param callback       Called when authentication completes (on message thread)
     */
    void startAuthentication(const std::string& serverBaseUrl,
                            const std::string& hardwareId,
                            CompletionCallback callback);

    /**
     * Cancel an ongoing authentication.
     */
    void cancelAuthentication();

    /**
     * Check if authentication is in progress.
     */
    bool isAuthenticating() const { return isRunning_; }

private:
    void run() override;

    // HTTP callback server
    bool startCallbackServer();
    void stopCallbackServer();
    std::string waitForCallback(int timeoutMs);

    // Exchange authorization code for license data (secure server-to-server call)
    AuthResult exchangeCodeForToken(const std::string& authCode);

    // Generate random state token for CSRF protection
    std::string generateStateToken();

    // URL encoding helper
    static std::string urlEncode(const std::string& value);

    // Configuration
    std::string serverBaseUrl_;
    std::string hardwareId_;
    std::string stateToken_;
    int callbackPort_ = 0;

    // Callback
    CompletionCallback completionCallback_;

    // Server socket
    std::unique_ptr<juce::StreamingSocket> serverSocket_;
    std::atomic<bool> isRunning_{false};
    std::atomic<bool> shouldStop_{false};
    std::atomic<bool> callbackReceived_{false};
    juce::String callbackData_;

    // Was 60s. There is no portable way to detect that the user closed the
    // browser tab/window the OS opened for us (launchInDefaultBrowser hands
    // off and we get no handle back). Shortened so an abandoned auth bails
    // quickly and the user can retry from the dialog without waiting a full
    // minute. Proper long-term fix is a JS heartbeat from the auth page back
    // to our localhost listener on callbackPort_.
    static constexpr int AUTH_TIMEOUT_MS = 20000;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OAuthBrowserAuthenticator)
};

} // namespace samplar
