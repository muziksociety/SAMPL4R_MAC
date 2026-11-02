# SAMPLAR licensing hardening, round 2 (review_round1.md L-H1, L-H2, L-M1, L-M2, L-M4, L-M5, L-M6, A5, A6)

Date: 2026-10-21. Build dir `build_lic/` only. Nothing committed or pushed, and no network calls were made.

## Verification

- **SamplarTests (build_lic, Release):** 13 classes, **471 checks, 0 failed**. That includes 11 new "Licensing hardening" groups (94 checks) in the new file `tests/licensing/LicensingHardeningTests.cpp`.
  - The tests use **real RSA-PSS**. A fresh 2048-bit CNG key is generated per run and installed through `LicenseSignature::testKeyOverride()`.
  - Every file the tests write goes to the sandbox folder (`SAMPLAR_CACHE_DIR_OVERRIDE`).
- **Server (local only):** the new `tests/test_revalidate_reasons.py` (15 tests) passes. The full suite, `pytest tests`, gives **611 passed, 0 failed**.
- **VST3:** `SamplarPlugin_VST3` builds in `build_lic` with 0 errors.
- **Smoke host** (`build/VST3SmokeHost_artefacts/Release/VST3SmokeHost.exe` against the `build_lic` bundle):
  - `--show-ms 2500`: **ALL PASS**, unload 75.6 ms.
  - `--two-instances`: **ALL PASS**, unload 27.8 ms / 93.5 ms.
  - I re-ran `--show-ms` after the final rebuild: ALL PASS.

## 1. HIGH: transient server errors no longer lock users out (L-H1, L-M6)

### Client
**Outcomes (`src/networking/LicenseServerClient.*`).** `RevalOutcome` is now `Valid / Denied / AuthRequired / NetworkError`, and `RevalResult` gains `reason`, `denialPayload` and `denialSignature`. Each reply maps as follows:

| Reply | Outcome |
|---|---|
| 401 / 403 | AuthRequired |
| `reason` = `token_*` | AuthRequired |
| 5xx / 429 | NetworkError |
| non-JSON body (any status, **404 included**) | NetworkError |
| `reason` = `service_unavailable` / `bad_request` / unknown | NetworkError |
| `reason` = `revoked`, `expired`, `inactive`, `unbound` or `blocked` (or `success:true, license_valid:false`) | **Denied** |

**Verifying a denial (`src/security/LicenseSignature.*`).** New `verifyDenial()`. A denial is accepted only if all of these hold:
- it is signed with the SAMPL4R key and names product SAMPL4R;
- its status is one of the five denial statuses (`active` never counts);
- it names this machine's ID;
- it echoes our nonce;
- it was issued within ±1 day.

**What `LicenseSystemService::applyRevalidation` does with each outcome:**
- **Denied + verified:** wipes `license_payload` / `signature_v2`. Stores `license_valid=false` and `denial_status` (so the next launch shows the right message). Locks with Expired or InvalidKey, mutes audio, and re-checks hourly (a re-purchase unlocks again).
- **Denied but unsigned, or signed but failing verification:** treated as NetworkError. The licence is kept.
- **AuthRequired:** keeps the licence and stays Valid inside the offline window, with the message "sign-in expired". Retries in 1 h.
- **NetworkError:** keeps the licence and status. Retries with **backoff of 1, 2, 4 ... 60 min**.

**Recovery when locked (L-M6).** The timer now re-checks while locked as long as a token is stored. A signed valid reply unlocks automatically.

### Server (`b:/SANTI/license_server`, local working tree, not committed or deployed)
- **`plugins/crypto_service/license_v2.py`:** `build_payload(..., status="active")`. Adds `DENIAL_STATUSES = {revoked, expired, inactive, unbound, blocked}`; any other status raises ValueError.
- **`plugins/crypto_service/crypto_service.py`:** `sign_license_v2(..., status="active")` pass-through.
- **`plugins/auth/__init__.py` (`/auth/revalidate` and `/auth/family-license`):**
  - Every failure now carries a `reason`: `token_missing`, `token_invalid`, `token_too_old` or `bad_request`.
  - **"Service unavailable" now returns HTTP 503** (it was 200 + `license_valid:false`). This applies both when there is no DB service and when the user lookup throws, so older SM5 and RUIDO clients also stop treating it as revoked.
  - Explicit denials carry `reason` plus, when a nonce was sent, `denial_payload` / `denial_signature`: a v2 payload signed with the product key, with denial status, this machine's ID and the nonce. The cases:

    | Case | Denial status |
    |---|---|
    | No licence bound to this machine | `unbound` |
    | Licence inactive (revoked / refunded) | `revoked` |
    | Licence expired | `expired` |
    | PROMO lapsed | `inactive` |
    | Account inactive | `inactive` |
    | Banned device | `blocked` |
  - The field names are separate from `license_payload` / `signature_v2` on purpose: old clients ignore them, and existing tests asserting "no `signature_v2` on invalid" still pass.
