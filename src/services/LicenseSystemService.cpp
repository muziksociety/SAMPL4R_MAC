#include "LicenseSystemService.h"

#include "../security/HardwareFingerprintGenerator.h"
#include "../security/LicenseCache.h"
#include "../security/SecureSessionManager.h"
#include "../security/SignedTokenVerifier.h"
#include "../networking/LicenseServerClient.h"
#include "../networking/OAuthBrowserAuthenticator.h"
#include "../licensing/FeatureManager.h"

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <algorithm>
#include <map>
#include <regex>

namespace samplar {

namespace {
// SAMPL4R production license server (live Render deployment).
// NOTE: the license.samplemonsta.com CNAME is not yet resolving — use the
// Render hostname directly until DNS is live.
constexpr auto kDefaultLicenseServerUrl = "https://license-server-n5it.onrender.com";
constexpr auto kClientVersion = "5.0.0";
constexpr auto kClientUserAgent = "SAMPLAR-sampl4r/5.0.0";
constexpr auto kSamplarProductId = "sampl4r";

// SAMPL4R license key regex:
//   SAMPL4R-{TYPE3}-{HARDWARE_HASH10}-{YYYYMMDD8}-{SIG5}
// where TYPE is PRO/STD/EDU/TRL/PROMO (3 chars), hash is alphanumeric or 0000000000,
// date is 8 digits (or 00000000 for first-use trials), signature is 5 chars.
const std::regex& samplarLicenseRegex()
{
    static const std::regex re(R"(^SAMPL4R-[A-Z0-9]{3}-[A-Z0-9]{10}-[0-9]{8}-[A-Z0-9]{5}$)");
    return re;
}

bool isSamplarKeyFormatValid(const std::string& key)
{
    return std::regex_match(key, samplarLicenseRegex());
}

// Truncate license key for logging — keep first 10 chars (covers "SAMPL4R-PRO")
std::string truncateKeyForLog(const std::string& key)
{
    if (key.size() <= 10) return key;
    return key.substr(0, 10) + "...";
}

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

bool LicenseSystemService::activateWithKey(const std::string& licenseKey)
{
    juce::Logger::writeToLog("[License] === activateWithKey START === key=" +
        juce::String(truncateKeyForLog(licenseKey)));

    // Client-side format check first — fail fast before hitting the server
    if (!isSamplarKeyFormatValid(licenseKey)) {
        std::scoped_lock lock(stateMutex_);
        lastResultMessage_ = "Invalid license key format. Expected: SAMPL4R-XXX-XXXXXXXXXX-YYYYMMDD-XXXXX";
        currentStatus_ = LicenseStatus::InvalidKey;
        juce::Logger::writeToLog("[License] Format check failed for: " +
            juce::String(truncateKeyForLog(licenseKey)));
        publishStatusUpdate("activation_format_invalid");
        return false;
    }

    std::string hardwareId;
    {
        std::scoped_lock lock(stateMutex_);
        if (!serverClient_ || !hardwareGenerator_ || !cache_) {
            currentStatus_ = LicenseStatus::NotInitialised;
            lastResultMessage_ = "Service not initialised";
            return false;
        }
        hardwareId = currentHardwareId_.empty() ? hardwareGenerator_->generateHardwareId() : currentHardwareId_;
        currentHardwareId_ = hardwareId;
    }

    LicenseServerClient::ValidationRequest request;
    request.licenseKey = licenseKey;
    request.hardwareId = hardwareId;
    request.appVersion = kClientVersion;
    request.userAgent = kClientUserAgent;

    const auto response = serverClient_->validateLicense(request);

    const auto status = applyServerResponse(licenseKey,
                                            hardwareId,
                                            response.valid,
                                            response.statusCode,
                                            response.licenseInfo,
                                            response.validationInfo,
                                            response.message);

    if (status == LicenseStatus::Valid) {
        juce::Logger::writeToLog("[License] Activation OK for " +
            juce::String(truncateKeyForLog(licenseKey)));
        // Immediately fetch the signed token so we have offline coverage from t=0
        revalidateCachedLicense();
    } else {
        juce::Logger::writeToLog("[License] Activation failed: " + juce::String(response.message));
    }

    return status == LicenseStatus::Valid;
}

bool LicenseSystemService::activateWithAccount(const std::string& email, const std::string& password)
{
    // Truncate email for logs (first 4 chars + "..."), never log password.
    const auto truncEmail = email.size() > 4 ? email.substr(0, 4) + "..." : email;
    juce::Logger::writeToLog("[License] === activateWithAccount START === email=" +
        juce::String(truncEmail));

    if (email.empty() || password.empty()) {
        std::scoped_lock lock(stateMutex_);
        lastResultMessage_ = "Email and password are required";
        currentStatus_ = LicenseStatus::InvalidKey;
        publishStatusUpdate("account_activation_input_invalid");
        return false;
    }

    std::string hardwareId;
    {
        std::scoped_lock lock(stateMutex_);
        if (!serverClient_ || !hardwareGenerator_ || !cache_) {
            currentStatus_ = LicenseStatus::NotInitialised;
            lastResultMessage_ = "Service not initialised";
            return false;
        }
        hardwareId = currentHardwareId_.empty() ? hardwareGenerator_->generateHardwareId() : currentHardwareId_;
        currentHardwareId_ = hardwareId;
    }

    // Call the live server. This is a blocking network call — callers must
    // invoke from a background thread (see LicenseActivationDialog).
    const auto resp = serverClient_->loginWithHardware(email, password, hardwareId);

    // Handle: !success → auth failed (bad password / unknown user / server error)
    if (!resp.success) {
        std::scoped_lock lock(stateMutex_);
        lastResultMessage_ = resp.message.empty()
            ? std::string("Sign-in failed. Check your email and password.")
            : resp.message;
        // Map HTTP status to a friendlier status
        if (resp.statusCode == 0 || resp.statusCode >= 500) {
            currentStatus_ = LicenseStatus::NetworkError;
        } else if (resp.statusCode == 423) {
            currentStatus_ = LicenseStatus::UserLockedOut;
        } else {
            currentStatus_ = LicenseStatus::InvalidKey; // auth failure
        }
        juce::Logger::writeToLog("[License] account auth FAILED status=" +
            juce::String(resp.statusCode) + " msg=" + juce::String(lastResultMessage_));
        publishStatusUpdate("account_activation_auth_failed");
        return false;
    }

    // Handle: success but license_valid=false → no entitlement on this account
    if (!resp.licenseValid) {
        std::scoped_lock lock(stateMutex_);
        lastResultMessage_ = resp.message.empty()
            ? std::string("No SAMPL4R license on this account — purchase at samplemonsta.com")
            : resp.message;
        currentStatus_ = LicenseStatus::InvalidKey;
        juce::Logger::writeToLog("[License] account auth OK but no SAMPL4R entitlement");
        publishStatusUpdate("account_activation_no_entitlement");
        return false;
    }

    // SUCCESS — verify signed_token (if present) for tamper protection, then cache.
    if (!resp.signedToken.empty()) {
        auto verified = SignedTokenVerifier::verify(resp.signedToken, hardwareId, kSamplarProductId);
        if (!verified.has_value()) {
            std::scoped_lock lock(stateMutex_);
            lastResultMessage_ = "Signed token verification failed — possible MITM";
            currentStatus_ = LicenseStatus::InvalidKey;
            juce::Logger::writeToLog("[License] signed_token HMAC verification FAILED — rejecting");
            publishStatusUpdate("account_activation_token_invalid");
            return false;
        }
    }

    // Build the cached license blob: customer_email + license_type + signed_token.
    // We synthesise an account-style cache entry; the server returns no license_key
    // for account auth, so we key the cache on a constant marker derived from email.
    juce::DynamicObject::Ptr data(new juce::DynamicObject());
    data->setProperty("customer_email", juce::String(resp.email));
    data->setProperty("license_type",   juce::String(resp.licenseType));
    if (!resp.signedToken.empty())
        data->setProperty("signed_token", juce::String(resp.signedToken));
    if (!resp.expiresAt.empty())
        data->setProperty("expires_at",   juce::String(resp.expiresAt));
    // Persist email as the cache "licenseKey" slot so legacy code paths still find
    // something — but it will fail isSamplarKeyFormatValid(), so the legacy key
    // path no longer mistakes this for a real SAMPL4R key. The startup-validation
    // logic has a dedicated "if email-shaped → use account revalidation" branch.
    const std::string cacheKey = std::string("ACCOUNT:") + resp.email;

    {
        std::scoped_lock lock(stateMutex_);
        currentStatus_ = LicenseStatus::Valid;
        lastResultMessage_.clear();
        currentLicense_.customerEmail = resp.email;
        currentLicense_.licenseType   = resp.licenseType;
        if (!resp.expiresAt.empty()) {
            auto t = juce::Time::fromISO8601(juce::String(resp.expiresAt));
            if (t != juce::Time()) {
                currentLicense_.expiresAt = std::chrono::system_clock::time_point(
                    std::chrono::milliseconds(t.toMilliseconds()));
            }
        }
        if (cache_) {
            cache_->storeLicense(cacheKey, juce::var(data.get()), hardwareId);
            cache_->updateLastValidation(hardwareId);
        }
    }

    juce::Logger::writeToLog("[License] account activation SUCCESS email=" +
        juce::String(truncEmail) + " type=" + juce::String(resp.licenseType));
    publishStatusUpdate("account_activation_success");
    return true;
}

LicenseSystemService::LicenseStatus LicenseSystemService::revalidateCachedLicense()
{
    juce::Logger::writeToLog("[License] === revalidateCachedLicense START ===");

    std::string licenseKey;
    std::string hardwareId;

    {
        std::scoped_lock lock(stateMutex_);
        if (!cache_ || !serverClient_) {
            juce::Logger::writeToLog("[License] Service not initialised");
            return LicenseStatus::NotInitialised;
        }
        hardwareId = currentHardwareId_;
        auto entry = cache_->retrieveLicense(hardwareId);
        if (!entry.has_value() || entry->licenseKey.empty()) {
            juce::Logger::writeToLog("[License] No cached license to revalidate");
            return LicenseStatus::InvalidKey;
        }
        licenseKey = entry->licenseKey;
    }

    // Online check
    const auto response = serverClient_->validateToken(licenseKey, hardwareId, kSamplarProductId);

    if (response.valid && !response.signedToken.empty()) {
        // Verify the signed token offline. Even though the server said valid,
        // we re-verify HMAC so an attacker who can MITM cannot fake responses.
        auto verified = SignedTokenVerifier::verify(response.signedToken,
                                                    hardwareId,
                                                    kSamplarProductId);
        if (!verified.has_value()) {
            juce::Logger::writeToLog("[License] Server returned token but HMAC failed — rejecting");
            std::scoped_lock lock(stateMutex_);
            currentStatus_ = LicenseStatus::InvalidKey;
            lastResultMessage_ = "Invalid license signature";
            publishStatusUpdate("token_signature_invalid");
            return LicenseStatus::InvalidKey;
        }

        // Cache token data + refresh last-validation timestamp (resets 30-day grace)
        {
            std::scoped_lock lock(stateMutex_);
            // Re-store the cache entry with the fresh signed token embedded so we
            // can verify offline next time.
            auto entry = cache_->retrieveLicense(hardwareId);
            if (entry.has_value()) {
                if (auto* obj = entry->licenseData.getDynamicObject()) {
                    obj->setProperty("signed_token", juce::String(response.signedToken));
                    cache_->storeLicense(licenseKey, entry->licenseData, hardwareId);
                } else {
                    juce::DynamicObject::Ptr data(new juce::DynamicObject());
                    data->setProperty("signed_token", juce::String(response.signedToken));
                    data->setProperty("customer_email", juce::String(verified->email));
                    cache_->storeLicense(licenseKey, juce::var(data.get()), hardwareId);
                }
            }
            cache_->updateLastValidation(hardwareId);
            currentStatus_ = LicenseStatus::Valid;
            lastResultMessage_.clear();
        }
        publishStatusUpdate("revalidation_online_success");
        return LicenseStatus::Valid;
    }

    // Server unreachable or returned an error — try offline grace
    juce::Logger::writeToLog("[License] Server check failed (status=" +
        juce::String(response.statusCode) + ") — trying offline grace");

    // Use the cached signed_token from last successful online validation
    std::string cachedToken;
    {
        std::scoped_lock lock(stateMutex_);
        auto entry = cache_->retrieveLicense(hardwareId);
        if (entry.has_value() && entry->licenseData.isObject()) {
            if (auto* obj = entry->licenseData.getDynamicObject()) {
                cachedToken = obj->getProperty(juce::Identifier("signed_token")).toString().toStdString();
            }
        }
    }

    const bool networkError = (response.statusCode == 0 || response.statusCode >= 500);

    if (networkError && !cachedToken.empty()) {
        auto verified = SignedTokenVerifier::verify(cachedToken, hardwareId, kSamplarProductId);
        if (verified.has_value()) {
            // Check 30-day offline grace window (720 hours from last validation)
            constexpr int kGraceHours = 30 * 24;
            const auto grace = cache_->getOfflineGraceStatus(hardwareId, kGraceHours);
            if (grace.hasGrace) {
                std::scoped_lock lock(stateMutex_);
                currentStatus_ = LicenseStatus::GracePeriodActive;
                lastResultMessage_ = "Offline — " +
                    std::to_string(grace.remainingTime.count() / (60 * 24)) + " days of grace remaining";
                juce::Logger::writeToLog("[License] Offline grace active: " +
                    juce::String(lastResultMessage_));
                publishStatusUpdate("revalidation_offline_grace");
                return LicenseStatus::GracePeriodActive;
            }
            juce::Logger::writeToLog("[License] 30-day grace expired");
            std::scoped_lock lock(stateMutex_);
            currentStatus_ = LicenseStatus::GracePeriodExpired;
            lastResultMessage_ = "Offline grace expired — please reconnect to verify your license";
            publishStatusUpdate("revalidation_grace_expired");
            return LicenseStatus::GracePeriodExpired;
        }
    }

    // Either non-network error from server, or no cached token — surface server status
    return applyServerResponse(licenseKey,
                               hardwareId,
                               false,
                               response.statusCode,
                               response.licenseInfo,
                               response.validationInfo,
                               response.message);
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

        if (licenseKey.isEmpty()) {
            juce::Logger::writeToLog("[License] license.activate missing license_key");
            return;
        }

        // SAMPL4R: key-only activation when no email/password supplied.
        // Legacy email+password path retained for compatibility.
        const bool success = (email.isEmpty() && password.isEmpty())
            ? activateWithKey(licenseKey.toStdString())
            : activateLicense(licenseKey.toStdString(), email.toStdString(), password.toStdString());

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
        juce::Logger::writeToLog("[License] No valid license - activation required");
        publishStatusUpdate("startup_no_license");
        return;
    }

