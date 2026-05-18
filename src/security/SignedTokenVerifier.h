#pragma once

#include <chrono>
#include <optional>
#include <string>

namespace samplar {

// SignedTokenVerifier
// ===================
// Offline HMAC-SHA256 verification of license tokens returned by the
// SAMPL4R license server's POST /api/licenses/validate-token endpoint.
//
// Token format:
//   token = <base64_payload> + "|" + <hex_signature_32>
//   payload (after base64 decode) = "email|hardware_id|expires_at|product_id|timestamp"
//   expected_signature = hex(HMAC_SHA256(kSamplarVerificationKey, base64_payload))[:32]
//
// All methods are pure functions — no I/O, no globals. Safe to call from
// any thread that is not the audio thread.

struct VerifiedToken {
    std::string email;
    std::string hardwareId;
    std::string productId;
    std::chrono::system_clock::time_point expiresAt{};   // 0 if no expiry
    std::chrono::system_clock::time_point issuedAt{};    // server timestamp
};

class SignedTokenVerifier {
public:
    // Verify a token against the embedded verification key. Returns the
    // decoded payload on success, std::nullopt on any verification failure
    // (bad format, signature mismatch, malformed payload).
    static std::optional<VerifiedToken> verify(const std::string& token,
                                               const std::string& expectedHardwareId,
                                               const std::string& expectedProductId);

    // Lower-level helper — HMAC-SHA256(key, message), returns full hex digest.
    static std::string hmacSha256Hex(const std::string& key,
                                     const std::string& message);
};

} // namespace samplar