- **Docs:** `docs/license_signing_v2.md` now documents the reasons and signed denials (new section "Reasons and signed denials").
- **Tests:** `tests/test_revalidate_reasons.py` (new).
- **Deploy order:**
  1. Upload the SAMPL4R key (as before).
  2. Deploy this server change.
  3. Ship the client.

  With an old server, the client still never wipes on an unsigned denial: revocation then only takes effect when the offline window runs out (30 or 3 days). That is safe, just slower.

## 2. HIGH: the sign-in thread no longer freezes the UI (L-H2)

**Child processes (`HardwareFingerprintGenerator`).** New `runBoundedCommand(cmdLine, timeoutMs, cancel*)`:
- It starts the process directly (no `cmd.exe` wrapper, so `kill()` reaches wmic / PowerShell itself).
- It polls `isRunning()` every 10 ms and never does a blocking read.
- It kills on timeout or cancel, and reads output only after a normal exit.
- `executeSystemCommand` uses it with a 3 s cap.
- Tested with `ping -n 30` and a 300 ms timeout: back in under 1.5 s. Cancel from another thread is also noticed in under 1.5 s.

**Machine ID cached per process.** New `HardwareFingerprintGenerator::licenceMachineId()`: the v2 ID (MachineGuid registry read) in a function-local static, so it is computed **once per process**. If the OS has no machine ID, it falls back to the same v2 hash over JUCE's SMBIOS device ID. No child process runs on Windows.
- The service, the re-check and sign-in now use only this ID; it is sent as both `hardware_id` and `hardware_id_v2`.
- **The wmic / PowerShell v1 and legacy-ID queries no longer run anywhere in the plugin.** They were on the OAuth thread, and on the message thread at first launch inside `initialise()`.
- `getLegacyHardwareIdCandidates` is deleted.

**OAuth (`OAuthBrowserAuthenticator`):**
- Socket polls are now 100 ms (were 500 ms).
- There is no HWID work on the thread.
- `startAuthentication` never joins: if a previous run is still running it refuses.
- `cancelAuthentication` (shutdown only) waits with `waitForThreadToExit`, with **no `stopThread`, so no force-kill**.

**Service:**
- On a new sign-in after Cancel, a cancelled authenticator that is still closing is **retired**: moved to `retiredAuthenticators_`, reaped once its thread has finished (on the next sign-in and on every timer tick), and joined only at shutdown.
- A live, not-cancelled sign-in refuses a second one without touching its run.
- Each sign-in has a run ID. Results from an older run never touch licence state, and the UI callback gets "Sign-in cancelled", which the dialog's own run token ignores.
- The re-check worker also uses bounded `waitForThreadToExit`, never `stopThread`.
- The worker always delivers its result, so the in-flight flag always clears; stale or late results are dropped by the alive token and the epoch.

**Message thread:** nothing on the message thread waits on the network or on child processes. The only joins are at shutdown or unload, and those threads are cancellable, so they take about 0.1 s.

## 3. MEDIUM items

**ClockGuard (A6).** New `ClockGuard::decode()`. It accepts digits only (length 1-19). A value that decodes outside [2024-01-01, now + 10 years] counts as **0 = "unknown"**. Applied to both the file and the registry copy.
- Tests: `""`, whitespace, garbage and `"0"` all read as 0 and never as "rolled back".
- A licensed service restarted with an empty or garbage `session.idx` stays Valid offline (the signed `clock_mark` in the licence file still guards against rollback).

**Re-check vs sign-out / sign-in race (L-M1).** `std::atomic<uint64_t> epoch_`:
- It is bumped by each accepted sign-in (`applyAuthResult`), by `deactivateLicense`, and by `shutdown`.
- `revalidateAsync` captures the epoch, and `applyRevalidation(result, epoch)` drops stale results.
- `deactivateLicense` also cancels the in-flight HTTP call and the sign-in, without waiting.
- Tests:
  - A valid reply after sign-out does not restore the licence.
  - A signed denial from before a fresh sign-in does not wipe it.

**v1 IDs and plaintext caches removed (L-M2).**
- `thisMachineIdsLocked()` now returns only the licence machine ID. Tested: a payload bound to a 64-hex v1 ID is rejected.
- `LicenseCache::decryptData` rejects blobs without `enc` (tested).
- `SecureSessionManager::decryptData` no longer returns undecryptable text as-is. It checks BlowFish padding and returns empty on failure. `encryptData` with no key stores nothing (it used to store plaintext).
- The session folder now follows `LicenseCache::cacheDirectory()`: same default path, and it honours the test override.
- Legacy migration paths are deleted: `.hardware_id` migration at `initialise`, the `signed_token` / `access_token` fallbacks, `ACCOUNT:` keys, and the "older layout" `licenseKey`.

