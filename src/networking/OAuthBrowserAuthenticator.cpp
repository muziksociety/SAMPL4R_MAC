#include "OAuthBrowserAuthenticator.h"
#include "LicenseServerClient.h"
#include "../Version.h"
#include "../security/LicenseSignature.h"

#include <juce_cryptography/juce_cryptography.h>

#if JUCE_WINDOWS
 #include <windows.h>
 #include <bcrypt.h>
 #pragma comment(lib, "bcrypt.lib")   // OS CSPRNG (BCryptGenRandom) for state + PKCE verifier
#endif

namespace samplar {

namespace {

constexpr int kFirstPort = 9876;
constexpr int kLastPort  = 9900;
constexpr int kJoinTimeoutMs = 5000;    // every wait in run() is cancellable; never force-kill
constexpr int kPollMs = 100;            // socket poll: how fast a cancel is noticed

// base64url without padding (RFC 4648 section 5), for the PKCE code_challenge.
std::string base64UrlNoPad(const void* data, size_t numBytes)
{
    juce::String b64 = juce::Base64::toBase64(data, numBytes);
    b64 = b64.replaceCharacter('+', '-').replaceCharacter('/', '_');
    while (b64.endsWithChar('='))
        b64 = b64.dropLastCharacters(1);
    return b64.toStdString();
}

juce::StringPairArray parseQuery(const juce::String& query)
{
    juce::StringPairArray params;
    juce::StringArray pairs;
    pairs.addTokens(query, "&", "");
    for (const auto& pair : pairs)
    {
        const int eqPos = pair.indexOf("=");
        if (eqPos > 0)
            params.set(pair.substring(0, eqPos), juce::URL::removeEscapeChars(pair.substring(eqPos + 1)));
    }
    return params;
}

juce::String maskEmail(const juce::String& email)
{
    const int at = email.indexOfChar('@');
    if (at <= 0)
        return email.isEmpty() ? juce::String("(empty)") : juce::String("***");
    return email.substring(0, juce::jmin(2, at)) + "***" + email.substring(at);
}

const char* kSuccessPage =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<!DOCTYPE html>"
    "<html><head><title>SAMPLAR</title>"
    "<style>"
    "body { font-family: 'Segoe UI', sans-serif; background: #050506; color: #fafafa; "
    "display: flex; align-items: center; justify-content: center; min-height: 100vh; margin: 0; }"
    ".container { text-align: center; padding: 2rem; }"
    ".icon { font-size: 4rem; color: #22c55e; }"
    "h1 { color: #22c55e; margin: 1rem 0; }"
    "p { color: #a1a1aa; }"
    "</style></head><body>"
    "<div class='container'>"
    "<div class='icon'>&#10004;</div>"
    "<h1>Sign-in received</h1>"
    "<p>You can close this window and return to SAMPLAR.</p>"
    "</div></body></html>";

const char* kNotFound =
    "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nNot Found";

const char* kBadState =
    "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n"
    "This sign-in link is out of date. Start the sign-in again from SAMPLAR.";

} // namespace

OAuthBrowserAuthenticator::OAuthBrowserAuthenticator()
    : juce::Thread("SamplarOAuth")
{
    juce::Logger::writeToLog("[License] OAuthBrowserAuthenticator created");
}

OAuthBrowserAuthenticator::~OAuthBrowserAuthenticator()
{
    cancelAuthentication();
    juce::Logger::writeToLog("[License] OAuthBrowserAuthenticator destroyed");
}

void OAuthBrowserAuthenticator::startAuthentication(const std::string& serverBaseUrl,
                                                    const std::string& hardwareId,
                                                    CompletionCallback callback)
{
    juce::Logger::writeToLog("[License] === OAuth startAuthentication START === hwid: " +
        juce::String(hardwareId.substr(0, 8)) + "...");

    if (isRunning_ && !shouldStop_)
    {
        juce::Logger::writeToLog("[License] OAuth: already in progress - refused");
        if (callback)
        {
            AuthResult result;
            result.errorMessage = "Sign-in is already in progress. Finish it in your browser.";
            juce::MessageManager::callAsync([callback, result]() { callback(result); });
        }
        return;
    }

    // A previous run may still be unwinding. Never join here (message thread):
    // LicenseSystemService hands a fresh authenticator to every sign-in and
    // reaps the old one later, so this is only a safety net.
    if (isThreadRunning())
    {
        juce::Logger::writeToLog("[License] OAuth: previous run still closing - refused (no blocking join)");
        if (callback)
        {
            AuthResult result;
            result.errorMessage = "The previous sign-in is still closing. Click Sign in again in a moment.";
            juce::MessageManager::callAsync([callback, result]() { callback(result); });
        }
        return;
    }
    isRunning_ = false;

    serverBaseUrl_ = serverBaseUrl;
    hardwareId_ = hardwareId;
    completionCallback_ = std::move(callback);
    shouldStop_ = false;
    http_ = std::make_shared<CancellableHttpPost>();

    // CSRF state + PKCE verifier (64 hex chars = valid RFC 7636 verifier). The
    // challenge goes in the browser URL; the verifier only in the HTTPS POST.
    stateToken_ = generateRandomToken();
    codeVerifier_ = generateRandomToken();

    isRunning_ = true;   // set before the thread exists: no double start
    if (!startThread())
    {
        isRunning_ = false;
        juce::Logger::writeToLog("[License] OAuth ERROR: could not start the sign-in thread");
        AuthResult result;
        result.errorMessage = "Could not start sign-in. Please try again.";
        postResult(result);
        return;
    }
    juce::Logger::writeToLog("[License] === OAuth startAuthentication END (thread started) ===");
}

void OAuthBrowserAuthenticator::requestCancel()
{
    juce::Logger::writeToLog("[License] OAuth requestCancel - running: " +
        juce::String(isRunning_ ? "yes" : "no"));

    shouldStop_ = true;
    if (http_ != nullptr)
        http_->cancel();   // aborts a token exchange in flight
    signalThreadShouldExit();
}

void OAuthBrowserAuthenticator::cancelAuthentication()
{
    requestCancel();

    // Only the auth thread touches the socket. Every wait in run() is bounded
    // and cancellable (socket polls of kPollMs, the exchange is aborted by
    // http_->cancel(), no child processes), so it exits within ~0.1 s. Join,
    // never detach, never force-kill (stopThread kills on timeout).
    if (isThreadRunning() && !waitForThreadToExit(kJoinTimeoutMs))
    {
        juce::Logger::writeToLog("[License] OAuth WARNING: sign-in thread slow to stop - still waiting (no force-kill)");
        waitForThreadToExit(-1);
    }

    isRunning_ = false;
}

void OAuthBrowserAuthenticator::postResult(const AuthResult& result)
{
    // The hop runs later on the message thread, possibly after this object is
    // destroyed - so it captures a COPY of the callback, never `this`.
    if (auto callback = completionCallback_)
        juce::MessageManager::callAsync([callback, result]() { callback(result); });
}

void OAuthBrowserAuthenticator::run()
{
    juce::Logger::writeToLog("[License] OAuth thread started");
    AuthResult result;

    auto finish = [this](AuthResult r, const char* why)
    {
        juce::Logger::writeToLog(juce::String("[License] OAuth finished: ") + why +
            (r.success ? juce::String() : juce::String(" - ") + juce::String(r.errorMessage)));
        stopCallbackServer();
        isRunning_ = false;
        postResult(r);
    };

    // 1. Local callback server (loopback only)
    if (!startCallbackServer())
    {
        result.errorMessage = "Could not start the local sign-in receiver. Close other SAMPLAR windows and try again.";
        finish(result, "no callback port");
        return;
    }

    // 2. Machine ID: the caller passes the licence machine ID (HWID v2, cached
    //    per process). SAMPLAR has no legacy v1 bindings, so no wmic /
    //    PowerShell recompute runs here any more.
    hardwareIdV2_ = hardwareId_;
    juce::Logger::writeToLog("[License] OAuth machine id: " + juce::String(hardwareIdV2_.empty() ? "(none)" : "present"));

    if (shouldStop_ || threadShouldExit())
    {
        result.errorMessage = "Sign-in cancelled";
        finish(result, "cancelled before browser launch");
        return;
    }

    // 3. Browser URL. product_id=SAMPL4R makes the server pick (or refuse) a
    //    SAMPLAR licence only. flow=code: the redirect carries only ?code=&state=.
    juce::SHA256 challengeHash(codeVerifier_.data(), codeVerifier_.size());
    const auto challengeRaw = challengeHash.getRawData();
    const std::string codeChallenge = base64UrlNoPad(challengeRaw.getData(), challengeRaw.getSize());

    // Real version (src/Version.h, from CMake): the server gates per product.
    const juce::String appVersion(samplar::kAppVersion);

    std::string hwidV2Params;
    if (!hardwareIdV2_.empty())
        hwidV2Params += "&hardware_id_v2=" + urlEncode(hardwareIdV2_);

    const std::string oauthUrl = serverBaseUrl_ + "/auth/app-login"
        "?callback_port=" + std::to_string(callbackPort_) +
        "&state=" + urlEncode(stateToken_) +
        "&hardware_id=" + urlEncode(hardwareId_) +
        "&product_id=SAMPL4R" +
        "&app_version=" + urlEncode(appVersion.toStdString()) +
        "&flow=code" +
        "&code_challenge=" + urlEncode(codeChallenge) +
        "&code_challenge_method=S256" +
        hwidV2Params;

    // Never log the URL itself: it carries the state and machine IDs.
    juce::Logger::writeToLog("[License] OAuth opening browser: " + juce::String(serverBaseUrl_) +
        "/auth/app-login (port " + juce::String(callbackPort_) + ", app_version " + appVersion + ")");

    if (!juce::URL(oauthUrl).launchInDefaultBrowser())
    {
        result.errorMessage = "Could not open your web browser. Check your default browser settings.";
        finish(result, "browser launch failed");
        return;
    }

    // 4. Wait for the redirect (matching state only).
    juce::Logger::writeToLog("[License] OAuth waiting for the browser (timeout " +
        juce::String(kAuthTimeoutMs / 1000) + " s)");
    const std::string query = waitForCallback(kAuthTimeoutMs);

    if (shouldStop_ || threadShouldExit())
    {
        result.errorMessage = "Sign-in cancelled";
        finish(result, "cancelled while waiting");
        return;
    }
    if (query.empty())
    {
        result.errorMessage = "Sign-in timed out. Click Sign in to try again.";
        finish(result, "timeout");
        return;
    }

    const auto params = parseQuery(juce::String(query));
    if (params["error"].isNotEmpty())
    {
        result.errorMessage = params["error"].toStdString();
        finish(result, "server reported an error");
        return;
    }

    // 5. Code -> licence data (HTTPS). The ONLY path that can activate: the old
    //    "licence data in the callback URL" path is gone (anyone could forge it).
    const juce::String authCode = params["code"];
    if (authCode.isEmpty())
    {
        result.errorMessage = "Sign-in response was incomplete. Please try again.";
        finish(result, "callback without code");
        return;
    }

    result = exchangeCodeForToken(authCode.toStdString());
    if (shouldStop_ || threadShouldExit())
    {
        AuthResult cancelled;
        cancelled.errorMessage = "Sign-in cancelled";
        finish(cancelled, "cancelled during token exchange");
        return;
    }
    finish(result, result.success ? "token exchange OK" : "token exchange failed");
}

bool OAuthBrowserAuthenticator::startCallbackServer()
{
    serverSocket_ = std::make_unique<juce::StreamingSocket>();

    // Loopback only: the server always redirects to http://127.0.0.1:<port>.
    // (1.0.0 listened on all interfaces: reachable from the LAN, firewall prompt.)
    for (int port = kFirstPort; port <= kLastPort; ++port)
    {
        if (serverSocket_->createListener(port, "127.0.0.1"))
        {
            callbackPort_ = port;
            juce::Logger::writeToLog("[License] OAuth listening on 127.0.0.1:" + juce::String(port));
            return true;
        }
    }

    juce::Logger::writeToLog("[License] OAuth ERROR: no free port in " + juce::String(kFirstPort) +
        ".." + juce::String(kLastPort));
    serverSocket_.reset();
    return false;
}

void OAuthBrowserAuthenticator::stopCallbackServer()
{
    // Auth thread only.
    if (serverSocket_)
    {
        serverSocket_->close();
        serverSocket_.reset();
    }
}

std::string OAuthBrowserAuthenticator::waitForCallback(int timeoutMs)
{
    if (!serverSocket_)
        return {};

    const auto startTime = juce::Time::getMillisecondCounter();
    int loopCount = 0;

    while (!shouldStop_ && !threadShouldExit())
    {
        const auto elapsed = juce::Time::getMillisecondCounter() - startTime;
        if ((++loopCount % 300) == 1)   // every ~30 s
            juce::Logger::writeToLog("[License] OAuth still waiting... " + juce::String((int) (elapsed / 1000)) + " s");

        if (elapsed > (juce::uint32) timeoutMs)
        {
            juce::Logger::writeToLog("[License] OAuth TIMEOUT after " + juce::String((int) (elapsed / 1000)) + " s");
            return {};
        }

        if (serverSocket_->waitUntilReady(true, kPollMs) != 1)
            continue;

        std::unique_ptr<juce::StreamingSocket> client(serverSocket_->waitForNextConnection());
        if (!client)
            continue;

        // Read the request headers (they can arrive split across packets).
        std::string acc;
        acc.reserve(8192);
        const auto readDeadline = juce::Time::getMillisecondCounter() + 4000;
        while (juce::Time::getMillisecondCounter() < readDeadline && !shouldStop_)
        {
            if (client->waitUntilReady(true, kPollMs) != 1)
            {
                if (acc.find("\r\n\r\n") != std::string::npos) break;
                continue;
            }
            char buf[8192];
            const int n = client->read(buf, (int) sizeof(buf), false);
            if (n <= 0) break;
            acc.append(buf, (size_t) n);
            if (acc.find("\r\n\r\n") != std::string::npos) break;
            if (acc.size() > 65536) break;   // a callback line is tiny in the code flow
        }

        const juce::String request = juce::String::fromUTF8(acc.data(), (int) acc.size());
        const juce::String requestLine = request.upToFirstOccurrenceOf("\r\n", false, false);
        const char* reply = kNotFound;
        std::string accepted;

        if (requestLine.startsWith("GET /callback?"))
        {
            const int queryStart = requestLine.indexOfChar('?');
            const int httpStart = requestLine.indexOf(" HTTP/");
            if (queryStart > 0 && httpStart > queryStart)
            {
                const auto query = requestLine.substring(queryStart + 1, httpStart);
                // CSRF: only the redirect for THIS sign-in counts. Anything else
                // (stale tab, other local process) is answered and ignored, so it
                // cannot abort the user's sign-in.
                if (parseQuery(query)["state"].toStdString() == stateToken_)
                {
                    reply = kSuccessPage;
                    accepted = query.toStdString();
                    juce::Logger::writeToLog("[License] OAuth callback accepted (" +
                        juce::String(query.length()) + " chars, values redacted)");
                }
                else
                {
                    reply = kBadState;
                    juce::Logger::writeToLog("[License] OAuth callback with wrong state - ignored, still waiting");
                }
            }
        }

        const juce::String response(reply);
        client->write(response.toRawUTF8(), (int) response.getNumBytesAsUTF8());
        client->close();

        if (!accepted.empty())
            return accepted;
    }

    juce::Logger::writeToLog("[License] OAuth wait loop exited - cancelled");
    return {};
}

std::string OAuthBrowserAuthenticator::generateRandomToken()
{
    uint8_t bytes[32] = {0};
    bool ok = false;

   #if JUCE_WINDOWS
    // BCRYPT_USE_SYSTEM_PREFERRED_RNG requires a null algorithm handle.
    ok = (BCryptGenRandom(nullptr, bytes, (ULONG) sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0);
   #endif

    if (!ok)
    {
        juce::Logger::writeToLog("[License] WARNING: OS CSPRNG unavailable, using fallback RNG");
        juce::Random random((juce::int64) (juce::Time::getHighResolutionTicks()
                            ^ juce::Random::getSystemRandom().nextInt64()));
        for (auto& b : bytes) b = (uint8_t) random.nextInt(256);
    }

    juce::String token;
    for (auto b : bytes)
        token += juce::String::toHexString((int) b).paddedLeft('0', 2);
    return token.toStdString();
}

std::string OAuthBrowserAuthenticator::urlEncode(const std::string& value)
{
    return juce::URL::addEscapeChars(juce::String(value), true).toStdString();
}

OAuthBrowserAuthenticator::AuthResult OAuthBrowserAuthenticator::exchangeCodeForToken(const std::string& authCode)
{
    juce::Logger::writeToLog("[License] === exchangeCodeForToken START === server: " + juce::String(serverBaseUrl_));
    AuthResult result;

    auto* payload = new juce::DynamicObject();
    payload->setProperty("code", juce::String(authCode));
    payload->setProperty("hardware_id", juce::String(hardwareId_));
    const auto nonce = LicenseSignature::makeNonce();   // echoed inside the signed licence
    payload->setProperty("nonce", nonce);
    result.requestNonce = nonce.toStdString();
    payload->setProperty("state", juce::String(stateToken_));
    payload->setProperty("code_verifier", juce::String(codeVerifier_));   // PKCE
    if (!hardwareIdV2_.empty())
        payload->setProperty("hardware_id_v2", juce::String(hardwareIdV2_));
    const juce::var payloadVar(payload);

    // code + code_verifier are one-time secrets: never logged.
    const auto url = juce::URL(juce::String(serverBaseUrl_) + "/auth/token-exchange")
                         .withPOSTData(juce::JSON::toString(payloadVar, true));
    const auto response = http_->post(url, "Content-Type: application/json\r\nAccept: application/json\r\n", 30000);

    if (!response.connected)
    {
        result.errorMessage = "Could not reach the SAMPLAR license server. Check your internet connection and try again.";
        juce::Logger::writeToLog("[License] exchangeCodeForToken: no connection");
        return result;
    }

    const auto json = juce::JSON::parse(response.body);
    if (response.statusCode != 200 || !json.isObject())
    {
        result.errorMessage = "The license server returned an error (" + std::to_string(response.statusCode) + "). Please try again.";
        if (json.isObject())
        {
            const auto err = json.getProperty("error", "").toString();
            if (err.isNotEmpty())
                result.errorMessage = err.toStdString();
        }
        juce::Logger::writeToLog("[License] exchangeCodeForToken: HTTP " + juce::String(response.statusCode) +
            ", json: " + juce::String(json.isObject() ? "yes" : "no"));
        return result;
    }

    if (!(bool) json.getProperty("success", false))
    {
        result.errorMessage = json.getProperty("error", "Sign-in failed").toString().toStdString();
        juce::Logger::writeToLog("[License] exchangeCodeForToken: server said not successful: " + juce::String(result.errorMessage));
        return result;
    }

    result.success        = true;
    result.accessToken    = json.getProperty("token", "").toString().toStdString();
    result.licenseKey     = json.getProperty("license_key", "").toString().toStdString();
    result.email          = json.getProperty("email", "").toString().toStdString();
    result.licenseType    = json.getProperty("license_type", "").toString().toStdString();
    result.expiresAt      = json.getProperty("expires_at", "").toString().toStdString();
    result.hardwareId     = json.getProperty("hardware_id", "").toString().toStdString();
    result.issuedAt       = json.getProperty("issued_at", "").toString().toStdString();
    // Exact server-signed text + its signature (field signature_v2; the old v1
    // "signature" is a different payload/key and is ignored).
    result.licensePayload = json.getProperty("license_payload", "").toString().toStdString();
    result.signatureV2    = json.getProperty("signature_v2", "").toString().toStdString();

    // Plain fields kept for display/caching. The access token is NOT copied into
    // the licence data: it lives only in the encrypted session file.
    auto* info = new juce::DynamicObject();
    info->setProperty("customer_email", juce::String(result.email));
    info->setProperty("license_type", juce::String(result.licenseType));
    info->setProperty("expires_at", juce::String(result.expiresAt));
    info->setProperty("issued_at", juce::String(result.issuedAt));
    result.licenseInfo = juce::var(info);

    juce::Logger::writeToLog("[License] === exchangeCodeForToken END === email " + maskEmail(juce::String(result.email)) +
        ", type " + juce::String(result.licenseType) +
        ", key prefix " + juce::String(result.licenseKey).upToFirstOccurrenceOf("-", false, false) +
        ", signed payload: " + juce::String(result.licensePayload.empty() ? "absent" : "present"));
    return result;
}

} // namespace samplar
