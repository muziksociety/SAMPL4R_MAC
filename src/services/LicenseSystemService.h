#pragma once

#include "../app/Service.h"
#include "../app/EventBus.h"
#include "../networking/LicenseServerClient.h"
#include "../networking/OAuthBrowserAuthenticator.h"
#include "../security/LicenseSignature.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <juce_events/juce_events.h>
#include <juce_data_structures/juce_data_structures.h>

// 1 = only server-signed licence data (RSA-PSS, LicenseSigningKey.h) unlocks
// SAMPLAR. Never ship 0. CMake may define it (e.g. 0 for a local dev server
// whose key differs from production); the default is strict.
#ifndef SAMPLAR_REQUIRE_LICENSE_SIGNATURE
 #define SAMPLAR_REQUIRE_LICENSE_SIGNATURE 1
#endif

namespace samplar {

class LicenseCache;
class SecureSessionManager;
class FeatureManager;

/**
 * SAMPLAR licensing:
 *  - sign-in: browser OAuth code flow + PKCE, /auth/token-exchange
 *  - proof:   server-signed v2 licence (RSA-PSS, public key embedded), bound to
 *             this machine's licence ID (HWID v2, HardwareFingerprintGenerator::
 *             licenceMachineId(); v1 IDs are not accepted)
 *  - re-check: /auth/revalidate in the background (6 h), rolled access token.
 *    Only SIGNED denials (revoked / expired / inactive / unbound / blocked,
 *    our nonce) count: the first one for a licence on file locks (mutes) but
 *    keeps the licence + token and re-checks in 1 min; a second consecutive one
 *    (>= kDenialConfirmMs later, persisted across restarts) wipes it. Transient
 *    failures (network, 5xx, unsigned or unknown replies) never count; they keep
 *    the licence and retry with backoff (1 min doubling to 1 h, never sooner
 *    than the server's Retry-After - 60 s on a 503 service_unavailable).
 *    token_missing / token_invalid / token_too_old (or 401/403): the licence is
 *    kept (audio stays on inside the offline window) and isSignInRequired()
 *    turns true until the next sign-in or valid re-check.
 *    A signed reply whose issued time is > 1 day from the local clock never
 *    unlocks ("check your date"; retries 1/5/15/60 min).
 *    Results of a re-check started before a sign-in / sign-out are dropped
 *    (epoch counter).
 *  - offline: usable 30 days (signed expiry "perpetual", any type except
 *    PROMO/PROMO2/TRL) / 3 days (any dated expiry, and every PROMO/PROMO2/TRL
 *    licence) after the
 *    last signed reply; clock rollback is detected (ClockGuard).
 *  - running session: a 1-minute timer enforces expiry, clock rollback and the
 *    offline window, and re-checks while stale or locked; every change is
 *    published as license.status.
 *  - audio gate: isAudioAllowed() / audioAllowedFlag() is a lock-free flag that
 *    is true exactly while the status is Valid (incl. the offline window).
 * Within the offline window the status is Valid, so every gate that checks
 * "== Valid" lets offline users in.
 */
class LicenseSystemService : public IService, private juce::Timer {
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
        std::string licenseType; // TRL, STD, PRO, EDU, PROMO, PROMO2 (aligned with license server)
        std::chrono::system_clock::time_point expiresAt{};   // {} = perpetual
        std::vector<std::string> features;
        int maxActivations{0};
        int currentActivations{0};
    };

    explicit LicenseSystemService(EventBus& eventBus);
    ~LicenseSystemService() override;

    void initialise() override;
    void shutdown() override;

    // Browser sign-in. `callback` runs on the message thread; the caller must
    // guard its own lifetime (SafePointer). Publishes license.status either way.
    // Never blocks the message thread (a cancelled sign-in that is still
    // closing is retired and reaped later, not joined).
    using OAuthCompletionCallback = std::function<void(bool success, const std::string& message)>;
    void startBrowserAuthentication(OAuthCompletionCallback callback);
    void cancelBrowserAuthentication();
    bool isBrowserAuthInProgress() const;

    /** "Try again": silent re-check now (coalesced, background thread). */
    void recheckNow();

    /** Sign out on this computer (local licence + session removed) and open the
        account page where the machine slot is released. Always true. */
    bool deactivateLicense();

    bool isFeatureEnabled(const std::string& featureName) const;
    LicenseInfo getCurrentLicenseInfo() const;

    /** This machine's licence ID (HWID v2, computed once per process). Thread-safe. */
    std::string generateHardwareId() const;

    /** Time left in the offline window before a re-check is required. */
    std::chrono::minutes getRemainingGraceTime() const;

    /** The server rejected the stored sign-in token (reason token_missing /
        token_invalid / token_too_old, or 401/403): the user must sign in again.
        The licence is NOT cleared: audio keeps running inside the offline
        window. Cleared by a sign-in, a valid re-check or a sign-out. Also in the
        license.status payload as "signInRequired". */
    bool isSignInRequired() const;

