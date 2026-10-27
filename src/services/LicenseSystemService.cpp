#include "LicenseSystemService.h"

#include "../Version.h"
#include "../security/HardwareFingerprintGenerator.h"
#include "../security/LicenseCache.h"
#include "../security/SecureSessionManager.h"
#include "../security/ClockGuard.h"
#include "../security/LicenseSigningKey.h"
#include "../licensing/FeatureManager.h"

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <algorithm>
#include <map>
#include <optional>

namespace samplar {

namespace {

// SAMPL4R production license server (live Render deployment).
// Public domain of the license server (www. - the bare host 301-redirects,
// which can turn POSTs into GETs). Same backend as the internal Render host; never use onrender.com.
constexpr auto kDefaultLicenseServerUrl = "https://www.samplemonsta.com";
constexpr int kWorkerJoinTimeoutMs = 5000;       // the worker's only wait is a cancellable HTTP call
constexpr juce::int64 kHourMs = 60LL * 60 * 1000;
constexpr juce::int64 kClockAdvanceEveryMs = 15LL * 60 * 1000;

// User-facing messages (shown by the sign-in panel / settings). ASCII only.
constexpr auto kMsgSignIn          = "Sign in with your SAMPLAR account to activate SAMPLAR.";
constexpr auto kMsgChecking        = "Checking your SAMPLAR license online...";
constexpr auto kMsgFileUnverified  = "Your license file could not be verified. Please sign in again.";
constexpr auto kMsgRevoked         = "This SAMPLAR license is no longer active (revoked or refunded). Sign in to check your account.";
constexpr auto kMsgExpired         = "Your SAMPLAR license has expired. Renew it in your account, then sign in again.";
constexpr auto kMsgUnbound         = "This computer is no longer activated for SAMPLAR (it was released from your account). Sign in to activate it again.";
constexpr auto kMsgInactive        = "Your SAMPLAR license or account is no longer active. Sign in to check your account.";
constexpr auto kMsgBlocked         = "This computer can no longer activate SAMPLAR. Contact support@samplemonsta.com.";
constexpr auto kMsgNeedsOnline     = "SAMPLAR needs to verify your license online. Connect to the internet and click Try again, or sign in.";
constexpr auto kMsgClockBack       = "Your computer's date looks wrong. Set the correct date and time and connect to the internet so SAMPLAR can verify your license.";
constexpr auto kMsgOfflineShort    = "SAMPLAR couldn't verify your license online for 3 days. Connect to the internet and click Try again, or sign in.";
constexpr auto kMsgOfflineLong     = "SAMPLAR couldn't verify your license online for 30 days. Connect to the internet and click Try again, or sign in.";
constexpr auto kMsgUnreachable     = "Could not reach the SAMPLAR license server. Check your internet connection and click Try again, or sign in.";
constexpr auto kMsgUnverifiedReply = "The license server's reply could not be verified. Please try again, or contact support if this keeps happening.";
constexpr auto kMsgSessionExpired  = "Your sign-in has expired. Sign in again to keep SAMPLAR verified.";
constexpr auto kMsgNoSamplarLicense = "This account has no SAMPLAR license. Buy SAMPLAR, or sign in with the account you bought it with.";
constexpr auto kMsgSignedOut       = "Signed out on this computer. To use this activation on another computer, release this one on your account page (opened in your browser).";
constexpr auto kMsgSignInCancelled = "Sign-in cancelled";
constexpr auto kMsgBadMachineId    = "SAMPLAR could not read this computer's ID, so it can't be activated. Restart SAMPLAR; if this keeps happening, contact support@samplemonsta.com.";
constexpr auto kMsgDenialPending   = "The SAMPLAR license server could not confirm your license. SAMPLAR is paused and will check again in a minute - or click Try again.";

// Cache keys of the "first signed denial, not yet confirmed" marker (N-1) and
// of the server/local clock skew of the last verified reply (N-5).
constexpr auto kKeyPendingDenial   = "pending_denial";      // denial status string
constexpr auto kKeyPendingDenialMs = "pending_denial_ms";   // local time of the first denial
constexpr auto kKeySkewMs          = "clock_skew_ms";       // signed issued - local receive

// Re-check delays while the clock looks wrong (N-5): never "due now".
constexpr juce::int64 kClockRetryMs[] = { 60LL * 1000, 5LL * 60 * 1000, 15LL * 60 * 1000, 60LL * 60 * 1000 };

/** Signed server time vs this clock: more than a day apart = wrong date. */
bool clockSkewTooLarge(juce::int64 skewMs)
{
    return skewMs > ClockGuard::kToleranceMs || skewMs < -ClockGuard::kToleranceMs;
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

// "santiago@x.org" -> "sa***@x.org" (D13: no full emails in Release logs).
juce::String maskEmail(const juce::String& email)
{
    const int at = email.indexOfChar('@');
    if (at <= 0)
        return email.isEmpty() ? juce::String("(empty)") : juce::String("***");
    return email.substring(0, juce::jmin(2, at)) + "***" + email.substring(at);
}

juce::String maskId(const std::string& id)
{
    return id.empty() ? juce::String("(empty)") : juce::String(id.substr(0, 8)) + "...";
}

// Deep copy of a licence-data object (JSON round trip), always an object.
juce::var mutableCopy(const juce::var& data)
{
    juce::var copy = data.isVoid() ? juce::var() : juce::JSON::parse(juce::JSON::toString(data, true));
    if (!copy.isObject())
        copy = juce::var(new juce::DynamicObject());
    return copy;
}

std::chrono::system_clock::time_point toTimePoint(juce::int64 ms)
{
    return std::chrono::system_clock::time_point(std::chrono::milliseconds(ms));
}

juce::int64 toMs(std::chrono::system_clock::time_point t)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(t.time_since_epoch()).count();
}

juce::int64 windowMsFor(bool shortWindow, int shortHours, int longHours)
{
    return (juce::int64) (shortWindow ? shortHours : longHours) * kHourMs;
}

juce::int64 remainingOfflineMs(juce::int64 lastVerifiedMs, bool shortWindow, int shortHours, int longHours, juce::int64 nowMs)
{
    if (lastVerifiedMs == 0)
        return 0;
    return juce::jmax<juce::int64>(0, lastVerifiedMs + windowMsFor(shortWindow, shortHours, longHours) - nowMs);
}

} // namespace

// ---- Background re-check thread (joinable; never detached, never force-killed) --

class LicenseSystemService::RevalidationWorker : public juce::Thread
{
public:
    struct Job
    {
        std::string serverUrl, accessToken, machineId;
    };
    using Done = std::function<void(const LicenseServerClient::RevalResult&)>;

    RevalidationWorker() : juce::Thread("SamplarLicenseRecheck") {}
    ~RevalidationWorker() override { cancelAndJoin(); }

    /** MESSAGE THREAD. The caller coalesces (one job in flight), so a previous
        run has already delivered and is only returning; never blocks long. */
    bool start(Job job, Done onDone)
    {
        if (isThreadRunning() && !waitForThreadToExit(200))
        {
            juce::Logger::writeToLog("[License] re-check thread still finishing - start refused");
            return false;
        }
        job_ = std::move(job);
        onDone_ = std::move(onDone);
        http_ = std::make_shared<CancellableHttpPost>();
        return startThread();
    }

    /** Any thread: abort the request without waiting. The run still delivers
        (a NetworkError), which the service drops by epoch / alive token. */
    void requestCancel()
    {
        signalThreadShouldExit();
        if (http_ != nullptr)
            http_->cancel();
    }

    /** MESSAGE THREAD (shutdown). Aborts the request and joins. Idempotent. */
    void cancelAndJoin()
    {
        requestCancel();
        if (isThreadRunning() && !waitForThreadToExit(kWorkerJoinTimeoutMs))
        {
            juce::Logger::writeToLog("[License] WARNING: re-check thread slow to stop - still waiting (no force-kill)");
            waitForThreadToExit(-1);
        }
    }

private:
    void run() override
    {
        // Copies only: no access to the service from this thread. Always
        // delivers, so the service's in-flight flag is always cleared; the hop
        // back checks the alive token and the epoch.
        const auto result = LicenseServerClient::revalidateWithToken(*http_, job_.serverUrl, job_.accessToken,
                                                                     job_.machineId);
        if (onDone_)
            onDone_(result);
    }

