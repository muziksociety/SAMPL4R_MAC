#include "LicenseServerClient.h"

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

namespace samplar {

namespace {
bool looksLikeLicenseInfo(const juce::var& candidate)
{
    if (!candidate.isObject()) {
        return false;
    }

    if (auto* obj = candidate.getDynamicObject()) {
        const auto type = obj->getProperty(juce::Identifier("license_type"));
        const auto email = obj->getProperty(juce::Identifier("customer_email"));
        return (!type.isVoid() && type.toString().isNotEmpty())
            || (!email.isVoid() && email.toString().isNotEmpty());
    }

    return false;
}

} // namespace

LicenseServerClient::ValidationResponse LicenseServerClient::validateLicense(const ValidationRequest& request)
{
    // SAMPL4R first-launch activation now uses /api/licenses/validate-token
    // (the production server does not deploy /api/v1/validate; validate-token
    // handles both first-time activation and recurring revalidation — the
    // server distinguishes via internal state).
    const auto truncatedKey = request.licenseKey.size() > 10
        ? request.licenseKey.substr(0, 10) + "..."
        : request.licenseKey;
    juce::Logger::writeToLog("[LicenseServerClient] validateLicense (activate) key=" +
        juce::String(truncatedKey) + " server=" + juce::String(serverUrl_));

    juce::URL url(juce::String(serverUrl_) + "/api/licenses/validate-token");
    auto* payloadObject = new juce::DynamicObject();
    payloadObject->setProperty("license_key", juce::String(request.licenseKey));
    payloadObject->setProperty("hardware_id", juce::String(request.hardwareId));
    payloadObject->setProperty("product_id", juce::String("sampl4r"));

    ValidationResponse response;
    {
        std::scoped_lock lock(networkMutex_);
        response = performPost(url, juce::var(payloadObject));
    }

    juce::Logger::writeToLog("[LicenseServerClient] validateLicense result: valid=" +
        juce::String(response.valid ? "true" : "false") +
        " status=" + juce::String(response.statusCode) +
        " msg=" + juce::String(response.message));

    return response;
}

LicenseServerClient::ValidationResponse LicenseServerClient::validateToken(const std::string& licenseKey,
                                                                          const std::string& hardwareId,
                                                                          const std::string& productId)
{
    // SAMPL4R every-launch signed-token check endpoint
    const auto truncatedKey = licenseKey.size() > 10
        ? licenseKey.substr(0, 10) + "..."
        : licenseKey;
    juce::Logger::writeToLog("[LicenseServerClient] validateToken key=" +
        juce::String(truncatedKey) + " product=" + juce::String(productId) +
        " server=" + juce::String(serverUrl_));

    juce::URL url(juce::String(serverUrl_) + "/api/licenses/validate-token");
    auto* payloadObject = new juce::DynamicObject();
    payloadObject->setProperty("license_key", juce::String(licenseKey));
    payloadObject->setProperty("hardware_id", juce::String(hardwareId));
    payloadObject->setProperty("product_id", juce::String(productId));

    ValidationResponse response;
    {
        std::scoped_lock lock(networkMutex_);
        response = performPost(url, juce::var(payloadObject));
    }

    juce::Logger::writeToLog("[LicenseServerClient] validateToken result: valid=" +
        juce::String(response.valid ? "true" : "false") +
        " status=" + juce::String(response.statusCode) +
        " hasSignedToken=" + juce::String(response.signedToken.empty() ? "no" : "yes"));

    return response;
}

LicenseServerClient::ValidationResponse LicenseServerClient::activateLicense(const ActivationRequest& request)
{
    // Credentials-style activation routes through the same validate-token
    // endpoint (production does not deploy /api/v1/validate). Email/password
    // are passed alongside so the server can associate the activation.
    juce::URL url(juce::String(serverUrl_) + "/api/licenses/validate-token");
    auto* payloadObject = new juce::DynamicObject();
    payloadObject->setProperty("license_key", juce::String(request.licenseKey));
    payloadObject->setProperty("hardware_id", juce::String(request.hardwareId));
    payloadObject->setProperty("product_id", juce::String("sampl4r"));
    payloadObject->setProperty("email", juce::String(request.email));
    payloadObject->setProperty("password", juce::String(request.password));

    ValidationResponse response;
    {
        std::scoped_lock lock(networkMutex_);
        response = performPost(url, juce::var(payloadObject));
    }

    return response;
}

LicenseServerClient::ValidationResponse LicenseServerClient::deactivateLicense(const std::string& licenseKey,
                                                                                const std::string& hardwareId)
{
    juce::Logger::writeToLog("[LicenseServerClient] Deactivating license for hardware: " + juce::String(hardwareId));

    juce::URL url(juce::String(serverUrl_) + "/api/licenses/deactivate");
    auto* payloadObject = new juce::DynamicObject();
    payloadObject->setProperty("license_key", juce::String(licenseKey));
    payloadObject->setProperty("hardware_id", juce::String(hardwareId));

    ValidationResponse response;
    {
        std::scoped_lock lock(networkMutex_);
        response = performPost(url, juce::var(payloadObject));
    }

    juce::Logger::writeToLog("[LicenseServerClient] Deactivation response: " +
        juce::String(response.valid ? "success" : "failed") + " - " + juce::String(response.message));

    return response;
}

LicenseServerClient::ValidationResponse LicenseServerClient::authenticateWithHardware(const std::string& email,
                                                                                     const std::string& password,
                                                                                     const std::string& hardwareId)
{
    juce::URL url(juce::String(serverUrl_) + "/api/license/authenticate");
    auto* payloadObject = new juce::DynamicObject();
    payloadObject->setProperty("email", juce::String(email));
    payloadObject->setProperty("password", juce::String(password));
    payloadObject->setProperty("hardware_id", juce::String(hardwareId));

    ValidationResponse response;
    {
        std::scoped_lock lock(networkMutex_);
        response = performPost(url, juce::var(payloadObject));
    }

    return response;
}

void LicenseServerClient::setServerUrl(const std::string& url)
{
    std::scoped_lock lock(networkMutex_);
    serverUrl_ = url;
}

void LicenseServerClient::setTimeout(int timeoutSeconds)
{
    std::scoped_lock lock(networkMutex_);
    timeoutSeconds_ = timeoutSeconds;
}

void LicenseServerClient::setRetryAttempts(int attempts)
{
    std::scoped_lock lock(networkMutex_);
    retryAttempts_ = juce::jmax(1, attempts);
}

LicenseServerClient::ValidationResponse LicenseServerClient::performPost(const juce::URL& baseUrl, const juce::var& payload)
{
    const auto jsonPayload = createJsonPayload(payload);
    auto requestUrl = baseUrl.withPOSTData(jsonPayload);

    juce::String headers;
    headers << "Content-Type: application/json\r\n"
            << "Accept: application/json\r\n"
            << "User-Agent: SAMPLAR-sampl4r/5.0.0\r\n";

    ValidationResponse lastResponse;
    lastResponse.statusCode = 0;
    lastResponse.valid = false;
    lastResponse.message = "No response from license server";

    for (int attempt = 0; attempt < retryAttempts_; ++attempt) {
        int statusCode = 0;
        juce::StringPairArray responseHeaders;

        auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                            .withHttpRequestCmd("POST")
                            .withExtraHeaders(headers)
                            .withConnectionTimeoutMs(timeoutSeconds_ * 1000)
                            .withResponseHeaders(&responseHeaders)
                            .withStatusCode(&statusCode);

        auto stream = requestUrl.createInputStream(options);

        if (stream != nullptr) {
            const auto body = stream->readEntireStreamAsString();
            auto response = parseValidationResponse(body, statusCode);
            if (response.message.empty()) {
                const auto fallback = (statusCode >= 200 && statusCode < 300)
                                           ? juce::String("OK")
                                           : juce::String("HTTP ") + juce::String(statusCode);
                response.message = fallback.toStdString();
            }

            if (statusCode >= 200 && statusCode < 300) {
                return response;
            }

            lastResponse = response;
        } else {
            lastResponse.statusCode = statusCode;
            lastResponse.valid = false;
            lastResponse.message = "Unable to contact license server";
        }

        if (attempt + 1 < retryAttempts_) {
            juce::Thread::sleep(retryDelayMs_);
        }
    }

    return lastResponse;
}

juce::String LicenseServerClient::createJsonPayload(const juce::var& data) const
{
    return juce::JSON::toString(data, false);
}

LicenseServerClient::ValidationResponse LicenseServerClient::parseValidationResponse(const juce::String& responseBody,
                                                                                    int statusCode) const
{
    ValidationResponse response;
    response.statusCode = statusCode;

    auto parsed = juce::JSON::parse(responseBody);
    if (parsed.isObject()) {
        if (auto* obj = parsed.getDynamicObject()) {
            const auto validVar = obj->getProperty(juce::Identifier("valid"));
            response.valid = validVar.isVoid() ? (statusCode >= 200 && statusCode < 300) : static_cast<bool>(validVar);

            auto messageVar = obj->getProperty(juce::Identifier("message"));
            if (!messageVar.isVoid()) {
                response.message = messageVar.toString().toStdString();
            }

            response.licenseInfo = obj->getProperty(juce::Identifier("license_info"));
            if (response.licenseInfo.isVoid()) {
                response.licenseInfo = obj->getProperty(juce::Identifier("license"));
            }
            if (response.licenseInfo.isVoid() && looksLikeLicenseInfo(parsed)) {
                response.licenseInfo = parsed;
            }

            response.validationInfo = obj->getProperty(juce::Identifier("validation_info"));
            if (response.validationInfo.isVoid()) {
                response.validationInfo = obj->getProperty(juce::Identifier("validation"));
            }

            // SAMPL4R: signed offline-verifiable token from /api/licenses/validate-token
            const auto tokenVar = obj->getProperty(juce::Identifier("signed_token"));
            if (!tokenVar.isVoid()) {
                response.signedToken = tokenVar.toString().toStdString();
            }

            if (response.message.empty()) {
                auto detail = obj->getProperty(juce::Identifier("detail"));
                if (!detail.isVoid()) {
                    response.message = detail.toString().toStdString();
                }
            }
        }
    } else {
        response.valid = statusCode >= 200 && statusCode < 300;
        response.message = responseBody.trim().toStdString();
    }

    if (response.message.empty()) {
        response.message = response.valid ? "OK" : "License validation failed";
    }

    return response;
}

LicenseServerClient::ValidationResponse LicenseServerClient::loginWithCredentials(const std::string& email, const std::string& password)
{
    std::scoped_lock lock(networkMutex_);

    juce::var payload = juce::var(new juce::DynamicObject());
    payload.getDynamicObject()->setProperty("email", juce::String(email));
    payload.getDynamicObject()->setProperty("password", juce::String(password));

    const juce::URL url(serverUrl_ + "/auth/login");
    return performPost(url, payload);
}

LicenseServerClient::ValidationResponse LicenseServerClient::activateWithCredentials(const std::string& email, const std::string& password, const std::string& hardwareId)
{
    std::scoped_lock lock(networkMutex_);

    juce::var payload = juce::var(new juce::DynamicObject());
    payload.getDynamicObject()->setProperty("email", juce::String(email));
    payload.getDynamicObject()->setProperty("password", juce::String(password));
    payload.getDynamicObject()->setProperty("hardware_id", juce::String(hardwareId));

    const juce::URL url(serverUrl_ + "/auth/login-with-hardware");
    return performPost(url, payload);
}

LicenseServerClient::LoginResponse LicenseServerClient::loginWithHardware(const std::string& email,
                                                                          const std::string& password,
                                                                          const std::string& hardwareId)
{
    // Truncated email for logging (per project convention — keep first 4 chars + "...").
    const auto truncEmail = email.size() > 4 ? email.substr(0, 4) + "..." : email;
    juce::Logger::writeToLog("[LicenseServerClient] loginWithHardware email=" +
        juce::String(truncEmail) + " server=" + juce::String(serverUrl_));

    juce::URL url(juce::String(serverUrl_) + "/auth/login-with-hardware");
    auto* payloadObject = new juce::DynamicObject();
    payloadObject->setProperty("email", juce::String(email));
    payloadObject->setProperty("password", juce::String(password));
    payloadObject->setProperty("hardware_id", juce::String(hardwareId));

    ValidationResponse raw;
    {
        std::scoped_lock lock(networkMutex_);
        raw = performPost(url, juce::var(payloadObject));
    }

    LoginResponse out;
    out.statusCode = raw.statusCode;
    out.message    = raw.message;

    // Re-parse the body via licenseInfo/validationInfo + the raw envelope.
    // performPost already extracted "license_info"/"validation_info" — the
    // top-level "success" / "license_valid" / "email" / "license_type" /
    // "signed_token" fields need fresh parsing from licenseInfo when present,
    // OR from the raw stream. To avoid a second HTTP call, we rely on
    // parseValidationResponse having stashed everything we need into the
    // response object below.

    // Robust extraction: look at the parsed top-level object via licenseInfo
    // fallback (parseValidationResponse already promotes licenseInfo to the
    // whole payload when it "looks like license info").
    auto extractString = [](const juce::var& src, const char* key) -> std::string {
        if (!src.isObject()) return {};
        auto* obj = src.getDynamicObject();
        if (!obj) return {};
        const auto v = obj->getProperty(juce::Identifier(key));
        return v.isVoid() ? std::string{} : v.toString().toStdString();
    };
    auto extractBool = [](const juce::var& src, const char* key, bool dflt) {
        if (!src.isObject()) return dflt;
        auto* obj = src.getDynamicObject();
        if (!obj) return dflt;
        const auto v = obj->getProperty(juce::Identifier(key));
        return v.isVoid() ? dflt : static_cast<bool>(v);
    };

    // For login-with-hardware, the server returns a flat envelope:
    //   { success, license_valid, message, email, license_type, signed_token, expires_at, ... }
    // parseValidationResponse stores the full payload in licenseInfo when it
    // contains license fields. We try both licenseInfo and validationInfo.
    const juce::var& candidate = raw.licenseInfo.isObject() ? raw.licenseInfo : raw.validationInfo;

    out.success      = extractBool(candidate, "success", raw.statusCode >= 200 && raw.statusCode < 300);
    out.licenseValid = extractBool(candidate, "license_valid", raw.valid);
    if (out.message.empty())
        out.message  = extractString(candidate, "message");
    out.email        = extractString(candidate, "email");
    out.licenseType  = extractString(candidate, "license_type");
    out.signedToken  = !raw.signedToken.empty() ? raw.signedToken : extractString(candidate, "signed_token");
    out.expiresAt    = extractString(candidate, "expires_at");
    out.raw          = candidate;

    juce::Logger::writeToLog("[LicenseServerClient] loginWithHardware result: success=" +
        juce::String(out.success ? "true" : "false") +
        " licenseValid=" + juce::String(out.licenseValid ? "true" : "false") +
        " status=" + juce::String(out.statusCode) +
        " hasToken=" + juce::String(out.signedToken.empty() ? "no" : "yes"));

    return out;
}

} // namespace samplar



