# SAMPLAR licensing port (SM5 -> SAMPLAR) - implementation report

Date: 2026-10-20.

**Status:** code complete. No project build was run, because one was already running in the same build dir.
- **C++ syntax check.** Every changed C++ file passes a syntax/semantic check: `cl /Zs`, using SamplarPlugin's Release include paths and defines, run from the scratchpad with no outputs.
- **Consumers I don't own.** These pass the same check against the new API: SettingsDialog.cpp, UpdateCheckService.cpp, App.cpp, MainWindow.cpp, tests/licensing/LicenseSystemTests.cpp.
- **Linking** is not verified until CMake lists the new files.
- Server tests: 17 new tests and 164 tests in the related suites pass locally.
- The patch was also tested against an `origin/master` snapshot: 78 passed.

**The next build fails until the CMakeLists.txt changes in section 3 are applied.** New .cpp files are not listed yet, and deleted files are still listed.

## 1. What the port does (audit D1 D2 D6 D7 D10 D12-D17)

| Audit | Fix |
|---|---|
| D1 | The browser JWT is no longer stored as `signed_token`. A local verification failure never wipes the cache. Sign-in stores the server-signed v2 licence plus the sign-in token (encrypted session file). Startup verifies the signed licence offline. |
| D2 | The HMAC verifier and its embedded key are deleted. Verification is now RSA-PSS (SHA-256, salt 32) against an embedded PUBLIC key: `src/security/LicenseSigningKey.h`, fingerprint `46cfe00569377025`. Strict by default (`SAMPLAR_REQUIRE_LICENSE_SIGNATURE`=1 in LicenseSystemService.h). Checks: product must be `SAMPL4R`, status active, bound to this machine (v1 or v2 HWID), and a live reply must echo our nonce. |
| D6 | Every `oauth_debug.txt` Desktop write is gone (service destructor and OAuth). |
| D7 | Listener binds `127.0.0.1` only. State and PKCE verifier come from BCryptGenRandom. PKCE code flow plus `/auth/token-exchange` is the only activation path. Callbacks with the wrong `state` get a 400 and are ignored, without aborting the sign-in. `app_signature` (shared SM5 HMAC) dropped; the server never read it. |
| D10 | Within the offline window the status is `Valid` (30 days perpetual, 3 days dated/trial/promo), so every `== Valid` gate (App.cpp, SettingsDialog) works offline. Background `/auth/revalidate` runs at startup when the licence is older than 6 h and every 15 min while stale; it stores the rolled token, enforces revocation/expiry, and has a ClockGuard against clock rollback. OAuth timeout is 5 min, with Cancel. "Deactivate" became an honest sign-out: it clears the local licence and session and opens `samplemonsta.com/portal/licenses` to release the slot (no client endpoint exists). |
| D13 | Emails masked (`sa***@x.org`). The OAuth URL, tokens, codes and nonces are never logged. HWIDs are shortened to 8 chars. Raw MAC, board, UUID and disk serials are masked in HardwareFingerprintGenerator. Cache keys log the prefix only. |
| D15 | The production test licence is removed from `src/security/README.md` (rewritten). Revoking it on the server is still required (section 5). |
| D16 | No modal loop in the editor constructor. The UI is built, then an in-window `LicenseGateOverlay` covers it while unlicensed. It follows `license.status`, so it appears on revocation or when the offline window ends, and disappears after a silent re-check. All async lambdas use SafePointer or an `alive` token. |
| D17 | All licence UI strings say SAMPLAR. Links now point to routes that exist: `/auth/register`, `/auth/forgot-password`, `/auth/buy/SAMPL4R`, `/portal/licenses`. On the server, the SAMPL4R brand is named "SAMPLAR". |
| D12 | Server `MIN_APP_VERSION_BY_PRODUCT["SAMPL4R"]` defaults to 1.0.0, so SM5 raising its minimum can't lock SAMPLAR out. 1.0.1 sends `app_version` from `src/Version.h`. |

**Threads.** Two threads exist, and both are joinable:
- **OAuth thread.** Only this thread touches the socket. SM5's version reset the socket from the message thread, which is a use-after-free; that bug was not ported.
  - The UI "Cancel sign-in" only signals it (`requestCancel`), so the DAW UI never freezes while the HWID shell queries finish.
  - Shutdown and the destructor signal it and join it (`cancelAuthentication`).
  - The dialog ignores late results from a cancelled run (run token).
- **Re-check worker** (`RevalidationWorker`). The service destructor and `shutdown()` cancel it and join it.