    Job job_;
    Done onDone_;
    std::shared_ptr<CancellableHttpPost> http_;
};

// ---- Lifecycle ----------------------------------------------------------------

LicenseSystemService::LicenseSystemService(EventBus& eventBus)
    : eventBus_(eventBus)
{
}

LicenseSystemService::~LicenseSystemService()
{
    juce::Logger::writeToLog("[License] ~LicenseSystemService");
    alive_->store(false);   // pending async hops must not touch this object
    audioAllowed_.store(false);
    stopTimer();
    teardownEventSubscriptions();
    stopBackgroundWork();   // joins the sign-in and re-check threads
}

void LicenseSystemService::initialise()
{
    juce::Logger::writeToLog("[License] === initialise START === SAMPLAR " + juce::String(samplar::kAppVersion) +
        ", strict signatures: " + juce::String(kRequireSignature ? "yes" : "no"));
    alive_ = std::make_shared<std::atomic<bool>>(true);   // fresh token (shutdown() cleared the old one)
    revalidateInFlight_ = false;

    // Process-wide cached registry read: no child process on the message thread.
    const auto machineId = HardwareFingerprintGenerator::licenceMachineId();
    {
        std::scoped_lock lock(stateMutex_);

        if (!cache_)             cache_ = std::make_unique<LicenseCache>();
        if (!sessionManager_)    sessionManager_ = std::make_unique<SecureSessionManager>();
        if (!featureManager_)    featureManager_ = std::make_unique<FeatureManager>();
        if (!revalidationWorker_) revalidationWorker_ = std::make_unique<RevalidationWorker>();

        currentHardwareId_ = machineId;
        nextRetryMs_ = 0;
        retryStep_ = 0;
        clockRetryStep_ = 0;
        maxSeenNowMs_ = 0;
        waitingForToken_ = false;
        signInRequired_ = false;
        juce::Logger::writeToLog("[License] Licence machine id: " + maskId(currentHardwareId_) +
            (LicenseServerClient::isValidMachineIdV2(juce::String(currentHardwareId_)) ? "" : " (NOT a valid v2 id - sign-in refused)"));
    }
    seenGeneration_ = licenceGeneration_.load();   // the files we are about to read are current

    setupEventSubscriptions();

    // Synchronous and local only (cache, clock marks, signature check): the
    // editor knows the status before it builds its UI. Network work is async.
    performStartupValidation();
    publishStatusUpdate("initialise");

   #if ! SAMPLAR_SELFTEST
    // Running-session checks (expiry, clock, offline window, re-check with
    // backoff). Started on the message thread; stopped in shutdown()/dtor.
    {
        auto alive = alive_;
        juce::MessageManager::callAsync([alive, this]() {
            if (!alive->load()) return;   // service gone: touch nothing
            startTimer(kEnforceTimerMs);
        });
    }
   #endif

    juce::Logger::writeToLog("[License] === initialise END === status " + toStatusString(getCurrentStatus()) +
        ", audio allowed: " + juce::String(isAudioAllowed() ? "yes" : "no"));
}

void LicenseSystemService::shutdown()
{
    juce::Logger::writeToLog("[License] === shutdown START ===");
    alive_->store(false);
    audioAllowed_.store(false);
    stopTimer();
    teardownEventSubscriptions();
    stopBackgroundWork();
    ++epoch_;

    std::scoped_lock lock(stateMutex_);
    oauthAuthenticator_.reset();
    retiredAuthenticators_.clear();
    revalidationWorker_.reset();
    cache_.reset();
    sessionManager_.reset();
    featureManager_.reset();
    resetCurrentLicense();
    currentHardwareId_.clear();
    lastResultMessage_.clear();
    lastVerifiedMs_ = 0;
    shortOfflineWindow_ = false;
    nextRetryMs_ = 0;
    retryStep_ = 0;
    clockRetryStep_ = 0;
    maxSeenNowMs_ = 0;
    waitingForToken_ = false;
    signInRequired_ = false;
    setStatusLocked(LicenseStatus::NotInitialised);
    juce::Logger::writeToLog("[License] === shutdown END ===");
}

void LicenseSystemService::stopBackgroundWork()
{
    // Join outside stateMutex_. All these objects are only created/destroyed on
    // the message thread, which is where we are. Every thread is cancellable, so
    // the joins are short; none is ever force-killed.
    std::vector<OAuthBrowserAuthenticator*> oauth;
    RevalidationWorker* worker = nullptr;
    {
        std::scoped_lock lock(stateMutex_);
        if (oauthAuthenticator_) oauth.push_back(oauthAuthenticator_.get());
        for (auto& a : retiredAuthenticators_) oauth.push_back(a.get());
        worker = revalidationWorker_.get();
    }
    for (auto* a : oauth) a->requestCancel();   // all abort in parallel
    if (worker != nullptr) worker->requestCancel();
    for (auto* a : oauth) a->cancelAuthentication();
    if (worker != nullptr) worker->cancelAndJoin();
    revalidateInFlight_ = false;
}

// ---- Simple getters -----------------------------------------------------------

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
    {
        std::scoped_lock lock(stateMutex_);
        if (!currentHardwareId_.empty())
            return currentHardwareId_;
    }
    return HardwareFingerprintGenerator::licenceMachineId();   // cached per process
}

std::chrono::minutes LicenseSystemService::getRemainingGraceTime() const
{
    const auto now = nowMs();
    std::scoped_lock lock(stateMutex_);
    const auto ms = remainingOfflineMs(lastVerifiedMs_, shortOfflineWindow_, kShortOfflineHours, kLongOfflineHours, now);
    return std::chrono::duration_cast<std::chrono::minutes>(std::chrono::milliseconds(ms));
}

bool LicenseSystemService::isSignInRequired() const
{
    std::scoped_lock lock(stateMutex_);
    return signInRequired_;
}

void LicenseSystemService::setServerUrl(const std::string& url)
{
    std::scoped_lock lock(stateMutex_);
    serverUrlOverride_ = url;
}

std::string LicenseSystemService::getServerUrl() const
{
    std::scoped_lock lock(stateMutex_);
    return serverUrlOverride_.empty() ? std::string{kDefaultLicenseServerUrl} : serverUrlOverride_;
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
    std::scoped_lock lock(stateMutex_);
    if (!sessionManager_ || currentHardwareId_.empty())
        return {};
    auto session = sessionManager_->retrieveSession(currentHardwareId_);
    return session.has_value() ? session->accessToken : std::string{};
}

void LicenseSystemService::resetCurrentLicense()
{
    currentLicense_ = LicenseInfo{};
}

void LicenseSystemService::setStatusLocked(LicenseStatus status)
{
    currentStatus_ = status;
    audioAllowed_.store(status == LicenseStatus::Valid, std::memory_order_relaxed);
}

juce::int64 LicenseSystemService::nowMs() const
{
   #if SAMPLAR_SELFTEST
    return juce::Time::currentTimeMillis() + testClockOffsetMs_;
   #else
    return juce::Time::currentTimeMillis();
   #endif
}

#if SAMPLAR_SELFTEST
juce::int64 LicenseSystemService::testRetryDelayMs() const
{
    const auto now = nowMs();
    std::scoped_lock lock(stateMutex_);
    return nextRetryMs_ == 0 ? 0 : juce::jmax<juce::int64>(0, nextRetryMs_ - now);
}
#endif

void LicenseSystemService::applySignedFieldsLocked(const SignedLicense& s)
{
    currentLicense_.customerEmail = s.email.toStdString();
    currentLicense_.licenseType = s.licenseType.toStdString();
    currentLicense_.expiresAt = s.expires.toMilliseconds() == 0   // 0 = perpetual (incl. the 9999 sentinel)
        ? std::chrono::system_clock::time_point{}
        : toTimePoint(s.expires.toMilliseconds());
}

void LicenseSystemService::scheduleRetryLocked(bool transient, juce::int64 now, int retryAfterSeconds)
{
    juce::int64 delay = kRetryMaxMs;
    if (transient)
    {
        retryStep_ = juce::jmin(retryStep_ + 1, 16);
        delay = juce::jmin<juce::int64>((juce::int64) kRetryMinMs << (retryStep_ - 1), kRetryMaxMs);
    }
    // The server's Retry-After (503 service_unavailable: 60 s) is a floor, never a ceiling.
    const juce::int64 retryAfterMs = (juce::int64) juce::jlimit(0, LicenseServerClient::kMaxRetryAfterSeconds, retryAfterSeconds) * 1000;
    const bool honoured = retryAfterMs > delay;
    delay = juce::jmax(delay, retryAfterMs);
    nextRetryMs_ = now + delay;
    juce::Logger::writeToLog("[License] next re-check in " + juce::String((int) (delay / 1000)) + " s (" +
        (transient ? "transient failure, backoff step " + juce::String(retryStep_) : juce::String("sign-in needed")) +
        (retryAfterSeconds > 0 ? ", server Retry-After " + juce::String(retryAfterSeconds) + " s" + (honoured ? " honoured" : " (backoff longer)")
                               : juce::String()) + ")");
}

