#pragma once

// SAMPLAR license-data signing key (PUBLIC half). Contract: license_server
// docs/license_signing_v2.md. Server product code SAMPL4R (the license key
// prefix). RSA-2048, generated offline on 2026-10-20 like the VARIA / LIGHT
// keys. SHA-256(DER SubjectPublicKeyInfo) starts 46cfe00569377025; the server
// pins the same fingerprint (crypto_service.PINNED_KEY_FINGERPRINTS["SAMPL4R"])
// and signs SAMPL4R replies with the matching PRIVATE key, which lives only on
// the server disk (KEYS_PATH/SAMPL4R/private_key.pem).
// Public keys are not secret. Nothing but that private key can produce a
// signature this key accepts. Changing this key means a new server pin first.

namespace samplar::LicenseSigningKey {

inline constexpr const char* kProductCode = "SAMPL4R";
inline constexpr const char* kFingerprint = "46cfe00569377025";

inline constexpr const char* kPublicKeyPem =
    "-----BEGIN PUBLIC KEY-----\n"
    "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuq2cGFAx2evcgBKy9QkR\n"
    "dMY7qnqhiNxS+5QzVBfim6xLGm23809k6Cs9qK/WJgwprLFvHeFVhQ13i8TrrZ8z\n"
    "60ec2OlJIKtUiDI4bEDN9EnAUpiG+rgX/nU4SnkZ6oTYfhtyUHdk3UFZ0IIC9UNU\n"
    "tSZ8jDAngZoUqEfleU8qljM/vHs9UdPVEYKenPEb2d5x671H9K4/X1xr1vYz1jac\n"
    "8PFvHKkCVnPHh9yJTTsG5FNdUL+FBYi08l2cPiEyk2djC8BDGxy0ewYsQyl0P7uj\n"
    "zDHasceHfGgpPAZ3/grv3hq/KWCTh1eXFJ069C1hsexzkDq2DON5Pe+zuhjeu77Z\n"
    "7QIDAQAB\n"
    "-----END PUBLIC KEY-----\n";

} // namespace samplar::LicenseSigningKey
