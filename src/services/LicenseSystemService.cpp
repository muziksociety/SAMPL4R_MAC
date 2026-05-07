#include "LicenseSystemService.h"

#include "../security/HardwareFingerprintGenerator.h"
#include "../security/LicenseCache.h"
#include "../security/SecureSessionManager.h"
#include "../networking/LicenseServerClient.h"
#include "../networking/OAuthBrowserAuthenticator.h"
#include "../licensing/FeatureManager.h"

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <algorithm>
#include <map>

namespace samplar {

namespace {
// Server URL for OAuth and license validation
constexpr auto kDefaultLicenseServerUrl = "https://license-server-n5it.onrender.com";
constexpr auto kClientVersion = "5.0.0";
constexpr auto kClientUserAgent = "Samplar/5.0.0";

const std::map<std::string, FeatureManager::FeatureType>& featureNameMap()
{
    static const std::map<std::string, FeatureManager::FeatureType> map = {
        {"premium_samples", FeatureManager::FeatureType::PremiumSamples},
        {"unlimited_exports", FeatureManager::FeatureType::UnlimitedExports},
        {"cloud_sync", FeatureManager::FeatureType::CloudSync},
        {"advanced_effects", FeatureManager::FeatureType::AdvancedEffects},
        {"batch_processing", FeatureManager::FeatureType::BatchProcessing},
        {"custom_presets", FeatureManager::FeatureType::CustomPresets},
        {"priority_support", FeatureManager::FeatureType::PrioritySupport},
        {"commercial_license", FeatureManager::FeatureType::CommercialLicense}
    };
    return map;
}

juce::String toStatusString(LicenseSystemService::LicenseStatus status)
{
    switch (status) {
        case LicenseSystemService::LicenseStatus::NotInitialised: return "NotInitialised";
        case LicenseSystemService::LicenseStatus::Valid: return "Valid";
        case LicenseSystemService::LicenseStatus::Expired: return "Expired";
        case LicenseSystemService::LicenseStatus::HardwareMismatch: return "HardwareMismatch";
        case LicenseSystemService::LicenseStatus::ActivationLimitExceeded: return "ActivationLimitExceeded";
        case LicenseSystemService::LicenseStatus::InvalidKey: return "InvalidKey";
        case LicenseSystemService::LicenseStatus::NetworkError: return "NetworkError";
        case LicenseSystemService::LicenseStatus::GracePeriodActive: return "GracePeriodActive";
        case LicenseSystemService::LicenseStatus::GracePeriodExpired: return "GracePeriodExpired";
        case LicenseSystemService::LicenseStatus::UserLockedOut: return "UserLockedOut";
    }

    return "Unknown";
}

juce::String extractStringProperty(const juce::var& source, const char* primary, const char* fallback = nullptr)
{
    if (!source.isObject()) {
        return {};
    }

    if (auto* obj = source.getDynamicObject()) {
        auto value = obj->getProperty(juce::Identifier(primary));
        if (!value.isVoid()) {
            auto str = value.toString();
            if (str.isNotEmpty()) {
                return str;
            }
        }

        if (fallback != nullptr) {
            value = obj->getProperty(juce::Identifier(fallback));
            if (!value.isVoid()) {
                auto str = value.toString();
                if (str.isNotEmpty()) {
                    return str;
                }
            }
        }
    }

    return {};
}

int extractIntProperty(const juce::var& source, const char* primary, const char* fallback, int defaultValue)
{
    if (!source.isObject()) {
        return defaultValue;
    }

    if (auto* obj = source.getDynamicObject()) {
        auto value = obj->getProperty(juce::Identifier(primary));
        if (!value.isVoid()) {
            return static_cast<int>(value);
        }

        if (fallback != nullptr) {
            value = obj->getProperty(juce::Identifier(fallback));
            if (!value.isVoid()) {
                return static_cast<int>(value);
            }
        }
    }

    return defaultValue;
}

juce::var getNestedObject(const juce::var& source, const char* property)
{
    if (!source.isObject()) {
        return {};
    }

    if (auto* obj = source.getDynamicObject()) {
        const auto value = obj->getProperty(juce::Identifier(property));
        if (!value.isVoid()) {
            return value;
        }
    }

    return {};
}

juce::String extractEventString(const juce::var& payload, const char* primary, const char* fallback = nullptr)
{
    if (payload.isString()) {
        return payload.toString();
    }

    return extractStringProperty(payload, primary, fallback);
}

} // namespace

LicenseSystemService::LicenseSystemService(EventBus& eventBus)
    : eventBus_(eventBus)
{
}

LicenseSystemService::~LicenseSystemService()
{
    juce::Logger::writeToLog("[LicenseSystem] ~LicenseSystemService DESTRUCTOR called!");

    // Debug: log to file
    auto debugFile = juce::File::getSpecialLocation(juce::File::userDesktopDirectory).getChildFile("oauth_debug.txt");
    debugFile.appendText("[" + juce::Time::getCurrentTime().toString(true, true) + "] ~LicenseSystemService DESTRUCTOR called!\n");

    teardownEventSubscriptions();
}

void LicenseSystemService::initialise()
{
    juce::Logger::writeToLog("[LicenseSystem] === initialise START ===");

    {
        std::scoped_lock lock(stateMutex_);

        if (!hardwareGenerator_) {
            hardwareGenerator_ = std::make_unique<HardwareFingerprintGenerator>();
        }
        if (!cache_) {
            cache_ = std::make_unique<LicenseCache>();
        }
        if (!sessionManager_) {
            sessionManager_ = std::make_unique<SecureSessionManager>();
        }
        if (!featureManager_) {
            featureManager_ = std::make_unique<FeatureManager>();
        }
        if (!serverClient_) {
            serverClient_ = std::make_unique<LicenseServerClient>();
        }

        serverClient_->setServerUrl(serverUrlOverride_.empty() ? kDefaultLicenseServerUrl : serverUrlOverride_);

        // MIGRATION: Before generating hardware ID, check if we have an existing
        // cached license with a hardware ID that we should preserve.
        // This handles the case where users have a valid license but the new
        // persistent hardware ID cache doesn't exist yet.
        if (cache_ && hardwareGenerator_) {
            juce::Logger::writeToLog("[LicenseSystem] Checking for existing license to migrate hardware ID...");

            // Try to find any cached license entries with hardware IDs
            // The cache stores entries keyed by hardware ID, so we need to check
            // if there are any entries we can migrate from
            auto allEntries = cache_->getAllCachedHardwareIds();
            for (const auto& existingHwId : allEntries) {
                if (!existingHwId.empty() && existingHwId.length() == 64) {
                    juce::Logger::writeToLog("[LicenseSystem] Found existing license with hwid: " +
                        juce::String(existingHwId.substr(0, 16)) + "...");

                    // Try to migrate this hardware ID to the persistent disk cache
                    if (hardwareGenerator_->migrateFromExistingHardwareId(existingHwId)) {
                        juce::Logger::writeToLog("[LicenseSystem] Successfully migrated hardware ID from existing license");
                        break;
                    }
                }
            }
        }

        // Now generate hardware ID (will use disk cache if migration succeeded,
        // or existing disk cache, or generate new one)
        currentHardwareId_ = hardwareGenerator_ ? hardwareGenerator_->generateHardwareId() : std::string{};

        juce::Logger::writeToLog("[LicenseSystem] Using hardware ID: " +
            juce::String(currentHardwareId_.empty() ? "(empty)" : currentHardwareId_.substr(0, 16) + "..."));

        loadCachedLicense();
    }

    setupEventSubscriptions();

    // Perform startup validation SYNCHRONOUSLY so the app can check license status
    // before showing the main window
    performStartupValidation();

    publishStatusUpdate("initialise");

    juce::Logger::writeToLog("[LicenseSystem] === initialise END ===");
}

void LicenseSystemService::shutdown()
{
    teardownEventSubscriptions();

    std::scoped_lock lock(stateMutex_);
    cache_.reset();
    sessionManager_.reset();
    serverClient_.reset();
    featureManager_.reset();
    hardwareGenerator_.reset();
    resetCurrentLicense();
    currentHardwareId_.clear();
    lastResultMessage_.clear();
    currentStatus_ = LicenseStatus::NotInitialised;
}

LicenseSystemService::LicenseStatus LicenseSystemService::validateLicense(const std::string& licenseKey)
{
    if (licenseKey.empty()) {
        {
            std::scoped_lock lock(stateMutex_);
            lastResultMessage_ = "License key is empty";
            currentStatus_ = LicenseStatus::InvalidKey;
        }
        publishStatusUpdate("validation_input_error");
        return LicenseStatus::InvalidKey;
    }

    std::string hardwareId;
    {
        std::scoped_lock lock(stateMutex_);
        if (!serverClient_ || !hardwareGenerator_ || !cache_) {
            juce::Logger::writeToLog("[LicenseSystem] validateLicense called before initialise()");
            currentStatus_ = LicenseStatus::NotInitialised;
            lastResultMessage_ = "Service not initialised";
            publishStatusUpdate("validation_not_initialised");
            return currentStatus_;
        }

        hardwareId = currentHardwareId_.empty() && hardwareGenerator_ ? hardwareGenerator_->generateHardwareId() : currentHardwareId_;
        if (currentHardwareId_.empty()) {
            currentHardwareId_ = hardwareId;
        }
    }

    LicenseServerClient::ValidationRequest request;
    request.licenseKey = licenseKey;
    request.hardwareId = hardwareId;
    request.appVersion = kClientVersion;
    request.userAgent = kClientUserAgent;
    request.ipAddress = {};

    const auto response = serverClient_->validateLicense(request);
    return applyServerResponse(licenseKey,
                               hardwareId,
                               response.valid,
                               response.statusCode,
                               response.licenseInfo,
                               response.validationInfo,
                               response.message);
}

bool LicenseSystemService::activateLicense(const std::string& licenseKey,
                                           const std::string& email,
                                           const std::string& password)
{
    std::string hardwareId;
    {
        std::scoped_lock lock(stateMutex_);
        if (!serverClient_ || !hardwareGenerator_ || !cache_) {
            juce::Logger::writeToLog("[LicenseSystem] activateLicense called before initialise()");
            currentStatus_ = LicenseStatus::NotInitialised;
            lastResultMessage_ = "Service not initialised";
            publishStatusUpdate("activation_not_initialised");
            return false;
        }

        hardwareId = currentHardwareId_.empty() && hardwareGenerator_ ? hardwareGenerator_->generateHardwareId() : currentHardwareId_;
        if (currentHardwareId_.empty()) {
            currentHardwareId_ = hardwareId;
        }
    }

    LicenseServerClient::ActivationRequest request;
    request.licenseKey = licenseKey;
    request.hardwareId = hardwareId;
    request.email = email;
    request.password = password;

    const auto response = serverClient_->activateLicense(request);
    const auto status = applyServerResponse(licenseKey,
                                            hardwareId,
                                            response.valid,
                                            response.statusCode,
                                            response.licenseInfo,
                                            response.validationInfo,
                                            response.message);
    return status == LicenseStatus::Valid;
}

bool LicenseSystemService::deactivateLicense()
{
    juce::Logger::writeToLog("[LicenseSystem] === deactivateLicense START ===");

    std::string licenseKey;
    std::string hardwareId;

    {
        std::scoped_lock lock(stateMutex_);
        if (!serverClient_ || !cache_) {
            juce::Logger::writeToLog("[LicenseSystem] deactivateLicense called before initialise()");
            lastResultMessage_ = "Service not initialised";
            return false;
        }

        hardwareId = currentHardwareId_;

        // Get license key from cache - it's stored in entry.licenseKey, not in licenseData
        auto cachedEntry = cache_->retrieveLicense(hardwareId);
        if (cachedEntry.has_value()) {
            // The license key is stored directly in the CacheEntry struct
            licenseKey = cachedEntry->licenseKey;
            juce::Logger::writeToLog("[LicenseSystem] Found cached license key: " +
                juce::String(licenseKey.empty() ? "(empty)" : licenseKey.substr(0, 8) + "..."));
        }
    }

    if (licenseKey.empty()) {
        juce::Logger::writeToLog("[LicenseSystem] No license key found to deactivate");
        std::scoped_lock lock(stateMutex_);
        lastResultMessage_ = "No active license to deactivate";
        return false;
    }

    // Call server to deactivate
    juce::Logger::writeToLog("[LicenseSystem] Calling server to deactivate license...");
    const auto response = serverClient_->deactivateLicense(licenseKey, hardwareId);

    {
        std::scoped_lock lock(stateMutex_);
        if (response.valid || response.statusCode == 200) {
            // Success - clear local cache
            juce::Logger::writeToLog("[LicenseSystem] Deactivation successful, clearing local cache");
            cache_->clearCache();
            resetCurrentLicense();
            currentStatus_ = LicenseStatus::InvalidKey;
            lastResultMessage_ = "License deactivated successfully";
            publishStatusUpdate("deactivation_success");
            return true;
        } else if (response.statusCode == 0) {
            // Network error - still clear local cache but warn user
            juce::Logger::writeToLog("[LicenseSystem] Network error during deactivation, clearing local cache anyway");
            cache_->clearCache();
            resetCurrentLicense();
            currentStatus_ = LicenseStatus::InvalidKey;
            lastResultMessage_ = "Deactivated locally (server unreachable - may need to deactivate online later)";
            publishStatusUpdate("deactivation_offline");
            return true;
        } else if (response.statusCode == 404) {
            // Server endpoint not found (likely OAuth-based license without deactivation endpoint)
            // Still clear local cache to deactivate this machine
            juce::Logger::writeToLog("[LicenseSystem] Server deactivation endpoint not found (404), clearing local cache");
            cache_->clearCache();
            if (sessionManager_) {
                sessionManager_->clearSession(hardwareId);
            }
            resetCurrentLicense();
            currentStatus_ = LicenseStatus::InvalidKey;
            lastResultMessage_ = "License deactivated locally";
            publishStatusUpdate("deactivation_local");
            return true;
        } else {
            // Server rejected deactivation - still clear local cache
            juce::Logger::writeToLog("[LicenseSystem] Server deactivation failed: " + response.message + ", clearing local cache anyway");
            cache_->clearCache();
            if (sessionManager_) {
                sessionManager_->clearSession(hardwareId);
            }
            resetCurrentLicense();
            currentStatus_ = LicenseStatus::InvalidKey;
            lastResultMessage_ = "License deactivated locally (server: " + response.message + ")";
            publishStatusUpdate("deactivation_local");
            return true;
        }
    }
}

bool LicenseSystemService::isFeatureEnabled(const std::string& featureName) const
{
    std::scoped_lock lock(stateMutex_);

    if (featureManager_) {
        const auto& map = featureNameMap();
        const auto it = map.find(featureName);
        if (it != map.end()) {
            return featureManager_->isFeatureEnabled(it->second, currentLicense_.licenseType);
        }
    }

    const auto predicate = [&](const std::string& feature) { return feature == featureName; };
    return std::any_of(currentLicense_.features.begin(), currentLicense_.features.end(), predicate);
}

LicenseSystemService::LicenseInfo LicenseSystemService::getCurrentLicenseInfo() const
{
    std::scoped_lock lock(stateMutex_);
    return currentLicense_;
}

std::string LicenseSystemService::generateHardwareId() const
{
    if (!hardwareGenerator_) {
        return {};
    }

    return hardwareGenerator_->generateHardwareId();
}

bool LicenseSystemService::isHardwareValid() const
{
    if (!hardwareGenerator_) {
        return false;
    }

    return hardwareGenerator_->verifyHardwareIdConsistency();
}

bool LicenseSystemService::hasOfflineGrace() const
{
    std::scoped_lock lock(stateMutex_);

    if (!cache_) {
        return false;
    }

    const auto status = cache_->getOfflineGraceStatus(currentHardwareId_);
    return status.hasGrace;
}

std::chrono::minutes LicenseSystemService::getRemainingGraceTime() const
{
    std::scoped_lock lock(stateMutex_);

    if (!cache_) {
        return std::chrono::minutes{0};
    }

    const auto status = cache_->getOfflineGraceStatus(currentHardwareId_);
    return status.remainingTime;
}

void LicenseSystemService::setServerUrl(const std::string& url)
{
    std::scoped_lock lock(stateMutex_);
    serverUrlOverride_ = url;
    if (serverClient_) {
        serverClient_->setServerUrl(url);
    }
}

std::string LicenseSystemService::getServerUrl() const
{
    std::scoped_lock lock(stateMutex_);
    if (!serverClient_) {
        return serverUrlOverride_.empty() ? std::string{kDefaultLicenseServerUrl} : serverUrlOverride_;
    }
    return serverUrlOverride_.empty() ? kDefaultLicenseServerUrl : serverUrlOverride_;
}

LicenseSystemService::LicenseStatus LicenseSystemService::getCurrentStatus() const
{
    std::scoped_lock lock(stateMutex_);
    return currentStatus_;
}

std::string LicenseSystemService::getLastResultMessage() const
{
    std::scoped_lock lock(stateMutex_);
    return lastResultMessage_;
}

std::string LicenseSystemService::getAccessToken() const
{
    if (!sessionManager_ || currentHardwareId_.empty()) {
        return "";
    }

    auto session = sessionManager_->retrieveSession(currentHardwareId_);
    if (session.has_value()) {
        return session->accessToken;
    }
    return "";
}

void LicenseSystemService::loadCachedLicense()
{
    if (!cache_) {
        currentStatus_ = LicenseStatus::NotInitialised;
        resetCurrentLicense();
        lastResultMessage_.clear();
        return;
    }

    const auto cached = cache_->retrieveLicense(currentHardwareId_);
    if (!cached.has_value()) {
        currentStatus_ = LicenseStatus::InvalidKey;
        resetCurrentLicense();
        lastResultMessage_ = "No cached license";
        return;
    }

    populateLicenseInfoFromVar(cached->licenseData);
    currentHardwareId_ = cached->hardwareId;

    if (cache_->isCacheValid(currentHardwareId_)) {
        currentStatus_ = LicenseStatus::Valid;
        lastResultMessage_.clear();
    } else {
        const auto graceStatus = cache_->getOfflineGraceStatus(currentHardwareId_);
        if (graceStatus.hasGrace) {
            currentStatus_ = LicenseStatus::GracePeriodActive;
            lastResultMessage_ = "Offline grace period active";
        } else {
            currentStatus_ = LicenseStatus::GracePeriodExpired;
            lastResultMessage_ = "Cached license expired";
            resetCurrentLicense();
        }
    }
}

void LicenseSystemService::resetCurrentLicense()
{
    currentLicense_ = LicenseInfo{};
}

void LicenseSystemService::setupEventSubscriptions()
{
    if (!subscriptions_.empty()) {
        return;
    }

    auto subscribe = [this](const std::string& topic, EventBus::Handler handler) {
        const auto id = eventBus_.subscribe(topic, std::move(handler));
        subscriptions_.push_back({topic, id});
    };

    subscribe("license.validate", [this](const juce::var& payload) {
        const auto licenseKey = extractEventString(payload, "license_key", "licenseKey");
        if (licenseKey.isEmpty()) {
            juce::Logger::writeToLog("[LicenseSystem] license.validate missing license key");
            return;
        }

        const auto status = validateLicense(licenseKey.toStdString());

        juce::DynamicObject::Ptr result(new juce::DynamicObject());
        result->setProperty("license_key", licenseKey);
        result->setProperty("status", static_cast<int>(status));
        result->setProperty("statusName", toStatusString(status));
        result->setProperty("valid", status == LicenseStatus::Valid);
        result->setProperty("message", juce::String(lastResultMessage_));

        eventBus_.publish("license.validation_result", juce::var(result.get()));
    });

    subscribe("license.activate", [this](const juce::var& payload) {
        const auto licenseKey = extractEventString(payload, "license_key", "licenseKey");
        const auto email = extractEventString(payload, "email");
        const auto password = extractEventString(payload, "password");

        if (licenseKey.isEmpty() || email.isEmpty() || password.isEmpty()) {
            juce::Logger::writeToLog("[LicenseSystem] license.activate missing credentials");
            return;
        }

        const bool success = activateLicense(licenseKey.toStdString(), email.toStdString(), password.toStdString());

        LicenseStatus statusSnapshot;
        {
            std::scoped_lock lock(stateMutex_);
            statusSnapshot = currentStatus_;
        }

        juce::DynamicObject::Ptr result(new juce::DynamicObject());
        result->setProperty("license_key", licenseKey);
        result->setProperty("success", success);
        result->setProperty("status", static_cast<int>(statusSnapshot));
        result->setProperty("statusName", toStatusString(statusSnapshot));
        result->setProperty("message", juce::String(lastResultMessage_));

        eventBus_.publish("license.activation_result", juce::var(result.get()));
    });

    subscribe("license.check_feature", [this](const juce::var& payload) {
        const auto featureName = extractEventString(payload, "feature", "feature_name");
        if (featureName.isEmpty()) {
            juce::Logger::writeToLog("[LicenseSystem] license.check_feature missing feature name");
            return;
        }

        const bool enabled = isFeatureEnabled(featureName.toStdString());

        juce::DynamicObject::Ptr result(new juce::DynamicObject());
        result->setProperty("feature", featureName);
        result->setProperty("enabled", enabled);

        eventBus_.publish("license.feature_result", juce::var(result.get()));
    });

    subscribe("license.status_request", [this](const juce::var&) {
        publishStatusUpdate("status_request");
    });

    subscribe("app.startup", [this](const juce::var&) {
        performStartupValidation();
    });
}

void LicenseSystemService::teardownEventSubscriptions()
{
    for (const auto& entry : subscriptions_) {
        eventBus_.unsubscribe(entry.first, entry.second);
    }

    subscriptions_.clear();
}

void LicenseSystemService::performStartupValidation()
{
    juce::Logger::writeToLog("[LicenseSystem] === performStartupValidation START ===");

    // Log hardware ID status (safe for any length)
    if (currentHardwareId_.empty()) {
        juce::Logger::writeToLog("[LicenseSystem] WARNING: currentHardwareId_ is EMPTY!");
    } else {
        juce::Logger::writeToLog("[LicenseSystem] currentHardwareId_ length: " + juce::String((int)currentHardwareId_.size()));
    }

    // First check if we have a valid local cache
    bool hasCachedLicense = false;
    std::string cachedLicenseKey;
    bool earlyReturn = false;

    {
        std::scoped_lock lock(stateMutex_);
        if (!cache_) {
            currentStatus_ = LicenseStatus::NotInitialised;
            juce::Logger::writeToLog("[LicenseSystem] No cache available");
            earlyReturn = true;
        } else {
            juce::Logger::writeToLog("[LicenseSystem] cache_ exists, calling retrieveLicense...");
            auto cachedEntry = cache_->retrieveLicense(currentHardwareId_);
            juce::Logger::writeToLog("[LicenseSystem] retrieveLicense returned: " +
                juce::String(cachedEntry.has_value() ? "has_value" : "nullopt"));
            hasCachedLicense = cachedEntry.has_value();

            if (hasCachedLicense) {
                // Extract license key - it's stored directly in CacheEntry::licenseKey
                cachedLicenseKey = cachedEntry->licenseKey;
                juce::Logger::writeToLog("[LicenseSystem] Found CacheEntry.licenseKey: " +
                    juce::String(cachedLicenseKey.empty() ? "(empty)" : cachedLicenseKey.substr(0, 8) + "..."));

                // Fallback: also check inside licenseData for backwards compatibility
                if (cachedLicenseKey.empty() && cachedEntry->licenseData.isObject()) {
                    auto* obj = cachedEntry->licenseData.getDynamicObject();
                    if (obj && obj->hasProperty("licenseKey")) {
                        cachedLicenseKey = obj->getProperty("licenseKey").toString().toStdString();
                        juce::Logger::writeToLog("[LicenseSystem] Fallback: found licenseKey in licenseData");
                    }
                }
                juce::Logger::writeToLog("[LicenseSystem] Cached license found, will validate with server");
            } else {
                juce::Logger::writeToLog("[LicenseSystem] No cached license found");
            }
        }
    }

    // Early return if no cache
    if (earlyReturn) {
        juce::Logger::writeToLog("[LicenseSystem] No cache - activation required");
        return;
    }

    // If no cached license OR no license key extracted, require activation immediately
    if (!hasCachedLicense || cachedLicenseKey.empty()) {
        {
            std::scoped_lock lock(stateMutex_);
            currentStatus_ = LicenseStatus::InvalidKey;
            lastResultMessage_ = "License activation required";
            if (cache_) cache_->clearCache();  // Clear any invalid cache
        }
        juce::Logger::writeToLog("[LicenseSystem] No valid license - activation required (hasCached=" +
            juce::String(hasCachedLicense ? "Y" : "N") + ", hasKey=" +
            juce::String(!cachedLicenseKey.empty() ? "Y" : "N") + ")");
        return;
    }

    // Check if we need periodic re-validation (3 days = 72 hours)
    constexpr int REVALIDATION_INTERVAL_HOURS = 72;  // 3 days
    bool needsRevalidation = cache_->needsPeriodicValidation(currentHardwareId_, REVALIDATION_INTERVAL_HOURS);

    if (!needsRevalidation) {
        // Within 3-day window - use cached license directly without server check
        juce::Logger::writeToLog("[LicenseSystem] Within 3-day window - using cached license");
        std::scoped_lock lock(stateMutex_);
        currentStatus_ = LicenseStatus::Valid;
        lastResultMessage_.clear();
        loadCachedLicense();
        return;
    }

    juce::Logger::writeToLog("[LicenseSystem] Cache expired (>7 days) - re-authentication required");

    // Beyond 7 days - require browser re-authentication
    // We don't clear the cache, just require re-login
    {
        std::scoped_lock lock(stateMutex_);
        currentStatus_ = LicenseStatus::InvalidKey;
        lastResultMessage_ = "Please log in again to continue using SAMPLAR";
        resetCurrentLicense();
    }

    juce::Logger::writeToLog("[LicenseSystem] Startup validation result: " +
        juce::String(static_cast<int>(currentStatus_)) + " - " + juce::String(lastResultMessage_));
    publishStatusUpdate("startup_validation");
}

void LicenseSystemService::publishStatusUpdate(const std::string& reason) const
{
    juce::var payload;
    {
        std::scoped_lock lock(stateMutex_);
        payload = buildStatusPayload(reason);
    }

    eventBus_.publish("license.status", payload);
}

juce::var LicenseSystemService::buildStatusPayload(const std::string& reason) const
{
    juce::DynamicObject::Ptr root(new juce::DynamicObject());
    root->setProperty("status", static_cast<int>(currentStatus_));
    root->setProperty("statusName", toStatusString(currentStatus_));
    root->setProperty("hardwareId", juce::String(currentHardwareId_));
    root->setProperty("reason", juce::String(reason));
    root->setProperty("message", juce::String(lastResultMessage_));

    auto licenseObject = new juce::DynamicObject();
    licenseObject->setProperty("customerEmail", juce::String(currentLicense_.customerEmail));
    licenseObject->setProperty("licenseType", juce::String(currentLicense_.licenseType));
    licenseObject->setProperty("maxActivations", currentLicense_.maxActivations);
    licenseObject->setProperty("currentActivations", currentLicense_.currentActivations);

    if (currentLicense_.expiresAt.time_since_epoch().count() != 0) {
        const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(currentLicense_.expiresAt.time_since_epoch()).count();
        licenseObject->setProperty("expiresAtMs", static_cast<double>(millis));
        licenseObject->setProperty("expiresAt", juce::Time(millis).toISO8601(true));
    }

    juce::Array<juce::var> featureArray;
    for (const auto& feature : currentLicense_.features) {
        featureArray.add(juce::String(feature));
    }
    licenseObject->setProperty("features", featureArray);

    root->setProperty("license", juce::var(licenseObject));

    if (cache_) {
        const auto grace = cache_->getOfflineGraceStatus(currentHardwareId_);
        auto graceObject = new juce::DynamicObject();
        graceObject->setProperty("hasGrace", grace.hasGrace);
        graceObject->setProperty("remainingMinutes", static_cast<int>(grace.remainingTime.count()));
        graceObject->setProperty("graceExpiresAt", grace.graceExpiresAt.time_since_epoch().count() == 0
                                                       ? juce::String()
                                                       : juce::Time(std::chrono::duration_cast<std::chrono::milliseconds>(grace.graceExpiresAt.time_since_epoch()).count()).toISO8601(true));
        root->setProperty("grace", juce::var(graceObject));
    }

    return juce::var(root.get());
}

void LicenseSystemService::populateLicenseInfoFromVar(const juce::var& licenseVar)
{
    juce::var target = licenseVar;
    if (target.isObject()) {
        const auto nested = getNestedObject(target, "license_info");
        if (!nested.isVoid()) {
            target = nested;
        }
    }

    LicenseInfo info{};

    if (target.isObject()) {
        info.customerEmail = extractStringProperty(target, "customer_email", "customerEmail").toStdString();
        const auto type = extractStringProperty(target, "license_type", "licenseType");
        info.licenseType = type.isNotEmpty() ? type.toStdString() : std::string{};
        info.maxActivations = extractIntProperty(target, "max_activations", "maxActivations", 0);
        info.currentActivations = extractIntProperty(target, "current_activations", "currentActivations", 0);

        info.features.clear();
        juce::var features = getNestedObject(target, "features");
        if (features.isArray()) {
            if (auto* arr = features.getArray()) {
                for (const auto& entry : *arr) {
                    info.features.emplace_back(entry.toString().toStdString());
                }
            }
        } else if (features.isString() && features.toString().isNotEmpty()) {
            info.features.emplace_back(features.toString().toStdString());
        }

        const auto expiresIso = extractStringProperty(target, "expires_at", "expiresAt");
        if (expiresIso.isNotEmpty()) {
            const auto time = juce::Time::fromISO8601(expiresIso);
            if (time != juce::Time()) {
                info.expiresAt = std::chrono::system_clock::time_point(std::chrono::milliseconds(time.toMilliseconds()));
            }
        } else {
            juce::var expiresMsVar = getNestedObject(target, "expiresAtMs");
            if (!expiresMsVar.isVoid()) {
                const auto ms = static_cast<int64_t>(expiresMsVar);
                info.expiresAt = std::chrono::system_clock::time_point(std::chrono::milliseconds(ms));
            }
        }
    }

    currentLicense_ = std::move(info);
}

juce::var LicenseSystemService::buildLicenseVar() const
{
    auto* licenseObject = new juce::DynamicObject();
    licenseObject->setProperty("customerEmail", juce::String(currentLicense_.customerEmail));
    licenseObject->setProperty("licenseType", juce::String(currentLicense_.licenseType));
    licenseObject->setProperty("maxActivations", currentLicense_.maxActivations);
    licenseObject->setProperty("currentActivations", currentLicense_.currentActivations);

    juce::Array<juce::var> featureArray;
    for (const auto& feature : currentLicense_.features) {
        featureArray.add(juce::String(feature));
    }
    licenseObject->setProperty("features", featureArray);

    if (currentLicense_.expiresAt.time_since_epoch().count() != 0) {
        const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(currentLicense_.expiresAt.time_since_epoch()).count();
        licenseObject->setProperty("expiresAtMs", static_cast<double>(millis));
    }

    return juce::var(licenseObject);
}

LicenseSystemService::LicenseStatus LicenseSystemService::mapStatusFromResponse(int statusCode, bool valid) const
{
    if (valid) {
        return LicenseStatus::Valid;
    }

    if (statusCode <= 0) {
        return LicenseStatus::NetworkError;
    }

    switch (statusCode) {
        case 401:
        case 404:
            return LicenseStatus::InvalidKey;
        case 402:
        case 429:
            return LicenseStatus::ActivationLimitExceeded;
        case 403:
        case 409:
            return LicenseStatus::HardwareMismatch;
        case 410:
            return LicenseStatus::Expired;
        case 423:
            return LicenseStatus::UserLockedOut;
        case 428:
            return LicenseStatus::GracePeriodExpired;
        default:
            break;
    }

    if (statusCode >= 500) {
        return LicenseStatus::NetworkError;
    }

    return LicenseStatus::InvalidKey;
}

LicenseSystemService::LicenseStatus LicenseSystemService::applyServerResponse(const std::string& licenseKey,
                                                                              const std::string& hardwareId,
                                                                              bool valid,
                                                                              int statusCode,
                                                                              const juce::var& licenseInfo,
                                                                              const juce::var& validationInfo,
                                                                              const std::string& message)
{
    LicenseStatus statusAfter = LicenseStatus::NetworkError;

    {
        std::scoped_lock lock(stateMutex_);
        lastResultMessage_ = message;

        if (!hardwareId.empty()) {
            currentHardwareId_ = hardwareId;
        }

        const auto resolvedStatus = mapStatusFromResponse(statusCode, valid);

        if (valid) {
            if (!licenseInfo.isVoid()) {
                populateLicenseInfoFromVar(licenseInfo);
            } else {
                populateLicenseInfoFromVar(buildLicenseVar());
            }

            if (cache_) {
                const auto dataToStore = !licenseInfo.isVoid() ? licenseInfo : buildLicenseVar();
                cache_->storeLicense(licenseKey, dataToStore, currentHardwareId_);
                cache_->updateLastValidation(currentHardwareId_);
            }

            if (sessionManager_ && validationInfo.isObject()) {
                if (auto* obj = validationInfo.getDynamicObject()) {
                    SecureSessionManager::SessionData session{};
                    session.accessToken = obj->getProperty(juce::Identifier("access_token")).toString().toStdString();
                    session.userEmail = obj->getProperty(juce::Identifier("email")).toString().toStdString();
                    session.userId = static_cast<int>(obj->getProperty(juce::Identifier("user_id")));
                    session.hardwareId = currentHardwareId_;

                    const auto expiresVar = obj->getProperty(juce::Identifier("expires_at_ms"));
                    if (!expiresVar.isVoid()) {
                        const auto ms = static_cast<int64_t>(expiresVar);
                        session.expiresAt = std::chrono::system_clock::time_point(std::chrono::milliseconds(ms));
                    }

                    if (!session.accessToken.empty() || !session.userEmail.empty()) {
                        sessionManager_->storeSession(session);
                    }
                }
            }

            currentStatus_ = LicenseStatus::Valid;
        } else {
            currentStatus_ = resolvedStatus;

            if (currentStatus_ != LicenseStatus::GracePeriodActive) {
                resetCurrentLicense();
            }

            if (cache_ && (currentStatus_ == LicenseStatus::InvalidKey ||
                           currentStatus_ == LicenseStatus::HardwareMismatch ||
                           currentStatus_ == LicenseStatus::ActivationLimitExceeded)) {
                cache_->clearCache();
            }
        }

        statusAfter = currentStatus_;
    }

    publishStatusUpdate(valid ? "validation_success" : "validation_failure");
    return statusAfter;
}

LicenseSystemService::LicenseStatus LicenseSystemService::validateUserCredentials(const std::string& email, const std::string& password)
{
    if (email.empty() || password.empty()) {
        {
            std::scoped_lock lock(stateMutex_);
            lastResultMessage_ = "Email and password are required";
            currentStatus_ = LicenseStatus::InvalidKey;
        }
        return LicenseStatus::InvalidKey;
    }

    // Perform credential validation via the server
    if (!serverClient_) {
        {
            std::scoped_lock lock(stateMutex_);
            lastResultMessage_ = "License server client not available";
            currentStatus_ = LicenseStatus::NetworkError;
        }
        return LicenseStatus::NetworkError;
    }

    try {
        // Call server login endpoint
        auto result = serverClient_->loginWithCredentials(email, password);
        {
            std::scoped_lock lock(stateMutex_);
            if (result.valid) {
                currentStatus_ = LicenseStatus::Valid;
                lastResultMessage_ = "Credentials validated successfully";
            } else {
                currentStatus_ = LicenseStatus::InvalidKey;
                lastResultMessage_ = result.message;
            }
        }

        publishStatusUpdate(result.valid ? "credential_validation_success" : "credential_validation_failure");
        return result.valid ? LicenseStatus::Valid : LicenseStatus::InvalidKey;
    }
    catch (const std::exception& e) {
        {
            std::scoped_lock lock(stateMutex_);
            lastResultMessage_ = "Network error during credential validation";
            currentStatus_ = LicenseStatus::NetworkError;
        }
        publishStatusUpdate("credential_validation_network_error");
        return LicenseStatus::NetworkError;
    }
}

bool LicenseSystemService::activateWithCredentials(const std::string& email, const std::string& password)
{
    if (email.empty() || password.empty()) {
        {
            std::scoped_lock lock(stateMutex_);
            lastResultMessage_ = "Email and password are required for activation";
            currentStatus_ = LicenseStatus::InvalidKey;
        }
        return false;
    }

    std::string hardwareId;
    bool handledDebug = false;

    try {
        hardwareId = generateHardwareId();
    } catch (const std::exception& e) {
        {
            std::scoped_lock lock(stateMutex_);
            lastResultMessage_ = "Failed to generate hardware ID";
            currentStatus_ = LicenseStatus::NetworkError;
        }
        return false;
    }

    // Perform activation via the server
    if (!serverClient_) {
        {
            std::scoped_lock lock(stateMutex_);
            lastResultMessage_ = "License server client not available";
            currentStatus_ = LicenseStatus::NetworkError;
        }
        return false;
    }

    try {
        // Call server activation endpoint with credentials and hardware ID
        auto result = serverClient_->activateWithCredentials(email, password, hardwareId);
        {
            std::scoped_lock lock(stateMutex_);
            if (result.valid) {
                currentStatus_ = LicenseStatus::Valid;
                lastResultMessage_ = "Activation successful";
                currentHardwareId_ = hardwareId;

                // Cache the license information if available
                if (cache_ && result.licenseInfo.isObject()) {
                    // Log what the server returned for debugging
                    juce::Logger::writeToLog("[LicenseSystem] Server returned licenseInfo: " +
                        juce::JSON::toString(result.licenseInfo, true));

                    // Extract the license key from the server response
                    std::string licenseKey;
                    if (auto* infoObj = result.licenseInfo.getDynamicObject()) {
                        // Try different possible field names for license key
                        auto keyVar = infoObj->getProperty("license_key");
                        juce::Logger::writeToLog("[LicenseSystem] Checking license_key: " +
                            (keyVar.isVoid() ? juce::String("(not found)") : keyVar.toString()));

                        if (keyVar.isVoid()) {
                            keyVar = infoObj->getProperty("licenseKey");
                            juce::Logger::writeToLog("[LicenseSystem] Checking licenseKey: " +
                                (keyVar.isVoid() ? juce::String("(not found)") : keyVar.toString()));
                        }
                        if (keyVar.isVoid()) {
                            keyVar = infoObj->getProperty("key");
                            juce::Logger::writeToLog("[LicenseSystem] Checking key: " +
                                (keyVar.isVoid() ? juce::String("(not found)") : keyVar.toString()));
                        }

                        if (!keyVar.isVoid()) {
                            licenseKey = keyVar.toString().toStdString();
                        }

                        // Ensure licenseKey is stored in the data for later retrieval
                        if (!licenseKey.empty()) {
                            infoObj->setProperty("licenseKey", juce::String(licenseKey));
                            juce::Logger::writeToLog("[LicenseSystem] Stored licenseKey in data: " +
                                juce::String(licenseKey.substr(0, 8)) + "...");
                        }
                    }

                    // If no license key found in response, use email as fallback identifier
                    if (licenseKey.empty()) {
                        juce::Logger::writeToLog("[LicenseSystem] WARNING: No license_key in server response, using email as cache key");
                        licenseKey = email;
                        // Store email as licenseKey in the data for later retrieval
                        if (auto* infoObj = result.licenseInfo.getDynamicObject()) {
                            infoObj->setProperty("licenseKey", juce::String(email));
                            juce::Logger::writeToLog("[LicenseSystem] Stored email as licenseKey: " +
                                juce::String(email.substr(0, 5)) + "...");
                        }
                    }

                    juce::Logger::writeToLog("[LicenseSystem] Caching license with key: " +
                        juce::String(licenseKey.substr(0, 8)) + "...");
                    cache_->storeLicense(licenseKey, result.licenseInfo, hardwareId);
                    juce::Logger::writeToLog("[LicenseSystem] License cached successfully");
                }
            } else {
                currentStatus_ = LicenseStatus::InvalidKey;
                lastResultMessage_ = result.message;
            }
        }

        publishStatusUpdate(result.valid ? "credential_activation_success" : "credential_activation_failure");
        return result.valid;
    }
    catch (const std::exception& e) {
        {
            std::scoped_lock lock(stateMutex_);
            lastResultMessage_ = "Network error during activation";
            currentStatus_ = LicenseStatus::NetworkError;
        }
        publishStatusUpdate("credential_activation_network_error");
        return false;
    }
}

void LicenseSystemService::startBrowserAuthentication(OAuthCompletionCallback callback)
{
    juce::Logger::writeToLog("[LicenseSystem] === startBrowserAuthentication START ===");

    // Get the server URL and hardware ID
    std::string serverUrl;
    std::string hardwareId;

    {
        std::scoped_lock lock(stateMutex_);

        if (!hardwareGenerator_) {
            juce::Logger::writeToLog("[LicenseSystem] ERROR: hardwareGenerator_ not initialised");
            if (callback) {
                juce::MessageManager::callAsync([callback]() {
                    callback(false, "Service not initialised");
                });
            }
            return;
        }

        serverUrl = serverUrlOverride_.empty() ? kDefaultLicenseServerUrl : serverUrlOverride_;
        hardwareId = currentHardwareId_.empty() ? hardwareGenerator_->generateHardwareId() : currentHardwareId_;

        if (currentHardwareId_.empty()) {
            currentHardwareId_ = hardwareId;
        }

        // Create OAuth authenticator if not exists
        if (!oauthAuthenticator_) {
            oauthAuthenticator_ = std::make_unique<OAuthBrowserAuthenticator>();
        }
    }

    juce::Logger::writeToLog("[LicenseSystem] Starting OAuth flow with server: " + juce::String(serverUrl));
    juce::Logger::writeToLog("[LicenseSystem] Hardware ID: " + juce::String(hardwareId.substr(0, 16)) + "...");

    // Start the OAuth authentication
    oauthAuthenticator_->startAuthentication(serverUrl, hardwareId,
        [this, callback, hardwareId](const OAuthBrowserAuthenticator::AuthResult& result) {
            juce::Logger::writeToLog("[LicenseSystem] OAuth callback received - success: " +
                juce::String(result.success ? "YES" : "NO"));

            if (result.success) {
                juce::Logger::writeToLog("[LicenseSystem] OAuth success - processing license data");
                juce::Logger::writeToLog("[LicenseSystem]   Email: " + juce::String(result.email));
                juce::Logger::writeToLog("[LicenseSystem]   License Type: " + juce::String(result.licenseType));
                juce::Logger::writeToLog("[LicenseSystem]   License Key: " +
                    juce::String(result.licenseKey.empty() ? "(empty)" : result.licenseKey.substr(0, 8) + "..."));

                {
                    std::scoped_lock lock(stateMutex_);

                    // Update license status
                    currentStatus_ = LicenseStatus::Valid;
                    lastResultMessage_ = "Activated via browser login";
                    currentHardwareId_ = hardwareId;

                    // Populate license info from OAuth result
                    currentLicense_.customerEmail = result.email;
                    currentLicense_.licenseType = result.licenseType;

                    // Parse expires_at if available
                    if (!result.expiresAt.empty()) {
                        auto time = juce::Time::fromISO8601(juce::String(result.expiresAt));
                        if (time != juce::Time()) {
                            currentLicense_.expiresAt = std::chrono::system_clock::time_point(
                                std::chrono::milliseconds(time.toMilliseconds()));
                        }
                    }

                    // Cache the license
                    if (cache_ && !result.licenseKey.empty()) {
                        juce::Logger::writeToLog("[LicenseSystem] Caching OAuth license...");
                        cache_->storeLicense(result.licenseKey, result.licenseInfo, hardwareId);
                        cache_->updateLastValidation(hardwareId);
                        juce::Logger::writeToLog("[LicenseSystem] License cached successfully");
                    }

                    // Store session if we have access token
                    if (sessionManager_ && !result.accessToken.empty()) {
                        SecureSessionManager::SessionData session{};
                        session.accessToken = result.accessToken;
                        session.userEmail = result.email;
                        session.hardwareId = hardwareId;

                        if (!result.expiresAt.empty()) {
                            auto time = juce::Time::fromISO8601(juce::String(result.expiresAt));
                            if (time != juce::Time()) {
                                session.expiresAt = std::chrono::system_clock::time_point(
                                    std::chrono::milliseconds(time.toMilliseconds()));
                            }
                        }

                        sessionManager_->storeSession(session);
                        juce::Logger::writeToLog("[LicenseSystem] Session stored");
                    }
                }

                publishStatusUpdate("oauth_activation_success");

                if (callback) {
                    callback(true, "License activated successfully");
                }
            } else {
                juce::Logger::writeToLog("[LicenseSystem] OAuth failed: " + juce::String(result.errorMessage));

                {
                    std::scoped_lock lock(stateMutex_);
                    currentStatus_ = LicenseStatus::InvalidKey;
                    lastResultMessage_ = result.errorMessage;
                }

                publishStatusUpdate("oauth_activation_failure");

                if (callback) {
                    callback(false, result.errorMessage);
                }
            }
        });
}

void LicenseSystemService::cancelBrowserAuthentication()
{
    juce::Logger::writeToLog("[LicenseSystem] Cancelling browser authentication");

    std::scoped_lock lock(stateMutex_);
    if (oauthAuthenticator_) {
        oauthAuthenticator_->cancelAuthentication();
    }
}

bool LicenseSystemService::isBrowserAuthInProgress() const
{
    std::scoped_lock lock(stateMutex_);
    return oauthAuthenticator_ && oauthAuthenticator_->isAuthenticating();
}

} // namespace samplar