**Network calls.** Every network call goes through `CancellableHttpPost`, a WebInputStream that `cancel()` aborts. A plugin unload never waits on a 30 s connect.

**Legacy hardware IDs.** These are recomputed with wmic/PowerShell. The recompute runs only on sign-in, or on a re-check before the binding has moved to HWID v2.

## 2. Files

### SAMPLAR: new
- `src/security/LicenseSignature.h/.cpp`: v2 payload parse plus RSA-PSS verify (CNG on Windows, Security.framework on Mac), and the nonce.
- `src/security/LicenseSigningKey.h`: the SAMPL4R public key and fingerprint.
- `src/security/ClockGuard.h`: clock rollback guard (file `session.idx` plus `HKCU\Software\MuzikSociety\Samplar\ix`).
- `src/ui/licensing/LicenseGateOverlay.h/.cpp`: in-window, non-modal sign-in panel. It is either owned by the editor or self-owned through `showInWindow`.

### SAMPLAR: rewritten
- `src/services/LicenseSystemService.h/.cpp`: SM5 port.
  - Added `recheckNow()`.
  - Kept every API used by files I don't own: `getCurrentStatus`, `getLastResultMessage`, `getCurrentLicenseInfo`, `generateHardwareId`, `getRemainingGraceTime`, `deactivateLicense`, browser-auth methods, `getAccessToken`, `get/setServerUrl`, `isFeatureEnabled`.
  - Removed the dead or insecure key/password/HMAC paths and the `license.validate` / `license.activate` subscriptions.
  - `kClientVersion "5.0.0"` is gone; it uses `samplar::kAppVersion` (`../Version.h`), as the lead asked.
- `src/networking/LicenseServerClient.h/.cpp`: `/auth/revalidate` only, plus `CancellableHttpPost`.
- `src/networking/OAuthBrowserAuthenticator.h/.cpp`: PKCE port (section 1).
- `src/ui/licensing/LicenseActivationDialog.h/.cpp`: SafePointer fixes. Adds Sign in / Cancel sign-in, Try again, links, and an optional Quit/Close button. Adds `showInWindow()`. `showModal()` is kept, for the standalone App only.
- `src/plugin/SamplarAudioProcessorEditor.h/.cpp`: in-window gate (section 1, D16).
- `src/security/README.md`

### SAMPLAR: edited
- `src/security/HardwareFingerprintGenerator.h/.cpp`: adds HWID v2 and the legacy candidates; masks logs.
- `src/security/LicenseCache.h/.cpp`: adds static `cacheDirectory()`; masks logs.
- `src/ui/licensing/LicenseStatusWidget.h/.cpp`: in-window sign-in instead of `showModal`, "Revalidate" calls `recheckNow`, Renew/Account open real pages, "Not signed in" text.

### SAMPLAR: deleted (from disk only, not from the git index)
- `src/security/SignedTokenVerifier.h`
- `src/security/SignedTokenVerifier.cpp`
- `src/security/SamplarVerificationKey.h`

### Server (`b:/SANTI/license_server`, local working tree, not committed)
- `plugins/crypto_service/crypto_service.py`: pin `"SAMPL4R": "46cfe00569377025"`.
- `core/branding.py`: `BRANDS["SAMPL4R"]` with name "SAMPLAR".
- `plugins/auth/__init__.py`: `MIN_APP_VERSION_BY_PRODUCT["SAMPL4R"]` (env `MIN_APP_VERSION_SAMPL4R`, default 1.0.0).
- `plugins/update_checker/__init__.py`:
  - The `samplar` soft licence lookup is filtered to `SAMPL4R-` keys. SM5 and SAMPLAR share a HWID, so without the filter an SM5 trial reminder would show in SAMPLAR.
  - While "1.2.2" is above the latest release, it is read as 1.0.0, so 1.0.0 clients are offered 1.0.1.
- `data/releases.json`: `samplar` latest 1.0.1, placed first to avoid upstream conflicts.
- `docs/license_signing_v2.md`, `00LICENSE_MODULE/docs/06-migration-per-plugin.md`
- `tests/test_samplar_product.py` (new)

The whole server change is in `scratchpad/license_server_sampl4r.patch`. It applies cleanly to local `master` and to `origin/master`.

**Local `master` is 36 commits behind `origin/master`.** Apply the patch on `origin/master`.

## 3. Required CMakeLists.txt changes (SAMPLAR_SOURCES list, ~lines 310-331)

