#include "SignedTokenVerifier.h"
#include "SamplarVerificationKey.h"

#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>

#include <array>
#include <cstdint>
#include <vector>

namespace samplar {

namespace {

constexpr int kSha256BlockSize = 64; // bytes
constexpr int kSha256OutputSize = 32; // bytes
constexpr int kSignatureHexLength = 32; // first 32 hex chars of HMAC digest

std::vector<uint8_t> sha256Raw(const uint8_t* data, size_t len)
{
    juce::MemoryBlock block(data, len);
    juce::SHA256 digest(block);
    auto raw = digest.getRawData();
    std::vector<uint8_t> out(raw.getSize());
    std::memcpy(out.data(), raw.getData(), raw.getSize());
    return out;
}

std::string toHex(const std::vector<uint8_t>& bytes)
{
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (auto b : bytes) {
        out.push_back(hex[(b >> 4) & 0xF]);
        out.push_back(hex[b & 0xF]);
    }
    return out;
}

// HMAC-SHA256 implementation per RFC 2104.
// JUCE provides SHA256 but not HMAC, so we build it manually:
//   HMAC(K, m) = SHA256( (K' XOR opad) || SHA256( (K' XOR ipad) || m ) )
//   K' = K padded/hashed to block size (64 bytes for SHA256)
std::vector<uint8_t> hmacSha256(const std::string& key, const std::string& message)
{
    std::array<uint8_t, kSha256BlockSize> kPrime{};
    kPrime.fill(0);

    if (key.size() > kSha256BlockSize) {
        const auto hashed = sha256Raw(reinterpret_cast<const uint8_t*>(key.data()), key.size());
        std::memcpy(kPrime.data(), hashed.data(), std::min<size_t>(hashed.size(), kSha256BlockSize));
    } else {
        std::memcpy(kPrime.data(), key.data(), key.size());
    }

    std::array<uint8_t, kSha256BlockSize> innerPad{};
    std::array<uint8_t, kSha256BlockSize> outerPad{};
    for (int i = 0; i < kSha256BlockSize; ++i) {
        innerPad[i] = static_cast<uint8_t>(kPrime[i] ^ 0x36);
        outerPad[i] = static_cast<uint8_t>(kPrime[i] ^ 0x5c);
    }

    std::vector<uint8_t> innerInput;
    innerInput.reserve(kSha256BlockSize + message.size());
    innerInput.insert(innerInput.end(), innerPad.begin(), innerPad.end());
    innerInput.insert(innerInput.end(),
                      reinterpret_cast<const uint8_t*>(message.data()),
                      reinterpret_cast<const uint8_t*>(message.data()) + message.size());

    const auto innerHash = sha256Raw(innerInput.data(), innerInput.size());

    std::vector<uint8_t> outerInput;
    outerInput.reserve(kSha256BlockSize + innerHash.size());
    outerInput.insert(outerInput.end(), outerPad.begin(), outerPad.end());
    outerInput.insert(outerInput.end(), innerHash.begin(), innerHash.end());

    return sha256Raw(outerInput.data(), outerInput.size());
}

// Constant-time compare for two equal-length strings — avoids timing oracles.
bool constantTimeEquals(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return diff == 0;
}

} // namespace

std::string SignedTokenVerifier::hmacSha256Hex(const std::string& key, const std::string& message)
{
    return toHex(hmacSha256(key, message));
}

std::optional<VerifiedToken> SignedTokenVerifier::verify(const std::string& token,
                                                         const std::string& expectedHardwareId,
                                                         const std::string& expectedProductId)
{
    juce::Logger::writeToLog("[SignedTokenVerifier] verify START");

    if (!isVerificationKeyConfigured()) {
        juce::Logger::writeToLog("[SignedTokenVerifier] ERROR: verification key not configured "
                                 "(placeholder still present in SamplarVerificationKey.h)");
        return std::nullopt;
    }

    if (token.empty()) {
        juce::Logger::writeToLog("[SignedTokenVerifier] empty token");
        return std::nullopt;
    }

    // Split on '|' (last occurrence — payload itself doesn't contain '|' since
    // it's base64).
    const auto pipePos = token.rfind('|');
    if (pipePos == std::string::npos || pipePos == 0 || pipePos == token.size() - 1) {
        juce::Logger::writeToLog("[SignedTokenVerifier] malformed token: no separator");
        return std::nullopt;
    }

    const std::string base64Payload = token.substr(0, pipePos);
    const std::string signature = token.substr(pipePos + 1);

    if (signature.size() != static_cast<size_t>(kSignatureHexLength)) {
        juce::Logger::writeToLog("[SignedTokenVerifier] bad signature length: "
                                 + juce::String((int)signature.size()));
        return std::nullopt;
    }

    // Compute expected signature: HMAC_SHA256(verification_key, base64Payload), first 32 hex chars.
    const auto fullHex = hmacSha256Hex(kSamplarVerificationKey, base64Payload);
    const std::string expectedSig = fullHex.substr(0, kSignatureHexLength);

    if (!constantTimeEquals(signature, expectedSig)) {
        juce::Logger::writeToLog("[SignedTokenVerifier] signature mismatch");
        return std::nullopt;
    }

    // Decode base64 payload
    juce::MemoryOutputStream decoded;
    if (!juce::Base64::convertFromBase64(decoded, juce::String(base64Payload))) {
        juce::Logger::writeToLog("[SignedTokenVerifier] base64 decode failed");
        return std::nullopt;
    }
    const std::string payload(static_cast<const char*>(decoded.getData()), decoded.getDataSize());

    // payload = "email|hardware_id|expires_at|product_id|timestamp"
    juce::StringArray parts;
    parts.addTokens(juce::String(payload), "|", "");
    if (parts.size() < 5) {
        juce::Logger::writeToLog("[SignedTokenVerifier] payload has fewer than 5 fields: "
                                 + juce::String(parts.size()));
        return std::nullopt;
    }

    VerifiedToken out;
    out.email      = parts[0].toStdString();
    out.hardwareId = parts[1].toStdString();
    const auto expiresStr = parts[2];
    out.productId  = parts[3].toStdString();
    const auto timestampStr = parts[4];

    // Bind to expected hardware and product (defence-in-depth — server already binds these)
    if (!expectedHardwareId.empty() && out.hardwareId != expectedHardwareId) {
        juce::Logger::writeToLog("[SignedTokenVerifier] hardware ID mismatch");
        return std::nullopt;
    }
    if (!expectedProductId.empty() && out.productId != expectedProductId) {
        juce::Logger::writeToLog("[SignedTokenVerifier] product ID mismatch (got "
                                 + juce::String(out.productId) + ", expected "
                                 + juce::String(expectedProductId) + ")");
        return std::nullopt;
    }

    // Parse expiresAt: accept ISO-8601 or "0" / "00000000" (no expiry / first-use trial)
    if (expiresStr.isNotEmpty() && expiresStr != "0" && expiresStr != "00000000") {
        auto t = juce::Time::fromISO8601(expiresStr);
        if (t == juce::Time()) {
            // Try Unix timestamp (seconds)
            const auto asInt = expiresStr.getLargeIntValue();
            if (asInt > 0) {
                out.expiresAt = std::chrono::system_clock::time_point(std::chrono::seconds(asInt));
            }
        } else {
            out.expiresAt = std::chrono::system_clock::time_point(std::chrono::milliseconds(t.toMilliseconds()));
        }
    }

    // Issued-at
    const auto tsInt = timestampStr.getLargeIntValue();
    if (tsInt > 0) {
        out.issuedAt = std::chrono::system_clock::time_point(std::chrono::seconds(tsInt));
    }

    juce::Logger::writeToLog("[SignedTokenVerifier] token verified OK for email="
                             + juce::String(out.email));
    return out;
}

} // namespace samplar