    void setServerUrl(const std::string& url);
    std::string getServerUrl() const;
    LicenseStatus getCurrentStatus() const;
    std::string getLastResultMessage() const;

    // Sign-in token for authenticated API calls.
    std::string getAccessToken() const;

    /** Lock-free licence gate for the AUDIO thread: true while the status is
        Valid (offline window included), false otherwise (before initialise,
        locked, expired, after shutdown). The flag lives as long as the service;
        processBlock may keep a pointer to it. */
    bool isAudioAllowed() const noexcept { return audioAllowed_.load(std::memory_order_relaxed); }
    const std::atomic<bool>& audioAllowedFlag() const noexcept { return audioAllowed_; }

    static constexpr const char* kAccountLicensesUrl = "https://samplemonsta.com/portal/licenses";
    static constexpr const char* kBuyUrl             = "https://samplemonsta.com/auth/buy/SAMPL4R";
    static constexpr const char* kCreateAccountUrl   = "https://samplemonsta.com/auth/register";
    static constexpr const char* kForgotPasswordUrl  = "https://samplemonsta.com/auth/forgot-password";

    static constexpr int kRecheckAfterHours = 6;          // silent /auth/revalidate at launch + while running
    static constexpr int kShortOfflineHours = 72;         // dated expiry, or PROMO / PROMO2 / TRL
    static constexpr int kLongOfflineHours  = 30 * 24;    // signed expiry "perpetual" (not PROMO / PROMO2 / TRL)
    static constexpr int kEnforceTimerMs    = 60 * 1000;  // running-session checks (cheap)
    static constexpr int kRetryMinMs        = 60 * 1000;          // first retry after a transient failure
    static constexpr int kRetryMaxMs        = 60 * 60 * 1000;     // backoff cap / "sign in needed" retry
    // A first signed denial for a licence on file only locks (no wipe); a second
    // one at least this long after the first wipes (review N-1).
    static constexpr int kDenialConfirmMs   = 50 * 1000;

    /** Process-wide licence-change counter (review N-7): bumped by an accepted
        sign-in, a successful re-check, a wipe and a sign-out in ANY instance of
        this process. Every instance polls it on its timer and reloads from the
        shared licence files when it changed. A plain atomic: no destructor, safe
        at DLL unload. */
    static std::uint64_t licenceGeneration() noexcept { return licenceGeneration_.load(); }

   #if SAMPLAR_SELFTEST
    // ---- TEST HOOKS (SamplarTests only; never compiled into the plugin) ----
    std::uint64_t testEpoch() const { return epoch_.load(); }
    void testApplyRevalidation(const LicenseServerClient::RevalResult& r, std::uint64_t epoch) { applyRevalidation(r, epoch); }
    std::string testApplyAuthResult(const OAuthBrowserAuthenticator::AuthResult& r) { return applyAuthResult(r, generateHardwareId()); }
    void testTick() { enforceRunningSession(); }
    void testSetClockOffsetMs(juce::int64 offsetMs) { testClockOffsetMs_ = offsetMs; }
    juce::int64 testRetryDelayMs() const;   // next scheduled re-check - now (0 = none / due)
   #endif

private:
    class RevalidationWorker;

    void resetCurrentLicense();
    void setupEventSubscriptions();
    void teardownEventSubscriptions();
    void performStartupValidation();
    void publishStatusUpdate(const std::string& reason) const;
    juce::var buildStatusPayload(const std::string& reason) const;
    void populateLicenseInfoFromVar(const juce::var& licenseVar);
    void applySignedFieldsLocked(const SignedLicense& s);

    /** The only way the status changes: also drives the audio gate flag. Caller holds stateMutex_. */
    void setStatusLocked(LicenseStatus status);

    /** Wall clock in ms (tests can shift it). */
    juce::int64 nowMs() const;

    static constexpr bool kRequireSignature = SAMPLAR_REQUIRE_LICENSE_SIGNATURE != 0;

    /** 3-day window? Decided only from SIGNED fields: a dated expiry, or a
        PROMO / PROMO2 / TRL type. A signed expiry of "perpetual" keeps 30 days
        whatever the type (server policy: the window keys off the signed
        expiry, not the licence type). */
    static bool isShortWindowLicense(const SignedLicense& s);

    /** Running session (timer, MESSAGE THREAD): expiry, clock rollback, offline
        window; re-check while stale or locked, honouring the retry backoff. */
    void enforceRunningSession();
    void timerCallback() override { enforceRunningSession(); }