void LicenseSystemService::scheduleClockRetryLocked(juce::int64 now)
{
    constexpr int steps = (int) (sizeof(kClockRetryMs) / sizeof(kClockRetryMs[0]));
    const auto delay = kClockRetryMs[juce::jlimit(0, steps - 1, clockRetryStep_)];
    clockRetryStep_ = juce::jmin(clockRetryStep_ + 1, steps - 1);
    nextRetryMs_ = now + delay;
    juce::Logger::writeToLog("[License] clock looks wrong - next re-check in " + juce::String((int) (delay / 1000)) +
        " s (clock backoff step " + juce::String(clockRetryStep_) + ")");
}

void LicenseSystemService::bumpLicenceGeneration()
{
    seenGeneration_ = ++licenceGeneration_;
    juce::Logger::writeToLog("[License] licence generation -> " + juce::String((juce::int64) seenGeneration_));
}

void LicenseSystemService::reloadAfterExternalChange(const char* why)
{
    juce::Logger::writeToLog("[License] === reloadAfterExternalChange (" + juce::String(why) + ") === status before: " +
        toStatusString(getCurrentStatus()));
    ++epoch_;   // a re-check this instance started earlier must not overwrite the new state
    {
        std::scoped_lock lock(stateMutex_);
        waitingForToken_ = false;
        retryStep_ = 0;
        clockRetryStep_ = 0;
        nextRetryMs_ = 0;
    }
    performStartupValidation();
    publishStatusUpdate(std::string("licence_changed_") + why);
    juce::Logger::writeToLog("[License] reloadAfterExternalChange END - status " + toStatusString(getCurrentStatus()) +
        ", audio allowed: " + juce::String(isAudioAllowed() ? "yes" : "no"));
}

// ---- Events -------------------------------------------------------------------

