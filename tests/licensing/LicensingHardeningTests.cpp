// Licensing hardening (review round 1: L-H1, L-H2, L-M1, L-M2, L-M4, L-M6, A5, A6).
// State machine of LicenseSystemService with REAL RSA-PSS signatures: a fresh
// 2048-bit key is generated with Windows CNG per run and installed through
// LicenseSignature::testKeyOverride() (SAMPLAR_SELFTEST only). Every file goes
// to a sandboxed SAMPLAR_CACHE_DIR_OVERRIDE folder. No network: re-check
// results are injected with the same epoch the service would have captured.
#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>

#include "app/EventBus.h"
#include "networking/LicenseServerClient.h"
#include "security/ClockGuard.h"
#include "security/HardwareFingerprintGenerator.h"
#include "security/LicenseCache.h"
#include "security/LicenseSignature.h"
#include "security/SecureSessionManager.h"
#include "services/LicenseSystemService.h"
#include "TestHelpers.h"
#include "TestSigner.h"

#include <thread>
#include <vector>

namespace samplar {

namespace {

#if JUCE_WINDOWS
using test::TestSigner;
#endif

juce::String iso (juce::int64 ms) { return juce::Time (ms).toISO8601 (true); }

juce::String payloadFor (const juce::String& hwid, juce::int64 issuedMs, const juce::String& expires,
                         const juce::String& status, const juce::String& nonce, const juce::String& type = "PRO")
{
    return "v2|SAMPL4R|user@example.com|" + type + "|" + hwid + "|" + iso (issuedMs) + "|" + expires + "|" + status + "|" + nonce;
}

/** Counts license.status publications (the UI's notification). */
struct StatusSpy
{
    explicit StatusSpy (EventBus& b) : bus (b)
    {
        id = bus.subscribe ("license.status", [this] (const juce::var& p) { ++count; last = p; });
    }
    ~StatusSpy() { bus.unsubscribe ("license.status", id); }
    EventBus& bus;
    EventBus::HandlerId id {};
    int count = 0;
    juce::var last;
};

} // namespace

class LicensingHardeningTests : public juce::UnitTest
{
public:
    LicensingHardeningTests() : juce::UnitTest ("Licensing hardening", "SAMPLAR Licensing") {}

    void runTest() override
    {
        beginTest ("interpretRevalidate: reasons -> outcomes (transient never Denied/locked)");
        testInterpret();

        beginTest ("ClockGuard: empty / corrupt session.idx reads as unknown, never as future");
        testClockGuardDecode();

        beginTest ("ClockGuard: fixed plausible range (N-6), advance never lowers, reset ignores absurd times");
        testClockGuardFixedRange();

        beginTest ("encryption.key: empty / short / garbage regenerated atomically, valid key kept (N-18)");
        testEncryptionKeyRegen();

        beginTest ("bounded child process: slow command killed on timeout and on cancel");
        testBoundedCommand();

        beginTest ("licence machine id: v2, cached once per process, fast");
        testMachineId();

       #if JUCE_WINDOWS
        TestSigner signer;
        expect (signer.ok(), "CNG test key generated");
        if (! signer.ok())
            return;
        LicenseSignature::testKeyOverride() = signer.pem();

        beginTest ("verifyDenial: signed denial for this machine + nonce only");
        testVerifyDenial (signer);

        beginTest ("state machine: transient failures keep the licence; only a SIGNED denial clears it");
        testTransientKeepsSignedDenialClears (signer);

        beginTest ("state machine: stale re-check after sign-out / sign-in is dropped (epoch)");
        testStaleRecheckDropped (signer);

        beginTest ("state machine: corrupt session.idx does not lock a valid offline licence");
        testCorruptSessionIdxAtStartup (signer);

        beginTest ("state machine: expiry during a running session locks + notifies + mutes audio");
        testExpiryMidSession (signer);

        beginTest ("state machine: v1 / other-machine payloads are not accepted");
        testV1Rejected (signer);

        beginTest ("state machine: locked + transient -> retries with backoff");
        testBackoff (signer);

        beginTest ("N-1: first signed denial locks but keeps licence + token; second wipes; restart counts; valid clears");
        testPendingDenial (signer);

        beginTest ("N-5: clock skewed > 1 day: no Valid/locked flapping over many ticks, retry interval grows to 60 min");
        testClockSkewNoFlapping (signer);

        beginTest ("N-5: clock 8 h ahead: verified reply is not 'stale' (no per-minute re-check); 2 days ahead at sign-in: stored, locked");
        testClockAhead (signer);

        beginTest ("N-6: mid-session rollback with garbage marks is caught by the in-memory maximum");
        testRollbackMaxSeen (signer);

        beginTest ("N-7: sign-in / sign-out in one instance reaches the other instances on their next tick");
        testNotifierTwoInstances (signer);

        LicenseSignature::testKeyOverride() = {};
       #endif
    }

private:
    using O = LicenseServerClient::RevalOutcome;

