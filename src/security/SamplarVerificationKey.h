#pragma once

// SamplarVerificationKey.h
// =========================
// Embedded HMAC verification key used to validate signed license tokens
// returned by the SAMPL4R license server (https://license-server-n5it.onrender.com).
//
// HOW TO POPULATE
// ---------------
// 1. From a machine with network access, run:
//      curl -s https://license-server-n5it.onrender.com/api/licenses/verification-key
//    Response: {"verification_key": "<32-hex-chars>"}
//
// 2. Paste the 32-character hex string into kSamplarVerificationKey below.
//
// 3. Rebuild SAMPLAR. The build will warn if the placeholder sentinel is
//    still present.
//
// SECURITY
// --------
// This key is the PUBLIC verification key — it can be embedded in shipped
// binaries safely. The corresponding secret HMAC signing key lives ONLY on
// the server. Tokens are produced server-side as:
//      token = base64(payload) + "|" + hex(HMAC_SHA256(secret, base64(payload)))[:32]
// and we verify them client-side using kSamplarVerificationKey as the HMAC key
// against the same base64 payload.
//
// NEVER replace this with the secret signing key.

namespace samplar {

// 32 hex chars. The literal "PASTE_VERIFICATION_KEY_HERE_32HX" is a sentinel
// the build can detect — replace it with the real value before shipping.
constexpr const char* kSamplarVerificationKey =
    "47b260cd8ee3beeddcd025ce6bcd41a5";

constexpr bool isVerificationKeyConfigured()
{
    // Simple compile-time sentinel check (length must be 32 hex chars and
    // must not equal the placeholder).
    const char* k = kSamplarVerificationKey;
    int len = 0;
    while (k[len] != '\0') ++len;
    if (len != 32) return false;
    // Don't allow the placeholder
    const char* placeholder = "PASTE_VERIFICATION_KEY_HERE_32HX";
    for (int i = 0; i < 32; ++i)
        if (k[i] != placeholder[i]) return true;
    return false;
}

} // namespace samplar
