#pragma once

#include "../app/Service.h"
#include "../app/EventBus.h"

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <juce_data_structures/juce_data_structures.h>

namespace samplar {

class HardwareFingerprintGenerator;
class LicenseCache;
class LicenseServerClient;
class SecureSessionManager;
class FeatureManager;
class OAuthBrowserAuthenticator;

class LicenseSystemService : public IService {
public:
    enum class LicenseStatus {
        NotInitialised,
        Valid,
        Expired,
        HardwareMismatch,
        ActivationLimitExceeded,
        InvalidKey,
        NetworkError,
        GracePeriodActive,
        GracePeriodExpired,
        UserLockedOut
    };

    struct LicenseInfo {
        std::string customerEmail;
        std::string licenseType; // TRL, STD, PRO, EDU, PROMO (aligned with license server)
        std::chrono::system_clock::time_point expiresAt{};
        std::vector<std::string> features;
        int maxActivations{0};
        int currentActivations{0};
    };

    explicit LicenseSystemService(EventBus& eventBus);
    ~LicenseSystemService() override;

    void initialise() override;
    void shutdown() override;

    LicenseStatus validateLicense(const std::string& licenseKey);
    bool activateLicense(const std::string& licenseKey,
                         const std::string& email,
                         const std::string& password);

    // [DEPRECATED] Legacy key-entry activation. Account-based auth is now the
    // primary path. Retained as a thin forwarder for any caller still using
    // license keys; new code MUST use activateWithAccount().
    bool activateWithKey(const std::string& licenseKey);

    // ── PRIMARY activation: account-based ─────────────────────────────────
    // POSTs {email, password, hardware_id} to /auth/login-with-hardware on
    // the license server. On success && license_valid, caches the email +
    // signed_token (never the password), sets currentStatus_=Valid, and
    // publishes the standard activation event. The cached signed_token is
    // offline-verifiable via SignedTokenVerifier for the 30-day grace window.
    bool activateWithAccount(const std::string& email, const std::string& password);

    // SAMPL4R every-launch signed-token check.
    // Hits POST /api/licenses/validate-token, verifies the returned signed_token
    // offline via SignedTokenVerifier, and refreshes the cache + last-validation
    // timestamp on success. Honors a 30-day offline grace period if the server
    // is unreachable but a previously verified token is still within its expiry.
    // Runs synchronously — call from a background thread.
    LicenseStatus revalidateCachedLicense();

    // New credential-based methods for username/password authentication
    LicenseStatus validateUserCredentials(const std::string& email, const std::string& password);
    bool activateWithCredentials(const std::string& email, const std::string& password);

    // OAuth browser-based authentication
    // Opens browser to login page, receives callback with license data
    using OAuthCompletionCallback = std::function<void(bool success, const std::string& message)>;
    void startBrowserAuthentication(OAuthCompletionCallback callback);
    void cancelBrowserAuthentication();
    bool isBrowserAuthInProgress() const;

    // Deactivate this machine's license (remove from server activations and clear local cache)
    bool deactivateLicense();

    bool isFeatureEnabled(const std::string& featureName) const;
    LicenseInfo getCurrentLicenseInfo() const;

    std::string generateHardwareId() const;
    bool isHardwareValid() const;

    bool hasOfflineGrace() const;
    std::chrono::minutes getRemainingGraceTime() const;

    void setServerUrl(const std::string& url);
    std::string getServerUrl() const;
    LicenseStatus getCurrentStatus() const;
    std::string getLastResultMessage() const;

    // Get access token for authenticated API calls (e.g., chat)
    std::string getAccessToken() const;

private:
    void loadCachedLicense();
    void resetCurrentLicense();
    void setupEventSubscriptions();
    void teardownEventSubscriptions();
    void performStartupValidation();
    void publishStatusUpdate(const std::string& reason) const;
    juce::var buildStatusPayload(const std::string& reason) const;
    void populateLicenseInfoFromVar(const juce::var& licenseVar);
    juce::var buildLicenseVar() const;
    LicenseStatus mapStatusFromResponse(int statusCode, bool valid) const;
    LicenseStatus applyServerResponse(const std::string& licenseKey,
                                      const std::string& hardwareId,
                                      bool valid,
                                      int statusCode,
                                      const juce::var& licenseInfo,
                                      const juce::var& validationInfo,
                                      const std::string& message);

    EventBus& eventBus_;
    std::vector<std::pair<std::string, EventBus::HandlerId>> subscriptions_;

    std::unique_ptr<HardwareFingerprintGenerator> hardwareGenerator_;
    std::unique_ptr<LicenseCache> cache_;
    std::unique_ptr<LicenseServerClient> serverClient_;
    std::unique_ptr<SecureSessionManager> sessionManager_;
    std::unique_ptr<FeatureManager> featureManager_;
    std::unique_ptr<OAuthBrowserAuthenticator> oauthAuthenticator_;

    LicenseStatus currentStatus_{LicenseStatus::NotInitialised};
    LicenseInfo currentLicense_{};
    std::string currentHardwareId_;
    std::string serverUrlOverride_;
    std::string lastResultMessage_;

    mutable std::mutex stateMutex_;};

} // namespace samplar






