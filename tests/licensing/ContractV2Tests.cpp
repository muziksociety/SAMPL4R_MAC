// SAMPL4R server contract (license_server docs/license_signing_v2.md, PR #132):
//  - every revalidate `reason` maps to the right client action
//      token_missing / token_invalid / token_too_old -> sign in again, licence KEPT
//      service_unavailable (503 + Retry-After 60)    -> licence kept, backoff >= Retry-After
//      revoked / expired / inactive / unbound / blocked -> only when SIGNED for
//        this machine + this nonce (RSA-PSS, SHA-256, salt 32) do they mute/clear
//  - offline window: keyed off the SIGNED expiry: "perpetual" -> 30 days for any
//    type (PRO, STD, EDU, Gold-earned ...); dated expiry -> 3 days; PROMO /
//    PROMO2 / TRL -> 3 days even when signed perpetual
//  - v2 machine ID (32 hex) required
// Signatures come from a TEST key generated per run (tests/licensing/TestSigner.h),
// never from the real SAMPL4R private key. No network (SAMPLAR_SELFTEST).
#include <juce_core/juce_core.h>

#include "app/EventBus.h"
#include "networking/LicenseServerClient.h"
#include "security/LicenseCache.h"
#include "security/LicenseSignature.h"
#include "services/LicenseSystemService.h"
#include "TestHelpers.h"
#include "TestSigner.h"

namespace samplar {

namespace {

constexpr juce::int64 kMin  = 60LL * 1000;
constexpr juce::int64 kHour = 60 * kMin;
constexpr juce::int64 kDay  = 24 * kHour;

juce::String isoAt (juce::int64 ms) { return juce::Time (ms).toISO8601 (true); }

juce::String v2Payload (const juce::String& hwid, juce::int64 issuedMs, const juce::String& expires,
                        const juce::String& status, const juce::String& nonce, const juce::String& type = "PRO",
                        const juce::String& product = "SAMPL4R")
{
    return "v2|" + product + "|user@example.com|" + type + "|" + hwid + "|" + isoAt (issuedMs) + "|" + expires
         + "|" + status + "|" + nonce;
}

} // namespace

class LicensingContractV2Tests : public juce::UnitTest
{
public:
    LicensingContractV2Tests() : juce::UnitTest ("Licensing contract v2", "SAMPLAR Licensing") {}

    void runTest() override
    {
        beginTest ("reason codes -> outcomes (incl. 503 + Retry-After)");
        testReasonMapping();

        beginTest ("Retry-After parsing and v2 machine id format");
        testHeaderAndIdHelpers();

       #if JUCE_WINDOWS
        test::TestSigner signer, otherKey;
        expect (signer.ok() && otherKey.ok(), "CNG test keys generated");
        if (! signer.ok() || ! otherKey.ok())
            return;
        LicenseSignature::testKeyOverride() = signer.pem();

        beginTest ("RSA-PSS: only salt length 32 verifies");
        testSaltLength (signer);

        beginTest ("signed denial: nonce / hwid / product / key / salt must all match");
        testDenialBinding (signer, otherKey);

        beginTest ("token_missing / token_invalid / token_too_old: licence kept, audio on, sign-in prompted");
        testTokenReasonsKeepLicence (signer);

        beginTest ("503 service_unavailable: licence kept, backoff honours Retry-After");
        testServiceUnavailableBackoff (signer);

        beginTest ("each signed denial status mutes; a denial for another machine / nonce / salt is ignored");
        testEachSignedDenial (signer);

        beginTest ("offline window: signed perpetual = 30 days (except PROMO/PROMO2/TRL), dated = 3 days");
        testOfflineWindows (signer);

        LicenseSignature::testKeyOverride() = {};
       #endif
    }

private:
    using O = LicenseServerClient::RevalOutcome;
    using S = LicenseSystemService::LicenseStatus;