void LicenseSystemService::setupEventSubscriptions()
{
    if (!subscriptions_.empty()) {
        return;
    }

    auto subscribe = [this](const std::string& topic, EventBus::Handler handler) {
        const auto id = eventBus_.subscribe(topic, std::move(handler));
        subscriptions_.push_back({topic, id});
    };

    subscribe("license.check_feature", [this](const juce::var& payload) {
        const auto featureName = extractEventString(payload, "feature", "feature_name");
        if (featureName.isEmpty()) {
            juce::Logger::writeToLog("[License] license.check_feature missing feature name");
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
        publishStatusUpdate("app_startup");
    });
}

void LicenseSystemService::teardownEventSubscriptions()
{
    for (const auto& entry : subscriptions_) {
        eventBus_.unsubscribe(entry.first, entry.second);
    }

    subscriptions_.clear();
}

// ---- Startup ------------------------------------------------------------------

void LicenseSystemService::performStartupValidation()
{
    juce::Logger::writeToLog("[License] === performStartupValidation START ===");

    std::optional<LicenseCache::CacheEntry> entry;
    juce::StringArray machineIds;
    {
        std::scoped_lock lock(stateMutex_);
        if (!cache_) {
            juce::Logger::writeToLog("[License] ERROR: cache not initialised");
            setStatusLocked(LicenseStatus::NotInitialised);
            return;
        }
        entry = cache_->retrieveLicense(currentHardwareId_);
        machineIds = thisMachineIdsLocked();
    }

    auto lockOut = [this](LicenseStatus status, const char* message, const char* reason) {
        juce::Logger::writeToLog("[License] Locked at startup (" + juce::String(reason) + ")");
        std::scoped_lock lock(stateMutex_);
        setStatusLocked(status);
        lastResultMessage_ = message;
        lastVerifiedMs_ = 0;
        resetCurrentLicense();
    };

    if (!entry.has_value() || entry->licenseKey.empty()) {
        // Nothing usable on disk. Never delete anything here. A stored sign-in
        // token can still restore the licence silently.
        lockOut(LicenseStatus::InvalidKey, kMsgSignIn, entry.has_value() ? "cache entry without key" : "no cached licence");
        revalidateAsync();
        juce::Logger::writeToLog("[License] === performStartupValidation END === (no cache)");
        return;
    }

    const auto now = juce::Time(nowMs());
    const auto payload = extractStringProperty(entry->licenseData, "license_payload");
    const auto signature = extractStringProperty(entry->licenseData, "signature_v2");
    const auto validVar = getNestedObject(entry->licenseData, "license_valid");
    const bool revokedAtLastCheck = !validVar.isVoid() && !(bool) validVar;

    SignedLicense signedData;
    const bool signedOk = LicenseSignature::verifyStored(payload, signature, machineIds, now, signedData);
    juce::Logger::writeToLog("[License] Cached signed payload: " + juce::String(payload.isEmpty() ? "absent" : "present") +
        ", verified: " + juce::String(signedOk ? "YES" : "NO") + ", deniedAtLastCheck: " + juce::String(revokedAtLastCheck ? "Y" : "N"));

    // The server SIGNED a denial at the last check: stay locked (the re-check
    // may re-unlock it, e.g. after a re-purchase or re-activation).
    if (revokedAtLastCheck) {
        const auto stored = extractStringProperty(entry->licenseData, "denial_status");
        const bool expiredDenial = stored == "expired";
        lockOut(expiredDenial ? LicenseStatus::Expired : LicenseStatus::InvalidKey,
                expiredDenial ? kMsgExpired : stored == "unbound" ? kMsgUnbound
                                            : stored == "blocked" ? kMsgBlocked
                                            : stored == "inactive" ? kMsgInactive : kMsgRevoked,
                "denied at last check");
        revalidateAsync();
        return;
    }

    // A first signed denial is pending confirmation (N-1): stay locked (muted)
    // with the licence and token kept, and re-check now. A restart does not
    // reset it: the next signed denial (>= kDenialConfirmMs after the first)
    // wipes, a valid reply clears it.
    const auto pendingDenial = extractStringProperty(entry->licenseData, kKeyPendingDenial);
    if (pendingDenial.isNotEmpty()) {
        lockOut(LicenseStatus::InvalidKey, kMsgDenialPending,
                ("signed denial pending confirmation (" + pendingDenial + ")").toRawUTF8());
        revalidateAsync();
        juce::Logger::writeToLog("[License] === performStartupValidation END === (pending denial)");
        return;
    }

    // Strict: only server-signed data for THIS machine counts.
    if (kRequireSignature && !signedOk) {
        // A genuine licence that only fails because this clock is days behind
        // its signed issue time gets the clock message (review L-M3).
        const bool clockBehind = payload.isNotEmpty() && signedData.issued.toMilliseconds() != 0
                              && LicenseSignature::isIssuedInFuture(signedData, now)
                              && LicenseSignature::verifyStored(payload, signature, machineIds,
                                                                signedData.issued, signedData);
        lockOut(LicenseStatus::InvalidKey, clockBehind ? kMsgClockBack : kMsgFileUnverified,
                payload.isEmpty() ? "no signed licence in the cache"
                                  : clockBehind ? "signed licence issued in the future (clock behind)"
                                                : "signed licence did not verify");
        revalidateAsync();
        return;
    }

    // Clock checks. The latest time seen lives in ClockGuard's file + registry
    // value and in "clock_mark" inside the integrity-protected licence file.
    // An unreadable mark counts as unknown (0), never as a future time.
    const auto nowMsValue = now.toMilliseconds();
    const juce::int64 dataMark = (juce::int64) entry->licenseData.getProperty("clock_mark", 0);
    const juce::int64 highWater = juce::jmax(ClockGuard::highWater(), dataMark);
    if (kRequireSignature && highWater == 0) {
        lockOut(LicenseStatus::InvalidKey, kMsgNeedsOnline, "clock marks missing");
        revalidateAsync();
        return;
    }
    if (kRequireSignature && nowMsValue + ClockGuard::kToleranceMs < highWater) {
        lockOut(LicenseStatus::InvalidKey, kMsgClockBack, "clock rolled back");
        {
            std::scoped_lock lock(stateMutex_);
            scheduleClockRetryLocked(nowMsValue);
        }
        revalidateAsync();
        return;
    }
    ClockGuard::advance(nowMsValue);
    lastClockAdvanceMs_ = nowMsValue;
    {
        std::scoped_lock lock(stateMutex_);
        maxSeenNowMs_ = juce::jmax(maxSeenNowMs_, nowMsValue);
    }
    if (nowMsValue > dataMark) {
        std::scoped_lock lock(stateMutex_);
        auto data = mutableCopy(entry->licenseData);
        data.getDynamicObject()->setProperty("clock_mark", nowMsValue);
        cache_->storeLicense(entry->licenseKey, data, currentHardwareId_);
    }

    // Offline clock: the SIGNED issued_at (the file's own lastValidated could be
    // edited by anyone), moved to this computer's clock by the skew measured
    // when the reply arrived (clamped to the 1-day tolerance, N-5). Unsigned
    // (permissive dev builds only): the file time.
    const juce::int64 storedSkew = juce::jlimit<juce::int64>(-ClockGuard::kToleranceMs, ClockGuard::kToleranceMs,
        (juce::int64) entry->licenseData.getProperty(kKeySkewMs, 0));
    const juce::Time lastVerified = signedOk ? juce::Time(signedData.issued.toMilliseconds() - storedSkew)
                                             : juce::Time(toMs(entry->lastValidated));
    const double ageHours = (now - lastVerified).inHours();
    const bool shortWindow = !signedOk || isShortWindowLicense(signedData);   // unsigned (dev builds only): short
    const int windowHours = shortWindow ? kShortOfflineHours : kLongOfflineHours;

    bool expired = false;
    {
        std::scoped_lock lock(stateMutex_);
        populateLicenseInfoFromVar(entry->licenseData);
        if (signedOk)
            applySignedFieldsLocked(signedData);
        const auto expiresMs = toMs(currentLicense_.expiresAt);
        expired = expiresMs > 0 && nowMsValue > expiresMs;
        lastVerifiedMs_ = lastVerified.toMilliseconds();
        shortOfflineWindow_ = shortWindow;
    }

    juce::Logger::writeToLog("[License] Last verified " + juce::String(ageHours, 1) + " h ago, offline window " +
        juce::String(windowHours) + " h (" + (shortWindow ? "dated expiry or PROMO/PROMO2/TRL" : "signed perpetual") + "), expired: " +
        juce::String(expired ? "Y" : "N"));

    if (expired) {
        std::scoped_lock lock(stateMutex_);
        setStatusLocked(LicenseStatus::Expired);
        lastResultMessage_ = kMsgExpired;
    } else if (ageHours <= kRecheckAfterHours) {
        std::scoped_lock lock(stateMutex_);
        setStatusLocked(LicenseStatus::Valid);
        lastResultMessage_.clear();
        juce::Logger::writeToLog("[License] === performStartupValidation END === Valid (verified recently, no re-check)");
        return;
    } else if (ageHours <= windowHours) {
        std::scoped_lock lock(stateMutex_);
        setStatusLocked(LicenseStatus::Valid);   // usable offline while the silent re-check runs
        lastResultMessage_.clear();
    } else {
        lockOut(LicenseStatus::InvalidKey, shortWindow ? kMsgOfflineShort : kMsgOfflineLong, "offline window over");
    }

    revalidateAsync();
    juce::Logger::writeToLog("[License] === performStartupValidation END === status " + toStatusString(getCurrentStatus()));
}

bool LicenseSystemService::isShortWindowLicense(const SignedLicense& s)
{
    // Server policy (licence-server owner, 2026-10-21): the window keys off the
    // SIGNED expiry, not the licence type. Signed expires_at "perpetual" ->
    // 30 days whatever the type (PRO, STD, EDU, Gold-earned ownership, ...);
    // any dated expiry -> 3 days. PROMO / PROMO2 / TRL are always 3 days, even
    // if signed perpetual. expires 0 = signed "perpetual" (or the 9999 sentinel).
    const bool perpetual = s.expires.toMilliseconds() == 0;
    const bool alwaysShortType = s.licenseType.equalsIgnoreCase("PROMO")
                              || s.licenseType.equalsIgnoreCase("PROMO2")
                              || s.licenseType.equalsIgnoreCase("TRL");
    return !perpetual || alwaysShortType;
}

void LicenseSystemService::recheckNow()
{
    juce::Logger::writeToLog("[License] recheckNow requested (Try again)");
    {
        std::scoped_lock lock(stateMutex_);
        if (currentStatus_ != LicenseStatus::Valid)
            lastResultMessage_ = kMsgChecking;
    }
    publishStatusUpdate("recheck_requested");
    revalidateAsync();   // explicit request: ignores the backoff
}

// ---- Running session ------------------------------------------------------------

void LicenseSystemService::enforceRunningSession()
{
    reapRetiredAuthenticators();

    // 0. Another instance in this process signed in / out or refreshed the
    //    licence files: reload them now instead of waiting for our own re-check (N-7).
    {
        const auto generation = licenceGeneration_.load();
        bool initialised = false;
        {
            std::scoped_lock lock(stateMutex_);
            initialised = cache_ != nullptr && currentStatus_ != LicenseStatus::NotInitialised;
        }
        if (initialised && generation != seenGeneration_) {
            juce::Logger::writeToLog("[License] licence generation changed (" + juce::String((juce::int64) seenGeneration_) +
                " -> " + juce::String((juce::int64) generation) + ") - reloading");
            seenGeneration_ = generation;
            reloadAfterExternalChange("generation");
            return;
        }
    }

    const auto now = nowMs();
    LicenseStatus status;
    juce::int64 lastVerified = 0, expiresMs = 0, nextRetry = 0, maxSeen = 0;
    bool shortWindow = false, waitingForToken = false;
    {
        std::scoped_lock lock(stateMutex_);
        if (!cache_)
            return;   // shut down
        status = currentStatus_;
        lastVerified = lastVerifiedMs_;
        shortWindow = shortOfflineWindow_;
        expiresMs = toMs(currentLicense_.expiresAt);
        nextRetry = nextRetryMs_;
        maxSeen = maxSeenNowMs_;
        waitingForToken = waitingForToken_;
    }
    if (status == LicenseStatus::NotInitialised)
        return;

    // Locked with no sign-in token: another process (standalone / other DAW) may
    // have signed in since. A session-file read per tick; ignores the backoff.
    if (waitingForToken && status != LicenseStatus::Valid) {
        bool tokenNow = false;
        {
            std::scoped_lock lock(stateMutex_);
            tokenNow = hasStoredTokenLocked();
        }
        if (tokenNow) {
            juce::Logger::writeToLog("[License] sign-in token appeared while locked - reloading");
            reloadAfterExternalChange("token_appeared");
            return;
        }
    }

    // 1. A Valid licence can stop being valid while SAMPLAR runs.
    if (status == LicenseStatus::Valid) {
        LicenseStatus lockStatus = LicenseStatus::InvalidKey;
        const char* lockMessage = nullptr;
        const char* why = nullptr;
        bool keepInfo = false;
        const auto windowMs = windowMsFor(shortWindow, kShortOfflineHours, kLongOfflineHours);
        // Rollback floor (N-6): persisted marks, the last verified reply (local
        // receive time) and the latest time this instance has seen - so garbage
        // marks or a huge rollback cannot hide a mid-session rollback.
        const auto clockFloor = juce::jmax(ClockGuard::highWater(), lastVerified, maxSeen);

        if (expiresMs > 0 && now > expiresMs) {
            lockStatus = LicenseStatus::Expired; lockMessage = kMsgExpired; why = "expired"; keepInfo = true;
        } else if (kRequireSignature && now + ClockGuard::kToleranceMs < clockFloor) {
            lockMessage = kMsgClockBack; why = "clock_rolled_back";
        } else if (lastVerified > 0 && now - lastVerified > windowMs) {
            lockMessage = shortWindow ? kMsgOfflineShort : kMsgOfflineLong; why = "offline_window_over";
        }

        if (lockMessage != nullptr) {
            const bool clockLock = std::string(why) == "clock_rolled_back";
            juce::Logger::writeToLog("[License] Running session locked: " + juce::String(why) +
                (clockLock ? " (now " + juce::String(now) + ", floor " + juce::String(clockFloor) + ")" : juce::String()));
            {
                std::scoped_lock lock(stateMutex_);
                setStatusLocked(lockStatus);
                lastResultMessage_ = lockMessage;
                if (!keepInfo)
                    resetCurrentLicense();
                if (clockLock) {
                    scheduleClockRetryLocked(now);   // never "due now": no per-minute flapping (N-5)
                } else {
                    nextRetryMs_ = 0;   // ask the server right away (renewal)
                }
                nextRetry = nextRetryMs_;
            }
            publishStatusUpdate(std::string("running_") + why);
            status = lockStatus;
        } else {
            {
                std::scoped_lock lock(stateMutex_);
                maxSeenNowMs_ = juce::jmax(maxSeenNowMs_, now);
            }
            if (now - lastClockAdvanceMs_ >= kClockAdvanceEveryMs) {
                ClockGuard::advance(now);
                lastClockAdvanceMs_ = now;
            }
        }
    }

    // 2. Re-check while stale (Valid, > 6 h) or locked - honouring the backoff.
    const bool stale = status == LicenseStatus::Valid && lastVerified > 0
                       && now - lastVerified > (juce::int64) kRecheckAfterHours * kHourMs;
    const bool locked = status != LicenseStatus::Valid;
    if (!(stale || locked) || now < nextRetry)
        return;

    {
        std::scoped_lock lock(stateMutex_);
        if (locked && !hasStoredTokenLocked()) {
            nextRetryMs_ = now + kRetryMaxMs;   // nothing to re-check with: a sign-in is needed
            if (!waitingForToken_)
                juce::Logger::writeToLog("[License] locked without a sign-in token - polling for one every tick");
            waitingForToken_ = true;
            return;
        }
        nextRetryMs_ = now + kRetryMinMs;       // in flight; the result reschedules
    }
    juce::Logger::writeToLog(juce::String("[License] Running session: re-check (") + (locked ? "locked" : "stale") + ")");
    revalidateAsync();
}

juce::StringArray LicenseSystemService::thisMachineIdsLocked() const
{
    // The licence machine ID only: SAMPLAR never shipped v1 bindings, and a v1
    // ID can come from local (copyable) data.
    juce::StringArray ids;
    if (!currentHardwareId_.empty())
        ids.add(juce::String(currentHardwareId_));
    return ids;
}

bool LicenseSystemService::hasStoredTokenLocked() const
{
    if (!sessionManager_ || currentHardwareId_.empty())
        return false;
    const auto session = sessionManager_->retrieveSession(currentHardwareId_);
    return session.has_value() && !session->accessToken.empty();
}

// ---- Re-check -----------------------------------------------------------------

void LicenseSystemService::revalidateAsync()
{
   #if SAMPLAR_SELFTEST
    juce::Logger::writeToLog("[License] revalidateAsync: self-test build - no network");
    return;
   #else
    if (revalidateInFlight_.exchange(true)) {
        juce::Logger::writeToLog("[License] revalidateAsync: already in flight - coalesced");
        return;
    }

    const std::uint64_t epoch = epoch_.load();
    RevalidationWorker::Job job;
    bool haveWorker = false;
    {
        std::scoped_lock lock(stateMutex_);
        job.machineId = currentHardwareId_;
        job.serverUrl = serverUrlOverride_.empty() ? kDefaultLicenseServerUrl : serverUrlOverride_;
        if (sessionManager_) {
            if (auto session = sessionManager_->retrieveSession(currentHardwareId_))
                job.accessToken = session->accessToken;
        }
        haveWorker = revalidationWorker_ != nullptr;
    }

    auto alive = alive_;
    if (job.accessToken.empty() || !haveWorker) {
        juce::Logger::writeToLog("[License] revalidateAsync: no stored sign-in token -> AuthRequired");
        LicenseServerClient::RevalResult r;
        r.outcome = LicenseServerClient::RevalOutcome::AuthRequired;
        juce::MessageManager::callAsync([alive, this, r, epoch]() {
            if (!alive->load()) return;   // service gone: touch nothing
            revalidateInFlight_ = false;
            applyRevalidation(r, epoch);
        });
        return;
    }

    juce::Logger::writeToLog("[License] revalidateAsync: background re-check (epoch " + juce::String((juce::int64) epoch) + ")");
    // The worker thread only copies; the hop back checks `alive` first.
    const bool started = revalidationWorker_->start(std::move(job),
        [alive, this, epoch](const LicenseServerClient::RevalResult& result) {
            juce::MessageManager::callAsync([alive, this, result, epoch]() {
                if (!alive->load()) return;   // service gone: touch nothing
                revalidateInFlight_ = false;
                applyRevalidation(result, epoch);
            });
        });
    if (!started) {
        juce::Logger::writeToLog("[License] revalidateAsync ERROR: could not start the re-check thread");
        revalidateInFlight_ = false;
        std::scoped_lock lock(stateMutex_);
        scheduleRetryLocked(true, nowMs());
    }
   #endif
}

void LicenseSystemService::applyRevalidation(const LicenseServerClient::RevalResult& r, std::uint64_t epoch)
{
    using O = LicenseServerClient::RevalOutcome;
    juce::Logger::writeToLog("[License] === applyRevalidation START === outcome " + juce::String((int) r.outcome) +
        ", reason '" + r.reason + "', http " + juce::String(r.statusCode) +
        ", signed payload: " + juce::String(r.licensePayload.isEmpty() ? "no" : "yes") +
        ", signed denial: " + juce::String(r.denialPayload.isEmpty() ? "no" : "yes"));

    if (epoch != epoch_.load()) {
        // Started before a sign-in / sign-out: applying it would restore a
        // signed-out licence or wipe the fresh one.
        juce::Logger::writeToLog("[License] applyRevalidation: STALE result (epoch " + juce::String((juce::int64) epoch) +
            " != " + juce::String((juce::int64) epoch_.load()) + ") - dropped");
        return;
    }

    const auto nowValue = nowMs();
    const auto now = juce::Time(nowValue);
    juce::StringArray machineIds;
    {
        std::scoped_lock lock(stateMutex_);
        if (!cache_) {
            juce::Logger::writeToLog("[License] applyRevalidation: service shut down - ignored");
            return;
        }
        machineIds = thisMachineIdsLocked();
    }

    // A "valid" answer only counts if the server signed it for this machine and
    // this request (otherwise a local proxy could fake it).
    SignedLicense signedData;
    const bool signedOk = r.outcome == O::Valid
        && LicenseSignature::verifyLive(r.licensePayload, r.signature, r.requestNonce, machineIds, now, signedData);
    // A denial only counts if the server SIGNED it for this machine and this
    // request. Anything else (network, 5xx, unsigned / unknown reply) is transient.
    SignedLicense denialData;
    const bool denialOk = r.outcome == O::Denied
        && LicenseSignature::verifyDenial(r.denialPayload, r.denialSignature, r.requestNonce, machineIds, now, denialData);

    O outcome = r.outcome;
    const bool unverifiedValid = r.outcome == O::Valid && kRequireSignature && !signedOk;
    const bool unverifiedDenial = r.outcome == O::Denied && !denialOk;
    if (unverifiedValid || unverifiedDenial)
        outcome = O::NetworkError;
    juce::Logger::writeToLog("[License] Re-check signature verified: " + juce::String(signedOk || denialOk ? "YES" : "NO") +
        " -> effective outcome " + juce::String((int) outcome) +
        (unverifiedDenial ? " (UNSIGNED denial ignored - licence kept)" : ""));
    // Server/local skew of this nonce-verified reply (N-5). More than a day:
    // this computer's date is wrong - never unlock on it and never move the
    // rollback mark to it.
    const juce::int64 skewMs = signedOk ? signedData.issued.toMilliseconds() - nowValue
                             : denialOk ? denialData.issued.toMilliseconds() - nowValue : 0;
    const bool clockWrong = signedOk && kRequireSignature && clockSkewTooLarge(skewMs);
    if (signedOk || denialOk)
        juce::Logger::writeToLog("[License] Re-check clock skew (signed - local): " + juce::String(skewMs / 1000) + " s" +
            (clockWrong ? " -> MORE THAN A DAY: date looks wrong, not unlocking" : ""));
    if ((signedOk && !clockWrong) || denialOk) {
        // Server-confirmed time, but never ahead of this clock (no flapping when it is behind).
        ClockGuard::reset(juce::jmin(nowValue, (signedOk ? signedData.issued : denialData.issued).toMilliseconds()));
    }

    bool generationChanged = false;
    {
        std::scoped_lock lock(stateMutex_);
        auto entry = cache_->retrieveLicense(currentHardwareId_);
        auto data = mutableCopy(entry.has_value() ? entry->licenseData : juce::var());
        auto* obj = data.getDynamicObject();
        const std::string licenseKey = (entry.has_value() && !entry->licenseKey.empty())
            ? entry->licenseKey : std::string(LicenseSigningKey::kProductCode);

        if (outcome == O::Valid && clockWrong) {
            // Genuine reply, wrong local date: keep the refreshed licence + rolled
            // token (so a corrected clock unlocks on the next re-check), clear any
            // pending denial, but stay locked and back off 1/5/15/60 min.
            obj->setProperty("license_valid", true);
            obj->removeProperty("denial_status");
            obj->removeProperty(kKeyPendingDenial);
            obj->removeProperty(kKeyPendingDenialMs);
            obj->setProperty("license_payload", r.licensePayload);
            obj->setProperty("signature_v2", r.signature);
            if (r.accessToken.isNotEmpty() && sessionManager_) {
                auto session = sessionManager_->retrieveSession(currentHardwareId_).value_or(SecureSessionManager::SessionData{});
                session.accessToken = r.accessToken.toStdString();
                session.hardwareId = currentHardwareId_;
                session.userEmail = signedData.email.toStdString();
                sessionManager_->storeSession(session);
            }
            cache_->storeLicense(licenseKey, data, currentHardwareId_);
            const bool wasValid = currentStatus_ == LicenseStatus::Valid;
            signInRequired_ = false;   // the token was accepted
            setStatusLocked(LicenseStatus::InvalidKey);
            lastResultMessage_ = kMsgClockBack;
            retryStep_ = 0;
            scheduleClockRetryLocked(nowValue);
            juce::Logger::writeToLog(juce::String("[License] Re-check: signed licence OK but the clock is off by more than a day -> ") +
                (wasValid ? "locked" : "stays locked") + " (check your date)");
        } else if (outcome == O::Valid) {
            obj->setProperty("license_valid", true);
            obj->removeProperty("denial_status");
            if (obj->hasProperty(kKeyPendingDenial))
                juce::Logger::writeToLog("[License] Re-check valid - pending denial cleared (it was transient)");
            obj->removeProperty(kKeyPendingDenial);
            obj->removeProperty(kKeyPendingDenialMs);
            if (r.licenseType.isNotEmpty()) obj->setProperty("license_type", r.licenseType);
            if (r.expiresAt.isNotEmpty())   obj->setProperty("expires_at", r.expiresAt);
            if (signedOk || r.licensePayload.isNotEmpty()) {   // unverified kept only in permissive builds
                obj->setProperty("license_payload", r.licensePayload);
                obj->setProperty("signature_v2", r.signature);
            }
            if (signedOk) {
                obj->setProperty("clock_mark", juce::jmin(nowValue, signedData.issued.toMilliseconds()));
                obj->setProperty(kKeySkewMs, skewMs);
            }
            // Offline window / staleness run on THIS clock from the moment the
            // verified reply arrived (N-5): a skew < 1 day never looks stale.
            lastVerifiedMs_ = nowValue;
            maxSeenNowMs_ = nowValue;   // trusted now: a corrected (lowered) clock is fine again
            clockRetryStep_ = 0;
            waitingForToken_ = false;
            if (signInRequired_)
                juce::Logger::writeToLog("[License] Re-check valid - sign-in no longer required");
            signInRequired_ = false;
            generationChanged = true;
            shortOfflineWindow_ = !signedOk || isShortWindowLicense(signedData);

            // The server rolls the sign-in token on every valid re-check (they
            // expire after 365 days): keep the newest.
            if (r.accessToken.isNotEmpty() && sessionManager_) {
                auto session = sessionManager_->retrieveSession(currentHardwareId_).value_or(SecureSessionManager::SessionData{});
                session.accessToken = r.accessToken.toStdString();
                session.hardwareId = currentHardwareId_;
                if (signedOk)
                    session.userEmail = signedData.email.toStdString();
                sessionManager_->storeSession(session);
                juce::Logger::writeToLog("[License] Re-check: stored the rolled sign-in token");
            }
            cache_->storeLicense(licenseKey, data, currentHardwareId_);
            cache_->updateLastValidation(currentHardwareId_);
            populateLicenseInfoFromVar(data);
            if (signedOk)
                applySignedFieldsLocked(signedData);
            setStatusLocked(LicenseStatus::Valid);
            lastResultMessage_.clear();
            retryStep_ = 0;
            nextRetryMs_ = 0;
            juce::Logger::writeToLog("[License] Re-check: license confirmed" + juce::String(signedOk ? " (signed)" : "") +
                (entry.has_value() ? "" : " - licence file restored from the stored sign-in"));
        } else if (outcome == O::Denied
                   && entry.has_value()
                   && extractStringProperty(entry->licenseData, "license_payload").isNotEmpty()
                   && (extractStringProperty(entry->licenseData, kKeyPendingDenial).isEmpty()
                       || nowValue - (juce::int64) entry->licenseData.getProperty(kKeyPendingDenialMs, 0) < kDenialConfirmMs)) {
            // FIRST verified, signed denial for a licence on file (N-1): could be
            // a server-side hiccup signed as a denial. Lock (mute) but keep the
            // licence + sign-in token; re-check in a minute. Persisted, so a
            // restart in between still counts it. A second denial too soon after
            // the first (e.g. another instance) stays "first".
            const auto status = denialData.status.toLowerCase();
            const bool firstOne = extractStringProperty(entry->licenseData, kKeyPendingDenial).isEmpty();
            signInRequired_ = false;   // the token was accepted; the licence is what's denied
            if (firstOne) {
                obj->setProperty(kKeyPendingDenial, status);
                obj->setProperty(kKeyPendingDenialMs, nowValue);
                cache_->storeLicense(licenseKey, data, currentHardwareId_);
            }
            const bool wasValid = currentStatus_ == LicenseStatus::Valid;
            setStatusLocked(LicenseStatus::InvalidKey);
            lastResultMessage_ = kMsgDenialPending;
            lastVerifiedMs_ = 0;
            retryStep_ = 0;
            const juce::int64 firstMs = firstOne ? nowValue : (juce::int64) entry->licenseData.getProperty(kKeyPendingDenialMs, nowValue);
            nextRetryMs_ = juce::jmax(nowValue + kRetryMinMs, firstMs + kDenialConfirmMs);
            juce::Logger::writeToLog("[License] Re-check: SIGNED denial (" + status + ") " +
                (firstOne ? juce::String("#1") : juce::String("#1 again (too soon to confirm)")) +
                " -> locked" + (wasValid ? "" : " (already locked)") + ", licence + token KEPT, re-check in " +
                juce::String((int) ((nextRetryMs_ - nowValue) / 1000)) + " s");
        } else if (outcome == O::Denied) {
            // Verified, signed denial confirmed (second consecutive one, or no
            // licence on file): drop the signed licence so it can't be revived
            // offline; keep a marker so the next launch explains why.
            const auto status = denialData.status.toLowerCase();
            generationChanged = true;
            signInRequired_ = false;
            if (entry.has_value()) {
                obj->removeProperty(kKeyPendingDenial);
                obj->removeProperty(kKeyPendingDenialMs);
                obj->removeProperty("license_payload");
                obj->removeProperty("signature_v2");
                obj->setProperty("license_valid", false);
                obj->setProperty("denial_status", status);
                obj->setProperty("clock_mark", denialData.issued.toMilliseconds());
                cache_->storeLicense(licenseKey, data, currentHardwareId_);
            }
            const bool expired = status == "expired";
            setStatusLocked(expired ? LicenseStatus::Expired : LicenseStatus::InvalidKey);
            lastResultMessage_ = expired ? kMsgExpired
                               : status == "unbound" ? kMsgUnbound
                               : status == "blocked" ? kMsgBlocked
                               : status == "inactive" ? kMsgInactive : kMsgRevoked;
            lastVerifiedMs_ = 0;
            resetCurrentLicense();
            retryStep_ = 0;
            scheduleRetryLocked(false, nowValue);   // hourly: a re-purchase / re-activation unlocks again
            juce::Logger::writeToLog("[License] Re-check: SIGNED denial (" + status + ") -> locked, licence removed");
        } else {
            // AuthRequired / NetworkError / unverifiable: never unlocks, never
            // locks or wipes a licence that is still inside its offline window.
            const bool locked = currentStatus_ != LicenseStatus::Valid;
            if (outcome == O::AuthRequired) {
                // token_missing / token_invalid / token_too_old (or 401/403):
                // sign in again. The licence is KEPT - audio stays on inside the
                // offline window; the UI shows a sign-in prompt.
                signInRequired_ = true;
                lastResultMessage_ = locked ? kMsgSignIn : kMsgSessionExpired;
                scheduleRetryLocked(false, nowValue);
                // No token at all: poll for one every tick (another process may sign in, N-7).
                waitingForToken_ = locked && !hasStoredTokenLocked();
            } else {
                if (locked && (lastResultMessage_.empty() || lastResultMessage_ == kMsgChecking))
                    lastResultMessage_ = (unverifiedValid || unverifiedDenial) ? kMsgUnverifiedReply : kMsgUnreachable;
                scheduleRetryLocked(true, nowValue, r.retryAfterSeconds);   // 503: honour Retry-After
            }
            juce::Logger::writeToLog("[License] Re-check inconclusive - status kept: " + toStatusString(currentStatus_) +
                ", licence kept, sign-in required: " + juce::String(signInRequired_ ? "YES" : "no") +
                (unverifiedValid ? " (server reply NOT signed with the SAMPL4R key)" : ""));
        }
    }

    if (generationChanged)
        bumpLicenceGeneration();   // other instances in this process reload the shared files
    publishStatusUpdate("revalidation");
    juce::Logger::writeToLog("[License] === applyRevalidation END === status " + toStatusString(getCurrentStatus()) +
        ", audio allowed: " + juce::String(isAudioAllowed() ? "yes" : "no"));
}

// ---- Status payload -------------------------------------------------------------

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
    const auto now = nowMs();
    juce::DynamicObject::Ptr root(new juce::DynamicObject());
    root->setProperty("status", static_cast<int>(currentStatus_));
    root->setProperty("statusName", toStatusString(currentStatus_));
    root->setProperty("hardwareId", juce::String(currentHardwareId_));
    root->setProperty("reason", juce::String(reason));
    root->setProperty("message", juce::String(lastResultMessage_));
    root->setProperty("audioAllowed", currentStatus_ == LicenseStatus::Valid);
    root->setProperty("signInRequired", signInRequired_);

    auto licenseObject = new juce::DynamicObject();
    licenseObject->setProperty("customerEmail", juce::String(currentLicense_.customerEmail));
    licenseObject->setProperty("licenseType", juce::String(currentLicense_.licenseType));
    licenseObject->setProperty("maxActivations", currentLicense_.maxActivations);
    licenseObject->setProperty("currentActivations", currentLicense_.currentActivations);

    if (currentLicense_.expiresAt.time_since_epoch().count() != 0) {
        const auto millis = toMs(currentLicense_.expiresAt);
        licenseObject->setProperty("expiresAtMs", static_cast<double>(millis));
        licenseObject->setProperty("expiresAt", juce::Time(millis).toISO8601(true));
    }

    juce::Array<juce::var> featureArray;
    for (const auto& feature : currentLicense_.features) {
        featureArray.add(juce::String(feature));
    }
    licenseObject->setProperty("features", featureArray);

    root->setProperty("license", juce::var(licenseObject));

    // "grace" = the offline window left before a re-check is required.
    const auto remainingMs = remainingOfflineMs(lastVerifiedMs_, shortOfflineWindow_, kShortOfflineHours, kLongOfflineHours, now);
    auto graceObject = new juce::DynamicObject();
    graceObject->setProperty("hasGrace", currentStatus_ == LicenseStatus::Valid && remainingMs > 0);
    graceObject->setProperty("remainingMinutes", static_cast<int>(remainingMs / 60000));
    graceObject->setProperty("graceExpiresAt", lastVerifiedMs_ == 0
        ? juce::String()
        : juce::Time(now + remainingMs).toISO8601(true));
    root->setProperty("grace", juce::var(graceObject));

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

        juce::var features = getNestedObject(target, "features");
        if (features.isArray()) {
            if (auto* arr = features.getArray()) {
                for (const auto& feature : *arr) {
                    info.features.emplace_back(feature.toString().toStdString());
                }
            }
        } else if (features.isString() && features.toString().isNotEmpty()) {
            info.features.emplace_back(features.toString().toStdString());
        }

        // "perpetual" / unparsable -> {} (perpetual). The signed fields override this.
        const auto expiresIso = extractStringProperty(target, "expires_at", "expiresAt");
        if (expiresIso.isNotEmpty()) {
            const auto time = SignedLicense::parseTime(expiresIso);
            if (time.toMilliseconds() != 0 && expiresIso.substring(0, 4).getIntValue() < 9999) {
                info.expiresAt = toTimePoint(time.toMilliseconds());
            }
        }
    }

    currentLicense_ = std::move(info);
}