    void testInterpret()
    {
        auto out = [] (int code, const char* body) { return LicenseServerClient::interpretRevalidate (code, body).outcome; };
        expect (out (200, R"({"success":true,"license_valid":true})") == O::Valid);
        expect (out (200, R"({"success":false,"license_valid":false,"reason":"service_unavailable"})") == O::NetworkError,
                "service unavailable (old server, HTTP 200) is transient");
        expect (out (503, R"({"success":false,"reason":"service_unavailable"})") == O::NetworkError);
        expect (out (502, "<html>Bad gateway</html>") == O::NetworkError);
        expect (out (404, "<html>Not Found</html>") == O::NetworkError, "non-JSON 404 (deploy / CDN) is transient");
        expect (out (429, "{}") == O::NetworkError);
        expect (out (200, R"({"success":false,"license_valid":false,"reason":"token_invalid"})") == O::AuthRequired);
        expect (out (200, R"({"success":false,"license_valid":false,"reason":"token_too_old"})") == O::AuthRequired);
        expect (out (401, "") == O::AuthRequired);
        expect (out (200, R"({"success":false,"license_valid":false,"error":"Something"})") == O::NetworkError,
                "unknown failure without reason is transient");
        expect (out (200, R"({"success":true,"license_valid":false,"reason":"revoked"})") == O::Denied);
        expect (out (200, R"({"success":false,"license_valid":false,"reason":"blocked"})") == O::Denied);

        const auto r = LicenseServerClient::interpretRevalidate (200,
            R"({"success":true,"license_valid":false,"reason":"expired","denial_payload":"P","denial_signature":"S"})");
        expectEquals (r.reason, juce::String ("expired"));
        expectEquals (r.denialPayload, juce::String ("P"));
        expectEquals (r.denialSignature, juce::String ("S"));
    }

    void testClockGuardDecode()
    {
        test::ScopedLicenseSandbox sandbox;
        const auto now = juce::Time::currentTimeMillis();
        expectEquals (ClockGuard::decode (""), (juce::int64) 0, "empty");
        expectEquals (ClockGuard::decode ("   \n"), (juce::int64) 0, "whitespace");
        expectEquals (ClockGuard::decode ("not a number"), (juce::int64) 0, "garbage");
        expectEquals (ClockGuard::decode ("12ab"), (juce::int64) 0, "partly digits");
        expectEquals (ClockGuard::decode ("0"), (juce::int64) 0, "0 decodes to a far-future mark -> unknown");

        // A real mark round-trips; one decoding outside the fixed range is unknown.
        ClockGuard::reset (now);
        expectEquals (ClockGuard::highWater(), now);
        const auto f = sandbox.dir().getChildFile ("session.idx");
        for (const auto* content : { "", "garbage", "0" })
        {
            f.replaceWithText (content);
            expectEquals (ClockGuard::highWater(), (juce::int64) 0, juce::String ("content '") + content + "' is unknown");
            expect (! ClockGuard::rolledBack (now), "an unreadable mark never means 'clock rolled back'");
        }
        f.deleteFile();
    }

    void testClockGuardFixedRange()
    {
        test::ScopedLicenseSandbox sandbox;
        const juce::int64 day = 24LL * 60 * 60 * 1000;
        const juce::int64 year = 365LL * day;
        const auto now = juce::Time::currentTimeMillis();

        // The upper bound is a constant far beyond "now" (build year + 30), not now + N.
        expect (ClockGuard::kMaxPlausibleMs > now + 20 * year, "upper bound is build year + 30 y");
        expect (ClockGuard::kMaxPlausibleMs < now + 40 * year, "upper bound is sane");

        // A real mark stays readable no matter how far the clock is rolled back:
        // decode does not depend on the current time any more.
        ClockGuard::reset (now);
        expectEquals (ClockGuard::highWater(), now);
        expect (ClockGuard::rolledBack (now - 15 * year), "rollback by 15 years still detected");
        expect (ClockGuard::rolledBack (now - 2 * day));

        // advance() never lowers the mark; reset() ignores absurd times.
        ClockGuard::advance (now - 5 * day);
        expectEquals (ClockGuard::highWater(), now, "advance(lower) is a no-op");
        ClockGuard::reset (ClockGuard::kMaxPlausibleMs + day);
        expectEquals (ClockGuard::highWater(), now, "reset(beyond the plausible range) ignored");
        ClockGuard::reset (0);
        expectEquals (ClockGuard::highWater(), now, "reset(0) ignored");
        ClockGuard::advance (ClockGuard::kMaxPlausibleMs + day);
        expectEquals (ClockGuard::highWater(), now, "advance(beyond the plausible range) ignored");
    }

