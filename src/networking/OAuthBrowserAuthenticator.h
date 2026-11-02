#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace samplar {

class CancellableHttpPost;

/**
 * OAuth browser sign-in (Authorization Code flow with PKCE, RFC 8252 / 7636).
 * Ported from SM5 (894f80ec, 042b7812).
 *
 * 1. Listens on 127.0.0.1 (loopback only) for the redirect.
 * 2. Opens the browser on /auth/app-login with state + PKCE challenge.
 * 3. The server redirects back with ONLY ?code=&state= (no secrets in the URL).
 * 4. The code (+ PKCE verifier + nonce) is exchanged over HTTPS at
 *    /auth/token-exchange for the licence data and its server signature.
 * 5. The result is delivered on the message thread.
 *
 * Lifetime: owned by LicenseSystemService. cancelAuthentication() (also run by
 * the destructor) aborts the network call and JOINS the thread.
 */
class OAuthBrowserAuthenticator : public juce::Thread
{
public:
    struct AuthResult
    {
        bool success = false;
        std::string errorMessage;

        // License data from /auth/token-exchange
        std::string accessToken;     // sign-in token, used for /auth/revalidate
        std::string licenseKey;
        std::string email;
        std::string licenseType;
        std::string expiresAt;
        std::string hardwareId;
        std::string issuedAt;
        std::string licensePayload;  // exact server-signed text (payload v2)
        std::string signatureV2;     // base64 RSA-PSS/SHA-256 over licensePayload
        std::string requestNonce;    // nonce we sent; must come back inside licensePayload

        // Plain licence fields for caching (never trusted for activation on their own)
        juce::var licenseInfo;
    };

    using CompletionCallback = std::function<void(const AuthResult&)>;

    OAuthBrowserAuthenticator();
    ~OAuthBrowserAuthenticator() override;

    /** Start the flow. `callback` runs on the message thread; it must not
        capture anything that can die before it runs without a liveness guard. */
    void startAuthentication(const std::string& serverBaseUrl,
                             const std::string& hardwareId,
                             CompletionCallback callback);

    /** Abort and JOIN (the browser wait, the token exchange). Shutdown /
        destructor only. Every wait in run() is cancellable, so this returns
        within ~0.1 s; it never force-kills the thread. */
    void cancelAuthentication();

    /** Ask the running sign-in to stop, without waiting (UI "Cancel"). It
        finishes on its own and reports "Sign-in cancelled". */
    void requestCancel();

    bool isAuthenticating() const { return isRunning_; }

    /** requestCancel() was called for the current run (it may still be closing). */
    bool isCancelRequested() const { return shouldStop_; }

    /** How long the user has to finish signing in (account creation and
        password resets take a while; Render can cold-start). */
    static constexpr int kAuthTimeoutMs = 5 * 60 * 1000;

private:
    void run() override;

    // Deliver the result on the message thread without capturing `this`.
    void postResult(const AuthResult& result);

    bool startCallbackServer();
    void stopCallbackServer();
    std::string waitForCallback(int timeoutMs);

    AuthResult exchangeCodeForToken(const std::string& authCode);

    // 32 CSPRNG bytes as 64 hex chars (state and PKCE verifier).
    static std::string generateRandomToken();
    static std::string urlEncode(const std::string& value);

    std::string serverBaseUrl_;
    std::string hardwareId_;
    std::string hardwareIdV2_;                 // auth thread only after start
    std::string stateToken_;
    std::string codeVerifier_;                 // PKCE: never in the browser URL
    int callbackPort_ = 0;

    CompletionCallback completionCallback_;
    std::shared_ptr<CancellableHttpPost> http_;   // fresh per run; cancel() aborts the exchange

    std::unique_ptr<juce::StreamingSocket> serverSocket_;
    std::atomic<bool> isRunning_{false};
    std::atomic<bool> shouldStop_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OAuthBrowserAuthenticator)
};

} // namespace samplar
