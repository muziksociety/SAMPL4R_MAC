#include "OAuthBrowserAuthenticator.h"
#include "../Version.h"
#include <juce_cryptography/juce_cryptography.h>

namespace samplar {

// ── Version attestation HMAC ────────────────────────────────────────────────
// Signs (app_version + "|" + hardware_id) with a compile-time secret so the
// server can verify the version claim wasn't edited in the browser URL bar.
//
// Raises the bar from "edit a query param" to "disassemble the binary and
// extract the key". Not unbreakable on desktop — but catches casual tampering.
//
// The server must have the same key in OAUTH_SIGNING_KEY env var.
// ─────────────────────────────────────────────────────────────────────────────
static std::string computeVersionSignature(const std::string& version,
                                            const std::string& hardwareId)
{
    // Signing key — MUST match the license server's OAUTH_SIGNING_KEY env var.
    static constexpr const char* signingKey = "sm5-oauth-sign-v1-a3f8c2d1e9b7";

    // Pipe-separated to prevent length-extension ambiguity.
    const std::string message = version + "|" + hardwareId;

    // HMAC-SHA256 (RFC 2104)
    constexpr size_t blockSize = 64;

    juce::MemoryBlock keyBlock(signingKey, strlen(signingKey));
    if (keyBlock.getSize() > blockSize)
    {
        juce::SHA256 keyHash(keyBlock.getData(), keyBlock.getSize());
        auto raw = keyHash.getRawData();
        keyBlock = juce::MemoryBlock(raw.getData(), raw.getSize());
    }
    keyBlock.setSize(blockSize, true);

    juce::MemoryBlock ipad(blockSize, true);
    juce::MemoryBlock opad(blockSize, true);
    auto* kb = static_cast<const uint8_t*>(keyBlock.getData());
    auto* ip = static_cast<uint8_t*>(ipad.getData());
    auto* op = static_cast<uint8_t*>(opad.getData());
    for (size_t i = 0; i < blockSize; ++i)
    {
        ip[i] = kb[i] ^ 0x36;
        op[i] = kb[i] ^ 0x5c;
    }

    // Inner: SHA256(ipad || message)
    juce::MemoryBlock innerInput;
    innerInput.append(ipad.getData(), ipad.getSize());
    innerInput.append(message.data(), message.size());
    juce::SHA256 innerHash(innerInput.getData(), innerInput.getSize());
    auto innerResult = innerHash.getRawData();

    // Outer: SHA256(opad || innerHash)
    juce::MemoryBlock outerInput;
    outerInput.append(opad.getData(), opad.getSize());
    outerInput.append(innerResult.getData(), innerResult.getSize());
    juce::SHA256 outerHash(outerInput.getData(), outerInput.getSize());

    return outerHash.toHexString().toStdString();
}

OAuthBrowserAuthenticator::OAuthBrowserAuthenticator()
    : juce::Thread("OAuthAuthenticator")
{
    juce::Logger::writeToLog("[OAuth] OAuthBrowserAuthenticator created");
}

OAuthBrowserAuthenticator::~OAuthBrowserAuthenticator()
{
    // Log destructor to debug file
    auto debugFile = juce::File::getSpecialLocation(juce::File::userDesktopDirectory).getChildFile("oauth_debug.txt");
    debugFile.appendText("[" + juce::Time::getCurrentTime().toString(true, true) + "] ~OAuthBrowserAuthenticator DESTRUCTOR called!\n");

    cancelAuthentication();
    juce::Logger::writeToLog("[OAuth] OAuthBrowserAuthenticator destroyed");
}

void OAuthBrowserAuthenticator::startAuthentication(
    const std::string& serverBaseUrl,
    const std::string& hardwareId,
    CompletionCallback callback)
{
    juce::Logger::writeToLog("[OAuth] === Starting OAuth authentication ===");
    juce::Logger::writeToLog("[OAuth] Server: " + juce::String(serverBaseUrl));
    juce::Logger::writeToLog("[OAuth] Hardware ID: " + juce::String(hardwareId.substr(0, 16)) + "...");

    if (isRunning_)
    {
        juce::Logger::writeToLog("[OAuth] ERROR: Authentication already in progress");
        if (callback)
        {
            AuthResult result;
            result.success = false;
            result.errorMessage = "Authentication already in progress";
            juce::MessageManager::callAsync([callback, result]() {
                callback(result);
            });
        }
        return;
    }

    serverBaseUrl_ = serverBaseUrl;
    hardwareId_ = hardwareId;
    completionCallback_ = callback;
    shouldStop_ = false;
    callbackReceived_ = false;
    callbackData_ = "";

    // Generate state token for CSRF protection
    stateToken_ = generateStateToken();
    juce::Logger::writeToLog("[OAuth] Generated state token: " + juce::String(stateToken_.substr(0, 8)) + "...");

    // Start the authentication thread
    startThread();
}

void OAuthBrowserAuthenticator::cancelAuthentication()
{
    juce::Logger::writeToLog("[OAuth] Cancelling authentication");

    // Debug: write cancellation to file
    auto debugFile = juce::File::getSpecialLocation(juce::File::userDesktopDirectory).getChildFile("oauth_debug.txt");
    debugFile.appendText("[" + juce::Time::getCurrentTime().toString(true, true) + "] cancelAuthentication() called - isRunning: " +
        juce::String(isRunning_ ? "true" : "false") + ", shouldStop: " + juce::String(shouldStop_ ? "true" : "false") + "\n");

    shouldStop_ = true;
    stopCallbackServer();

    if (isThreadRunning())
    {
        stopThread(5000);
    }

    isRunning_ = false;
}

void OAuthBrowserAuthenticator::run()
{
    isRunning_ = true;
    AuthResult result;

    juce::Logger::writeToLog("[OAuth] Authentication thread started");

    // Write to debug file for diagnostics
    auto debugFile = juce::File::getSpecialLocation(juce::File::userDesktopDirectory).getChildFile("oauth_debug.txt");
    debugFile.appendText("[" + juce::Time::getCurrentTime().toString(true, true) + "] OAuth thread started\n");

    // 1. Start local callback server
    if (!startCallbackServer())
    {
        juce::Logger::writeToLog("[OAuth] ERROR: Failed to start callback server");
        result.success = false;
        result.errorMessage = "Failed to start local authentication server";
        isRunning_ = false;
        if (completionCallback_)
        {
            juce::MessageManager::callAsync([this, result]() {
                if (completionCallback_) completionCallback_(result);
            });
        }
        return;
    }

    juce::Logger::writeToLog("[OAuth] Callback server started on port " + juce::String(callbackPort_));
    debugFile.appendText("[" + juce::Time::getCurrentTime().toString(true, true) + "] Server started on port " + juce::String(callbackPort_) + "\n");

    // 2. Build OAuth URL and open browser
    //    `product_id=sampl4r` tells the server which product is requesting auth
    //    so it can filter the user's licenses and pick the SAMPL4R one rather
    //    than e.g. an SM5 license attached to the same account.
    std::string oauthUrl = serverBaseUrl_ + "/auth/app-login"
        "?callback_port=" + std::to_string(callbackPort_) +
        "&state=" + urlEncode(stateToken_) +
        "&hardware_id=" + urlEncode(hardwareId_) +
        "&product_id=sampl4r" +
        "&app_version=" + urlEncode(samplar::kAppVersion) +
        "&app_signature=" + urlEncode(computeVersionSignature(samplar::kAppVersion, hardwareId_));

    juce::Logger::writeToLog("[OAuth] Attempting to open browser...");
    juce::Logger::writeToLog("[OAuth] URL: " + juce::String(oauthUrl));

    juce::URL url(oauthUrl);
    bool browserLaunched = url.launchInDefaultBrowser();

    juce::Logger::writeToLog("[OAuth] launchInDefaultBrowser returned: " + juce::String(browserLaunched ? "SUCCESS" : "FAILED"));

    if (!browserLaunched)
    {
        juce::Logger::writeToLog("[OAuth] ERROR: Failed to open browser - check default browser settings");
        result.success = false;
        result.errorMessage = "Failed to open browser. Please check your default browser settings.";
        stopCallbackServer();
        isRunning_ = false;
        if (completionCallback_)
        {
            juce::MessageManager::callAsync([this, result]() {
                if (completionCallback_) completionCallback_(result);
            });
        }
        return;
    }

    juce::Logger::writeToLog("[OAuth] Browser launched successfully, waiting for callback...");

    // 3. Wait for callback from browser
    juce::Logger::writeToLog("[OAuth] Waiting for browser callback (timeout: " +
        juce::String(AUTH_TIMEOUT_MS / 1000) + "s)...");
    debugFile.appendText("[" + juce::Time::getCurrentTime().toString(true, true) + "] Waiting for callback...\n");

    std::string callbackResponse = waitForCallback(AUTH_TIMEOUT_MS);

    debugFile.appendText("[" + juce::Time::getCurrentTime().toString(true, true) + "] waitForCallback returned, response length: " + juce::String((int)callbackResponse.length()) + "\n");

    stopCallbackServer();

    if (shouldStop_)
    {
        juce::Logger::writeToLog("[OAuth] Authentication cancelled by user");
        result.success = false;
        result.errorMessage = "Authentication cancelled";
        isRunning_ = false;
        if (completionCallback_)
        {
            juce::MessageManager::callAsync([this, result]() {
                if (completionCallback_) completionCallback_(result);
            });
        }
        return;
    }

    if (callbackResponse.empty())
    {
        juce::Logger::writeToLog("[OAuth] ERROR: Timeout waiting for callback");
        result.success = false;
        result.errorMessage = "Authentication timed out. Please try again.";
        isRunning_ = false;
        if (completionCallback_)
        {
            juce::MessageManager::callAsync([this, result]() {
                if (completionCallback_) completionCallback_(result);
            });
        }
        return;
    }

    // 4. Parse callback - contains all license data in URL params
    juce::Logger::writeToLog("[OAuth] Received callback, parsing license data...");

    // Parse all params from callback URL
    juce::StringPairArray params;
    juce::StringArray pairs;
    pairs.addTokens(juce::String(callbackResponse), "&", "");
    for (const auto& pair : pairs)
    {
        int eqPos = pair.indexOf("=");
        if (eqPos > 0)
        {
            juce::String key = pair.substring(0, eqPos);
            juce::String value = juce::URL::removeEscapeChars(pair.substring(eqPos + 1));
            params.set(key, value);
        }
    }

    juce::String receivedState = params["state"];

    // 5. Verify state token (CSRF protection)
    if (receivedState.toStdString() != stateToken_)
    {
        juce::Logger::writeToLog("[OAuth] ERROR: State token mismatch - possible CSRF attack!");
        result.success = false;
        result.errorMessage = "Security verification failed. Please try again.";
        isRunning_ = false;
        if (completionCallback_)
        {
            juce::MessageManager::callAsync([this, result]() {
                if (completionCallback_) completionCallback_(result);
            });
        }
        return;
    }

    // Check for error in callback
    if (params["error"].isNotEmpty())
    {
        result.success = false;
        result.errorMessage = params["error"].toStdString();
        juce::Logger::writeToLog("[OAuth] Auth failed: " + juce::String(result.errorMessage));
        isRunning_ = false;
        if (completionCallback_)
        {
            juce::MessageManager::callAsync([this, result]() {
                if (completionCallback_) completionCallback_(result);
            });
        }
        return;
    }

    // Check for success param
    if (params["success"] != "true")
    {
        juce::Logger::writeToLog("[OAuth] ERROR: Authentication not successful");
        result.success = false;
        result.errorMessage = "Authentication failed on server";
        isRunning_ = false;
        if (completionCallback_)
        {
            juce::MessageManager::callAsync([this, result]() {
                if (completionCallback_) completionCallback_(result);
            });
        }
        return;
    }

    // 6. Extract license data directly from callback URL params
    juce::Logger::writeToLog("[OAuth] Parsing license data from callback...");
    result.success = true;
    result.accessToken = params["token"].toStdString();
    result.licenseKey = params["license_key"].toStdString();
    result.email = params["email"].toStdString();
    result.licenseType = params["license_type"].toStdString();
    result.expiresAt = params["expires_at"].toStdString();
    result.hardwareId = params["hardware_id"].toStdString();
    result.issuedAt = params["issued_at"].toStdString();
    result.signature = params["signature"].toStdString();

    // Build licenseInfo juce::var for caching
    auto* licenseObj = new juce::DynamicObject();
    licenseObj->setProperty("email", juce::String(result.email));
    licenseObj->setProperty("license_key", juce::String(result.licenseKey));
    licenseObj->setProperty("license_type", juce::String(result.licenseType));
    licenseObj->setProperty("expires_at", juce::String(result.expiresAt));
    licenseObj->setProperty("hardware_id", juce::String(result.hardwareId));
    licenseObj->setProperty("signature", juce::String(result.signature));
    licenseObj->setProperty("issued_at", juce::String(result.issuedAt));
    result.licenseInfo = juce::var(licenseObj);

    juce::Logger::writeToLog("[OAuth] License data received - email: " + params["email"] +
                            ", type: " + params["license_type"]);

    isRunning_ = false;

    // 7. Call completion callback on message thread
    if (completionCallback_)
    {
        juce::MessageManager::callAsync([this, result]() {
            if (completionCallback_) completionCallback_(result);
        });
    }

    juce::Logger::writeToLog("[OAuth] Authentication " +
        juce::String(result.success ? "successful" : "failed: " + result.errorMessage));
}

bool OAuthBrowserAuthenticator::startCallbackServer()
{
    serverSocket_ = std::make_unique<juce::StreamingSocket>();

    // Try to find an available port (9876-9900)
    // Use empty string to listen on all local interfaces (both IPv4 and IPv6)
    // This ensures browser can connect regardless of whether it uses localhost (IPv6) or 127.0.0.1 (IPv4)
    for (int port = 9876; port <= 9900; ++port)
    {
        if (serverSocket_->createListener(port, ""))
        {
            callbackPort_ = port;
            juce::Logger::writeToLog("[OAuth] Listening on port " + juce::String(port) + " (all interfaces)");
            return true;
        }
    }

    juce::Logger::writeToLog("[OAuth] ERROR: Could not find available port for callback server");
    return false;
}

void OAuthBrowserAuthenticator::stopCallbackServer()
{
    if (serverSocket_)
    {
        serverSocket_->close();
        serverSocket_.reset();
    }
}

std::string OAuthBrowserAuthenticator::waitForCallback(int timeoutMs)
{
    if (!serverSocket_)
        return "";

    auto debugFile = juce::File::getSpecialLocation(juce::File::userDesktopDirectory).getChildFile("oauth_debug.txt");
    auto startTime = juce::Time::getMillisecondCounter();
    int loopCount = 0;

    while (!shouldStop_)
    {
        loopCount++;

        // Log every 10 seconds
        auto elapsed = juce::Time::getMillisecondCounter() - startTime;
        if (loopCount % 20 == 1) // Every ~10 seconds (20 * 500ms)
        {
            debugFile.appendText("[" + juce::Time::getCurrentTime().toString(true, true) + "] Still waiting... elapsed: " + juce::String((int)(elapsed/1000)) + "s, shouldStop: " + juce::String(shouldStop_ ? "true" : "false") + "\n");
        }

        // Check timeout
        if (elapsed > (juce::uint32)timeoutMs)
        {
            debugFile.appendText("[" + juce::Time::getCurrentTime().toString(true, true) + "] TIMEOUT after " + juce::String((int)(elapsed/1000)) + "s\n");
            return "";
        }

        // Wait for connection with shorter timeout for responsiveness
        if (serverSocket_->waitUntilReady(true, 500) == 1)
        {
            // Accept connection
            auto clientSocket = std::unique_ptr<juce::StreamingSocket>(
                serverSocket_->waitForNextConnection());

            if (clientSocket)
            {
                juce::Logger::writeToLog("[OAuth] Client connected");

                // Read HTTP request
                char buffer[8192];
                int bytesRead = clientSocket->read(buffer, sizeof(buffer) - 1, false);

                if (bytesRead > 0)
                {
                    buffer[bytesRead] = '\0';
                    juce::String request(buffer);

                    // Extract the GET request line
                    juce::StringArray lines;
                    lines.addTokens(request, "\r\n", "");

                    if (lines.size() > 0 && lines[0].startsWith("GET /callback"))
                    {
                        // Extract query string
                        juce::String requestLine = lines[0];
                        int queryStart = requestLine.indexOf("?");
                        int httpStart = requestLine.indexOf(" HTTP/");

                        if (queryStart > 0 && httpStart > queryStart)
                        {
                            juce::String queryString = requestLine.substring(queryStart + 1, httpStart);
                            juce::Logger::writeToLog("[OAuth] Received callback query: " +
                                queryString.substring(0, 100) + "...");

                            // Send HTTP response (success page)
                            juce::String response =
                                "HTTP/1.1 200 OK\r\n"
                                "Content-Type: text/html; charset=utf-8\r\n"
                                "Connection: close\r\n"
                                "\r\n"
                                "<!DOCTYPE html>"
                                "<html><head><title>Success</title>"
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
                                "<h1>Authorization Complete!</h1>"
                                "<p>You can close this window and return to SAMPLAR.</p>"
                                "</div></body></html>";

                            clientSocket->write(response.toRawUTF8(), (int)response.getNumBytesAsUTF8());
                            clientSocket->close();

                            return queryString.toStdString();
                        }
                    }

                    // Not the callback we're looking for - send 404
                    juce::String response =
                        "HTTP/1.1 404 Not Found\r\n"
                        "Content-Type: text/plain\r\n"
                        "Connection: close\r\n"
                        "\r\n"
                        "Not Found";
                    clientSocket->write(response.toRawUTF8(), (int)response.getNumBytesAsUTF8());
                }

                clientSocket->close();
            }
        }
    }

    // Exited while loop - shouldStop must be true
    debugFile.appendText("[" + juce::Time::getCurrentTime().toString(true, true) + "] Exited wait loop - shouldStop is TRUE (cancelled)\n");
    return "";
}

std::string OAuthBrowserAuthenticator::generateStateToken()
{
    // Generate 32 random bytes, encode as hex
    juce::Random random(juce::Time::currentTimeMillis());
    juce::String token;

    for (int i = 0; i < 32; ++i)
    {
        token += juce::String::toHexString(random.nextInt(256)).paddedLeft('0', 2);
    }

    return token.toStdString();
}

std::string OAuthBrowserAuthenticator::urlEncode(const std::string& value)
{
    return juce::URL::addEscapeChars(juce::String(value), true).toStdString();
}

OAuthBrowserAuthenticator::AuthResult OAuthBrowserAuthenticator::exchangeCodeForToken(const std::string& authCode)
{
    AuthResult result;

    juce::Logger::writeToLog("[OAuth] === Exchanging auth code for license data ===");
    juce::Logger::writeToLog("[OAuth] Server: " + juce::String(serverBaseUrl_));

    // Build the token exchange URL
    juce::URL tokenUrl(juce::String(serverBaseUrl_) + "/auth/token-exchange");

    // Create JSON payload (compact, no newlines)
    auto* payload = new juce::DynamicObject();
    payload->setProperty("code", juce::String(authCode));
    payload->setProperty("hardware_id", juce::String(hardwareId_));
    payload->setProperty("state", juce::String(stateToken_));
    juce::var payloadVar(payload);

    juce::String jsonPayload = juce::JSON::toString(payloadVar, true); // compact JSON
    juce::Logger::writeToLog("[OAuth] Sending token exchange request...");
    juce::Logger::writeToLog("[OAuth] JSON payload: " + jsonPayload);

    // Create MemoryBlock for POST body
    juce::MemoryBlock postData(jsonPayload.toRawUTF8(), jsonPayload.getNumBytesAsUTF8());

    // Use withDataToUpload for proper JSON POST
    tokenUrl = tokenUrl.withDataToUpload("json", "", postData, "application/json");

    int statusCode = 0;
    juce::URL::InputStreamOptions options(juce::URL::ParameterHandling::inAddress);
    options.withConnectionTimeoutMs(10000)
           .withExtraHeaders("Accept: application/json")
           .withStatusCode(&statusCode);

    auto stream = tokenUrl.createInputStream(options);

    if (!stream)
    {
        juce::Logger::writeToLog("[OAuth] ERROR: Failed to connect to token exchange endpoint");
        result.success = false;
        result.errorMessage = "Failed to connect to license server";
        return result;
    }

    // Read response
    juce::String responseBody = stream->readEntireStreamAsString();

    juce::Logger::writeToLog("[OAuth] Token exchange response status: " + juce::String(statusCode));
    juce::Logger::writeToLog("[OAuth] Response length: " + juce::String(responseBody.length()));

    // Log first 500 chars of response for debugging
    juce::Logger::writeToLog("[OAuth] Response body: " + responseBody.substring(0, 500));

    if (statusCode != 200)
    {
        juce::Logger::writeToLog("[OAuth] ERROR: Token exchange failed with status " + juce::String(statusCode));
        result.success = false;
        result.errorMessage = "License server returned error: " + std::to_string(statusCode);

        // Try to parse error message from response
        auto responseJson = juce::JSON::parse(responseBody);
        if (responseJson.isObject())
        {
            juce::String errorMsg = responseJson.getProperty("error", "").toString();
            if (errorMsg.isNotEmpty())
                result.errorMessage = errorMsg.toStdString();
        }
        return result;
    }

    // Parse the JSON response
    auto responseJson = juce::JSON::parse(responseBody);
    if (!responseJson.isObject())
    {
        juce::Logger::writeToLog("[OAuth] ERROR: Invalid JSON response from server");
        result.success = false;
        result.errorMessage = "Invalid response from license server";
        return result;
    }

    // Check for success in response
    bool success = responseJson.getProperty("success", false);
    if (!success)
    {
        result.success = false;
        result.errorMessage = responseJson.getProperty("error", "Authentication failed").toString().toStdString();
        juce::Logger::writeToLog("[OAuth] Token exchange failed: " + juce::String(result.errorMessage));
        return result;
    }

    // Extract license data from response
    result.success = true;
    result.accessToken = responseJson.getProperty("token", "").toString().toStdString();
    result.licenseKey = responseJson.getProperty("license_key", "").toString().toStdString();
    result.email = responseJson.getProperty("email", "").toString().toStdString();
    result.licenseType = responseJson.getProperty("license_type", "").toString().toStdString();
    result.expiresAt = responseJson.getProperty("expires_at", "").toString().toStdString();
    result.hardwareId = responseJson.getProperty("hardware_id", "").toString().toStdString();
    result.signature = responseJson.getProperty("signature", "").toString().toStdString();
    result.issuedAt = responseJson.getProperty("issued_at", "").toString().toStdString();

    // Build license info object for caching
    auto* licenseObj = new juce::DynamicObject();
    licenseObj->setProperty("success", true);
    licenseObj->setProperty("license_valid", true);
    licenseObj->setProperty("access_token", juce::String(result.accessToken));
    licenseObj->setProperty("licenseKey", juce::String(result.licenseKey));
    licenseObj->setProperty("email", juce::String(result.email));
    licenseObj->setProperty("license_type", juce::String(result.licenseType));
    licenseObj->setProperty("expires_at", juce::String(result.expiresAt));
    licenseObj->setProperty("hardware_bound_to", juce::String(result.hardwareId));
    licenseObj->setProperty("signature", juce::String(result.signature));
    licenseObj->setProperty("issued_at", juce::String(result.issuedAt));

    result.licenseInfo = juce::var(licenseObj);

    juce::Logger::writeToLog("[OAuth] Token exchange successful!");
    juce::Logger::writeToLog("[OAuth]   Email: " + juce::String(result.email));
    juce::Logger::writeToLog("[OAuth]   License Type: " + juce::String(result.licenseType));
    juce::Logger::writeToLog("[OAuth]   License Key: " +
        juce::String(result.licenseKey.empty() ? "(empty)" : result.licenseKey.substr(0, 8) + "..."));

    return result;
}

} // namespace samplar