    void testEncryptionKeyRegen()
    {
        test::ScopedLicenseSandbox sandbox;
        const auto dir = sandbox.dir().getChildFile ("sessions");
        dir.createDirectory();
        const auto keyFile = dir.getChildFile ("encryption.key");

        auto roundTrip = [this] (const juce::String& what)
        {
            SecureSessionManager m;
            expect (m.isEncryptionAvailable(), what + ": encryption available");
            SecureSessionManager::SessionData s;
            s.accessToken = "tok-123";
            s.hardwareId = "KEYTEST";
            expect (m.storeSession (s), what + ": stored");
            auto back = SecureSessionManager().retrieveSession ("KEYTEST");
            expect (back.has_value() && back->accessToken == "tok-123", what + ": token round-trips");
        };

        // Fresh: created, 32 hex chars.
        roundTrip ("fresh");
        const auto created = keyFile.loadFileAsString().trim();
        expect (SecureSessionManager::isUsableKey (created), "created key is usable (" + created + ")");

        // A valid key is kept as is.
        roundTrip ("existing");
        expectEquals (keyFile.loadFileAsString().trim(), created, "valid key unchanged");

        // Empty / short / garbage keys are replaced.
        for (const auto* bad : { "", "   ", "abc", "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz" })
        {
            keyFile.replaceWithText (bad);
            roundTrip (juce::String ("bad key '") + bad + "'");
            const auto now = keyFile.loadFileAsString().trim();
            expect (SecureSessionManager::isUsableKey (now), juce::String ("regenerated for '") + bad + "'");
            expect (now != created, "a new key, not the old one");
        }

        // No temp files left behind.
        juce::Array<juce::File> leftovers;
        dir.findChildFiles (leftovers, juce::File::findFiles, false, "encryption.key.tmp-*");
        expectEquals (leftovers.size(), 0, "no temp files left");
    }

    void testBoundedCommand()
    {
        // ping -n 30 runs ~29 s: must be killed after the timeout.
        auto t0 = juce::Time::getMillisecondCounter();
        auto r = HardwareFingerprintGenerator::runBoundedCommand ("ping -n 30 127.0.0.1", 300);
        auto elapsed = (int) (juce::Time::getMillisecondCounter() - t0);
        expect (r.started, "started");
        expect (r.timedOut, "timed out");
        expect (r.output.empty(), "no partial output used");
        expect (elapsed < 1500, "returned within the bound (" + juce::String (elapsed) + " ms)");

        // Cancel from another thread.
        std::atomic<bool> cancel { false };
        std::thread canceller ([&cancel] { juce::Thread::sleep (200); cancel = true; });
        t0 = juce::Time::getMillisecondCounter();
        r = HardwareFingerprintGenerator::runBoundedCommand ("ping -n 30 127.0.0.1", 20000, &cancel);
        elapsed = (int) (juce::Time::getMillisecondCounter() - t0);
        canceller.join();
        expect (r.cancelled && ! r.timedOut, "cancelled");
        expect (elapsed < 1500, "cancel noticed quickly (" + juce::String (elapsed) + " ms)");

        // A normal command still returns its output.
        r = HardwareFingerprintGenerator::runBoundedCommand ("cmd.exe /C echo samplar", 5000);
        expect (! r.timedOut && ! r.cancelled);
        expectEquals (juce::String (r.output), juce::String ("samplar"));
    }

    void testMachineId()
    {
        const auto t0 = juce::Time::getMillisecondCounter();
        const auto a = HardwareFingerprintGenerator::licenceMachineId();
        const auto b = HardwareFingerprintGenerator::licenceMachineId();
        const auto elapsed = (int) (juce::Time::getMillisecondCounter() - t0);
        expectEquals ((int) a.length(), 32, "v2 = 32 hex chars");
        expect (a == b, "stable");
        expect (elapsed < 100, "no child process / cached (" + juce::String (elapsed) + " ms)");
    }