    void testReasonMapping()
    {
        auto interp = [] (int code, const char* body, int retryAfter = 0)
        { return LicenseServerClient::interpretRevalidate (code, body, retryAfter); };

        for (const auto* reason : { "token_missing", "token_invalid", "token_too_old" })
        {
            const auto body = juce::String (R"({"success":false,"license_valid":false,"hardware_registered":false,"error":"x","reason":")")
                              + reason + "\"}";
            const auto r = interp (200, body.toRawUTF8());
            expect (r.outcome == O::AuthRequired, juce::String ("200 ") + reason + " -> AuthRequired");
            expectEquals (r.reason, juce::String (reason));
        }

        // The server's 503: {"success":false,...,"reason":"service_unavailable"} + Retry-After: 60.
        const auto r503 = interp (503, R"({"success":false,"license_valid":false,"hardware_registered":false,"error":"Service unavailable","reason":"service_unavailable"})", 60);
        expect (r503.outcome == O::NetworkError, "503 is transient, never a verdict");
        expectEquals (r503.reason, juce::String ("service_unavailable"));
        expectEquals (r503.retryAfterSeconds, 60, "Retry-After carried to the state machine");
        expectEquals (interp (503, "", 99999).retryAfterSeconds, LicenseServerClient::kMaxRetryAfterSeconds, "capped");

        // Denials: success true (revoked / expired / inactive / unbound) or false (blocked, unknown account).
        for (const auto* reason : { "revoked", "expired", "inactive", "unbound", "blocked" })
            for (const auto* success : { "true", "false" })
            {
                const auto body = juce::String (R"({"success":)") + success + R"(,"license_valid":false,"reason":")" + reason + "\"}";
                expect (interp (200, body.toRawUTF8()).outcome == O::Denied, juce::String (reason) + " success " + success + " -> Denied");
            }
        expect (interp (200, R"({"success":false,"license_valid":false,"reason":"bad_request"})").outcome == O::NetworkError,
                "bad_request (family only) is not a verdict");
        expect (interp (200, R"({"success":false,"license_valid":false,"reason":"brand_new_reason"})").outcome == O::NetworkError,
                "unknown reason is transient");
    }

    void testHeaderAndIdHelpers()
    {
        expectEquals (LicenseServerClient::parseRetryAfter ("60"), 60);
        expectEquals (LicenseServerClient::parseRetryAfter (" 120 "), 120);
        expectEquals (LicenseServerClient::parseRetryAfter (""), 0);
        expectEquals (LicenseServerClient::parseRetryAfter ("Wed, 21 Oct 2026 07:28:00 GMT"), 0, "HTTP-date: normal backoff");
        expectEquals (LicenseServerClient::parseRetryAfter ("-5"), 0);
        expectEquals (LicenseServerClient::parseRetryAfter ("12abc"), 0);
        expectEquals (LicenseServerClient::parseRetryAfter ("99999"), LicenseServerClient::kMaxRetryAfterSeconds);

        expect (LicenseServerClient::isValidMachineIdV2 ("ABCDEF0123456789ABCDEF0123456789"));
        expect (LicenseServerClient::isValidMachineIdV2 ("abcdef0123456789abcdef0123456789"));
        expect (! LicenseServerClient::isValidMachineIdV2 (""), "missing");
        expect (! LicenseServerClient::isValidMachineIdV2 (juce::String::repeatedString ("ab", 32)), "64-hex v1 id");
        expect (! LicenseServerClient::isValidMachineIdV2 ("ABCDEF0123456789ABCDEF012345678G"), "non-hex");
    }

   #if JUCE_WINDOWS
    /** Fresh service in a sandbox; optionally signed in with a signed licence. */
    struct Fixture
    {
        test::ScopedLicenseSandbox sandbox;
        EventBus bus;
        std::unique_ptr<LicenseSystemService> svc;
        juce::String hw;

        Fixture()
        {
            svc = std::make_unique<LicenseSystemService> (bus);
            svc->initialise();
            hw = svc->generateHardwareId();
        }
        ~Fixture() { svc->shutdown(); svc.reset(); }

        std::string signIn (const test::TestSigner& signer, const juce::String& type, const juce::String& expires)
        {
            OAuthBrowserAuthenticator::AuthResult a;
            a.success = true;
            a.licenseKey = "SAMPL4R-" + type.toStdString() + "-TEST";
            a.email = "user@example.com";
            a.licenseType = type.toStdString();
            a.accessToken = "token-1";
            a.requestNonce = LicenseSignature::makeNonce().toStdString();
            const auto p = v2Payload (hw, juce::Time::currentTimeMillis(), expires, "active", a.requestNonce, type);
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

        LicenseServerClient::RevalResult reply (O outcome, const juce::String& reason = {}, int retryAfter = 0) const
        {
            LicenseServerClient::RevalResult r;
            r.outcome = outcome;
            r.reason = reason;
            r.retryAfterSeconds = retryAfter;
            r.requestNonce = LicenseSignature::makeNonce();
            return r;
        }

        LicenseServerClient::RevalResult denial (const test::TestSigner& signer, const juce::String& status,
                                                 const juce::String& forHw = {}, ULONG salt = 32) const
        {
            auto r = reply (O::Denied, status);
            r.denialPayload = v2Payload (forHw.isEmpty() ? hw : forHw, juce::Time::currentTimeMillis(),
                                         "perpetual", status, r.requestNonce, "NONE");
            r.denialSignature = signer.sign (r.denialPayload, salt);
            return r;
        }

        LicenseServerClient::RevalResult valid (const test::TestSigner& signer) const
        {
            auto r = reply (O::Valid);
            r.licensePayload = v2Payload (hw, juce::Time::currentTimeMillis(), "perpetual", "active", r.requestNonce);
            r.signature = signer.sign (r.licensePayload);
            r.accessToken = "token-rolled";
            return r;
        }
    };

