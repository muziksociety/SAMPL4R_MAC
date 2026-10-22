// RSA-PSS v2 licence signature (audit D1/D2): a vector signed by the license
// server with the real SAMPL4R private key (public data only; docs/revival/
// impl_licensing.md section 4e) must verify against the key embedded in the
// plugin, and any edit / wrong machine / replay must not.
#include <juce_core/juce_core.h>

#include "security/LicenseSignature.h"
#include "security/LicenseSigningKey.h"

namespace samplar {

namespace {

const juce::String kPayload =
    "v2|SAMPL4R|user@example.com|PRO|ABCDEF0123456789ABCDEF0123456789|2026-10-21T03:43:28Z|perpetual|active|0123456789abcdef0123456789abcdef";
const juce::String kSig =
    "QLCloQOt1lNvkQiRagR5QH4T5T9Pwk/y3/q25gYJqkI+zQG8W7HTmUpCw+o6N33KOSqsuS4hS5xuasGqNyAwjqdj89GeKzPNgz4J8A3VuUIAYfP+U3asaProtIIky/irg/NvBtyEani6P9xZIXMLDuXHWtGjaB1EyIHNRRyrnBCPF/HK5Ag8nWIqLoXsVuHSenYzt2MjqsEVAt54Zt5h2c8EAklAynbUVoBaEPZIH8YYntEjHvG9myEir5mkJ/6ZUnGusNqSY80+hB0BwiRy9MMeFcgcvAoGkN/i3DHoHi5lyCK8H3lS15e2hXPuPoIwKSi6EgzvWrgNQZ01038RhA==";
const juce::String kMachine = "abcdef0123456789abcdef0123456789";   // matches the payload, other case
const juce::String kNonce   = "0123456789abcdef0123456789abcdef";

juce::Time issued() { return SignedLicense::parseTime ("2026-10-21T03:43:28Z"); }

} // namespace

class LicenseSignatureTests : public juce::UnitTest
{
public:
    LicenseSignatureTests() : juce::UnitTest ("LicenseSignature", "SAMPLAR Licensing") {}

