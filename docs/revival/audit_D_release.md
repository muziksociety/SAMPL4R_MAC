# SAMPLAR Audit D: Licensing / Security / Update / Release / Build Hygiene

Scope: SAMPLAR @ main (cf8066ba) + uncommitted 1.0.1 working tree, license_server @ 6c3bf5b.
Read-only audit. Nothing was built, edited, or fetched over the network.
Tags: VERIFIED = traced in code (plus log or server evidence where given). PLAUSIBLE = likely, but needs a runtime or production check.

Severity ranking: CRITICAL > HIGH > MEDIUM > LOW.

---

## D1 - CRITICAL - VERIFIED - Every launch (and every plugin instance) forces browser re-login

**Root cause chain**
1. Browser OAuth is the only sign-in path the UI can reach (see D15). On success the client stores the OAuth `token` URL param as `signed_token`:
   - `src/services/LicenseSystemService.cpp:1693-1699`
     ```cpp
     // OAuth delivers the signed token via the URL "token" param,
     // which is mapped to AuthResult::accessToken by the parser.
     ...
     if (!result.accessToken.empty())
         data->setProperty("signed_token", juce::String(result.accessToken));
     ```
   - `src/networking/OAuthBrowserAuthenticator.cpp:319` `result.accessToken = params["token"].toStdString();`
2. The server's `token` is a JWT (HS256, 24 h), not the HMAC `base64|sig` token:
   - `license_server/plugins/auth/__init__.py:2594` `access_token = create_access_token({"sub": email, "uid": user_id})`
   - `:2652-2655` legacy callback params: `"token": access_token`
   - The HMAC `signed_token` comes only from `POST /api/licenses/validate-token` (`plugins/license_api/license_service.py:967`). The OAuth flow never calls that endpoint.
3. On the next launch, `performStartupValidation()` takes the ACCOUNT branch and calls `SignedTokenVerifier::verify(JWT)`. A JWT has no `|`, so verification fails, and the code wipes the cache:
   - `src/security/SignedTokenVerifier.cpp:162-165` `rfind('|') == npos -> "malformed token: no separator"`
   - `src/services/LicenseSystemService.cpp:1082-1092` `"signed_token verification FAILED at startup ... forcing re-auth"` then `cache_->clearCache()`
4. Log evidence (`%APPDATA%/Samplar/logs/SAMPLAR_console.log:54-56`):
   - `[SignedTokenVerifier] malformed token: no separator`
   - `[License] signed_token verification FAILED at startup forcing re-auth`
   - `:141` callback query `token=eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...` (a JWT)
5. Each plugin instance owns its own `LicenseSystemService` (`src/plugin/SamplarAudioProcessor.cpp:308`) and runs startup validation. Every instance's startup therefore wipes the shared `%APPDATA%/Samplar/license_cache.json`. A project with two SAMPLAR instances can make the user sign in twice.

**Impact:** No customer can stay signed in, and offline use is impossible. This is the main support-load and refund driver.

**Fix:** Port SM5's current licensing stack, which already exists:
- `Sample_monsta5/src/networking/OAuthBrowserAuthenticator.cpp`: PKCE code flow, `/auth/token-exchange` with nonce, bind to 127.0.0.1.
- `src/security/LicenseSignature.*` and `LicenseSigningKey.h`: RSA-PSS public-key verify of `license_payload` / `signature_v2`.
- `LicenseServerClient` `/auth/revalidate`.

The server already supports this per product (`plugins/crypto_service/crypto_service.py:187 sign_license_v2`, `auth/__init__.py:2772 _sign_license_v2`, `/auth/revalidate`). It needs a pinned SAMPL4R key pair; get the public key from `/auth/license-public-key/SAMPL4R`.

Minimum hotfix if a port is too large for 1.0.1:
- Stop writing the JWT into `signed_token`.
- Store `result.licenseKey` (SAMPL4R-...) plus the JWT.
- At startup, accept an ACCOUNT entry within grace without HMAC.
- Revalidate in the background via `/auth/revalidate`.
- Never call `clearCache()` on a local verify failure.