   #if JUCE_WINDOWS
    void testVerifyDenial (const TestSigner& signer)
    {
        const juce::String hw = "ABCDEF0123456789ABCDEF0123456789";
        const juce::String nonce = "0123456789abcdef0123456789abcdef";
        const auto now = juce::Time::currentTimeMillis();
        SignedLicense s;
        auto deny = payloadFor (hw, now, "perpetual", "revoked", nonce);
        expect (LicenseSignature::verifyDenial (deny, signer.sign (deny), nonce, { hw }, juce::Time (now), s));
        expect (! LicenseSignature::verifyDenial (deny, signer.sign (deny), "ffffffffffffffffffffffffffffffff", { hw }, juce::Time (now), s), "wrong nonce");
        expect (! LicenseSignature::verifyDenial (deny, signer.sign (deny), nonce, { "OTHER" }, juce::Time (now), s), "other machine");
        expect (! LicenseSignature::verifyDenial (deny, "", nonce, { hw }, juce::Time (now), s), "unsigned");
        expect (! LicenseSignature::verifyDenial (deny, signer.sign (deny), nonce, { hw }, juce::Time (now + 3LL * 86400000), s), "stale");
        auto active = payloadFor (hw, now, "perpetual", "active", nonce);
        expect (! LicenseSignature::verifyDenial (active, signer.sign (active), nonce, { hw }, juce::Time (now), s),
                "an active licence payload is never a denial");
        auto bogus = payloadFor (hw, now, "perpetual", "whatever", nonce);
        expect (! LicenseSignature::verifyDenial (bogus, signer.sign (bogus), nonce, { hw }, juce::Time (now), s));
    }

    /** Fresh service in the sandbox, signed in with a signed licence. */
    struct Fixture
    {
        test::ScopedLicenseSandbox sandbox;
        EventBus bus;
        std::unique_ptr<LicenseSystemService> svc;
        juce::String hw;

        explicit Fixture (bool signIn, const TestSigner& signer, const juce::String& expires = "perpetual",
                          const juce::String& type = "PRO")
        {
            svc = std::make_unique<LicenseSystemService> (bus);
            svc->initialise();
            hw = svc->generateHardwareId();
            if (signIn)
                signInWith (signer, expires, type);
        }

        std::string signInWith (const TestSigner& signer, const juce::String& expires = "perpetual",
                                const juce::String& type = "PRO", juce::String hwid = {})
        {
            if (hwid.isEmpty()) hwid = hw;
            OAuthBrowserAuthenticator::AuthResult a;
            a.success = true;
            a.licenseKey = "SAMPL4R-PRO-TEST";
            a.email = "user@example.com";
            a.licenseType = type.toStdString();
            a.accessToken = "token-1";
            a.requestNonce = LicenseSignature::makeNonce().toStdString();
            const auto p = payloadFor (hwid, juce::Time::currentTimeMillis(), expires, "active", a.requestNonce, type);
            a.licensePayload = p.toStdString();
            a.signatureV2 = signer.sign (p).toStdString();
            auto* info = new juce::DynamicObject();
            info->setProperty ("customer_email", "user@example.com");
            info->setProperty ("license_type", type);
            a.licenseInfo = juce::var (info);
            return svc->testApplyAuthResult (a);
        }

        juce::String cachedPayload() const
        {
            auto e = LicenseCache().retrieveLicense (hw.toStdString());
            return e.has_value() ? e->licenseData.getProperty ("license_payload", "").toString() : juce::String();
        }

        LicenseServerClient::RevalResult result (O outcome, const juce::String& reason = {}) const
        {
            LicenseServerClient::RevalResult r;
            r.outcome = outcome;
            r.reason = reason;
            r.requestNonce = LicenseSignature::makeNonce();
            return r;
        }

        LicenseServerClient::RevalResult signedDenial (const TestSigner& signer, const juce::String& status) const
        {
            auto r = result (O::Denied, status);
            r.denialPayload = payloadFor (hw, juce::Time::currentTimeMillis(), "perpetual", status, r.requestNonce);
            r.denialSignature = signer.sign (r.denialPayload);
            return r;
        }

        LicenseServerClient::RevalResult signedValid (const TestSigner& signer) const
        {
            auto r = result (O::Valid);
            r.licensePayload = payloadFor (hw, juce::Time::currentTimeMillis(), "perpetual", "active", r.requestNonce);
            r.signature = signer.sign (r.licensePayload);
            r.accessToken = "token-rolled";
            return r;
        }

        ~Fixture() { svc->shutdown(); svc.reset(); }
    };

    using S = LicenseSystemService::LicenseStatus;