    // Account-based cache entry: keys are prefixed "ACCOUNT:<email>". For these
    // we don't validate via key regex — instead verify the cached signed_token
    // offline against current hardware. If verification fails, require sign-in.
    const bool isAccountEntry = cachedLicenseKey.rfind("ACCOUNT:", 0) == 0;
    if (isAccountEntry) {
        // Extract + truncate the email portion of the ACCOUNT:<email> key for log safety.
        std::string emailPart = cachedLicenseKey.size() > 8 ? cachedLicenseKey.substr(8) : std::string();
        std::string truncEmail = emailPart;
        if (truncEmail.size() > 6) {
            auto at = truncEmail.find('@');
            if (at != std::string::npos && at > 3) {
                truncEmail = truncEmail.substr(0, 3) + "***" + truncEmail.substr(at);
            }
        }
        juce::Logger::writeToLog("[LicenseSystem] Startup: found ACCOUNT cache for " +
            juce::String(truncEmail) + ", verifying signed_token...");

        std::string cachedToken;
        {
            std::scoped_lock lock(stateMutex_);
            auto entry = cache_->retrieveLicense(currentHardwareId_);
            if (entry.has_value() && entry->licenseData.isObject()) {
                if (auto* obj = entry->licenseData.getDynamicObject()) {
                    cachedToken = obj->getProperty(juce::Identifier("signed_token")).toString().toStdString();
                }
            }
        }

        if (cachedToken.empty()) {
            juce::Logger::writeToLog("[License] Account entry has no signed_token — forcing re-auth");
            {
                std::scoped_lock lock(stateMutex_);
                currentStatus_ = LicenseStatus::InvalidKey;
                lastResultMessage_ = "Please sign in again";
            }
            if (cache_) cache_->clearCache();
            resetCurrentLicense();
            publishStatusUpdate("startup_account_no_token");
            return;
        }

        auto verified = SignedTokenVerifier::verify(cachedToken, currentHardwareId_, kSamplarProductId);
        if (!verified.has_value()) {
            juce::Logger::writeToLog("[License] signed_token verification FAILED at startup — forcing re-auth");
            {
                std::scoped_lock lock(stateMutex_);
                currentStatus_ = LicenseStatus::InvalidKey;
                lastResultMessage_ = "Session expired — please sign in again";
            }
            if (cache_) cache_->clearCache();
            resetCurrentLicense();
            publishStatusUpdate("startup_account_token_invalid");
            return;
        }

        // Check 30-day offline grace from last validation
        constexpr int kGraceHours = 30 * 24;
        const auto grace = cache_->getOfflineGraceStatus(currentHardwareId_, kGraceHours);
        if (!grace.hasGrace) {
            // Grace expired — UX limitation: account auth requires password re-entry
            // because the server has no token-refresh endpoint. Flag this for the
            // user to address with the server author.
            juce::Logger::writeToLog("[License] WARNING: 30-day offline grace expired for account session. "
                                     "Server has no token-refresh endpoint; user must re-enter password.");
            {
                std::scoped_lock lock(stateMutex_);
                currentStatus_ = LicenseStatus::GracePeriodExpired;
                lastResultMessage_ = "Session expired — please sign in again to refresh";
            }
            publishStatusUpdate("startup_account_grace_expired");
            return;
        }

        // Valid signed_token within grace — proceed without server round-trip.
        {
            std::scoped_lock lock(stateMutex_);
            currentStatus_ = LicenseStatus::Valid;
            lastResultMessage_.clear();
            loadCachedLicense();
        }
        juce::Logger::writeToLog("[License] Account session verified offline via signed_token");
        publishStatusUpdate("startup_account_cached_valid");
        return;
    }