    /** Next re-check after a non-valid result. `transient`: exponential backoff
        (1 min doubling, max 1 h); otherwise 1 h. Never sooner than
        `retryAfterSeconds` (the server's Retry-After). Caller holds stateMutex_. */
    void scheduleRetryLocked(bool transient, juce::int64 now, int retryAfterSeconds = 0);

    /** Next re-check while the clock looks wrong (signed time > 1 day from the
        local clock, or a rollback): 1, 5, 15, then 60 min. Never "due now".
        Caller holds stateMutex_. */
    void scheduleClockRetryLocked(juce::int64 now);

    /** Bump the process-wide licence generation and mark it as seen here. */
    void bumpLicenceGeneration();

    /** Another instance changed the licence files: reload them now (local only;
        drops this instance's in-flight re-check by epoch). MESSAGE THREAD. */
    void reloadAfterExternalChange(const char* why);

    /** IDs a signed payload may be bound to: the licence machine ID only. Caller holds stateMutex_. */
    juce::StringArray thisMachineIdsLocked() const;

    bool hasStoredTokenLocked() const;

    /** Sign-in reply -> verify, store, unlock (bumps the epoch). Empty = accepted,
        else the user message. MESSAGE THREAD. */
    std::string applyAuthResult(const OAuthBrowserAuthenticator::AuthResult& result, const std::string& hardwareId);

    /** Background re-check with the stored sign-in token (coalesced, joinable thread). */
    void revalidateAsync();

    /** Re-check result -> refresh / lock. Dropped when `epoch` is stale (a sign-in
        or sign-out happened after the re-check started). MESSAGE THREAD. */
    void applyRevalidation(const LicenseServerClient::RevalResult& result, std::uint64_t epoch);

    /** Destroy retired sign-in threads that have finished (never joins). MESSAGE THREAD. */
    void reapRetiredAuthenticators();

    void stopBackgroundWork();

    EventBus& eventBus_;
    std::vector<std::pair<std::string, EventBus::HandlerId>> subscriptions_;

    std::unique_ptr<LicenseCache> cache_;
    std::unique_ptr<SecureSessionManager> sessionManager_;
    std::unique_ptr<FeatureManager> featureManager_;
    std::unique_ptr<OAuthBrowserAuthenticator> oauthAuthenticator_;
    std::vector<std::unique_ptr<OAuthBrowserAuthenticator>> retiredAuthenticators_;   // cancelled, still closing
    std::unique_ptr<RevalidationWorker> revalidationWorker_;

    LicenseStatus currentStatus_{LicenseStatus::NotInitialised};
    LicenseInfo currentLicense_{};
    std::string currentHardwareId_;   // licence machine ID (HWID v2)
    std::string serverUrlOverride_;
    std::string lastResultMessage_;

    // Audio-thread gate (see isAudioAllowed). Written only by setStatusLocked / shutdown.
    std::atomic<bool> audioAllowed_{false};

    // Lifetime token for async hops back to the message thread: checked first.
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
    std::atomic<bool> revalidateInFlight_{false};

    // Bumped by every accepted sign-in and every sign-out: a re-check result
    // carrying an older epoch is dropped.
    std::atomic<std::uint64_t> epoch_{1};
    // Current sign-in run (message thread): results of older runs never touch state.
    std::uint64_t authRun_ = 0;

    // Offline clock of the licence in force (stateMutex_): last signed issued_at
    // (ms) and whether the 3-day window applies. 0 = nothing verified yet.
    // lastVerifiedMs_ is the LOCAL receive time of the last nonce-verified signed
    // reply (signed issued - stored skew at startup), so a skewed clock never
    // makes the licence look stale or fresh (review N-5).
    juce::int64 lastVerifiedMs_ = 0;
    bool shortOfflineWindow_ = false;

    // Re-check retry schedule (stateMutex_): 0 = no retry pending (due now).
    juce::int64 nextRetryMs_ = 0;
    int retryStep_ = 0;
    int clockRetryStep_ = 0;               // scheduleClockRetryLocked
    juce::int64 lastClockAdvanceMs_ = 0;   // message thread

    // Latest local time this instance has seen (stateMutex_): a mid-session
    // rollback is detected even when the persisted marks are unreadable (N-6).
    juce::int64 maxSeenNowMs_ = 0;

    // Locked with no stored sign-in token: poll for a token on every tick (N-7).
    bool waitingForToken_ = false;

    // The server rejected the sign-in token (token_*): ask for a sign-in, keep
    // the licence (stateMutex_).
    bool signInRequired_ = false;

    // Process-wide licence generation (see licenceGeneration()) and the value
    // this instance has applied (message thread).
    static inline std::atomic<std::uint64_t> licenceGeneration_{1};
    std::uint64_t seenGeneration_ = 0;

   #if SAMPLAR_SELFTEST
    juce::int64 testClockOffsetMs_ = 0;
   #endif

    mutable std::mutex stateMutex_;
};

} // namespace samplar