    void testTransientKeepsSignedDenialClears (const TestSigner& signer)
    {
        Fixture f (false, signer);
        expect (! f.svc->isAudioAllowed(), "no licence: audio not allowed");
        expectEquals (juce::String (f.signInWith (signer)), juce::String(), "signed sign-in accepted");
        expect (f.svc->getCurrentStatus() == S::Valid);
        expect (f.svc->isAudioAllowed(), "Valid: audio allowed");
        expect (f.cachedPayload().isNotEmpty());

        const auto e = f.svc->testEpoch();
        for (auto r : { f.result (O::NetworkError),
                        f.result (O::NetworkError, "service_unavailable"),
                        f.result (O::Denied, "revoked"),            // UNSIGNED denial
                        f.result (O::AuthRequired, "token_invalid") })
        {
            f.svc->testApplyRevalidation (r, e);
            expect (f.svc->getCurrentStatus() == S::Valid, "transient / unsigned / auth: still Valid (" + r.reason + ")");
            expect (f.svc->isAudioAllowed());
            expect (f.cachedPayload().isNotEmpty(), "signed licence kept");
        }

        // A denial signed for another request (replay) or another machine is ignored.
        auto replay = f.signedDenial (signer, "revoked");
        replay.requestNonce = LicenseSignature::makeNonce();
        f.svc->testApplyRevalidation (replay, e);
        expect (f.svc->getCurrentStatus() == S::Valid, "replayed denial ignored");

        // A properly signed denial locks (N-1: the first one keeps the licence),
        // a second one a minute later wipes.
        StatusSpy spy (f.bus);
        f.svc->testApplyRevalidation (f.signedDenial (signer, "revoked"), e);
        expect (f.svc->getCurrentStatus() == S::InvalidKey, "signed revocation locks");
        expect (! f.svc->isAudioAllowed(), "audio muted");
        expect (f.cachedPayload().isNotEmpty(), "first signed denial: licence kept");
        f.svc->testSetClockOffsetMs (2LL * 60 * 1000);
        f.svc->testApplyRevalidation (f.signedDenial (signer, "revoked"), e);
        expect (f.svc->getCurrentStatus() == S::InvalidKey);
        expect (f.cachedPayload().isEmpty(), "second signed denial: signed licence removed");
        expect (spy.count >= 1, "UI notified");

        // Next launch stays locked with the right message.
        auto again = std::make_unique<LicenseSystemService> (f.bus);
        again->initialise();
        expect (again->getCurrentStatus() == S::InvalidKey);
        expect (juce::String (again->getLastResultMessage()).contains ("revoked"));
        again->shutdown();

        // A signed "expired" denial maps to Expired.
        Fixture g (true, signer);
        g.svc->testApplyRevalidation (g.signedDenial (signer, "expired"), g.svc->testEpoch());
        g.svc->testSetClockOffsetMs (2LL * 60 * 1000);
        g.svc->testApplyRevalidation (g.signedDenial (signer, "expired"), g.svc->testEpoch());
        expect (g.svc->getCurrentStatus() == S::Expired);
    }

    void testStaleRecheckDropped (const TestSigner& signer)
    {
        // (a) re-check started, user signs out, a VALID reply arrives: must not restore.
        {
            Fixture f (true, signer);
            const auto e = f.svc->testEpoch();
            f.svc->deactivateLicense();
            expect (f.svc->getCurrentStatus() == S::InvalidKey);
            f.svc->testApplyRevalidation (f.signedValid (signer), e);
            expect (f.svc->getCurrentStatus() == S::InvalidKey, "stale valid reply after sign-out dropped");
            expect (! f.svc->isAudioAllowed());
            expect (f.cachedPayload().isEmpty(), "signed-out cache not rebuilt");
        }
        // (b) re-check started, user signs in again, a signed DENIAL for the old run arrives: must not wipe.
        {
            Fixture f (true, signer);
            const auto e = f.svc->testEpoch();
            auto oldDenial = f.signedDenial (signer, "revoked");
            expectEquals (juce::String (f.signInWith (signer)), juce::String(), "fresh sign-in");
            expect (f.svc->testEpoch() != e, "sign-in bumped the epoch");
            f.svc->testApplyRevalidation (oldDenial, e);
            expect (f.svc->getCurrentStatus() == S::Valid, "stale denial dropped");
            expect (f.cachedPayload().isNotEmpty(), "fresh licence kept");
        }
    }

    void testCorruptSessionIdxAtStartup (const TestSigner& signer)
    {
        Fixture f (true, signer);
        f.sandbox.dir().getChildFile ("session.idx").replaceWithText ("");   // AV / disk error / zero-length
        auto next = std::make_unique<LicenseSystemService> (f.bus);
        next->initialise();
        expect (next->getCurrentStatus() == S::Valid, "empty session.idx: still Valid offline");
        next->shutdown();

        f.sandbox.dir().getChildFile ("session.idx").replaceWithText ("@@corrupt@@");
        next = std::make_unique<LicenseSystemService> (f.bus);
        next->initialise();
        expect (next->getCurrentStatus() == S::Valid, "garbage session.idx: still Valid offline");
        next->shutdown();
    }

    void testExpiryMidSession (const TestSigner& signer)
    {
        const auto in1h = iso (juce::Time::currentTimeMillis() + 60LL * 60 * 1000);
        Fixture f (true, signer, in1h, "TRL");
        expect (f.svc->getCurrentStatus() == S::Valid, "trial valid now");
        f.svc->testTick();
        expect (f.svc->getCurrentStatus() == S::Valid, "tick before expiry changes nothing");

        StatusSpy spy (f.bus);
        f.svc->testSetClockOffsetMs (2LL * 60 * 60 * 1000);   // 2 h later, same session
        f.svc->testTick();
        expect (f.svc->getCurrentStatus() == S::Expired, "expired while running");
        expect (! f.svc->isAudioAllowed(), "audio muted");
        expect (spy.count >= 1, "license.status published");
        if (spy.last.isObject())
            expect (spy.last.getProperty ("reason", "").toString() == "running_expired");
    }

