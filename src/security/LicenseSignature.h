#pragma once

#include <juce_core/juce_core.h>

// Server-signed license data, payload v2. Contract: license_server
// docs/license_signing_v2.md. Ported from SM5 (src/security/LicenseSignature.*,
// itself from RUIDO fa1aa12).
//
//   v2|<product>|<email>|<license_type>|<hardware_id>|<issued_at>|<expires_at>|<status>|<nonce>
//
// Signed by the server's PRIVATE per-product key (RSA-PSS, SHA-256, MGF1-SHA-256,
// salt 32). SAMPLAR ships only the PUBLIC key (LicenseSigningKey.h), so license
// data edited by anyone else fails to verify (replaces the old symmetric HMAC
// token, whose embedded key could mint tokens).
//   product - must be SAMPL4R (a sibling product's license can't unlock SAMPLAR)
//   status  - must be "active" (validity is signed, not a loose JSON flag)
//   nonce   - echoes the client's per-request random value (no replay of old
//             replies); cached copies accept any nonce
namespace samplar {

struct SignedLicense
{
    juce::String product, email, licenseType, hardwareId, issuedAt, expiresAt, status, nonce;
    juce::Time   issued, expires;   // expires 0 = perpetual

    /** "2026-09-28T12:00:00.123456Z" -> "...:00.123Z" (JUCE reads at most 3
        fraction digits). Requires a 'Z' or +/-hh:mm zone; otherwise returns 0. */
    static juce::Time parseTime (juce::String t);

    /** Split a v2 payload into its fields. False if malformed. */
    bool parse (const juce::String& payload);
};

namespace LicenseSignature
{
    /** RSA-PSS/SHA-256 (salt 32) verify of the UTF-8 payload.
        `publicKeyPem` = SubjectPublicKeyInfo PEM. */
    bool verify (const juce::String& payload, const juce::String& signatureBase64,
                 const juce::String& publicKeyPem);

   #if SAMPLAR_SELFTEST
    /** TESTS ONLY (never compiled into the app/plugin). */
    juce::String& testKeyOverride();
   #endif

    /** The key license data is verified with (built-in production key). */
    juce::String activePublicKey();

    /** Stored/cached license data: signed with activePublicKey(), product SAMPL4R,
        status active, bound to one of `thisMachineIds` (case-insensitive), not
        issued more than a day in the future. Any nonce. */
    bool verifyStored (const juce::String& payload, const juce::String& signatureBase64,
                       const juce::StringArray& thisMachineIds, juce::Time now, SignedLicense& out);

    /** True if `s` was issued more than a day after `now` (this clock is behind). */
    bool isIssuedInFuture (const SignedLicense& s, juce::Time now);

    /** A LIVE reply (sign-in / re-check): signature, product, status and machine
        as verifyStored(), echoes `sentNonce` (that is the freshness proof - the
        local clock is NOT used, review L-M3), and not expired at its signed
        issue time. */
    bool verifyLive (const juce::String& payload, const juce::String& signatureBase64,
                     const juce::String& sentNonce, const juce::StringArray& thisMachineIds,
                     juce::Time now, SignedLicense& out);

    /** A SIGNED DENIAL from /auth/revalidate (fields denial_payload /
        denial_signature): signed with activePublicKey(), product SAMPL4R, status
        one of revoked / expired / inactive / unbound / blocked, bound to one of
        `thisMachineIds`, echoes `sentNonce`, issued within a day of `now`. Only
        a denial that passes this may wipe the cached licence. */
    bool verifyDenial (const juce::String& payload, const juce::String& signatureBase64,
                       const juce::String& sentNonce, const juce::StringArray& thisMachineIds,
                       juce::Time now, SignedLicense& out);

    /** Unpredictable per-request value (anti-replay): 32 hex chars. The server
        accepts [A-Za-z0-9_-]{16,128}. */
    juce::String makeNonce();
}

} // namespace samplar
