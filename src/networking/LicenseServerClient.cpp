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
    juce::URL url(juce::String(serverUrl_) + "/api/license/validate");
    auto* payloadObject = new juce::DynamicObject();
    payloadObject->setProperty("license_key", juce::String(request.licenseKey));
    payloadObject->setProperty("hardware_id", juce::String(request.hardwareId));
    payloadObject->setProperty("app_version", juce::String(request.appVersion));
    payloadObject->setProperty("user_agent", juce::String(request.userAgent));
    payloadObject->setProperty("ip_address", juce::String(request.ipAddress));

    ValidationResponse response;
    {
        std::scoped_lock lock(networkMutex_);
        response = performPost(url, juce::var(payloadObject));
    }

    return response;
}

LicenseServerClient::ValidationResponse LicenseServerClient::activateLicense(const ActivationRequest& request)
{
    juce::URL url(juce::String(serverUrl_) + "/api/license/activate");
    auto* payloadObject = new juce::DynamicObject();
    payloadObject->setProperty("license_key", juce::String(request.licenseKey));
    payloadObject->setProperty("hardware_id", juce::String(request.hardwareId));
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

    juce::URL url(juce::String(serverUrl_) + "/api/license/deactivate");
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
            << "User-Agent: Samplar/5.0.0\r\n";

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

} // namespace samplar