Remove these three lines:
```
  src/security/SamplarVerificationKey.h
  src/security/SignedTokenVerifier.h
  src/security/SignedTokenVerifier.cpp
```
Add these lines, for example right after `src/security/SecureSessionManager.cpp`:
```
  src/security/ClockGuard.h
  src/security/LicenseSigningKey.h
  src/security/LicenseSignature.h
  src/security/LicenseSignature.cpp
```
Add these after `src/ui/licensing/LicenseStatusWidget.cpp`:
```
  src/ui/licensing/LicenseGateOverlay.h
  src/ui/licensing/LicenseGateOverlay.cpp
```
Recommended: in `target_compile_definitions(SamplarPlugin PRIVATE ...)` (~line 687), delete `JUCE_MODAL_LOOPS_PERMITTED=1`.
- No plugin code needs it any more. I grepped for runModalLoop, sync AlertWindow/PopupMenu/FileChooser calls and DialogWindow::showModalDialog.
- Keep it on `SamplarApp` (line 413) for `showModal`.

Optional: `-DSAMPLAR_REQUIRE_LICENSE_SIGNATURE=0`, only for a local dev server whose key differs. Never ship it.

## 4. Required changes in files I don't own

**a. `src/services/UpdateCheckService.cpp`.** The server sends `license_status`; the client reads `license_reminder`. Fix it in the client: in `parseResponse()`, replace the two lookups
```cpp
        if (updateObj->hasProperty("license_reminder"))
        {
            auto reminderVar = parsed["license_reminder"];
```
with
```cpp
        if (updateObj->hasProperty("license_status"))
        {
            auto reminderVar = parsed["license_status"];
```
Also update the three log strings `license_reminder.` to read `license_status.`.

D13: the same file logs the full HWID at ~:97 and the full request URL (which contains the HWID) at ~:107. Replace them with:
```cpp
    juce::Logger::writeToLog("[UpdateCheckService]   hardwareId: "
                             + juce::String(hwId.empty() ? "(empty)" : hwId.substr(0, 8) + "..."));
...
    juce::Logger::writeToLog("[UpdateCheckService]   Request: " + baseUrl + " (product samplar, version " + currentVersion + ")");
```
`getHardwareId()` already uses `LicenseSystemService::generateHardwareId()`, which is now thread-safe and returns the cached ID.

**b. `src/ui/Components/SettingsDialog.cpp`.** `deactivateLicense()` now means "sign out here and open the account page". In `onDeactivate()` (~:1150-1185):
- Title: `"Deactivate License"` becomes `"Sign out"`.
- Confirmation message:
  ```
  "Sign out of SAMPLAR on this computer?\n\nYour account page opens in your browser: release this computer there to use the activation on another one."
  ```
- Button: `"Deactivate"` becomes `"Sign out"`.
- Success alert: `"Deactivated"` / `"License has been deactivated..."` becomes
  ```
  "Signed out", juce::String(svc->getLastResultMessage())
  ```
- Rename the `deactivateBtn_` label to "Sign out" if it says "Deactivate".

**c. `src/app/App.cpp` (optional; the standalone app may keep its modal).** The `!= Valid` gate now works offline. For no modal at all, mirror SM5 b0526074:
```cpp
samplar::LicenseActivationDialog::showInWindow(mainWindow.get(), *licenseService, eventBus_, false,
    [](int r) { if (r != 1) samplar::ServiceLocator::requestQuit(); });
```
This must run after the main window exists.

**d. `tools/release_linter/intentional_orphan_events.txt` (optional).** `license.activate` and `license.validate` are no longer subscribed anywhere; their entries can go.

**e. `tests/` (recommended, D2/D21).** Add a verifier test using this vector, signed by the server code with the real SAMPL4R key (public data only):
```cpp
const juce::String payload = "v2|SAMPL4R|user@example.com|PRO|ABCDEF0123456789ABCDEF0123456789|2026-10-21T03:43:28Z|perpetual|active|0123456789abcdef0123456789abcdef";
const juce::String sig = "QLCloQOt1lNvkQiRagR5QH4T5T9Pwk/y3/q25gYJqkI+zQG8W7HTmUpCw+o6N33KOSqsuS4hS5xuasGqNyAwjqdj89GeKzPNgz4J8A3VuUIAYfP+U3asaProtIIky/irg/NvBtyEani6P9xZIXMLDuXHWtGjaB1EyIHNRRyrnBCPF/HK5Ag8nWIqLoXsVuHSenYzt2MjqsEVAt54Zt5h2c8EAklAynbUVoBaEPZIH8YYntEjHvG9myEir5mkJ/6ZUnGusNqSY80+hB0BwiRy9MMeFcgcvAoGkN/i3DHoHi5lyCK8H3lS15e2hXPuPoIwKSi6EgzvWrgNQZ01038RhA==";
expect(samplar::LicenseSignature::verify(payload, sig, samplar::LicenseSignature::activePublicKey()));
expect(! samplar::LicenseSignature::verify(payload.replace("|PRO|", "|EDU|"), sig, samplar::LicenseSignature::activePublicKey()));
samplar::SignedLicense s;
expect(samplar::LicenseSignature::verifyStored(payload, sig, { "abcdef0123456789abcdef0123456789" }, juce::Time::getCurrentTime(), s));
expect(! samplar::LicenseSignature::verifyStored(payload, sig, { "OTHERMACHINE" }, juce::Time::getCurrentTime(), s));
```

