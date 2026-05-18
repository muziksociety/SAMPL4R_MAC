# SAMPLAR Licensing — Build Procedure

## Verification key (one-time, before each release)

The SAMPL4R license server signs every offline-verifiable token with HMAC-SHA256
and exposes the public verification key at:

    GET https://license-server-n5it.onrender.com/api/licenses/verification-key
    → { "verification_key": "<32-hex-chars>" }

Before producing a shipping build, paste the current verification key into
`src/security/SamplarVerificationKey.h`:

1. From a machine with network access, run:

   ```
   curl -s https://license-server-n5it.onrender.com/api/licenses/verification-key
   ```

2. Copy the 32-character hex value from `verification_key`.

3. Open `src/security/SamplarVerificationKey.h` and replace the placeholder
   `PASTE_VERIFICATION_KEY_HERE_32HX` with the real value:

   ```cpp
   constexpr const char* kSamplarVerificationKey =
       "a1b2c3d4e5f6...";  // 32 hex chars from server
   ```

4. Rebuild. The header has a compile-time sentinel check that
   `isVerificationKeyConfigured()` returns true. Offline token verification
   will refuse to validate anything until the placeholder is replaced.

### Security

The key shipped in `SamplarVerificationKey.h` is the **public** HMAC key used
only to *verify* tokens. The *signing* key lives only on the server. There is
nothing sensitive about embedding it in a release binary — an attacker who has
it cannot forge tokens, only verify them.

NEVER replace this with the secret signing key.

## License key format

```
SAMPL4R-{TYPE}-{HARDWARE_HASH}-{YYYYMMDD}-{SIG5}
```

- `TYPE` ∈ {`PRO`, `STD`, `EDU`, `TRL`, `PROMO`} (3 chars)
- `HARDWARE_HASH` — 10 alphanumeric chars (server fills `0000000000` on unbound keys)
- `YYYYMMDD` — expiry date, or `00000000` for first-use trials
- `SIG5` — 5-char signature

Regex: `^SAMPL4R-[A-Z0-9]{3}-[A-Z0-9]{10}-[0-9]{8}-[A-Z0-9]{5}$`

## Test key

The production server has this test key registered:

    SAMPL4R-PRO-0000000000-20270511-G1MFG

Paste it into the activation dialog on first launch to verify the flow end-to-end.

## Architecture summary

- `LicenseSystemService` — top-level orchestrator. Holds the cache, hardware
  generator, server client, session manager, and feature manager.
- `LicenseServerClient` — HTTP client for the SAMPL4R server endpoints:
  - `POST /api/licenses/validate-token` — used for BOTH first-launch activation
    and every-launch signed-token refresh (the server distinguishes via
    internal state). The previously-specced `/api/v1/validate` is not deployed.
  - `POST /api/licenses/deactivate` — remove machine binding
- `HardwareFingerprintGenerator` — SHA256 over CPU|MAC|Mobo|UUID|Disk, cached
  to `%APPDATA%/Samplar/.hardware_id`. Matches the SM5/VARIA pattern exactly.
- `LicenseCache` — encrypted on-disk cache with 30-day offline-grace tracking.
- `SignedTokenVerifier` — manual HMAC-SHA256 (RFC 2104) over the base64 payload
  returned by the server. Used to verify cached tokens offline.
- `SecureSessionManager` — stores access tokens (legacy OAuth flow).
- `LicenseActivationDialog` — first-launch UI: license-key entry + activate
  button, with browser-OAuth fallback retained for compatibility.

## Logging

All license-related logs use the prefix `🔐 [License]` and truncate license
keys to the first 10 characters so support can correlate without exposing the
full key.
