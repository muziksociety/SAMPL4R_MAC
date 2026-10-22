#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include <juce_core/juce_core.h>

namespace samplar {

/**
 * One blocking HTTP POST that another thread can abort (shutdown, Cancel).
 * Plugin threads must be joinable: a request stuck in a 30 s connect would
 * otherwise outlive the plugin. cancel() aborts the in-flight request and
 * makes every later post() return at once. Thread-safe.
 */
class CancellableHttpPost
{
public:
    struct Response
    {
        bool connected = false;   // false = no connection / cancelled
        int statusCode = 0;
        juce::String body;
        int retryAfterSeconds = 0;   // "Retry-After: <seconds>" (0 = absent / HTTP-date / invalid)
    };

    /** BACKGROUND THREAD ONLY. `url` carries the POST body (withPOSTData). */
    Response post (const juce::URL& url, const juce::String& extraHeaders, int timeoutMs);

    /** Any thread. Idempotent. */
    void cancel();

    bool isCancelled() const { return cancelled_.load(); }

private:
    std::mutex mutex_;
    juce::WebInputStream* active_ = nullptr;   // guarded by mutex_
    std::atomic<bool> cancelled_ { false };
};

/**
 * License server calls. Activation is OAuth-only (OAuthBrowserAuthenticator,
 * PKCE code flow + /auth/token-exchange). The old /api/licenses/validate-token,
 * /auth/login-with-hardware and /api/licenses/deactivate paths were removed:
 * their HMAC token was forgeable and the deactivate route never existed.
 * Machine release is done in the account portal.
 */
class LicenseServerClient
{
public:
    // ---- Periodic re-check: POST /auth/revalidate (Bearer access_token) ----
    // Contract: license_server docs/license_signing_v2.md. Every non-valid reply
    // carries a machine-readable `reason`. Only a SIGNED denial may wipe the
    // cached licence; everything else is transient or "sign in again".
    enum class RevalOutcome
    {
        Valid,          // license active (still needs a valid signature to count)
        Denied,         // the server says "not valid" (reason revoked / expired /
                        // inactive / unbound / blocked). Acted on ONLY when the
                        // denial payload verifies (LicenseSignature::verifyDenial);
                        // an unsigned denial counts as a transient failure.
        AuthRequired,   // token rejected (401/403, reason token_*) -> sign in again;
                        // the cached licence stays usable inside its offline window
        NetworkError    // unreachable / 5xx / 429 / unparsable / unknown reply -> transient
    };

    struct RevalResult
    {
        RevalOutcome outcome = RevalOutcome::NetworkError;
        juce::String reason;                           // server's machine-readable reason (may be empty)
        juce::String licenseType, expiresAt;
        juce::String licensePayload, signature;        // fresh server-signed data (field signature_v2)
        juce::String denialPayload, denialSignature;   // signed denial (denial_payload / denial_signature)
        juce::String requestNonce;                     // what we sent; must be inside the signed payload
        juce::String accessToken;                      // fresh sign-in token on a valid reply (server rolls it)
        bool hardwareRegistered = false;
        int statusCode = 0;
        int retryAfterSeconds = 0;                     // server's Retry-After (503 service_unavailable: 60)
    };

    /** Pure: map the server reply to an outcome. `retryAfterSeconds` is the
        parsed Retry-After header (0 = none); it is copied into the result. */
    static RevalResult interpretRevalidate (int statusCode, const juce::String& body, int retryAfterSeconds = 0);

    /** Pure: "Retry-After" header value -> seconds. Only the delta-seconds form
        is honoured (the server sends "60"); an HTTP-date, garbage or a negative
        value gives 0. Capped at kMaxRetryAfterSeconds. */
    static int parseRetryAfter (const juce::String& headerValue);
    static constexpr int kMaxRetryAfterSeconds = 60 * 60;

    /** SAMPL4R requires a v2 machine ID: exactly 32 hex chars (the server
        refuses anything else on sign-in, re-check and key paste). */
    static bool isValidMachineIdV2 (const juce::String& id);

    /** Blocking network call - BACKGROUND THREAD ONLY. Touches no instance state.
        `machineId` is this machine's licence ID (HWID v2); it is sent as both
        hardware_id and hardware_id_v2 (SAMPLAR has no legacy v1 bindings). */
    static RevalResult revalidateWithToken (CancellableHttpPost& http,
                                            const std::string& serverBaseUrl,
                                            const std::string& accessToken,
                                            const std::string& machineId);
};

} // namespace samplar