    // Reject any cached key that does NOT match the SAMPL4R format (e.g. legacy
    // SM5- prefixed keys, OAuth email-as-key entries, raw emails). Force re-auth.
    if (!isSamplarKeyFormatValid(cachedLicenseKey)) {
        juce::Logger::writeToLog("[License] Cached key '" +
            juce::String(cachedLicenseKey.substr(0, juce::jmin<int>(12, (int)cachedLicenseKey.size()))) +
            "...' is not in SAMPL4R format — clearing cache and requiring account sign-in");

        // Tight lock scope: set state under the mutex, then RELEASE before
        // calling resetCurrentLicense() / publishStatusUpdate() — both of
        // those acquire stateMutex_ themselves. Holding it across the call
        // is a recursive lock on a non-recursive mutex → Windows deadlock
        // detection fires ("resource deadlock would occur").
        {
            std::scoped_lock lock(stateMutex_);
            currentStatus_ = LicenseStatus::InvalidKey;
            lastResultMessage_ = "Please sign in with your SAMPLAR account";
        }
        if (cache_) cache_->clearCache();
        resetCurrentLicense();
        publishStatusUpdate("startup_legacy_key_purged");
        return;
    }

    // Within 3-day window — use cached license directly without server check.
    // (Avoids hammering the server on every launch.)
    constexpr int kRevalidationIntervalHours = 72;
    const bool needsRevalidation = cache_->needsPeriodicValidation(currentHardwareId_, kRevalidationIntervalHours);