    void testV1Rejected (const TestSigner& signer)
    {
        Fixture f (false, signer);
        // A licence signed for a 64-hex v1 id (or any other id) never activates.
        const auto msg = f.signInWith (signer, "perpetual", "PRO", juce::String::repeatedString ("ab", 32));
        expect (! msg.empty(), "v1-bound payload rejected");
        expect (f.svc->getCurrentStatus() != S::Valid);
        expect (! f.svc->isAudioAllowed());

        // A plaintext (unencrypted) cache blob is rejected.
        auto* inner = new juce::DynamicObject();
        inner->setProperty ("licenseData", juce::var (new juce::DynamicObject()));
        auto* outer = new juce::DynamicObject();
        outer->setProperty ("licenseKey", "SAMPL4R-X");
        outer->setProperty ("hardwareId", f.hw);
        outer->setProperty ("licenseData", juce::var (inner));
        f.sandbox.dir().getChildFile ("license_cache.json").replaceWithText (juce::JSON::toString (juce::var (outer)));
        expect (! LicenseCache().retrieveLicense (f.hw.toStdString()).has_value(), "plaintext cache rejected");
    }

    void testBackoff (const TestSigner& signer)
    {
        Fixture f (false, signer);   // locked, no licence
        const auto e = f.svc->testEpoch();
        f.svc->testApplyRevalidation (f.result (O::NetworkError), e);
        const auto d1 = f.svc->testRetryDelayMs();
        f.svc->testApplyRevalidation (f.result (O::NetworkError), e);
        const auto d2 = f.svc->testRetryDelayMs();
        expect (d1 > 0 && d1 <= LicenseSystemService::kRetryMinMs, "first retry ~1 min");
        expect (d2 > d1, "backoff grows");
        for (int i = 0; i < 12; ++i)
            f.svc->testApplyRevalidation (f.result (O::NetworkError), e);
        expect (f.svc->testRetryDelayMs() <= LicenseSystemService::kRetryMaxMs, "capped at 1 h");

        // A valid signed reply unlocks a locked user (auto re-check, L-M6) and clears the backoff.
        f.svc->testApplyRevalidation (f.signedValid (signer), e);
        expect (f.svc->getCurrentStatus() == S::Valid);
        expectEquals (f.svc->testRetryDelayMs(), (juce::int64) 0);
    }

    static constexpr juce::int64 kMin = 60LL * 1000;
    static constexpr juce::int64 kDay = 24LL * 60 * 60 * 1000;

    void testPendingDenial (const TestSigner& signer)
    {
        // (a) first denial: locked, muted, licence + token kept, re-check in ~1 min.
        {
            Fixture f (true, signer);
            const auto e = f.svc->testEpoch();
            f.svc->testApplyRevalidation (f.signedDenial (signer, "inactive"), e);
            expect (f.svc->getCurrentStatus() == S::InvalidKey, "first denial locks");
            expect (! f.svc->isAudioAllowed(), "first denial mutes");
            expect (f.cachedPayload().isNotEmpty(), "first denial keeps the signed licence");
            expect (! f.svc->getAccessToken().empty(), "first denial keeps the sign-in token");
            const auto d = f.svc->testRetryDelayMs();
            expect (d > 50 * 1000 && d <= kMin, "re-check in about a minute (" + juce::String (d) + " ms)");

            // A second denial right away (e.g. another instance) is not a confirmation.
            f.svc->testApplyRevalidation (f.signedDenial (signer, "inactive"), e);
            expect (f.cachedPayload().isNotEmpty(), "second denial within 50 s does not wipe");

            // Transient errors in between never count and never reset the pending state.
            f.svc->testApplyRevalidation (f.result (O::NetworkError), e);
            expect (f.cachedPayload().isNotEmpty(), "transient: still kept");
            expect (f.svc->getCurrentStatus() == S::InvalidKey, "transient: still locked");

            // Confirmed a minute later: wiped.
            f.svc->testSetClockOffsetMs (2 * kMin);
            f.svc->testApplyRevalidation (f.signedDenial (signer, "inactive"), e);
            expect (f.cachedPayload().isEmpty(), "confirmed denial wipes");
            expect (juce::String (f.svc->getLastResultMessage()).contains ("no longer active"), "denial message shown");
        }
        // (b) restart between the two denials: the pending denial is persisted.
        {
            Fixture f (true, signer);
            f.svc->testApplyRevalidation (f.signedDenial (signer, "revoked"), f.svc->testEpoch());
            auto again = std::make_unique<LicenseSystemService> (f.bus);
            again->initialise();
            expect (again->getCurrentStatus() == S::InvalidKey, "restart: still locked (pending denial)");
            expect (! again->isAudioAllowed());
            expect (f.cachedPayload().isNotEmpty(), "restart: licence still on file");
            again->testSetClockOffsetMs (2 * kMin);
            again->testApplyRevalidation (f.signedDenial (signer, "revoked"), again->testEpoch());
            expect (f.cachedPayload().isEmpty(), "denial after restart confirms and wipes");
            again->shutdown();
        }
        // (c) a valid reply clears the pending denial: the next denial is a "first" again.
        {
            Fixture f (true, signer);
            const auto e = f.svc->testEpoch();
            f.svc->testApplyRevalidation (f.signedDenial (signer, "revoked"), e);
            f.svc->testApplyRevalidation (f.signedValid (signer), e);
            expect (f.svc->getCurrentStatus() == S::Valid, "valid reply unlocks");
            expect (f.svc->isAudioAllowed());
            f.svc->testSetClockOffsetMs (2 * kMin);
            f.svc->testApplyRevalidation (f.signedDenial (signer, "revoked"), e);
            expect (f.cachedPayload().isNotEmpty(), "denial after a valid reply is a first one again (kept)");
            auto again = std::make_unique<LicenseSystemService> (f.bus);
            again->initialise();
            expect (again->getCurrentStatus() == S::InvalidKey, "pending persisted");
            again->shutdown();
        }
    }

