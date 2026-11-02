# impl_licensing3: SAMPLAR client vs final SAMPL4R server contract

Contract: `license_server` `origin/feat/sampl4r-product:docs/license_signing_v2.md` (PR #132). Cross-checked read-only against `plugins/auth/__init__.py` (app-login, token-exchange, `_revalidate_core`, `_signed_denial`, `_service_unavailable`) and `plugins/update_checker/__init__.py` on that branch. Nothing in `b:/SANTI/license_server` was modified. Only a fetch was run there.

## Contract vs client

| Contract item | Before | After |
|---|---|---|
| Login: PKCE `/auth/app-login` + `/auth/token-exchange`, `app_version`, `hardware_id_v2` | Already compliant. Sends `product_id=SAMPL4R`, `app_version` (1.0.1 >= gate 1.0.0), S256 challenge, `hardware_id_v2`, and the nonce + verifier on exchange. | Unchanged. The sign-in is now refused locally, with a clear message, when the machine ID is not 32 hex. The server would refuse it anyway ("out of date" page). |
| `OAUTH_SIGNING_KEY` / `app_signature` | Committed HEAD still computes `app_signature` with the hard-coded key. The working tree already had it removed (earlier uncommitted round). The server never verifies `app_signature(_v2)`; it only passes it through to the template. | Confirmed absent from src, tests and cmake. Nothing replaces it: the contract expects no app signature. **The removal is still uncommitted: commit it.** |
| Test licence key in README/repo | No top-level README. `docs/LICENSE_SERVER_COMPLETE_DOCUMENTATION.md` had test credentials (`<REDACTED-DEBUG-KEY>` with an email and password, and `<REDACTED-TEST-KEY>`). | Redacted. No code or test depends on any real key: the tests use dummy keys (`SAMPL4R-PRO-TEST`) and sign with a throw-away RSA key. Git history and the graphify cache still hold the old text. |
| Revalidate request `{hardware_id, hardware_id_v2, product_id:"SAMPL4R", nonce}` + Bearer | Compliant | Unchanged. It now refuses to send without a valid v2 ID (no v1 fallback). |
| `token_missing` / `token_invalid` / `token_too_old` (200) | Mapped to AuthRequired and the licence was kept. The message changed, but the UI had no sign-in prompt while Valid: the widget offered "Revalidate", which would only fail again. | New `isSignInRequired()`, plus `signInRequired` in the `license.status` payload. Licence and audio are kept inside the offline window. LicenseStatusWidget shows "Sign in". The sign-in panel no longer auto-closes on a Valid status while sign-in is required. Cleared by a sign-in, a valid re-check or a sign-out. |
| 503 + `Retry-After: 60` (`service_unavailable`) | Treated as transient with the licence kept. `Retry-After` was ignored (the 1-min backoff happened to match 60 s). | `Retry-After` is parsed (delta-seconds, capped at 1 h) and used as a floor on the backoff. The reason is now also parsed from 5xx bodies for the logs. |
| Signed denials (5 statuses), RSA-PSS SHA-256 salt 32, SAMPL4R key, nonce + hwid binding | Compliant: `verifyDenial` checks the key, salt 32, the five statuses, this v2 ID, our nonce, and issued within 1 day. The first signed denial mutes; a second one at least 50 s later clears. Unsigned denials are transient. | Unchanged (the stricter two-step clear is kept: the first denial already mutes). New tests cover each status, nonce mismatch, hwid mismatch, salt != 32, wrong key and a wrong product. |
| Offline grace: 3 days for every non-perpetual licence, 30 days ONLY for perpetual PRO | 3 days for PROMO, PROMO2, TRL and any dated expiry. **STD, EDU and other perpetual types got 30 days.** Unsigned (dev builds only) got 30 days. | `isShortWindowLicense = !(type == PRO && expires == perpetual)`. Unsigned now gets 3 days. Messages and README updated. |
| Update check GET `/api/update/check?product=samplar&current_version&hardware_id` | Field names compliant (`license_status`, `changes[]`). It offered an update even when `download_url` was `""` (unreleased) and parsed non-200 bodies. | Non-200 replies are ignored and the `error` field is logged. No update is offered when `download_url` is empty. `days_remaining: null` keeps -1. |

## Files

- `src/networking/LicenseServerClient.{h,cpp}`: `Retry-After` header capture, `parseRetryAfter`, `isValidMachineIdV2`, `RevalResult::retryAfterSeconds`, v2 ID guard.
- `src/services/LicenseSystemService.{h,cpp}`: `signInRequired_` / `isSignInRequired()` / payload field, `scheduleRetryLocked(..., retryAfterSeconds)`, new offline-window rule, sign-in v2 ID guard.
- `src/ui/licensing/LicenseStatusWidget.cpp`: "Sign in" button while Valid with sign-in required.
- `src/ui/licensing/LicenseActivationDialog.cpp`: the panel no longer closes on Valid while sign-in is required.
- `src/services/UpdateCheckService.cpp`: HTTP status, empty `download_url`, `error`, null `days_remaining`.
- `src/security/README.md`: reason table, offline rule, update check, v2-only. The stale "v1 or v2" and `.hardware_id` lines were removed.
- `tests/licensing/TestSigner.h` (new, shared CNG test signer with a salt parameter), `tests/licensing/ContractV2Tests.cpp` (new), `tests/licensing/LicensingHardeningTests.cpp` (uses the shared signer), `cmake/extra_licensing.cmake`.
- `docs/LICENSE_SERVER_COMPLETE_DOCUMENTATION.md`: test credentials redacted. This file is outside the listed ownership, but the task required removing the test key from the repo.

## Verification

- `cmake -B build_lic ... -DSAMPLAR_BUILD_TESTS=ON`, then build SamplarTests and SamplarPlugin_VST3 (Release, `--parallel`): no errors.
- `SamplarTests.exe`: **14 classes, 874 checks passed, 0 failed.** The new "Licensing contract v2" class covers:
  - Reason mapping, including 503 + Retry-After and `bad_request` / unknown reasons.
  - `Retry-After` parsing and the v2 ID format.
  - Salt 32 only (salt 0/20/31/33/64/222 rejected).
  - Denial binding: nonce, hwid, salt, other key, SM5 product.
  - Each `token_*` reason: licence kept, audio on, `signInRequired` set, still plays at +1 day, mutes after 30 days with the licence still on file, cleared by a sign-in or a valid re-check.
  - 503: licence kept, retry at about 60 s, Retry-After 300 wins over the backoff, a shorter one never shortens it.
  - Each of the 5 signed denials mutes and then clears; denials for another hwid, another nonce, salt 20 or unsigned are ignored.
  - Offline windows: PRO perpetual 30 days (plays at 3 d 1 h and at 29 d, locked after 30 d); PRO dated, STD, EDU, PROMO, PROMO2 and TRL lock after 3 days.
- Smoke host `VST3SmokeHost.exe --plugin build_lic/.../Release/VST3/SAMPLAR.vst3 --show-ms 2500`: **ALL PASS (exit 0)**, unload 36.5 ms.
- No signed denial vector exists in the contract doc. Denials were tested with a per-run TEST key; the real-key positive vector stays in LicenseSignatureTests.

## Follow-ups (outside my files)

1. **Plugin editor / SettingsDialog**: neither shows a sign-in prompt while Valid. They should read `isSignInRequired()` or `license.status.signInRequired` (for example, a banner or a "Sign in" button in LicenseInfoPanel). The editor only opens the sign-in panel when the status is not Valid. These files belong to another agent.
2. **Policy note**: the server doc says "only `perpetual` keeps 30 days" (it doesn't name the type). This client follows the stricter brief: 30 days only for perpetual PRO, so perpetual STD/EDU get 3 days. Confirm with the product owner.
3. Commit the `OAUTH_SIGNING_KEY` removal (it is still in HEAD). Ask the server owner to rotate that key, since it is in git history.