**f. Not done (out of my file list).** `SamplarAudioProcessor::processBlock` has no licence check, so audio from a restored project plays while unlicensed (D16 note).

## 5. Server: what the user must do (order matters)

The brief assumed an env var. SM5 does not use one. Per-product signing keys are **files on the Render persistent disk**: `KEYS_PATH/<PRODUCT>/private_key.pem` (`KEYS_PATH` is already set on Render), plus a fingerprint pin in code. On Render (`RENDER=true`) a pinned key is never auto-generated, and a key that doesn't match its pin is refused.

**1. Upload the key (before deploying or shipping anything).**
- Private key: `C:/Users/Muzik/AppData/Local/Temp/claude/b--SANTI-VST-PROJECT-SAMPLAR/4dadef21-585f-41d5-bed7-0a9d8f807952/scratchpad/sampl4r_license_private.pem`. It is SECRET, was never printed, is outside every git repo, and has not been committed.
- Public key: `.../scratchpad/sampl4r_license_public.pem`.
- In the Render dashboard, open the license server, then Shell, and run:
  ```
  mkdir -p "$KEYS_PATH/SAMPL4R"
  cat > "$KEYS_PATH/SAMPL4R/private_key.pem" <<'EOF'
  (paste the private PEM)
  EOF
  chmod 600 "$KEYS_PATH/SAMPL4R/private_key.pem"
  ```
  Do the same for `public_key.pem`.
- Overwrite any existing files in that folder. The server may have auto-generated an unpinned SAMPL4R key there. No client embeds it, and replacing it doesn't affect existing licence keys: their 5-char suffix is only format-checked.
- Then move the private key file into your password manager and delete it from the temp folder.

**2. Deploy the server code.** Apply `scratchpad/license_server_sampl4r.patch` on `origin/master` with `git apply`. Run `pytest tests/test_samplar_product.py`. Then commit, push, and deploy.
- In the startup log, expect `[KEY] SAMPL4R signing key matches pin 46cfe00569377025`.
- `GET https://www.samplemonsta.com/auth/license-public-key/SAMPL4R` must return fingerprint `46cfe00569377025`.

**3. Ship SAMPLAR 1.0.1 only after step 2 checks pass.** The client is strict: with no key on the server, every SAMPLAR user is locked out ("The license server's reply could not be verified").

**Env vars.** None are required. `MIN_APP_VERSION_SAMPL4R` is optional (default 1.0.0, only enforced when `VERSION_GATE_MODE=enforce`).

**Follow-ups:**
- Revoke the test licence that was in `src/security/README.md`, from the admin panel. It is still in git history (`github.com/muziksociety/SAMPL4R`). Scrub the history if the repo is or becomes public.
- Confirm `LICENSE_TOKEN_SECRET` is set and rotated on Render (D2, server half). No shipped client (SM5, VARIA, RUIDO, ili) calls validate-token any more (confirmed by license-server-42, 2026-10-21), so rotation is safe.
- `samplar.download_url` in releases.json is a placeholder (`/portal/downloads`). The portal lists SAMPL4R downloads only if it is added to `_RELEASES_JSON_KEY_BY_CODE` (portal_context.py) with installer files. Set the real URL and release date before deploying.
- `origin/master` has `INTERNAL_ONLY_CODES` (core/branding.py). Consider adding `"SAMPL4R"` so portal cards show "SAMPLAR" instead of "SAMPL4R ·".

## 6. Migration for existing 1.0.0 users

A 1.0.0 cache holds `ACCOUNT:<email>`, a JWT stored as `signed_token`, no signed payload, and a session file containing the JWT.