    void testClockSkewNoFlapping (const TestSigner& signer)
    {
        Fixture f (true, signer);   // signed in with a correct clock
        const auto e = f.svc->testEpoch();
        const auto markBefore = ClockGuard::highWater();
        f.svc->testSetClockOffsetMs (-2 * kDay);   // the clock is now 2 days behind

        int validCount = 0;
        juce::int64 prevDelay = 0;
        juce::Array<juce::int64> delays;
        for (int i = 0; i < 8; ++i)
        {
            f.svc->testTick();   // running-session check
            if (f.svc->getCurrentStatus() == S::Valid || f.svc->isAudioAllowed()) ++validCount;
            const auto afterTick = f.svc->testRetryDelayMs();
            expect (afterTick > 0, "tick never zeroes the retry while the clock is wrong (step " + juce::String (i) + ")");

            f.svc->testApplyRevalidation (f.signedValid (signer), e);   // genuine reply, server time
            if (f.svc->getCurrentStatus() == S::Valid || f.svc->isAudioAllowed()) ++validCount;
            const auto d = f.svc->testRetryDelayMs();
            delays.add (d);
            expect (d + 1000 >= prevDelay, "retry interval never shrinks (" + juce::String (prevDelay) + " -> " + juce::String (d) + ")");
            prevDelay = d;
        }
        expectEquals (validCount, 0, "never Valid while the clock is off by 2 days (no flapping)");
        expect (delays[0] >= 4 * kMin && delays[0] <= 5 * kMin, "second step ~5 min (" + juce::String (delays[0]) + ")");
        expect (delays.getLast() > 59 * kMin && delays.getLast() <= 60 * kMin, "capped at 60 min (" + juce::String (delays.getLast()) + ")");
        expect (juce::String (f.svc->getLastResultMessage()).contains ("date"), "'check your date' message");
        expect (f.cachedPayload().isNotEmpty(), "licence kept");
        expect (ClockGuard::highWater() >= markBefore, "rollback mark never lowered to the skewed clock");

        // The user fixes the date: the next genuine reply unlocks, ticks keep it Valid.
        f.svc->testSetClockOffsetMs (0);
        f.svc->testApplyRevalidation (f.signedValid (signer), e);
        expect (f.svc->getCurrentStatus() == S::Valid, "corrected clock unlocks without a new sign-in");
        for (int i = 0; i < 3; ++i)
            f.svc->testTick();
        expect (f.svc->getCurrentStatus() == S::Valid, "and stays Valid");
        expectEquals (f.svc->testRetryDelayMs(), (juce::int64) 0, "no re-check scheduled");
    }