    void testSaltLength (const test::TestSigner& signer)
    {
        const auto p = v2Payload ("ABCDEF0123456789ABCDEF0123456789", juce::Time::currentTimeMillis(), "perpetual",
                                  "active", "0123456789abcdef0123456789abcdef");
        expect (LicenseSignature::verify (p, signer.sign (p, 32), signer.pem()), "salt 32 (contract) verifies");
        for (ULONG salt : { 0UL, 20UL, 31UL, 33UL, 64UL, 222UL })   // 222 = MAX_LENGTH for RSA-2048 / SHA-256
            expect (! LicenseSignature::verify (p, signer.sign (p, salt), signer.pem()),
                    "salt " + juce::String ((int) salt) + " rejected");
    }

    void testDenialBinding (const test::TestSigner& signer, const test::TestSigner& otherKey)
    {
        const juce::String hw = "ABCDEF0123456789ABCDEF0123456789";
        const juce::String nonce = "0123456789abcdef0123456789abcdef";
        const auto now = juce::Time::currentTimeMillis();
        SignedLicense s;
        // Contract shape: v2|SAMPL4R|email|type-or-NONE|<this hwid>|issued|expiry-or-perpetual|status|nonce
        const auto deny = v2Payload (hw, now, "perpetual", "unbound", nonce, "NONE");
        expect (LicenseSignature::verifyDenial (deny, signer.sign (deny), nonce, { hw }, juce::Time (now), s), "valid denial");
        expectEquals (s.status, juce::String ("unbound"));
        expectEquals (s.licenseType, juce::String ("NONE"));

        const auto expiredDeny = v2Payload (hw, now, isoAt (now - kDay), "expired", nonce, "TRL");
        expect (LicenseSignature::verifyDenial (expiredDeny, signer.sign (expiredDeny), nonce, { hw }, juce::Time (now), s),
                "expired denial carrying its real (past) expiry");

        expect (! LicenseSignature::verifyDenial (deny, signer.sign (deny), "ffffffffffffffffffffffffffffffff", { hw }, juce::Time (now), s),
                "nonce mismatch");
        expect (! LicenseSignature::verifyDenial (deny, signer.sign (deny), nonce, { "11111111111111111111111111111111" }, juce::Time (now), s),
                "hwid mismatch (another machine)");
        expect (! LicenseSignature::verifyDenial (deny, signer.sign (deny, 20), nonce, { hw }, juce::Time (now), s), "salt 20");
        expect (! LicenseSignature::verifyDenial (deny, otherKey.sign (deny), nonce, { hw }, juce::Time (now), s),
                "signed with a different key (e.g. another product's)");
        const auto sm5 = v2Payload (hw, now, "perpetual", "revoked", nonce, "PRO", "SM5");
        expect (! LicenseSignature::verifyDenial (sm5, signer.sign (sm5), nonce, { hw }, juce::Time (now), s), "product SM5");
    }

