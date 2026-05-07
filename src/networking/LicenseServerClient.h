#pragma once

#include <mutex>
#include <string>

#include <juce_core/juce_core.h>

namespace samplar {

class LicenseServerClient {
public:
    struct ValidationRequest {
        std::string licenseKey;
        std::string hardwareId;
        std::string appVersion;
        std::string userAgent;
        std::string ipAddress;
    };

    struct ValidationResponse {
        bool valid{false};
        juce::var licenseInfo;
        juce::var validationInfo;
        std::string message;
        int statusCode{0};
    };

    struct ActivationRequest {
        std::string licenseKey;
        std::string hardwareId;
        std::string email;
        std::string password;
    };

    ValidationResponse validateLicense(const ValidationRequest& request);
    ValidationResponse activateLicense(const ActivationRequest& request);
    ValidationResponse authenticateWithHardware(const std::string& email,
                                                const std::string& password,
                                                const std::string& hardwareId);

    // New credential-based authentication methods
    ValidationResponse loginWithCredentials(const std::string& email, const std::string& password);
    ValidationResponse activateWithCredentials(const std::string& email, const std::string& password, const std::string& hardwareId);

    // Deactivation - removes this hardware from license activations
    ValidationResponse deactivateLicense(const std::string& licenseKey, const std::string& hardwareId);

    void setServerUrl(const std::string& url);
    void setTimeout(int timeoutSeconds);
    void setRetryAttempts(int attempts);

private:
    ValidationResponse performPost(const juce::URL& url, const juce::var& payload);
    juce::String createJsonPayload(const juce::var& data) const;
    ValidationResponse parseValidationResponse(const juce::String& responseBody, int statusCode) const;

    std::string serverUrl_ { "https://samplemonsta.com" };
    int timeoutSeconds_ { 8 };
    int retryAttempts_ { 2 };
    int retryDelayMs_ { 1000 };

    mutable std::mutex networkMutex_;
};

} // namespace samplar