    if (!needsRevalidation) {
        juce::Logger::writeToLog("[License] Within 3-day window — using cached license");
        std::scoped_lock lock(stateMutex_);
        currentStatus_ = LicenseStatus::Valid;
        lastResultMessage_.clear();
        loadCachedLicense();
        publishStatusUpdate("startup_cached_valid");
        return;
    }

    // Beyond the 3-day fast-path: hit the server (or fall back to 30-day grace).
    // This runs synchronously here because performStartupValidation is already
    // called synchronously at startup before showing the main window. If you
    // need to background it, call from a separate thread.
    juce::Logger::writeToLog("[License] Cache stale — calling revalidateCachedLicense");
    revalidateCachedLicense();
    publishStatusUpdate("startup_revalidated");
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

                // SAMPL4R-entitlement gate: the OAuth login may succeed for an
                // account that has *other* SampleMonsta-product licenses (e.g.
                // SM5) but NO SAMPL4R license. Block activation if the server
                // didn't return a SAMPL4R-prefixed license_key.
                const bool hasSamplarKey = result.licenseKey.size() >= 7
                                        && result.licenseKey.compare(0, 7, "SAMPL4R") == 0;
                if (!hasSamplarKey)
                {
                    juce::Logger::writeToLog("[License] OAuth login OK but account has NO SAMPL4R license "
                        "(returned key prefix='" + juce::String(result.licenseKey.substr(0, 8)) + "') — rejecting");
                    {
                        std::scoped_lock lock(stateMutex_);
                        currentStatus_ = LicenseStatus::InvalidKey;
                        lastResultMessage_ = "This account does not own SAMPL4R. "
                            "Purchase at samplemonsta.com or sign in with a different account.";
                    }
                    publishStatusUpdate("oauth_no_samplar_entitlement");
                    if (callback) callback(false, lastResultMessage_);
                    return;
                }

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