    void runTest() override
    {
        const auto key = LicenseSignature::activePublicKey();
        const auto soon = issued() + juce::RelativeTime::hours (1);

        beginTest ("embedded key is the pinned SAMPL4R production key");
        expect (key.contains ("BEGIN PUBLIC KEY"));
        expectEquals (juce::String (LicenseSigningKey::kProductCode), juce::String ("SAMPL4R"));

        beginTest ("server vector verifies (positive)");
        expect (LicenseSignature::verify (kPayload, kSig, key));

        beginTest ("tampered payload / signature / key are rejected");
        expect (! LicenseSignature::verify (kPayload.replace ("|PRO|", "|EDU|"), kSig, key), "licence type edited");
        expect (! LicenseSignature::verify (kPayload.replace ("perpetual", "2099-01-01T00:00:00Z"), kSig, key), "expiry edited");
        expect (! LicenseSignature::verify (kPayload + " ", kSig, key), "trailing byte");
        {
            auto sig = kSig;
            const auto i = sig.length() / 2;
            sig = sig.substring (0, i) + (sig[i] == 'A' ? "B" : "A") + sig.substring (i + 1);
            expect (! LicenseSignature::verify (kPayload, sig, key), "signature bit flipped");
        }
        expect (! LicenseSignature::verify (kPayload, "not base64 !!", key), "garbage signature");
        expect (! LicenseSignature::verify (kPayload, {}, key), "empty signature");
        expect (! LicenseSignature::verify (kPayload, kSig, {}), "empty key");
        expect (! LicenseSignature::verify (kPayload, kSig, "-----BEGIN PUBLIC KEY-----\nAAAA\n-----END PUBLIC KEY-----"), "bogus key");

        beginTest ("payload parse");
        {
            SignedLicense s;
            expect (s.parse (kPayload));
            expectEquals (s.product, juce::String ("SAMPL4R"));
            expectEquals (s.email, juce::String ("user@example.com"));
            expectEquals (s.licenseType, juce::String ("PRO"));
            expectEquals (s.status, juce::String ("active"));
            expectEquals (s.nonce, kNonce);
            expectEquals (s.issued.toMilliseconds(), issued().toMilliseconds());
            expectEquals (s.expires.toMilliseconds(), (juce::int64) 0, "perpetual = 0");
            expect (! SignedLicense().parse ("v1|SAMPL4R|x"), "wrong version / field count");
            expect (! SignedLicense().parse ({}), "empty");
        }

        beginTest ("parseTime: fractions, zones, and no-zone rejection");
        {
            expectEquals (SignedLicense::parseTime ("2026-09-28T12:00:00.123456Z").toMilliseconds(),
                          SignedLicense::parseTime ("2026-09-28T12:00:00.123Z").toMilliseconds());
            expectEquals (SignedLicense::parseTime ("2026-09-28T14:00:00+02:00").toMilliseconds(),
                          SignedLicense::parseTime ("2026-09-28T12:00:00Z").toMilliseconds());
            expectEquals (SignedLicense::parseTime ("2026-09-28T12:00:00").toMilliseconds(), (juce::int64) 0);
        }

        beginTest ("verifyStored: this machine (v1 or v2 id, any case) accepted");
        {
            SignedLicense s;
            expect (LicenseSignature::verifyStored (kPayload, kSig, { kMachine }, soon, s));
            expect (LicenseSignature::verifyStored (kPayload, kSig, { "OTHER-V1-ID", kMachine.toUpperCase() }, soon, s),
                    "second id (v2) matches");
            // A stored copy is valid long after issue (offline window is enforced by the service).
            expect (LicenseSignature::verifyStored (kPayload, kSig, { kMachine }, issued() + juce::RelativeTime::days (400), s));
        }

        beginTest ("verifyStored: wrong machine / tampered / future-issued rejected");
        {
            SignedLicense s;
            expect (! LicenseSignature::verifyStored (kPayload, kSig, { "OTHERMACHINE" }, soon, s), "wrong machine");
            expect (! LicenseSignature::verifyStored (kPayload, kSig, {}, soon, s), "no machine id");
            expect (! LicenseSignature::verifyStored (kPayload, kSig, { "" }, soon, s), "empty machine id");
            expect (! LicenseSignature::verifyStored (kPayload.replace ("|PRO|", "|EDU|"), kSig, { kMachine }, soon, s), "tampered");
            expect (! LicenseSignature::verifyStored (kPayload, kSig, { kMachine }, issued() - juce::RelativeTime::days (2), s),
                    "issued > 1 day in the future (clock behind)");
            expect (! LicenseSignature::verifyStored ({}, kSig, { kMachine }, soon, s), "empty payload");
        }

        beginTest ("verifyLive: nonce echo + freshness");
        {
            SignedLicense s;
            expect (LicenseSignature::verifyLive (kPayload, kSig, kNonce, { kMachine }, soon, s));
            expect (! LicenseSignature::verifyLive (kPayload, kSig, "ffffffffffffffffffffffffffffffff", { kMachine }, soon, s),
                    "replayed reply (nonce mismatch)");
            expect (! LicenseSignature::verifyLive (kPayload, kSig, {}, { kMachine }, soon, s), "no nonce sent");
            // L-M3: freshness = the nonce, not this computer's clock.
            expect (LicenseSignature::verifyLive (kPayload, kSig, kNonce, { kMachine }, issued() + juce::RelativeTime::days (2), s),
                    "clock 2 days ahead: a reply to THIS request is still accepted");
            expect (LicenseSignature::verifyLive (kPayload, kSig, kNonce, { kMachine }, issued() - juce::RelativeTime::days (3), s),
                    "clock 3 days behind: still accepted");
            expect (LicenseSignature::isIssuedInFuture (s, issued() - juce::RelativeTime::days (3)), "clock-behind is detectable");
            expect (! LicenseSignature::isIssuedInFuture (s, issued() + juce::RelativeTime::hours (1)));
            expect (! LicenseSignature::verifyLive (kPayload, kSig, kNonce, { "OTHERMACHINE" }, soon, s), "wrong machine");
        }

        beginTest ("makeNonce: 32 hex chars, unique");
        {
            const auto a = LicenseSignature::makeNonce();
            const auto b = LicenseSignature::makeNonce();
            expectEquals (a.length(), 32);
            expect (a.containsOnly ("0123456789abcdef"));
            expect (a != b);
        }

        beginTest ("test key override (SAMPLAR_SELFTEST) switches the active key and back");
        {
            LicenseSignature::testKeyOverride() = "-----BEGIN PUBLIC KEY-----\nAAAA\n-----END PUBLIC KEY-----";
            SignedLicense s;
            expect (! LicenseSignature::verifyStored (kPayload, kSig, { kMachine }, soon, s), "other key: rejected");
            LicenseSignature::testKeyOverride() = {};
            expect (LicenseSignature::verifyStored (kPayload, kSig, { kMachine }, soon, s), "production key again");
        }
    }
};

static LicenseSignatureTests licenseSignatureTests;

} // namespace samplar
