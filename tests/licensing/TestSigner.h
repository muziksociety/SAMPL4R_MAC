// Test-only RSA-PSS signer (Windows CNG). A fresh 2048-bit key per instance;
// the public half is exported as SPKI PEM for LicenseSignature::testKeyOverride().
// NEVER the real SAMPL4R key: that private key exists only on the server.
#pragma once

#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>

#if JUCE_WINDOWS
 #include <windows.h>
 #include <bcrypt.h>
 #include <wincrypt.h>
 #include <vector>
 #pragma comment(lib, "bcrypt.lib")
 #pragma comment(lib, "crypt32.lib")

namespace samplar::test {

class TestSigner
{
public:
    TestSigner()
    {
        if (BCryptOpenAlgorithmProvider (&alg_, BCRYPT_RSA_ALGORITHM, nullptr, 0) != 0) return;
        if (BCryptGenerateKeyPair (alg_, &key_, 2048, 0) != 0) return;
        if (BCryptFinalizeKeyPair (key_, 0) != 0) return;

        DWORD cb = 0;
        if (! CryptExportPublicKeyInfoFromBCryptKeyHandle (key_, X509_ASN_ENCODING, (LPSTR) szOID_RSA_RSA, 0, nullptr, nullptr, &cb))
            return;
        std::vector<BYTE> buf (cb);
        auto* info = reinterpret_cast<PCERT_PUBLIC_KEY_INFO> (buf.data());
        if (! CryptExportPublicKeyInfoFromBCryptKeyHandle (key_, X509_ASN_ENCODING, (LPSTR) szOID_RSA_RSA, 0, nullptr, info, &cb))
            return;
        BYTE* der = nullptr;
        DWORD derLen = 0;
        if (! CryptEncodeObjectEx (X509_ASN_ENCODING, X509_PUBLIC_KEY_INFO, info, CRYPT_ENCODE_ALLOC_FLAG, nullptr, &der, &derLen))
            return;
        pem_ = "-----BEGIN PUBLIC KEY-----\n" + juce::Base64::toBase64 (der, derLen) + "\n-----END PUBLIC KEY-----\n";
        LocalFree (der);
    }

    ~TestSigner()
    {
        if (key_ != nullptr) BCryptDestroyKey (key_);
        if (alg_ != nullptr) BCryptCloseAlgorithmProvider (alg_, 0);
    }

    TestSigner (const TestSigner&) = delete;
    TestSigner& operator= (const TestSigner&) = delete;

    bool ok() const { return pem_.isNotEmpty(); }
    const juce::String& pem() const { return pem_; }

    /** RSA-PSS / SHA-256 / MGF1-SHA-256 with `saltBytes` of salt (the
        contract is 32; other values exist to prove the verifier rejects them). */
    juce::String sign (const juce::String& payload, ULONG saltBytes = 32) const
    {
        juce::SHA256 sha (payload.toRawUTF8(), payload.getNumBytesAsUTF8());
        auto digest = sha.getRawData();
        BCRYPT_PSS_PADDING_INFO pad { BCRYPT_SHA256_ALGORITHM, saltBytes };
        ULONG cb = 0;
        BCryptSignHash (key_, &pad, (PUCHAR) digest.getData(), (ULONG) digest.getSize(), nullptr, 0, &cb, BCRYPT_PAD_PSS);
        std::vector<UCHAR> sig (cb);
        if (BCryptSignHash (key_, &pad, (PUCHAR) digest.getData(), (ULONG) digest.getSize(), sig.data(), cb, &cb, BCRYPT_PAD_PSS) != 0)
            return {};
        return juce::Base64::toBase64 (sig.data(), cb);
    }

private:
    BCRYPT_ALG_HANDLE alg_ = nullptr;
    BCRYPT_KEY_HANDLE key_ = nullptr;
    juce::String pem_;
};

} // namespace samplar::test
#endif