// ---- Sign-in ------------------------------------------------------------------

void LicenseSystemService::reapRetiredAuthenticators()
{
    std::vector<std::unique_ptr<OAuthBrowserAuthenticator>> finished;
    {
        std::scoped_lock lock(stateMutex_);
        for (auto it = retiredAuthenticators_.begin(); it != retiredAuthenticators_.end();) {
            if (!(*it)->isThreadRunning()) {
                finished.push_back(std::move(*it));
                it = retiredAuthenticators_.erase(it);
            } else {
                ++it;
            }
        }
    }
    // Destroyed outside the lock; their threads have exited, so no join waits.
    if (!finished.empty())
        juce::Logger::writeToLog("[License] reaped " + juce::String((int) finished.size()) + " finished sign-in thread(s)");
}

void LicenseSystemService::startBrowserAuthentication(OAuthCompletionCallback callback)
{
    juce::Logger::writeToLog("[License] === startBrowserAuthentication START ===");
    reapRetiredAuthenticators();

    std::string serverUrl;
    std::string hardwareId;
    OAuthBrowserAuthenticator* authenticator = nullptr;
    {
        std::scoped_lock lock(stateMutex_);

        if (!cache_) {
            juce::Logger::writeToLog("[License] ERROR: startBrowserAuthentication before initialise()");
            if (callback) {
                juce::MessageManager::callAsync([callback]() {
                    callback(false, "SAMPLAR's license service is not ready. Please reopen SAMPLAR.");
                });
            }
            return;
        }

        serverUrl = serverUrlOverride_.empty() ? kDefaultLicenseServerUrl : serverUrlOverride_;
        hardwareId = currentHardwareId_;

        // SAMPL4R refuses a sign-in without a valid v2 machine ID (no v1
        // fallback): say so here instead of a confusing "out of date" page.
        if (!LicenseServerClient::isValidMachineIdV2(juce::String(hardwareId))) {
            juce::Logger::writeToLog("[License] ERROR: no valid v2 machine id - sign-in refused locally");
            if (callback) {
                juce::MessageManager::callAsync([callback]() { callback(false, kMsgBadMachineId); });
            }
            return;
        }

        // A live (not cancelled) sign-in keeps its run: refuse the new one here
        // so the running one's result still counts.
        if (oauthAuthenticator_ && oauthAuthenticator_->isThreadRunning() && !oauthAuthenticator_->isCancelRequested()) {
            juce::Logger::writeToLog("[License] sign-in already in progress - refused");
            if (callback) {
                juce::MessageManager::callAsync([callback]() {
                    callback(false, "Sign-in is already in progress. Finish it in your browser.");
                });
            }
            return;
        }

        // A cancelled sign-in may still be closing (socket poll / aborted HTTP):
        // retire it instead of joining it on the message thread.
        if (oauthAuthenticator_ && oauthAuthenticator_->isThreadRunning() && oauthAuthenticator_->isCancelRequested()) {
            juce::Logger::writeToLog("[License] previous (cancelled) sign-in still closing - retired, not joined");
            retiredAuthenticators_.push_back(std::move(oauthAuthenticator_));
        }
        if (!oauthAuthenticator_)
            oauthAuthenticator_ = std::make_unique<OAuthBrowserAuthenticator>();
        authenticator = oauthAuthenticator_.get();
    }

    const std::uint64_t run = ++authRun_;
    juce::Logger::writeToLog("[License] Starting browser sign-in #" + juce::String((juce::int64) run) + " with " + juce::String(serverUrl));

    // The authenticator delivers on the message thread. The service may be gone
    // by then (plugin closed during sign-in): `alive` is checked first.
    auto alive = alive_;
    authenticator->startAuthentication(serverUrl, hardwareId,
        [alive, this, callback, hardwareId, run](const OAuthBrowserAuthenticator::AuthResult& result) {
            if (!alive->load()) return;   // service gone: touch nothing
            juce::Logger::writeToLog("[License] OAuth result #" + juce::String((juce::int64) run) +
                " received - success: " + juce::String(result.success ? "YES" : "NO"));

            if (run != authRun_) {
                // An older (cancelled / superseded / signed-out) sign-in: never
                // touches the licence state.
                juce::Logger::writeToLog("[License] OAuth result is from an older sign-in - ignored");
                if (callback) callback(false, kMsgSignInCancelled);
                return;
            }

            std::string message = result.errorMessage;
            if (result.success) {
                message = applyAuthResult(result, hardwareId);
                if (message.empty()) {
                    publishStatusUpdate("oauth_activation_success");
                    if (callback) callback(true, "SAMPLAR is activated. Enjoy!");
                    return;
                }
            }
            if (message.empty())
                message = "Sign-in failed. Please try again.";
            juce::Logger::writeToLog("[License] Sign-in not accepted: " + juce::String(message));

            {
                std::scoped_lock lock(stateMutex_);
                // A failed or cancelled sign-in never locks a working licence.
                if (currentStatus_ != LicenseStatus::Valid) {
                    setStatusLocked(LicenseStatus::InvalidKey);
                    lastResultMessage_ = message;
                }
            }
            publishStatusUpdate("oauth_activation_failure");
            if (callback) callback(false, message);
        });
}