    void testTokenReasonsKeepLicence (const test::TestSigner& signer)
    {
        for (const auto* reason : { "token_missing", "token_invalid", "token_too_old" })
        {
            Fixture f;
            expectEquals (juce::String (f.signIn (signer, "PRO", "perpetual")), juce::String(), "signed in");
            StatusSpy spy (f.bus);
            f.svc->testApplyRevalidation (f.reply (O::AuthRequired, reason), f.svc->testEpoch());

            const juce::String why (reason);
            expect (f.svc->getCurrentStatus() == S::Valid, why + ": still Valid");
            expect (f.svc->isAudioAllowed(), why + ": audio still allowed (inside the offline window)");
            expect (f.cachedPayload().isNotEmpty(), why + ": signed licence NOT cleared");
            expect (! f.svc->getAccessToken().empty(), why + ": token file kept (a sign-in replaces it)");
            expect (f.svc->isSignInRequired(), why + ": sign-in prompted");
            expect ((bool) spy.last.getProperty ("signInRequired", false), why + ": license.status carries signInRequired");
            expect (juce::String (f.svc->getLastResultMessage()).containsIgnoreCase ("sign in"), why + ": message asks to sign in");

            // Still inside the window after a day; locked only when the window runs out.
            f.svc->testSetClockOffsetMs (kDay);
            f.svc->testTick();
            expect (f.svc->getCurrentStatus() == S::Valid && f.svc->isAudioAllowed(), why + ": +1 day still plays");
            f.svc->testSetClockOffsetMs (31 * kDay);
            f.svc->testTick();
            expect (! f.svc->isAudioAllowed(), why + ": muted once the 30-day window is over");
            expect (f.cachedPayload().isNotEmpty(), why + ": even then the licence stays on file");

            // A fresh sign-in clears the prompt.
            f.svc->testSetClockOffsetMs (0);
            expectEquals (juce::String (f.signIn (signer, "PRO", "perpetual")), juce::String(), "sign-in again");
            expect (! f.svc->isSignInRequired(), why + ": sign-in clears the prompt");
        }

        // A valid re-check (another instance signed in) also clears it.
        Fixture f;
        f.signIn (signer, "PRO", "perpetual");
        f.svc->testApplyRevalidation (f.reply (O::AuthRequired, "token_too_old"), f.svc->testEpoch());
        expect (f.svc->isSignInRequired());
        f.svc->testApplyRevalidation (f.valid (signer), f.svc->testEpoch());
        expect (! f.svc->isSignInRequired(), "valid re-check clears the prompt");
        expect (f.svc->getCurrentStatus() == S::Valid);
    }

    void testServiceUnavailableBackoff (const test::TestSigner& signer)
    {
        // Valid licence: kept, re-check no sooner than Retry-After.
        {
            Fixture f;
            f.signIn (signer, "PRO", "perpetual");
            f.svc->testApplyRevalidation (f.reply (O::NetworkError, "service_unavailable", 60), f.svc->testEpoch());
            expect (f.svc->getCurrentStatus() == S::Valid && f.svc->isAudioAllowed(), "503: licence kept, audio on");
            expect (f.cachedPayload().isNotEmpty(), "503: licence not cleared");
            expect (! f.svc->isSignInRequired(), "503 is not an auth problem");
            const auto d = f.svc->testRetryDelayMs();
            expect (d >= 59 * 1000 && d <= 60 * 1000, "503: retry after ~60 s (" + juce::String (d) + " ms)");
        }
        // Retry-After longer than the backoff wins; a shorter one never shortens it.
        {
            Fixture f;   // locked (no licence): backoff 1, 2, 4 ... min
            f.svc->testApplyRevalidation (f.reply (O::NetworkError, "service_unavailable", 300), f.svc->testEpoch());
            auto d = f.svc->testRetryDelayMs();
            expect (d >= 299 * 1000 && d <= 300 * 1000, "Retry-After 300 > backoff 60: 300 s (" + juce::String (d) + ")");
            for (int i = 0; i < 4; ++i)
                f.svc->testApplyRevalidation (f.reply (O::NetworkError, "service_unavailable", 60), f.svc->testEpoch());
            d = f.svc->testRetryDelayMs();
            expect (d >= 15 * kMin, "Retry-After 60 never shortens the 16-min backoff (" + juce::String (d) + ")");
            expect (f.svc->getCurrentStatus() != S::Valid, "still locked, never unlocked by a 503");
        }
    }

    void testEachSignedDenial (const test::TestSigner& signer)
    {
        for (const auto* status : { "revoked", "expired", "inactive", "unbound", "blocked" })
        {
            const juce::String why (status);
            Fixture f;
            f.signIn (signer, "PRO", "perpetual");
            const auto e = f.svc->testEpoch();

            // Bound to someone else's machine, another request, or a wrong salt: ignored.
            f.svc->testApplyRevalidation (f.denial (signer, status, "11111111111111111111111111111111"), e);
            expect (f.svc->isAudioAllowed() && f.cachedPayload().isNotEmpty(), why + ": denial for another hwid ignored");
            auto replay = f.denial (signer, status);
            replay.requestNonce = LicenseSignature::makeNonce();
            f.svc->testApplyRevalidation (replay, e);
            expect (f.svc->isAudioAllowed() && f.cachedPayload().isNotEmpty(), why + ": nonce mismatch ignored");
            f.svc->testApplyRevalidation (f.denial (signer, status, {}, 20), e);
            expect (f.svc->isAudioAllowed() && f.cachedPayload().isNotEmpty(), why + ": salt-20 signature ignored");
            auto unsignedDenial = f.reply (O::Denied, status);
            f.svc->testApplyRevalidation (unsignedDenial, e);
            expect (f.svc->isAudioAllowed() && f.cachedPayload().isNotEmpty(), why + ": unsigned denial ignored");

            // The real thing: muted at once; confirmed a minute later -> licence cleared.
            f.svc->testApplyRevalidation (f.denial (signer, status), e);
            expect (! f.svc->isAudioAllowed(), why + ": signed denial mutes");
            f.svc->testSetClockOffsetMs (2 * kMin);
            f.svc->testApplyRevalidation (f.denial (signer, status), e);
            expect (! f.svc->isAudioAllowed(), why + ": still muted");
            expect (f.cachedPayload().isEmpty(), why + ": confirmed signed denial clears the licence");
            expect (why != "expired" || f.svc->getCurrentStatus() == S::Expired, "expired -> Expired status");
        }
    }