    void testClockAhead (const TestSigner& signer)
    {
        // 8 h ahead: within the tolerance. Before N-5 lastVerified was the signed
        // issue time, so the licence looked > 6 h stale right after every reply.
        {
            Fixture f (false, signer);
            f.svc->testSetClockOffsetMs (8LL * 60 * kMin);
            expectEquals (juce::String (f.signInWith (signer)), juce::String(), "sign-in accepted");
            expect (f.svc->getCurrentStatus() == S::Valid);
            for (int i = 0; i < 3; ++i)
            {
                f.svc->testTick();
                expect (f.svc->getCurrentStatus() == S::Valid);
                expectEquals (f.svc->testRetryDelayMs(), (juce::int64) 0, "no re-check requested (not stale)");
                f.svc->testApplyRevalidation (f.signedValid (signer), f.svc->testEpoch());
            }
        }
        // 2 days ahead at sign-in: stored (licence + token), locked with the date message.
        {
            Fixture f (false, signer);
            f.svc->testSetClockOffsetMs (2 * kDay);
            const auto msg = juce::String (f.signInWith (signer));
            expect (msg.contains ("date"), "sign-in reports the wrong date (" + msg + ")");
            expect (f.svc->getCurrentStatus() != S::Valid && ! f.svc->isAudioAllowed(), "not unlocked");
            expect (f.cachedPayload().isNotEmpty() && ! f.svc->getAccessToken().empty(), "licence + token stored");
            expect (f.svc->testRetryDelayMs() > 0, "re-check scheduled");
            f.svc->testSetClockOffsetMs (0);
            f.svc->testApplyRevalidation (f.signedValid (signer), f.svc->testEpoch());
            expect (f.svc->getCurrentStatus() == S::Valid, "fixed date: unlocks without a second sign-in");
        }
    }

    void testRollbackMaxSeen (const TestSigner& signer)
    {
        Fixture f (true, signer);
        f.svc->testSetClockOffsetMs (5 * kDay);
        f.svc->testTick();   // the running session sees +5 days
        expect (f.svc->getCurrentStatus() == S::Valid);

        // The pirate wipes the persisted marks, then rolls back 3 days (still
        // 2 days after the last verified reply, so only the in-memory max sees it).
        f.sandbox.dir().getChildFile ("session.idx").replaceWithText ("@@garbage@@");
        expectEquals (ClockGuard::highWater(), (juce::int64) 0, "persisted marks unreadable");
        f.svc->testSetClockOffsetMs (2 * kDay);
        f.svc->testTick();
        expect (f.svc->getCurrentStatus() != S::Valid, "rollback caught by the in-memory maximum");
        expect (! f.svc->isAudioAllowed());
        expect (juce::String (f.svc->getLastResultMessage()).contains ("date"));
        expect (f.svc->testRetryDelayMs() > 0, "clock lock never schedules 'due now'");
        expectEquals (ClockGuard::highWater(), (juce::int64) 0, "the rolled-back time was not written as a mark");
    }

    void testNotifierTwoInstances (const TestSigner& signer)
    {
        Fixture a (false, signer);
        auto b = std::make_unique<LicenseSystemService> (a.bus);
        b->initialise();
        expect (b->getCurrentStatus() != S::Valid, "B starts locked");
        b->testTick();   // locked, no token: B starts polling for a token

        const auto g0 = LicenseSystemService::licenceGeneration();
        expectEquals (juce::String (a.signInWith (signer)), juce::String(), "A signs in");
        expect (LicenseSystemService::licenceGeneration() != g0, "generation bumped");
        expect (b->getCurrentStatus() != S::Valid, "B unchanged until its tick");
        b->testTick();
        expect (b->getCurrentStatus() == S::Valid, "B unlocked on its next tick");
        expect (b->isAudioAllowed(), "B audio allowed");

        const auto g1 = LicenseSystemService::licenceGeneration();
        b->testTick();
        expectEquals ((juce::int64) LicenseSystemService::licenceGeneration(), (juce::int64) g1, "B's reload does not bump (no ping-pong)");

        a.svc->deactivateLicense();
        b->testTick();
        expect (b->getCurrentStatus() != S::Valid && ! b->isAudioAllowed(), "sign-out in A locks B");
        b->testTick();   // locked, no token -> polling

        // Another PROCESS signs in (files appear, no in-process generation bump).
        {
            const auto nonce = LicenseSignature::makeNonce();
            const auto now = juce::Time::currentTimeMillis();
            const auto p = payloadFor (a.hw, now, "perpetual", "active", nonce);
            auto* data = new juce::DynamicObject();
            data->setProperty ("license_valid", true);
            data->setProperty ("license_payload", p);
            data->setProperty ("signature_v2", signer.sign (p));
            data->setProperty ("clock_mark", now);
            LicenseCache().storeLicense ("SAMPL4R-PRO-TEST", juce::var (data), a.hw.toStdString());
            SecureSessionManager::SessionData s;
            s.accessToken = "token-other-process";
            s.hardwareId = a.hw.toStdString();
            SecureSessionManager().storeSession (s);
        }
        b->testTick();
        expect (b->getCurrentStatus() == S::Valid, "token + licence from another process picked up on the next tick");
        b->shutdown();
    }
   #endif
};

static LicensingHardeningTests licensingHardeningTests;

} // namespace samplar