void LicenseSystemService::cancelBrowserAuthentication()
{
    juce::Logger::writeToLog("[License] cancelBrowserAuthentication (non-blocking)");
    OAuthBrowserAuthenticator* authenticator = nullptr;
    {
        std::scoped_lock lock(stateMutex_);
        authenticator = oauthAuthenticator_.get();
    }
    // Never join on the UI's behalf; the thread reports "Sign-in cancelled"
    // when it stops. A new sign-in retires it instead of waiting for it.
    if (authenticator != nullptr)
        authenticator->requestCancel();
}

bool LicenseSystemService::isBrowserAuthInProgress() const
{
    std::scoped_lock lock(stateMutex_);
    return oauthAuthenticator_ && oauthAuthenticator_->isAuthenticating() && !oauthAuthenticator_->isCancelRequested();
}

std::string LicenseSystemService::applyAuthResult(const OAuthBrowserAuthenticator::AuthResult& result, const std::string& hardwareId)
{
    const juce::String licenseKey(result.licenseKey);
    juce::Logger::writeToLog("[License] === applyAuthResult START === email " + maskEmail(juce::String(result.email)) +
        ", type " + juce::String(result.licenseType) +
        ", key prefix " + licenseKey.upToFirstOccurrenceOf("-", false, false));

    // Product filter: the server picks licences by product_id=SAMPL4R; an
    // account with only SM5 / other products must not unlock SAMPLAR.
    if (!licenseKey.startsWith(juce::String(LicenseSigningKey::kProductCode) + "-")) {
        juce::Logger::writeToLog("[License] === applyAuthResult END === rejected: not a SAMPL4R licence");
        return kMsgNoSamplarLicense;
    }

    const juce::String payload(result.licensePayload), signature(result.signatureV2);
    const auto nowValue = nowMs();
    const auto now = juce::Time(nowValue);
    juce::StringArray machineIds;
    {
        std::scoped_lock lock(stateMutex_);
        machineIds = thisMachineIdsLocked();
    }

    // Only a reply the server signed for THIS machine and THIS request
    // activates (a proxy or a forged localhost callback can't produce one).
    SignedLicense signedData;
    const bool signedOk = LicenseSignature::verifyLive(payload, signature, juce::String(result.requestNonce),
                                                       machineIds, now, signedData);
    juce::Logger::writeToLog("[License]   signed payload: " + juce::String(payload.isEmpty() ? "absent" : "present") +
        ", verified: " + juce::String(signedOk ? "YES" : "NO"));
    if (kRequireSignature && !signedOk) {
        juce::Logger::writeToLog("[License] === applyAuthResult END === rejected (strict, not verified) - nothing stored");
        return kMsgUnverifiedReply;
    }

    // Accepted: any re-check started before this sign-in is now stale.
    ++epoch_;
    const juce::int64 skewMs = signedOk ? signedData.issued.toMilliseconds() - nowValue : 0;
    const bool clockWrong = signedOk && kRequireSignature && clockSkewTooLarge(skewMs);
    juce::Logger::writeToLog("[License]   clock skew (signed - local): " + juce::String(skewMs / 1000) + " s" +
        (clockWrong ? " -> MORE THAN A DAY: licence stored, NOT unlocked (check your date)" : ""));
    if (signedOk && !clockWrong)
        ClockGuard::reset(juce::jmin(nowValue, signedData.issued.toMilliseconds()));   // server-confirmed, never ahead of this clock

    auto licenseData = mutableCopy(result.licenseInfo);
    if (auto* obj = licenseData.getDynamicObject()) {
        obj->setProperty("license_valid", true);
        if (signedOk && !clockWrong) {
            obj->setProperty("clock_mark", juce::jmin(nowValue, signedData.issued.toMilliseconds()));
            obj->setProperty(kKeySkewMs, skewMs);
        }
        if (payload.isNotEmpty()) {   // unverified kept only in permissive builds; grants nothing in strict
            obj->setProperty("license_payload", payload);
            obj->setProperty("signature_v2", signature);
        }
    }

    {
        std::scoped_lock lock(stateMutex_);

        if (clockWrong) {
            // Stored below (licence + token) so a re-check unlocks once the date
            // is corrected - no second sign-in, no second activation (N-5).
            setStatusLocked(LicenseStatus::InvalidKey);
            lastResultMessage_ = kMsgClockBack;
            retryStep_ = 0;
            clockRetryStep_ = 0;
            scheduleClockRetryLocked(nowValue);
        } else {
            setStatusLocked(LicenseStatus::Valid);
            lastResultMessage_.clear();
            lastVerifiedMs_ = nowValue;    // local receive time of the verified reply (N-5)
            maxSeenNowMs_ = nowValue;
            retryStep_ = 0;
            clockRetryStep_ = 0;
            nextRetryMs_ = 0;
        }
        waitingForToken_ = false;
        signInRequired_ = false;
        currentHardwareId_ = hardwareId;
        shortOfflineWindow_ = !signedOk || isShortWindowLicense(signedData);

        populateLicenseInfoFromVar(licenseData);
        if (signedOk)
            applySignedFieldsLocked(signedData);

        if (cache_) {
            cache_->storeLicense(result.licenseKey, licenseData, hardwareId);
            cache_->updateLastValidation(hardwareId);
            juce::Logger::writeToLog("[License] Licence cached" + juce::String(signedOk ? " (signed)" : ""));
        }

        if (sessionManager_ && !result.accessToken.empty()) {
            SecureSessionManager::SessionData session{};
            session.accessToken = result.accessToken;
            session.userEmail = result.email;
            session.hardwareId = hardwareId;
            // Re-check tokens stay usable for 365 days server-side; the local
            // expiry only matters for display.
            session.expiresAt = std::chrono::system_clock::now() + std::chrono::hours(24 * 365);
            sessionManager_->storeSession(session);
            juce::Logger::writeToLog("[License] Session stored");
        }
    }

    if (clockWrong) {
        juce::Logger::writeToLog("[License] === applyAuthResult END === stored, locked: clock off by more than a day");
        return kMsgClockBack;
    }
    bumpLicenceGeneration();   // other instances in this process unlock now (N-7)
    juce::Logger::writeToLog("[License] === applyAuthResult END === activated (epoch " + juce::String((juce::int64) epoch_.load()) + ")");
    return {};
}