    void testOfflineWindows (const test::TestSigner& signer)
    {
        const auto inAYear = isoAt (juce::Time::currentTimeMillis() + 365 * kDay);
        struct Case { const char* type; juce::String expires; bool longWindow; };
        const Case cases[] = {
            { "PRO",    "perpetual", true  },
            { "STD",    "perpetual", true  },   // window keys off the signed expiry, not the type
            { "EDU",    "perpetual", true  },
            { "GOLD",   "perpetual", true  },   // Gold-earned ownership
            { "PRO",    "9999-12-31T23:59:59Z", true },   // server's perpetual sentinel
            { "PRO",    inAYear,     false },   // dated: subscription / Gold paid-through date
            { "STD",    inAYear,     false },
            { "EDU",    inAYear,     false },
            { "PROMO",  "perpetual", false },   // PROMO / PROMO2 / TRL: always 3 days
            { "PROMO2", "perpetual", false },
            { "TRL",    "perpetual", false },
            { "PROMO2", inAYear,     false },
            { "TRL",    inAYear,     false },
        };

        for (const auto& c : cases)
        {
            const auto why = juce::String (c.type) + " / " + (c.expires == "perpetual" ? juce::String ("perpetual") : c.expires);
            Fixture f;
            expectEquals (juce::String (f.signIn (signer, c.type, c.expires)), juce::String(), why + ": signed in");

            const auto graceH = f.svc->getRemainingGraceTime().count() / 60;
            const auto expectedH = c.longWindow ? LicenseSystemService::kLongOfflineHours : LicenseSystemService::kShortOfflineHours;
            expect (graceH >= expectedH - 1 && graceH <= expectedH, why + ": window " + juce::String ((int) graceH) + " h");

            f.svc->testSetClockOffsetMs (2 * kDay + 23 * kHour);   // just inside 3 days
            f.svc->testTick();
            expect (f.svc->isAudioAllowed(), why + ": plays offline at 2 d 23 h");

            f.svc->testSetClockOffsetMs (3 * kDay + kHour);       // just past 3 days
            f.svc->testTick();
            expect (f.svc->isAudioAllowed() == c.longWindow, why + (c.longWindow ? ": signed perpetual still plays at 3 d 1 h"
                                                                                 : ": locked at 3 d 1 h"));
            if (! c.longWindow)
                expect (juce::String (f.svc->getLastResultMessage()).contains ("3 days"), why + ": 3-day message");

            if (c.longWindow)
            {
                f.svc->testSetClockOffsetMs (29 * kDay);
                f.svc->testTick();
                expect (f.svc->isAudioAllowed(), why + ": plays at 29 days");
                f.svc->testSetClockOffsetMs (30 * kDay + kHour);
                f.svc->testTick();
                expect (! f.svc->isAudioAllowed(), why + ": locked after 30 days");
                expect (juce::String (f.svc->getLastResultMessage()).contains ("30 days"), why + ": 30-day message");
            }
            expect (f.cachedPayload().isNotEmpty(), why + ": offline lock never clears the licence");
        }
    }

    struct StatusSpy
    {
        explicit StatusSpy (EventBus& b) : bus (b)
        {
            id = bus.subscribe ("license.status", [this] (const juce::var& p) { last = p; });
        }
        ~StatusSpy() { bus.unsubscribe ("license.status", id); }
        EventBus& bus;
        EventBus::HandlerId id {};
        juce::var last;
    };
   #endif
};

static LicensingContractV2Tests licensingContractV2Tests;

} // namespace samplar