**Running-session enforcement (L-M4).** The timer now runs every 60 s (was 15 min). `enforceRunningSession()`:
- **Expiry:** sets Expired.
- **ClockGuard rollback:** locks.
- **Offline window:** locks.
- Each of these publishes `license.status` with reason `running_*`, which the editor's gate and the overlay already follow.
- `ClockGuard::advance` runs every 15 min.
- Tested: a TRL licence expiring in 1 h becomes Expired when the clock is +2 h; audio turns off and a status event is published.

**PII in logs.** The `.hardware_id` save path and the directory-creation path no longer log the Windows user path. Everything else in the owned files was already masked (emails `sa***@`, IDs as 8 characters, no tokens / codes / URLs).

## 4. Audio licence gate (A5 / L-M5): the lead applies this
`LicenseSystemService` now exposes `bool isAudioAllowed() const noexcept` and `const std::atomic<bool>& audioAllowedFlag() const noexcept`.
- The flag is written only by `setStatusLocked()`: it is true exactly while the status is Valid, offline window included.
- It is false before `initialise`, after `shutdown` and in the destructor.
- The flag is a member of the service, which lives in the processor's registry, so its address is stable for the processor's lifetime.

**`src/plugin/SamplarAudioProcessor.h`** (private members):
```cpp
    const std::atomic<bool>* licenceAudioAllowed_ { nullptr };   // LicenseSystemService gate (owned by serviceRegistry_)
```
**`src/plugin/SamplarAudioProcessor.cpp`**, constructor: replace the `registerService("license", ...)` statement (~:408) with:
```cpp
    auto licenseSvc = std::make_unique<LicenseSystemService>(eventBus_);
    licenceAudioAllowed_ = &licenseSvc->audioAllowedFlag();
    serviceRegistry_->registerService("license", std::move(licenseSvc));
```
**`processBlock`**: right after `midiMapping_->renderBlock(buffer, midiMessages, host);`. The engine keeps running, so FIFOs drain, voices advance and nothing stalls; only the output is muted.
```cpp
    if (licenceAudioAllowed_ == nullptr || ! licenceAudioAllowed_->load(std::memory_order_relaxed))
        buffer.clear();   // unlicensed / expired / locked: silent output (engine still drained)
```
- **Optional:** add the same two lines at the end of `processBlockBypassed`.
- Add `midiMessages.clear();` inside the `if` if the plugin ever emits MIDI out.
- Separate from the 5-line change: the editor fails open when `licenseService_` is null (L-M5, `SamplarAudioProcessorEditor.cpp:42`). It could fail closed instead.

## Files changed
**SAMPLAR:**
- `src/services/LicenseSystemService.h/.cpp` (rewritten)
- `src/networking/LicenseServerClient.h/.cpp`
- `src/networking/OAuthBrowserAuthenticator.h/.cpp`
- `src/security/LicenseSignature.h/.cpp`
- `src/security/ClockGuard.h`
- `src/security/HardwareFingerprintGenerator.h/.cpp`
- `src/security/LicenseCache.cpp`
- `src/security/SecureSessionManager.cpp`
- New: `cmake/extra_licensing.cmake`. It adds the test file plus `LicenseSystemService.cpp`, `SecureSessionManager.cpp`, `LicenseServerClient.cpp` and `OAuthBrowserAuthenticator.cpp` to SamplarTests. `licensing` is already in the main CMakeLists `foreach`, so no lead edit is needed.
- New: `tests/licensing/LicensingHardeningTests.cpp`.
- UI files are unchanged; they already react to `license.status`.

**Server:**
- `plugins/auth/__init__.py`
- `plugins/crypto_service/license_v2.py`
- `plugins/crypto_service/crypto_service.py`
- `docs/license_signing_v2.md`
- New: `tests/test_revalidate_reasons.py`

## Notes / not done
- **Kept, unused by the plugin:** `LicenseCache::getAllCachedHardwareIds` and the v1 `.hardware_id` binding code in HardwareFingerprintGenerator, because QA tests I don't own (`LicenseSystemTests`, `HardwareIdBindingTests`) use them. They can be deleted together with those tests.
- **Not unit-tested:** the OAuth retire/reap path, because it needs a real browser launch. It was reviewed by reading the code.
- **UpdateCheckService / SettingsDialog** now receive the v2 ID (32 characters) from `generateHardwareId()`. This is intended: it is what the server binds.
