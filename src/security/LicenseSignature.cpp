#include "LicenseSignature.h"
#include "LicenseSigningKey.h"

#include <juce_cryptography/juce_cryptography.h>
#include <vector>

#if JUCE_WINDOWS
 #include <windows.h>
 #include <wincrypt.h>
 #include <bcrypt.h>
 #pragma comment(lib, "crypt32.lib")
 #pragma comment(lib, "bcrypt.lib")
#elif JUCE_MAC
 #include <Security/Security.h>
#endif

namespace samplar {

// ---- SignedLicense ----------------------------------------------------------

juce::Time SignedLicense::parseTime (juce::String t)
{
    const int dot = t.indexOfChar ('.');
    if (dot > 0)
    {
        int end = dot + 1;
        while (end < t.length() && juce::CharacterFunctions::isDigit (t[end])) ++end;
        t = t.substring (0, juce::jmin (end, dot + 4)) + t.substring (end);
    }
    const bool zoned = t.endsWithChar ('Z') || t.fromLastOccurrenceOf ("T", false, false).containsAnyOf ("+-");
    return zoned ? juce::Time::fromISO8601 (t) : juce::Time();
}

bool SignedLicense::parse (const juce::String& payload)
{
    juce::StringArray f;
    f.addTokens (payload, "|", "");
    if (f.size() != 9 || f[0] != "v2")
        return false;
    product = f[1]; email = f[2]; licenseType = f[3]; hardwareId = f[4];
    issuedAt = f[5]; expiresAt = f[6]; status = f[7]; nonce = f[8];
    if (product.isEmpty() || email.isEmpty() || licenseType.isEmpty() || hardwareId.isEmpty() || status.isEmpty())
        return false;
    issued = parseTime (issuedAt);
    if (issued.toMilliseconds() == 0)
        return false;
    // The server signs a perpetual licence either as "perpetual" or with its
    // year-9999 sentinel. Both mean "never expires": a 9999 date must not count
    // as a dated licence (3-day window) or reach std::localtime (NULL after year
    // 3000 on Windows).
    const bool perpetual = expiresAt.equalsIgnoreCase ("perpetual") || expiresAt.substring (0, 4).getIntValue() >= 9999;
    expires = perpetual ? juce::Time() : parseTime (expiresAt);
    return perpetual || expires.toMilliseconds() != 0;
}

namespace LicenseSignature {

// ---- OS verifiers -----------------------------------------------------------

#if JUCE_WINDOWS
static bool verifyImpl (const juce::String& payload, const juce::MemoryBlock& sig, const juce::String& pem)
{
    // PEM -> DER (SubjectPublicKeyInfo) -> CNG key handle
    const std::string pemStr = pem.toStdString();
    DWORD derLen = 0;
    if (! CryptStringToBinaryA (pemStr.c_str(), 0, CRYPT_STRING_BASE64HEADER, nullptr, &derLen, nullptr, nullptr))
        return false;
    std::vector<BYTE> der (derLen);
    if (! CryptStringToBinaryA (pemStr.c_str(), 0, CRYPT_STRING_BASE64HEADER, der.data(), &derLen, nullptr, nullptr))
        return false;

    CERT_PUBLIC_KEY_INFO* info = nullptr;
    DWORD infoLen = 0;
    if (! CryptDecodeObjectEx (X509_ASN_ENCODING, X509_PUBLIC_KEY_INFO, der.data(), derLen,
                               CRYPT_DECODE_ALLOC_FLAG, nullptr, &info, &infoLen))
        return false;
    BCRYPT_KEY_HANDLE key = nullptr;
    const BOOL imported = CryptImportPublicKeyInfoEx2 (X509_ASN_ENCODING, info, 0, nullptr, &key);
    LocalFree (info);
    if (! imported || key == nullptr)
        return false;

    juce::SHA256 sha (payload.toRawUTF8(), payload.getNumBytesAsUTF8());
    auto digest = sha.getRawData();

    // Salt 32 only (= hash length): the same rule as macOS's verifier, so a
    // signature accepted on one platform is accepted on every platform.
    BCRYPT_PSS_PADDING_INFO pad { BCRYPT_SHA256_ALGORITHM, 32 };
    const bool ok = BCryptVerifySignature (key, &pad, (PUCHAR) digest.getData(), (ULONG) digest.getSize(),
                                           (PUCHAR) sig.getData(), (ULONG) sig.getSize(), BCRYPT_PAD_PSS) == 0;
    BCryptDestroyKey (key);
    return ok;
}

#elif JUCE_MAC
// Minimal DER walk: SubjectPublicKeyInfo -> the RSAPublicKey (PKCS#1) inside the BIT STRING.
static bool readLen (const juce::uint8* d, size_t n, size_t& i, size_t& len)
{
    if (i >= n) return false;
    len = d[i++];
    if (len & 0x80)
    {
        const size_t count = len & 0x7f;
        if (count == 0 || count > 4 || i + count > n) return false;
        len = 0;
        for (size_t k = 0; k < count; ++k) len = (len << 8) | d[i++];
    }
    return i + len <= n;
}

static bool spkiToPkcs1 (const juce::MemoryBlock& spki, juce::MemoryBlock& out)
{
    auto* d = (const juce::uint8*) spki.getData();
    const size_t n = spki.getSize();
    size_t i = 0, len = 0;
    if (i >= n || d[i++] != 0x30 || ! readLen (d, n, i, len)) return false;   // SPKI SEQUENCE
    if (i >= n || d[i++] != 0x30 || ! readLen (d, n, i, len)) return false;   // AlgorithmIdentifier
    i += len;
    if (i >= n || d[i++] != 0x03 || ! readLen (d, n, i, len)) return false;   // BIT STRING
    if (len < 1 || d[i] != 0x00) return false;                                // no unused bits
    out = juce::MemoryBlock (d + i + 1, len - 1);
    return true;
}

static bool verifyImpl (const juce::String& payload, const juce::MemoryBlock& sig, const juce::String& pem)
{
    const auto b64 = pem.fromFirstOccurrenceOf ("-----BEGIN PUBLIC KEY-----", false, false)
                        .upToFirstOccurrenceOf ("-----END PUBLIC KEY-----", false, false)
                        .removeCharacters (" \r\n\t");
    juce::MemoryOutputStream spkiStream;
    if (! juce::Base64::convertFromBase64 (spkiStream, b64)) return false;
    juce::MemoryBlock pkcs1;
    if (! spkiToPkcs1 (spkiStream.getMemoryBlock(), pkcs1)) return false;

    CFDataRef keyData = CFDataCreate (nullptr, (const UInt8*) pkcs1.getData(), (CFIndex) pkcs1.getSize());
    if (keyData == nullptr) return false;
    const void* keys[]   = { kSecAttrKeyType, kSecAttrKeyClass };
    const void* values[] = { kSecAttrKeyTypeRSA, kSecAttrKeyClassPublic };
    CFDictionaryRef attrs = CFDictionaryCreate (nullptr, keys, values, 2, &kCFTypeDictionaryKeyCallBacks,
                                                &kCFTypeDictionaryValueCallBacks);
    SecKeyRef key = attrs != nullptr ? SecKeyCreateWithData (keyData, attrs, nullptr) : nullptr;
    if (attrs != nullptr) CFRelease (attrs);
    CFRelease (keyData);
    if (key == nullptr) return false;

    CFDataRef data = CFDataCreate (nullptr, (const UInt8*) payload.toRawUTF8(), (CFIndex) payload.getNumBytesAsUTF8());
    CFDataRef sigData = CFDataCreate (nullptr, (const UInt8*) sig.getData(), (CFIndex) sig.getSize());
    bool ok = false;
    if (data != nullptr && sigData != nullptr)   // Apple's PSS verifier uses salt == hash length (32)
        ok = SecKeyVerifySignature (key, kSecKeyAlgorithmRSASignatureMessagePSSSHA256, data, sigData, nullptr);
    if (data != nullptr)    CFRelease (data);
    if (sigData != nullptr) CFRelease (sigData);
    CFRelease (key);
    return ok;
}

#else
static bool verifyImpl (const juce::String&, const juce::MemoryBlock&, const juce::String&)
{
    return false;   // no OS verifier wired for this platform: never trust unverified data
}
#endif

// ---- Public API -------------------------------------------------------------

bool verify (const juce::String& payload, const juce::String& signatureBase64, const juce::String& publicKeyPem)
{
    if (payload.isEmpty() || signatureBase64.isEmpty() || ! publicKeyPem.contains ("BEGIN PUBLIC KEY"))
        return false;
    juce::MemoryOutputStream sigStream;
    if (! juce::Base64::convertFromBase64 (sigStream, signatureBase64.trim()))
        return false;
    const auto sig = sigStream.getMemoryBlock();
    if (sig.getSize() < 128 || sig.getSize() > 1024)   // 1024..8192-bit keys only
        return false;
    return verifyImpl (payload, sig, publicKeyPem);
}

#if SAMPLAR_SELFTEST
juce::String& testKeyOverride()
{
    static juce::String pem;
    return pem;
}
#endif

juce::String activePublicKey()
{
   #if SAMPLAR_SELFTEST
    if (testKeyOverride().isNotEmpty()) return testKeyOverride();
   #endif
    return juce::String (LicenseSigningKey::kPublicKeyPem);
}

// Signature, product, status and machine: the checks that do not depend on
// this computer's clock.
static bool verifySignedFields (const juce::String& payload, const juce::String& signatureBase64,
                                const juce::StringArray& thisMachineIds, SignedLicense& out)
{
    if (payload.isEmpty())
        return false;   // nothing signed to check (caller logs "absent")
    if (! out.parse (payload))
    {
        juce::Logger::writeToLog ("[License] verifyStored: payload malformed");
        return false;
    }
    if (! verify (payload, signatureBase64, activePublicKey()))
    {
        juce::Logger::writeToLog ("[License] verifyStored: signature INVALID");
        return false;
    }
    if (out.product != LicenseSigningKey::kProductCode)
    {
        juce::Logger::writeToLog ("[License] verifyStored: signed for another product (" + out.product + ")");
        return false;   // a sibling product's license (e.g. SM5) can't unlock SAMPLAR
    }
    if (! out.status.equalsIgnoreCase ("active"))
    {
        juce::Logger::writeToLog ("[License] verifyStored: signed status not active");
        return false;   // validity is part of the signature, not a loose JSON flag
    }

    bool thisMachine = false;
    for (const auto& id : thisMachineIds)
        if (id.isNotEmpty() && id.equalsIgnoreCase (out.hardwareId))
            thisMachine = true;
    if (! thisMachine)
    {
        juce::Logger::writeToLog ("[License] verifyStored: signed for a different machine");
        return false;   // a valid license - for someone else's computer
    }
    return true;
}

bool isIssuedInFuture (const SignedLicense& s, juce::Time now)
{
    return s.issued > now + juce::RelativeTime::days (1);
}

bool verifyStored (const juce::String& payload, const juce::String& signatureBase64,
                   const juce::StringArray& thisMachineIds, juce::Time now, SignedLicense& out)
{
    if (! verifySignedFields (payload, signatureBase64, thisMachineIds, out))
        return false;
    if (isIssuedInFuture (out, now))
    {
        juce::Logger::writeToLog ("[License] verifyStored: issued in the future (clock skew > 1 day)");
        return false;
    }
    return true;
}

bool verifyLive (const juce::String& payload, const juce::String& signatureBase64,
                 const juce::String& sentNonce, const juce::StringArray& thisMachineIds,
                 juce::Time now, SignedLicense& out)
{
    // Freshness comes from the nonce (a reply to THIS request), never from the
    // local clock: a computer whose date is off by more than a day must still
    // be able to sign in (review L-M3). The clock is judged later by ClockGuard
    // against the signed issued time, with a clear "check your date" message.
    juce::ignoreUnused (now);
    if (! verifySignedFields (payload, signatureBase64, thisMachineIds, out))
        return false;
    if (sentNonce.isEmpty() || out.nonce != sentNonce)
    {
        juce::Logger::writeToLog ("[License] verifyLive: nonce mismatch (not a reply to this request)");
        return false;   // replayed recording
    }
    if (out.expires.toMilliseconds() != 0 && out.expires <= out.issued)
    {
        juce::Logger::writeToLog ("[License] verifyLive: license already expired (server time)");
        return false;
    }
    return true;
}

bool verifyDenial (const juce::String& payload, const juce::String& signatureBase64,
                   const juce::String& sentNonce, const juce::StringArray& thisMachineIds,
                   juce::Time now, SignedLicense& out)
{
    if (payload.isEmpty())
        return false;   // unsigned denial (caller logs it)
    if (! out.parse (payload))
    {
        juce::Logger::writeToLog ("[License] verifyDenial: payload malformed");
        return false;
    }
    if (! verify (payload, signatureBase64, activePublicKey()))
    {
        juce::Logger::writeToLog ("[License] verifyDenial: signature INVALID");
        return false;
    }
    if (out.product != LicenseSigningKey::kProductCode)
    {
        juce::Logger::writeToLog ("[License] verifyDenial: signed for another product (" + out.product + ")");
        return false;
    }
    static const juce::StringArray denialStatuses { "revoked", "expired", "inactive", "unbound", "blocked" };
    if (! denialStatuses.contains (out.status.toLowerCase()))
    {
        juce::Logger::writeToLog ("[License] verifyDenial: status is not a denial (" + out.status + ")");
        return false;   // an "active" licence payload is never a denial
    }

    bool thisMachine = false;
    for (const auto& id : thisMachineIds)
        if (id.isNotEmpty() && id.equalsIgnoreCase (out.hardwareId))
            thisMachine = true;
    if (! thisMachine)
    {
        juce::Logger::writeToLog ("[License] verifyDenial: signed for a different machine");
        return false;
    }
    if (sentNonce.isEmpty() || out.nonce != sentNonce)
    {
        juce::Logger::writeToLog ("[License] verifyDenial: nonce mismatch (not a reply to this request)");
        return false;   // a recorded denial can't be replayed
    }
    if (out.issued > now + juce::RelativeTime::days (1) || out.issued < now - juce::RelativeTime::days (1))
    {
        juce::Logger::writeToLog ("[License] verifyDenial: issued time more than a day from this clock");
        return false;
    }
    return true;
}

juce::String makeNonce()
{
    juce::uint8 bytes[16] = {};
   #if JUCE_WINDOWS
    if (BCryptGenRandom (nullptr, bytes, (ULONG) sizeof (bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
   #endif
    {
        juce::Random rng (juce::Time::getHighResolutionTicks() ^ juce::Random::getSystemRandom().nextInt64());
        for (auto& b : bytes) b = (juce::uint8) rng.nextInt (256);
    }
    return juce::String::toHexString (bytes, (int) sizeof (bytes), 0);
}

} // namespace LicenseSignature
} // namespace samplar