---

## D2 - CRITICAL - VERIFIED (production-secret part PLAUSIBLE) - Offline token crypto is broken in both directions and forgeable

**Evidence**
- The client verifies with HMAC keyed by the embedded "verification key": `src/security/SignedTokenVerifier.cpp:178` `hmacSha256Hex(kSamplarVerificationKey, base64Payload)`, with `src/security/SamplarVerificationKey.h:34-35` `"<REDACTED-KEY>"`.
- The server signs with a different key, the raw secret: `license_service.py:220-225` `hmac.new(LICENSE_TOKEN_SECRET, base64_data, sha256)`. The "verification key" is `SHA256(secret)[:32]` (`license_service.py:231-236`).
- Because HMAC(K1) != HMAC(K2), a genuine server token can never verify. `activateWithAccount` (:471-479) and `revalidateCachedLicense` (:551-561) would always reject with "possible MITM" / "Invalid license signature".
- HMAC is symmetric. Anyone holding the embedded key (any customer with the binary) can mint `base64(payload)|HMAC(K_embedded)[:32]` tokens that this client **accepts**. So the verifier rejects every genuine token and accepts every forged one.
- Computed locally: `sha256("<REDACTED-DEFAULT-SECRET>")[:32] == <REDACTED-KEY>`. That string is the server's hardcoded fallback (`license_service.py:28 os.environ.get('LICENSE_TOKEN_SECRET', '<REDACTED-DEFAULT-SECRET>')`). So the shipped key was derived from the **default dev secret**. Either:
  - (a) Render production runs on the default secret, which is public in license_server source. Then validate-token tokens for every product (correction 2026-10-21, per license-server-42: no shipped client - SM5, VARIA, RUIDO, ili - calls validate-token; VARIA's only HMAC is a local cache-integrity check keyed from salt + device ID) are forgeable. PLAUSIBLE; verify with `GET /api/licenses/verification-key` on prod.
  - (b) Production uses a different secret, and the shipped key is simply wrong.
- `license_server/docs/SAMPL4R_DEV_HANDOFF.md:21` itself says "This key was generated against the local dev LICENSE_TOKEN_SECRET".
- Both docs state something false: `SamplarVerificationKey.h:12-20` and `src/security/README.md` ("an attacker who has it cannot forge tokens").
- Also inside `verify()`:
  - The server timestamp is in ms (`license_service.py:214 *1000`), but the client treats it as seconds (`SignedTokenVerifier.cpp:239`).
  - `expiresAt` is parsed but never enforced by any caller. `verified->` is used only at LicenseSystemService.cpp:576.

**Impact:** No real offline licensing. If (a) holds, there is a cross-product license-forgery hole on the server side.

**Fix:**
- Drop HMAC offline verification and use the RSA-PSS v2 signature from D1.
- Ensure `LICENSE_TOKEN_SECRET` is set on Render, then rotate it.
- Fix the README and header comments.
- Add a unit test with a server-generated token vector. One test would have caught D1 and D2.

---

## D3 - CRITICAL - VERIFIED - Update check can never work

**Evidence**
- Client: `src/services/UpdateCheckService.cpp:91` `currentVersion = samplar::kAppVersion` and `src/Version.h:14` `kAppVersion = "1.2.2"` (an SM5 value). Product is set at `:104` `.withParameter("product", "samplar")`.
- Server: `license_server/data/releases.json` contains only `['sample_monsta', 'simple_monsta_beta']`. `plugins/update_checker/__init__.py:75-85` returns `"error": "Unknown product: samplar"`.
- The log confirms this (`SAMPLAR_console.log:108-110`).
- Even after `samplar` is registered with `latest_version` 1.0.x, the client claims 1.2.2. `1.0.x > 1.2.2` is false, so an update is never offered.
- The license reminder is dead: the client parses `license_reminder` (`UpdateCheckService.cpp:261`), but the server sends `license_status` (`update_checker/__init__.py:194`). The server's HWID lookup is also not product-filtered (`:144-153`), so SM5 trial/expiry messages would leak into SAMPLAR if the key were fixed.

**Impact:** 1.0.1 and later can never be pushed to 1.0.0 users through the app.

**Fix:**
- Add a `"samplar"` entry to releases.json (pick one product id; see D17).
- Make `kAppVersion` come from CMake `PROJECT_VERSION` via `configure_file` (D4).
- Align the field name to `license_status` and filter the server lookup by product.
- Ship this in 1.0.1. 1.0.0 users still send "1.2.2", so to reach them the server needs a special case (for example, `samplar` + `current_version=1.2.2` treated as 1.0.0).

---

## D4 - HIGH - VERIFIED - No single source of truth for the version (8 values)

| Location | Value |
|---|---|
| `CMakeLists.txt:7` project + `:400`/`:648` juce targets (working tree) | 1.0.1 |
| `release_assets/installer/samplar.iss:28-30` default | 1.0.1 (overridden by `/DMyAppVersion`) |
| `release_assets/build_installer.ps1:50-56` | latest git tag (`v1.0.0`) unless `-Version` is passed |
| `CMakeLists.txt:23-43` -> `SamplarVersion.h` (standalone `getApplicationVersion`, `App.cpp:29`) | git tag = 1.0.0 |
| `src/Version.h:14` (OAuth `app_version` + update check) | 1.2.2 |
| `src/services/LicenseSystemService.cpp:25-26` `kClientVersion` / UA | 5.0.0 |
| `src/networking/LicenseServerClient.cpp:182` hardcoded UA | 5.0.0 |
| `VERSION.txt` | 2.0.0 |

`Version.h:3,8-10` comments are stale ("installer_sm5.iss", "CMake says 2.0.0"). The skill (`.claude/skills/samplar-release/SKILL.md:58-66`) lists only the three CMake spots.

**Fix:** Generate one header from `project(VERSION)` and use it everywhere. Have `build_installer.ps1` read the CMake version, not the git tag, and fail if the tag differs. Delete VERSION.txt or generate it.

---

## D5 - HIGH - VERIFIED - Release ships a full linker map; installer and plugin are unsigned

**Evidence**
- `CMakeLists.txt:758,763` adds `/MAP /MAPINFO:EXPORTS` to the Release plugin. The map lands in the bundle (`build/.../Samplar.vst3/Contents/x86_64-win/SAMPLAR.map`, 15.4 MB), and the installer copies the whole bundle recursively (`samplar.iss:83-84`).
- The v1.0.0 build log shows `Compressing: ...\x86_64-win\SAMPLAR.map`. The map has 1,895 `SignedTokenVerifier` / `LicenseSystemService` symbol lines, which makes cracking trivial. `desktop.ini` and stray `Contents\data\*.json` are shipped too.
- Signing: `Get-AuthenticodeSignature` reports `NotSigned` for both `SAMPLAR-Setup-1.0.0.exe` and `SAMPLAR.vst3`. There is no SignTool in `samplar.iss` or `build_installer.ps1`. Expect SmartScreen "unknown publisher" warnings and AV heuristics, made worse by D7, D18 and the TLS callback.

**Fix:**
- Write the map/PDB outside the bundle (`/MAP:<path>`), or add `Excludes: "*.map,*.pdb,desktop.ini"` in `[Files]`.
- Archive symbols privately.
- Add Authenticode signing (SignTool in .iss plus sign the .vst3 before ISCC).

---

## D6 - HIGH - VERIFIED - Debug files written to the customer's Desktop and to C:\Users\Public

**Evidence**
- `src/services/LicenseSystemService.cpp:172-174`: the destructor appends to `Desktop\oauth_debug.txt` every time a plugin instance is destroyed.
- `src/networking/OAuthBrowserAuthenticator.cpp:73,124,147,391-409`: also appends every ~10 s while waiting.
- The dev machine's `Desktop\oauth_debug.txt` holds 1,077 "~LicenseSystemService DESTRUCTOR" lines (110 KB).
- `src/utils/OnnxDelayLoad.cpp:21`: `C:\Users\Public\samplar_dll_load.log` is appended on every plugin load, including DAW scans, never truncated, and shared across users. The comment at `:20` claims `SAMPLAR_DLL_LOAD_TRACE=0` disables it, but no such check exists.

**Impact:** Unexplained files on every customer's Desktop look like malware or adware, and the Public log grows without bound.

**Fix:** Delete the Desktop writes. Gate the DLL log behind a real env check or `#ifndef NDEBUG`, and write it under `%LOCALAPPDATA%\Samplar\logs`.

---

## D7 - HIGH - VERIFIED (firewall prompt PLAUSIBLE) - OAuth loopback listener is exposed to the LAN, its state is predictable, and the callback is unauthenticated

**Evidence**
- `src/networking/OAuthBrowserAuthenticator.cpp:361-368`: `createListener(port, "")` with the comment "listen on all local interfaces". The server always redirects to `http://127.0.0.1:{port}` (`auth/__init__.py:2667`), so all-interfaces binding is unnecessary. SM5 already binds `"127.0.0.1"` (`Sample_monsta5/src/networking/OAuthBrowserAuthenticator.cpp:344`).
- `:502` `juce::Random random(juce::Time::currentTimeMillis())`: the CSRF state is seeded from the clock, so it is predictable.
- `:437-475` returns on the first `GET /callback` regardless of state. Any LAN host that hits port 9876-9900 aborts the user's sign-in with "Security verification failed" (DoS).
- `:316-326` trusts `license_key`, `email` and `expires_at` from the URL. `signature` is parsed but never verified. `LicenseSystemService.cpp:1632` checks only that the `"SAMPL4R"` prefix is present.
- Listening on INADDR_ANY inside a DAW typically triggers a Windows Defender Firewall "allow FL64.exe" prompt (PLAUSIBLE).

**Fix:** Port SM5's code flow: bind 127.0.0.1, use PKCE, state from `juce::Random::getSystemRandom()` or `BCryptGenRandom`, and get license data via `/auth/token-exchange` (signed v2), never from URL params.

---

## D8 - HIGH - VERIFIED code path, PLAUSIBLE crash timing - UpdateCheckService use-after-free

**Evidence**
- `src/services/UpdateCheckService.cpp:74-77` starts `std::thread([this]{ performCheck(); }).detach();`.
- `:129` uses `juce::MessageManager::callAsync([this]() {...})`.
- The destructor (`:35-39`) only calls `stopTimer()`, with no join, cancel flag or `WeakReference`.
- The network call has an 8 s connect timeout, plus reads, plus Render cold start. Removing the plugin, closing the project, or switching projects within roughly 5-60 s after load can write into a freed object or call into one, which crashes the DAW.
- `updateInfo_` and `licenseReminder_` are written on the worker thread and read on the message thread without synchronization.
- `getHardwareId()` (`:302`) goes through the global `ServiceLocator`, which can point at a different plugin instance.

**Fix:** Use a `juce::Thread` member joined in `shutdown()`, or `std::shared_ptr` / `WeakReference` guards in both lambdas. Pass the HWID in at construction.

---

## D9 - HIGH - VERIFIED - Release is not reproducible from git; required content is missing silently

**Evidence**
- `.gitignore:63 *.onnx`: `models/htdemucs.onnx` (290 MB) and `third_party/beat_this/models/beat_this.onnx` (83 MB) exist only on the dev disk.
- `CMakeLists.txt:177-181`: if `beat_this.onnx` is missing, CMake gives only a WARNING and sets `WITH_BEAT_THIS OFF`, so the build succeeds without tempo detection.
- The stage script (`CMakeLists.txt:824-849`) runs with `$ErrorActionPreference='SilentlyContinue'`, swallows every copy, and ends with `exit 0`.
- Pattern data comes from the gitignored, dated SM5 folder `SM5_Portable_2026-02-18/data/patterns_with_rolls.json` (56 MB). `data/patterns_with_rolls.json` does not exist (`.gitignore:14`).
- `Samples/` is gitignored (`.gitignore:291`), yet 66 wavs ship from it. 4 of the 13 BACKGROUNDS that ship are untracked. ffmpeg comes from `portable_cache/ffmpeg/ffmpeg.exe` (gitignored).
- ORT DLL search is hardcoded to `$RepoRoot/build/_deps` (`:831`), not `CMAKE_BINARY_DIR`.
- The "mandatory" model preflight exists only as prose in `SKILL.md:20-48`. `build_installer.ps1` has no check.

**Fix:**
- Move the preflight into `build_installer.ps1`: hash and size of both models, the data JSON, ffmpeg, the ORT DLL, and the file count in the bundle.
- Make the stage script `$ErrorActionPreference='Stop'` and fail on a missing required file.
- Make `WITH_BEAT_THIS` missing-model a `FATAL_ERROR` in Release.
- Version the model and data inputs (LFS or a pinned download with SHA256).

---

## D10 - HIGH - VERIFIED - Offline, grace and deactivation behavior is broken or misleading

**Evidence**
- Gates require exactly `Valid`: `src/plugin/SamplarAudioProcessorEditor.cpp:26` and `src/app/App.cpp:243` check `!= LicenseStatus::Valid`. `GracePeriodActive`, returned by the offline path at `LicenseSystemService.cpp:614-620`, still pops the sign-in dialog, which needs internet. The 30-day grace only works for the account path, and that path is broken by D1.
- The account path never revalidates online (`:1096-1123`). Revoked or refunded licenses keep working until 30 days after the last validation. Token expiry is never enforced (D2). The comment at `:1100-1104` says "Server has no token-refresh endpoint", but the server has `POST /auth/revalidate` (`auth/__init__.py:2745-2856`).
- OAuth timeout is `AUTH_TIMEOUT_MS = 60000` (`OAuthBrowserAuthenticator.h:107`). 60 s is too short for creating an account or resetting a password, especially with a Render cold start.
- "Deactivate" in Settings (`SettingsDialog.cpp:1170`) calls `POST /api/licenses/deactivate` (`LicenseServerClient.cpp:118`). No such route exists in license_api, so the server returns 404, and the client reports "License deactivated locally" (`LicenseSystemService.cpp:699-711`). The machine slot is never freed, so moving machines hits the machine limit. Status strings for limit or mismatch say "Contact support" with no portal link (`LicenseActivationDialog.cpp:396-397`).
- Key-path startup revalidation runs synchronously on the message thread inside plugin construction (`:1168-1169`, 8 s x 2 retries + 1 s).

**Fix:**
- Treat `GracePeriodActive` as licensed, with a banner.
- Background `/auth/revalidate` every N days; enforce token expiry.
- Raise the OAuth timeout to 5 min with a Cancel button.
- Implement deactivate via the user_portal machine route, or open the portal page.

---

## D11 - HIGH - VERIFIED - `portable_root.txt` overrides the installed bundle and survives uninstall

**Evidence**
- `src/plugin/SamplarAudioProcessor.cpp:74-104` "0. Portable pointer file (highest priority)" is checked **before** the bundle's own `Contents/x86_64-win` (`:106-120`).
- `src/app/App.cpp:92-129` still writes the pointer whenever the standalone exe has `data/`, `models/` or `python/` siblings.
- `SKILL.md:145` says it was "deprecated 2026-05-18".
- On the dev machine, `%APPDATA%\Samplar\portable_root.txt` points to `B:\SANTI\VST PROJECT\SAMPLAR\build\SAMPLAR_Portable\Samplar_Data`. Every smoke test of the installed VST3 on the dev box loads dev resources, which hides missing installer content (D9).
- `samplar.iss:108-110` deletes only `{app}`. `%APPDATA%\Samplar` stays behind: license cache, `.hardware_id`, `sessions/encryption.key`, logs, the pointer, and ~690 MB on the dev machine (stem_cache 544 MB + BACKGROUNDS 142 MB). The Public log and Desktop file also stay.
- The registry strategy (`SamplarAudioProcessor.cpp:122-147`) can never match: `InstallPath={app}` is the bundle root, which has no `data/`.

**Fix:** Check the bundle first and remove pointer support (or put it last and ignore it when the bundle has resources). Stop writing it in Release. Add `[UninstallDelete]` for `{userappdata}\Samplar\portable_root.txt`, and optionally offer to remove caches. Set `InstallPath` to the `Contents\x86_64-win` path.

---

## D12 - MEDIUM - VERIFIED (lockout PLAUSIBLE) - Shared SM5 OAuth signing key and SM5 version gate

**Evidence**
- `src/networking/OAuthBrowserAuthenticator.cpp:20` `signingKey = "sm5-oauth-sign-v1-a3f8c2d1e9b7"` is hardcoded and identical to SM5's.
- `app_version` is "1.2.2" (D4).
- The server has no SAMPL4R entry in `MIN_APP_VERSION_BY_PRODUCT` (`auth/__init__.py:333-340`). SAMPLAR therefore inherits SM5's global `MIN_APP_VERSION`. Once D4 is fixed and SAMPLAR reports 1.0.1, SM5 raising its minimum under `VERSION_GATE_MODE=enforce` would lock SAMPLAR out.

**Fix:** Add `"SAMPL4R": os.getenv("MIN_APP_VERSION_SAMPL4R","1.0.0")`. Treat the HMAC "attestation" as obfuscation only (it is extractable from the binary).

---

## D13 - MEDIUM - VERIFIED - PII and session data in Release logs

**Evidence**
- `LicenseSystemService.cpp:1623` logs the full email. `OAuthBrowserAuthenticator.cpp:339-340` logs the full email.
- `:182` logs the full OAuth URL, including state, HWID and app_signature (log line 136).
- `:447-448` logs the first 100 chars of the callback, including the JWT header and payload. The payload base64-decodes to `{"sub":"santiago@muziksociety.org"` (log line 141).
- `UpdateCheckService.cpp:97,107` logs the full HWID twice.
- `OAuthBrowserAuthenticator.cpp:537,568` (`exchangeCodeForToken`, currently unused) would log the full JSON body.
- There is no Release log kill switch, unlike SM5's `SM5_ENABLE_LOG=0` hardening (see the DEBUG skill).

**Fix:** Truncate or redact these values. Add `SAMPLAR_ENABLE_LOG` defaulting OFF in Release, or a reduced level.

---

## D14 - MEDIUM - VERIFIED - Hardware ID is trust-on-first-use from a plain file

**Evidence:** `src/security/HardwareFingerprintGenerator.cpp:69-87` returns `%APPDATA%\Samplar\.hardware_id` (64 hex chars, only hidden) whenever it exists, without re-checking hardware. Copying that file to another PC makes the server see the same machine, which bypasses the per-plugin machine limit (server commit 884eea6 "one machine per plugin across accounts"). `verifyHardwareIdConsistency()` exists but is called only from `isHardwareValid()`, and that is never used in the gates.

**Fix:** Use SM5's hwid v2 (`hardware_id_v2` / `app_signature_v2` fields the server already accepts), or recompute and compare components at startup.

---

## D15 - MEDIUM - VERIFIED - Production test license committed; dead key-activation path

**Evidence**
- `src/security/README.md` "Test key ... production server has this test key registered: <REDACTED-TEST-KEY>". It is PRO, unbound (`0000000000`), and valid until 2027-05-11, in a repo pushed to `github.com/muziksociety/SAMPL4R`.
- `activateWithKey` / `isSamplarKeyFormatValid` / `license.activate` are unreachable: no code publishes `license.activate` or `license.validate`.
- `FeatureManager` is never queried outside licensing.

**Fix:** Revoke the key on the server and scrub it from the README (history too, if the repo is or becomes public). Delete the dead key path and FeatureManager, or wire them up deliberately.

---

## D16 - MEDIUM - PLAUSIBLE - Modal sign-in inside the plugin editor constructor; dead-end unlicensed UI

**Evidence**
- `CMakeLists.txt:678` `JUCE_MODAL_LOOPS_PERMITTED=1` for the plugin. `SamplarAudioProcessorEditor.cpp:29` calls `LicenseActivationDialog::showModal` -> `runModalLoop()` (`LicenseActivationDialog.cpp:148-150`) from inside host `createEditor()`. That is a re-entrancy and hang risk in hosts (FL, Ableton, Reaper).
- Cancel leaves a 400x200 "SAMPLAR / License required." panel with no button (`SamplarAudioProcessorEditor.cpp:36-37,77-88`). The user must close and reopen the editor.
- `processBlock` has no license check (`SamplarAudioProcessor.cpp:451`). Whether restored state plays without the editor is unverified.
- `LicenseActivationDialog.cpp:103-107` uses `callAsync` with raw `this` (UAF if the dialog closes first).

**Fix:** Use a non-modal in-editor overlay (`showAsync` already exists at `:158`) with a "Sign in" button, plus `SafePointer` in the async lambdas.

---

## D17 - MEDIUM - VERIFIED - Rebrand is inconsistent and leaves no purchase path

**Evidence**
- The 1.0.1 diff changes the dialog to "Sign in with your SAMPLAR account" and removes every "samplemonsta.com" purchase hint (`LicenseSystemService.cpp:461,1641`; `LicenseActivationDialog.cpp:48,56,77,395`).
- The browser still opens a Sample Monsta-branded page: `core/branding.py:31-60` has no SAMPL4R brand and `get_brand` falls back to Sample Monsta.
- The forgot/create links still go to `samplemonsta.com` (`LicenseActivationDialog.h:72-74`).
- "Purchase a SAMPLAR license" has no URL. I found no SAMPL4R checkout in paddle_service; product_code comes from checkout `custom_data`, so it may exist as a link (PLAUSIBLE).
- Product id spellings: `samplar` (update check), `sampl4r` (OAuth / validate-token), `SAMPL4R` (server DB / license keys). The `Sm5LookAndFeel` class name and log strings also survive.

**Fix:** Add a SAMPL4R brand on the server, a "Buy SAMPLAR" URL button, and a single product id constant.

---

## D18 - MEDIUM - PLAUSIBLE - LoadLibrary inside a TLS callback (loader lock)

**Evidence:** `src/utils/OnnxDelayLoad.cpp:161-174` calls `LoadLibraryExW(onnxruntime.dll)` at `DLL_PROCESS_ATTACH` from a TLS callback, under the loader lock. Microsoft documents this as a deadlock hazard. It also loads the 11 MB ORT on every DAW plugin scan. The delay-load notify hook (`:105-121`) already solves the System32-stub problem on its own.

**Fix:** Remove the TLS preload and rely on `__pfnDliNotifyHook2` (`dliNotePreLoadLibrary`).

---

## D19 - MEDIUM - PLAUSIBLE - Third-party notice gaps

**Evidence**
- `tools/ffmpeg.exe` (148 MB, likely a GPL build) ships in the installer but is not mentioned in `NOTICE.md` or `LICENSE.txt`.
- `LICENSE.txt` lists PyTorch and OpenBLAS (not shipped).
- `NOTICE.md` lists Loop Tempo Estimator as GPL-2.0, but its vendored LICENSE is GPLv3. It is linked (`CMakeLists.txt:716-718` `target_link_libraries(SamplarPlugin PRIVATE loop-tempo-estimator)`) but unused (`LteWrapper.h` is included nowhere), and the map has 0 LTE symbols, so it is effectively stripped.

**Fix:** Add the ffmpeg license and build source, or use an LGPL build. Remove the LTE link. Correct LICENSE.txt.

---

## D20 - LOW - VERIFIED - Repo bloat and junk tracked in git (29,839 files / 256 MB)

**Evidence**
- Unrelated or unused trees:
  - `openclaw/` (4,710 files, 40 MB; a different project)
  - `third_party/libtorch` (9,391 files, 41 MB)
  - `third_party/demucs.cpp` (36 MB)
  - `third_party/vcpkg` (9,499 files, 13 MB)
  - `third_party/openblas`
  - None of these are referenced by CMake (SAMPLAR uses ONNX).
- Root junk:
  - 4 UUID-named PNGs (~11 MB)
  - `loading_transparent.apng` (7.7 MB), `loading animation.mp4` (6.9 MB), `monsta.gif` (6.6 MB), other loading gifs and sprites, `backdrop.png`, `logo.png`
  - `.axon/kuzu` (3.4 MB)
  - `.sm5_weak_nodes.json`, `.sm5_suspicious.json`, `SERVICELOCATOR_FEASIBILITY.json`
  - `test_batch_ui.cpp`, `test_progress_implementation.cpp`, `copy_fix.ps1`, `fix_file.ps1`, `000portable.ps1`, `001installer.ps1`
  - stale `installer_samplar.iss` (v1.3.0, Program Files layout)
  - `render.yaml` (SM5 license-server deploy), `license_server/app.py`
  - `VERSION.txt` (2.0.0)
  - Untracked on disk: `nul`, the SM5 zip/rar, `test_progress_implementation.exe`, CSV traces.

**Fix:** `git rm -r --cached` the unused trees and root junk, and extend `.gitignore`. Consider `git filter-repo` before any public release.

---

## D21 - LOW - VERIFIED - Build and test hygiene

**Evidence**
- `CMakeLists.txt:324` compiles `tests/licensing/LicenseSystemTests.cpp` **into the shipping plugin** (as a static `juce::UnitTest`).
- The `SamplarTests` target (`:868-882`) compiles only `tests/main.cpp` + `EventBus.cpp`, so it runs 0 tests and always passes.
- `tests/mentor/MentorServiceTests.cpp` and the Python tests are SM5 leftovers.
- Orphan source: `src/ui/Debug/GrayDetectionDebugDialog.cpp` (not in CMake, included nowhere).
- `src/deprecated/` (Chat*) is not compiled (OK).
- `SamplarLicenseCleaner` is always built (`:854-860`).
- No test covers `SignedTokenVerifier`, the OAuth parse, or `UpdateCheckService` parsing.

**Fix:** Move LicenseSystemTests into SamplarTests and add verifier/update-parse tests. Delete the orphans.

---

## D22 - LOW - VERIFIED - CLAUDE.md and the release skill are inaccurate

**Evidence**
- `CLAUDE.md` describes the `Sample_monsta5/` tree, `src/services/variation/engines/*` (directory does not exist), `GeneratorService` / `VariationService` / `DrumSliceService`, `ui/WaveEditor`, `ui/Games`, and the `SM5App_artefacts/.../Sample Monsta 5.exe` run path.
- The **uncommitted** CLAUDE.md diff appends an unrelated "context-mode" tool-routing block. Keep it out of the 1.0.1 release commit.
- `SKILL.md:116` says `git push origin master`, but the branch is `main`. `:58-66` lists 3 version spots instead of 8 (D4). `:145` claims portable_root is deprecated (it is not; D11).

**Fix:** Regenerate CLAUDE.md from the real `src/` tree and update the skill.

---

## D23 - LOW - VERIFIED - Installer nits

**Evidence**
- ISCC warnings: `x64` and `{cf}` are deprecated (use `x64compatible` and `{commoncf}`).
- `SetupIconFile=` is empty.
- `vc_redist` runs unconditionally on every install (`samplar.iss:95-98`, no `Check:`).
- The installer has no `MinVersion`, and no check for a running DAW holding the .vst3 (relies on Restart Manager defaults).

**Fix:** Address with D5 and D11.

---

## Top recommended order for 1.0.1
1. D1 + D2: port SM5's v2 licensing (PKCE + RSA-PSS + `/auth/revalidate`), or at minimum stop storing the JWT as `signed_token`. Confirm the prod `LICENSE_TOKEN_SECRET`.
2. D3 + D4: single version source, `samplar` entry in releases.json, server special case for "1.2.2" clients.
3. D6 + D5: remove Desktop/Public writes, exclude `.map`, sign binaries.
4. D7 + D8: bind 127.0.0.1 + secure state; fix the UpdateCheck thread lifetime.
5. D9 + D11: fail-hard preflight in `build_installer.ps1`, bundle-first resource lookup, uninstall cleanup.