**Startup (strict).** No signed licence, so SAMPLAR shows the in-window panel with "Checking your SAMPLAR license online..." and starts a background re-check. The token comes from the session, or from `signed_token` as a fallback.

**Re-check.**
- The server accepts the JWT (exp not enforced, up to 365 days old) and finds the SAMPL4R licence bound to this machine.
- The cached `.hardware_id` is unchanged, so the v1 ID matches. The binding also migrates to HWID v2.
- The server returns a signed payload. SAMPLAR verifies it and stores it, with the clock mark and the rolled token. It also drops `signed_token` and replaces the `ACCOUNT:<email>` key slot.
- The panel closes by itself. No browser is needed.

**Fallbacks:**
- Offline: "Could not reach the SAMPLAR license server..." with Try again.
- Token rejected: "Sign in with your SAMPLAR account".
- After one browser sign-in they are on 1.0.1's scheme.

**Afterwards.** Launches are offline-verified. Re-checks are silent. The licence works offline for 30 days (3 days for trial/promo).

**Update offer.** Once `releases.json` is deployed, 1.0.0 (reporting "1.2.2") is offered 1.0.1.

## 7. Manual test plan

Build Release, install, and run each in FL Studio, Reaper and Ableton (VST3), and in the standalone.

1. **Fresh machine.** Delete `%APPDATA%\Samplar\license_cache.json`, `sessions\`, `session.idx` and `HKCU\Software\MuzikSociety\Samplar\ix`.
   - Open the plugin: the UI is visible and dimmed, with the sign-in panel.
   - The host does not hang, and no modal window appears.
   - Sign in with a SAMPL4R account. The panel closes.
   - Log shows `verified: YES` and `Licence cached (signed)`.
2. **Relaunch the DAW.** No browser. Log shows `Cached signed payload: present, verified: YES`, then Valid.
3. **Offline.** Disconnect the network and relaunch: still Valid (within the window). Log shows `Re-check inconclusive - status kept: Valid`.
4. **SM5-only account.** Expect "This account has no SAMPLAR license..." (or the server's unreleased/beta message). Nothing is cached.
5. **Cancel.** Click Sign in, then Cancel sign-in: the button resets. Close the plugin window during sign-in, then finish in the browser: SAMPLAR still activates, with no crash.
6. **Timeout.** Leave the browser open for more than 5 min: "Sign-in timed out".
7. **Unload safety.** Remove the plugin within 2 s of opening it (re-check in flight), and also during sign-in. No crash, and unload takes under 1 s (log `http post: cancelling`).
8. **Two instances.** Open two instances. Sign in in one; the other's panel closes after its next `license.status`, or on reopen. No double sign-in on relaunch.
9. **Clock rollback.** Set the Windows date back 3 days and relaunch: locked with "Your computer's date looks wrong". Restore the date, click Try again: unlocked.
10. **Revocation.** Revoke the licence in admin. Set `lastVerified` older than 6 h (or wait), then relaunch: locked with "no longer active".
11. **Tamper.** Edit one character of `license_payload` in a copied cache: "license file could not be verified", and the cache is NOT deleted.
12. **1.0.0 migration.** Install 1.0.0, sign in, install 1.0.1 over it, launch online: the panel shows "Checking..." and closes without a browser.
13. **Settings, Sign out.** The account page opens, and the panel appears.
14. **Logs.** Grep `%APPDATA%\Samplar\logs\SAMPLAR_console.log` for `@`, `eyJ`, `code_verifier` and `token=`. Only masked emails may appear.
15. **No files.** No `oauth_debug.txt` on the Desktop.
16. **Firewall.** No Windows Firewall prompt (loopback-only listener).
17. **Update check.** Once releases.json is deployed, `https://samplemonsta.com/api/update/check?product=samplar&current_version=1.2.2` returns `update_available: true` with `latest_version: 1.0.1`.

## 8. Known risks

- **Not linked or run.** The MSVC `/Zs` check passed for every changed file, and the JUCE 8 APIs used were verified against `b:/SANTI/VST PROJECT/JUCE`. Link errors are expected only if the section 3 CMake edits are missing. Runtime behaviour still needs the section 7 tests.
- **Warnings.** New code uses `juce::Font(float, int)`, which raises the same C4996 deprecation warning as the code it replaced. There are no new error-level diagnostics.
- **Strict client, unprepared server.** If the client ships before the server key is uploaded, everyone is locked out (section 5 order).
- **Shell queries on the auth thread.** The legacy-ID recompute runs wmic/PowerShell for a few seconds on the auth thread. Unloading during that waits for it (bounded join, no force-kill).
