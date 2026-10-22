# SAMPLAR Licensing

Ported from Sample Monsta 5 (Oct 2026). Server contract: `license_server/docs/license_signing_v2.md`.
Server product code: `SAMPL4R` (licence-key prefix). User-facing name: SAMPLAR.

## Flow

1. **Sign-in** (`networking/OAuthBrowserAuthenticator`): browser OAuth, Authorization Code + PKCE.
   - Listens on `127.0.0.1:9876-9900` (loopback only).
   - Opens `/auth/app-login?product_id=SAMPL4R&app_version=<x.y.z>&hardware_id=<v2>&hardware_id_v2=<v2>&flow=code&code_challenge=...`.
   - The server gates SAMPL4R at `app_version >= 1.0.0` and refuses a missing or invalid `hardware_id_v2` (32 hex). There is no v1 fallback. SAMPLAR refuses to start a sign-in without a valid v2 ID.
   - No `app_signature` / `app_signature_v2` is sent. The server never verifies them. The old shared `OAUTH_SIGNING_KEY` HMAC is gone from the client.
   - Accepts only the redirect that carries this sign-in's `state`.
   - Exchanges the code at `POST /auth/token-exchange` (PKCE verifier and a fresh nonce).
   - The user has 5 minutes, and can cancel.
2. **Proof** (`security/LicenseSignature`): the reply carries `license_payload` + `signature_v2`.
   - The payload format is `v2|SAMPL4R|email|type|hwid|issued|expires|active|nonce`.
   - The signature is RSA-PSS / SHA-256 / salt 32, verified against the PUBLIC key in `LicenseSigningKey.h`.
   - A live reply must name SAMPL4R, this machine's v2 ID, status active, and our nonce.
3. **Re-check** (`LicenseSystemService::revalidateAsync`): `POST /auth/revalidate`, `Authorization: Bearer <token>`, body `{hardware_id, hardware_id_v2, product_id: "SAMPL4R", nonce}` (both IDs are the v2 ID).
   - Runs at startup when the licence is older than 6 h, then while stale or locked, with backoff.
   - Runs on a joinable worker thread whose network call is cancellable on shutdown.
   - Each valid reply returns a fresh signed payload and a rolled sign-in token.
   - Every other reply carries a `reason`:

| `reason` | HTTP | SAMPLAR does |
|---|---|---|
| `token_missing`, `token_invalid`, `token_too_old` (also 401/403) | 200 | Keeps the licence and audio inside the offline window. `isSignInRequired()` turns true (`signInRequired` in `license.status`) and the UI offers "Sign in". Re-asks hourly. |
| `service_unavailable` (or any 5xx, 429, non-JSON) | 503, `Retry-After: 60` | Keeps the licence. Retries with backoff (1 min doubling to 1 h), never sooner than `Retry-After`. |
| `revoked`, `expired`, `inactive`, `unbound`, `blocked` | 200 | Acted on ONLY with a `denial_payload` + `denial_signature` that verifies: SAMPL4R key, RSA-PSS salt 32, status one of the five, this machine's v2 ID, this request's nonce, issued within a day. The first one mutes (licence + token kept, re-check in 1 min). A second one at least 50 s later clears the licence. Unsigned or mismatched denials count as transient. |
| `bad_request`, unknown reasons | 200 | Transient. |

4. **Offline window**: the licence stays usable (status `Valid`) without a signed reply for a limited time, counted from the last verified reply.
   - 30 days when the SIGNED expiry is `perpetual` (or the 9999 sentinel), whatever the type: PRO, STD, EDU, Gold-earned ownership.
   - 3 days for any dated expiry (subscriptions, Gold signed with a paid-through date, dated PRO/STD/EDU), and always for TRL, PROMO and PROMO2 (even if signed perpetual).
   - The window is decided from the signed payload only.
5. **Clock rollback** (`security/ClockGuard.h`): the latest time SAMPLAR has seen is kept in three places.
   - `session.idx`
   - `HKCU\Software\MuzikSociety\Samplar\ix`
   - `clock_mark` inside the licence file
   - A clock turned back by more than one day requires an online check.

A cache that fails verification is never deleted locally. The user is asked to sign in or to click Try again.

## Signing key

- `LicenseSigningKey.h` holds the **public** key (RSA-2048, fingerprint `46cfe00569377025`). Public keys are not secret.
- The private key exists only on the license server, at `KEYS_PATH/SAMPL4R/private_key.pem` on the Render disk.
- The server pins the same fingerprint (`crypto_service.PINNED_KEY_FINGERPRINTS["SAMPL4R"]`).
- Rotating the key needs a new pin on the server and a new client build, in that order.
- `SAMPLAR_REQUIRE_LICENSE_SIGNATURE` defaults to 1 (strict). Set it to 0 only for a local dev server whose key differs. Never ship with 0.

The old HMAC token (`SignedTokenVerifier`, `SamplarVerificationKey.h`) was removed. Its key was embedded in the binary, so anyone could mint tokens with it.

## Files on disk (`%APPDATA%\Samplar`, or `SAMPLAR_CACHE_DIR_OVERRIDE`)

| File | Contents |
|---|---|
| `license_cache.json` | Licence data, including the signed payload. Blowfish-encrypted blob plus integrity HMAC. |
| `sessions/session_<hwid>.json` | Sign-in token, encrypted. |
| `session.idx` | Clock high-water mark. |

## Update check (`services/UpdateCheckService`)

`GET /api/update/check?current_version=<x.y.z>&product=samplar&hardware_id=<v2>`. SAMPLAR reads `update_available`, `latest_version`, `download_url`, `release_date`, `changes` and `license_status {status, type, days_remaining, message}`. It offers an update only when `download_url` is not empty (it is `""` while SAMPLAR is unreleased). Non-200 replies are ignored.

## Machine release

There is no client "deactivate" endpoint. **Sign out** clears the local licence and session, then opens `https://samplemonsta.com/portal/licenses`, where the user releases the machine slot.

## Logging

All licence logs use the `[License]` prefix. They never contain:
- full emails (masked `sa***@domain`)
- tokens, codes, nonces or the OAuth URL
- more than 8 characters of a hardware ID

## Testing a release build

Use a real account with a SAMPL4R licence. Never commit licence keys, tokens or test accounts to this repository. Nothing in the code or tests depends on a real licence key: the tests sign their own payloads with a throw-away RSA key (`tests/licensing/TestSigner.h`).
