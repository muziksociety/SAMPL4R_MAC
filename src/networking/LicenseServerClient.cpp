#include "LicenseServerClient.h"
#include "../security/LicenseSignature.h"

#include <juce_core/juce_core.h>

namespace samplar {

// ---- CancellableHttpPost ----------------------------------------------------

CancellableHttpPost::Response CancellableHttpPost::post (const juce::URL& url,
                                                         const juce::String& extraHeaders,
                                                         int timeoutMs)
{
    Response r;
    if (cancelled_.load())
    {
        juce::Logger::writeToLog ("[License] http post skipped - cancelled");
        return r;
    }

    juce::WebInputStream stream (url, false);   // body = url's POST data
    stream.withExtraHeaders (extraHeaders)
          .withConnectionTimeout (timeoutMs);
    {
        std::scoped_lock lock (mutex_);
        if (cancelled_.load())
            return r;
        active_ = &stream;
    }

    const bool connected = stream.connect (nullptr);
    if (connected && ! cancelled_.load())
    {
        r.statusCode = stream.getStatusCode();
        // StringPairArray keys compare case-insensitively ("retry-after" too).
        r.retryAfterSeconds = LicenseServerClient::parseRetryAfter (stream.getResponseHeaders()["Retry-After"]);
        r.body = stream.readEntireStreamAsString();
        r.connected = ! cancelled_.load();
    }

    {
        std::scoped_lock lock (mutex_);
        active_ = nullptr;
    }

    juce::Logger::writeToLog ("[License] http post done - connected: " + juce::String (r.connected ? "yes" : "no")
        + ", status " + juce::String (r.statusCode) + ", body " + juce::String (r.body.length()) + " chars"
        + (r.retryAfterSeconds > 0 ? ", Retry-After " + juce::String (r.retryAfterSeconds) + " s" : juce::String())
        + (cancelled_.load() ? " (cancelled)" : ""));
    return r;
}

void CancellableHttpPost::cancel()
{
    cancelled_ = true;
    std::scoped_lock lock (mutex_);
    if (active_ != nullptr)
    {
        juce::Logger::writeToLog ("[License] http post: cancelling in-flight request");
        active_->cancel();
    }
}

// ---- LicenseServerClient ----------------------------------------------------

int LicenseServerClient::parseRetryAfter (const juce::String& headerValue)
{
    const auto v = headerValue.trim();
    if (v.isEmpty() || ! v.containsOnly ("0123456789") || v.length() > 9)
        return 0;   // absent, HTTP-date or garbage: the normal backoff applies
    return juce::jlimit (0, kMaxRetryAfterSeconds, v.getIntValue());
}

bool LicenseServerClient::isValidMachineIdV2 (const juce::String& id)
{
    return id.length() == 32 && id.containsOnly ("0123456789abcdefABCDEF");
}

LicenseServerClient::RevalResult LicenseServerClient::interpretRevalidate (int statusCode, const juce::String& body,
                                                                          int retryAfterSeconds)
{
    RevalResult r;
    r.statusCode = statusCode;
    r.retryAfterSeconds = juce::jlimit (0, kMaxRetryAfterSeconds, retryAfterSeconds);
    const auto json = juce::JSON::parse (body);
    if (json.isObject())   // for the log / tests; 5xx below is transient whatever it says
        r.reason = json.getProperty ("reason", "").toString().trim().toLowerCase();

    if (statusCode == 401 || statusCode == 403)   { r.outcome = RevalOutcome::AuthRequired; return r; }
    // 503 + Retry-After (reason service_unavailable), any 5xx, 429: transient,
    // never signed, never a verdict. The licence stays inside its offline window.
    if (statusCode >= 500 || statusCode == 429)   { r.outcome = RevalOutcome::NetworkError; return r; }

    // No JSON (CDN / proxy error page, a route missing during a deploy - any
    // status, 404 included) is never a verdict.
    if (! json.isObject())
    {
        r.outcome = RevalOutcome::NetworkError;
        return r;
    }

    r.licenseType        = json.getProperty ("license_type", "").toString();
    r.expiresAt          = json.getProperty ("expires_at", "").toString();
    r.hardwareRegistered = json.getProperty ("hardware_registered", false);
    r.licensePayload     = json.getProperty ("license_payload", "").toString();
    r.signature          = json.getProperty ("signature_v2", "").toString();   // NOT "signature" (old v1)
    r.denialPayload      = json.getProperty ("denial_payload", "").toString();
    r.denialSignature    = json.getProperty ("denial_signature", "").toString();
    r.accessToken        = json.getProperty ("access_token", "").toString();   // rolled on every valid re-check
    const bool success   = json.getProperty ("success", false);
    const bool valid     = json.getProperty ("license_valid", false);

    static const juce::StringArray tokenReasons  { "token_missing", "token_invalid", "token_too_old" };
    static const juce::StringArray denialReasons { "revoked", "expired", "inactive", "unbound", "blocked" };

    if (statusCode < 400 && success && valid)
        r.outcome = RevalOutcome::Valid;
    else if (tokenReasons.contains (r.reason))
        r.outcome = RevalOutcome::AuthRequired;
    else if (denialReasons.contains (r.reason) || (statusCode < 400 && success && json.hasProperty ("license_valid")))
        r.outcome = RevalOutcome::Denied;         // acted on only with a verified signed denial
    else
        r.outcome = RevalOutcome::NetworkError;   // service_unavailable, bad_request, unknown
    return r;
}

LicenseServerClient::RevalResult LicenseServerClient::revalidateWithToken (CancellableHttpPost& http,
                                                                          const std::string& serverBaseUrl,
                                                                          const std::string& accessToken,
                                                                          const std::string& machineId)
{
    juce::Logger::writeToLog ("[License] === revalidateWithToken START === machine id: " +
        juce::String (machineId.empty() ? "(none)" : "present"));

    if (! isValidMachineIdV2 (juce::String (machineId)))
    {
        // SAMPL4R refuses a missing / invalid v2 ID (no v1 fallback): don't ask.
        juce::Logger::writeToLog ("[License] revalidate: no valid v2 machine id (32 hex) -> not sent, NetworkError");
        return {};
    }

    juce::var payload (new juce::DynamicObject());
    auto* o = payload.getDynamicObject();
    // One stable ID (HWID v2). The server binds hardware_id_v2 and compares IDs
    // case-insensitively; SAMPLAR has no legacy (v1) bindings to migrate.
    o->setProperty ("hardware_id", juce::String (machineId));
    o->setProperty ("hardware_id_v2", juce::String (machineId));
    o->setProperty ("product_id", "SAMPL4R");   // server filters licences by key prefix
    const auto nonce = LicenseSignature::makeNonce();   // echoed inside the signed reply / denial
    o->setProperty ("nonce", nonce);

    const juce::URL url = juce::URL (juce::String (serverBaseUrl) + "/auth/revalidate")
                              .withPOSTData (juce::JSON::toString (payload, true));
    // The bearer token is a secret: it goes in the header only, never in a log line.
    const juce::String headers = "Content-Type: application/json\r\nAccept: application/json\r\n"
                                 "Authorization: Bearer " + juce::String (accessToken) + "\r\n";

    const auto response = http.post (url, headers, 30000);   // 30 s: Render cold start
    if (! response.connected)
    {
        juce::Logger::writeToLog ("[License] revalidate: no connection -> NetworkError");
        RevalResult r;
        r.requestNonce = nonce;
        return r;
    }

    auto r = interpretRevalidate (response.statusCode, response.body, response.retryAfterSeconds);
    r.requestNonce = nonce;
    juce::Logger::writeToLog ("[License] === revalidateWithToken END === status " + juce::String (response.statusCode) +
        ", outcome " + juce::String ((int) r.outcome) + ", reason '" + r.reason + "'" +
        ", Retry-After " + juce::String (r.retryAfterSeconds) + " s" +
        ", signed payload: " + (r.licensePayload.isEmpty() ? "no" : "yes") +
        ", signed denial: " + (r.denialPayload.isEmpty() ? "no" : "yes"));
    return r;
}

} // namespace samplar