                    // Cache the license as an ACCOUNT-keyed entry so startup
                    // validation does NOT regex-check the legacy license_key
                    // (which may be a foreign SM5- prefixed key the user owns
                    // on the SampleMonsta account but is not a SAMPL4R key).
                    // We synthesise the cache key from the email and stash the
                    // signed_token + metadata inside the licenseData blob so
                    // SignedTokenVerifier can verify offline on next launch.
                    if (cache_ && !result.email.empty()) {
                        const std::string cacheKey = std::string("ACCOUNT:") + result.email;

                        // Truncate email for log safety (e.g. "san***@muzik...")
                        std::string truncEmail = result.email;
                        if (truncEmail.size() > 6) {
                            auto at = truncEmail.find('@');
                            if (at != std::string::npos && at > 3) {
                                truncEmail = truncEmail.substr(0, 3) + "***" + truncEmail.substr(at);
                            }
                        }
                        juce::Logger::writeToLog("[LicenseSystem] OAuth cached as ACCOUNT:" +
                            juce::String(truncEmail) + " with signed_token");

                        // Build account-style cache blob: customer_email + license_type
                        // + signed_token + expires_at. Mirrors activateWithAccount().
                        // OAuth delivers the signed token via the URL "token" param,
                        // which is mapped to AuthResult::accessToken by the parser.
                        juce::DynamicObject::Ptr data(new juce::DynamicObject());
                        data->setProperty("customer_email", juce::String(result.email));
                        data->setProperty("license_type",   juce::String(result.licenseType));
                        if (!result.accessToken.empty())
                            data->setProperty("signed_token", juce::String(result.accessToken));
                        if (!result.expiresAt.empty())
                            data->setProperty("expires_at",   juce::String(result.expiresAt));

                        cache_->storeLicense(cacheKey, juce::var(data.get()), hardwareId);
                        cache_->updateLastValidation(hardwareId);
                        juce::Logger::writeToLog("[LicenseSystem] License cached successfully (ACCOUNT entry)");
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