// ---- Sign out -----------------------------------------------------------------

bool LicenseSystemService::deactivateLicense()
{
    // There is no client "deactivate" endpoint. Machine slots are released in
    // the account portal; here we sign out locally and open that page.
    juce::Logger::writeToLog("[License] === deactivateLicense (sign out on this computer) START ===");

    // Every re-check / sign-in started before now is stale from here on.
    ++epoch_;
    ++authRun_;
    OAuthBrowserAuthenticator* authenticator = nullptr;
    RevalidationWorker* worker = nullptr;
    {
        std::scoped_lock lock(stateMutex_);
        authenticator = oauthAuthenticator_.get();
        worker = revalidationWorker_.get();
        if (cache_)
            cache_->clearCache();
        if (sessionManager_ && !currentHardwareId_.empty())
            sessionManager_->clearSession(currentHardwareId_);
        resetCurrentLicense();
        setStatusLocked(LicenseStatus::InvalidKey);
        lastResultMessage_ = kMsgSignedOut;
        lastVerifiedMs_ = 0;
        shortOfflineWindow_ = false;
        signInRequired_ = false;
        retryStep_ = 0;
        clockRetryStep_ = 0;
        nextRetryMs_ = 0;
    }
    // Abort in-flight network work without waiting (results are dropped by epoch / run).
    if (authenticator != nullptr) authenticator->requestCancel();
    if (worker != nullptr) worker->requestCancel();
    bumpLicenceGeneration();   // other instances in this process lock too (same computer)
    publishStatusUpdate("signed_out");

   #if SAMPLAR_SELFTEST
    const bool opened = false;   // tests never open a browser
   #else
    const bool opened = juce::URL(kAccountLicensesUrl).launchInDefaultBrowser();
   #endif
    juce::Logger::writeToLog("[License] === deactivateLicense END === account page opened: " + juce::String(opened ? "yes" : "no"));
    return true;
}

} // namespace samplar
