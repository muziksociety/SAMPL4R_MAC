# OAuth Version Gate — Plan

**Date:** 2026-04-12
**Target release:** SAMPLAR v1.2.2
**Source of truth for version:** `installer_sm5.iss` line 15 (`#define MyAppVersion "1.2.1"` → bump to `"1.2.2"`)
**Related server change:** already shipped to `license_server` master — see companion plan `b:/SANTI/license_server/docs/superpowers/plans/2026-04-12-oauth-version-gate-server.md` (will be created alongside)

---

## Why

The license server's `/auth/app-login` OAuth endpoint currently accepts OAuth requests from **any version** of the SAMPLAR/SM4 desktop app. This means:

1. Users running ancient builds can still log in even if those builds have known bugs or security holes.
2. We cannot deprecate old clients without a forced server-side kill switch.
3. We have no visibility into which versions are actively in the wild.

The fix is to make every SAMPLAR client **self-identify its version** on the OAuth URL, and have the server check it against a minimum-version floor that we can raise at any time without a code deploy (env var).

---

## Constraints

- **No regressions for current v1.2.1 users.** The server will roll out in `log` mode first (record which versions are connecting but do not block anyone). Only flipped to `enforce` after we ship a desktop build that sends `app_version` and give users time to auto-update.
- **No new dependencies.** Reuse JUCE primitives and the existing `MyAppVersion` macro chain.
- **Single-source-of-truth.** Version string must come from one place and propagate everywhere — installer, about-box, OAuth URL.

---

## Scope

| Layer | Change | Effort |
|---|---|---|
| SAMPLAR C++ client | 1 file touched (`OAuthBrowserAuthenticator.cpp`), 3 lines added. Plus a version header propagation. | ~10 min |
| Installer | Bump `MyAppVersion` from `1.2.1` to `1.2.2` | 1 line |
| License server | Already shipped: `app_version` param, `MIN_APP_VERSION` + `VERSION_GATE_MODE` env vars, log-mode gate | Done |
| Testing | Build debug SAMPLAR, run against sandbox or real server, confirm log line `[version-gate]` appears | ~15 min |
| Release | Build installer, upload to R2 via `r2-upload` skill, bump `releases.json`, push master, email announcement | ~20 min |

---

## Files to touch (pinpoint)

### 1. `src/networking/OAuthBrowserAuthenticator.cpp` — lines 111-114

**Current:**
```cpp
// 2. Build OAuth URL and open browser
std::string oauthUrl = serverBaseUrl_ + "/auth/app-login"
    "?callback_port=" + std::to_string(callbackPort_) +
    "&state=" + urlEncode(stateToken_) +
    "&hardware_id=" + urlEncode(hardwareId_);
```

**New (1-line addition):**
```cpp
// 2. Build OAuth URL and open browser
std::string oauthUrl = serverBaseUrl_ + "/auth/app-login"
    "?callback_port=" + std::to_string(callbackPort_) +
    "&state=" + urlEncode(stateToken_) +
    "&hardware_id=" + urlEncode(hardwareId_) +
    "&app_version=" + urlEncode(samplar::kAppVersion);  // NEW
```

### 2. `src/networking/OAuthBrowserAuthenticator.cpp` — top of file, after `#include`

Add `#include "../Version.h"` (see new file below).

Or, if we don't want a new header, hard-code via preprocessor: `#include <JuceHeader.h>` should already expose a project version macro from `JucePlugin_VersionString` or we can wire one through CMake. **Recommended:** create `src/Version.h` for clarity and stable propagation (one line in one header → used by OAuth + About dialog + anywhere else).

### 3. `src/Version.h` — **NEW FILE**

```cpp
// src/Version.h
// Single source of truth for the SAMPLAR version string.
// Keep this in sync with installer_sm5.iss MyAppVersion and the next
// SAMPLAR release tag. Consumed by: OAuthBrowserAuthenticator (sent on every
// OAuth request to the license server for version-gating).
#pragma once

namespace samplar {
    inline constexpr const char* kAppVersion = "1.2.2";
}
```

**Why not CMake-derived?** We *could* define this from `CMAKE_PROJECT_VERSION` via `configure_file`, but CMakeLists.txt currently reads `VERSION 2.0.0` which is out of sync with the real shipped version (`1.2.1`). Fixing CMake to match the installer is a separate issue. For now, a hand-maintained `Version.h` is the fastest safe path. Add a TODO comment to unify with CMake later.

### 4. `installer_sm5.iss` — line 15

```ini
#define MyAppVersion "1.2.2"
```

### 5. `src/services/LicenseSystemService.cpp` — line 1193

**No change required.** The call site already passes `serverUrl` and `hardwareId`; the version is picked up internally by `OAuthBrowserAuthenticator::startAuthentication` via the new `samplar::kAppVersion` constant.

### 6. (Optional) `src/networking/OAuthBrowserAuthenticator.h`

**No change required.** The public API of `startAuthentication(serverBaseUrl, hardwareId, callback)` stays the same. The version is baked in.

**Alternative (deferred):** pass the version as an explicit parameter to `startAuthentication` so it's visible at call sites. Cleaner for testability (can mock old version in tests) but requires touching both `.h`, both call sites, and updating the graphify index. Skip for this iteration.

---

## Step-by-step execution

### Step 0 — Verify the license server side is already deployed (prerequisite)

Before starting client work, confirm the server already accepts `app_version` query param in log mode:

```bash
curl -sI "https://www.samplemonsta.com/auth/app-login?callback_port=9876&state=test&hardware_id=$(printf '0%.0s' {1..64})&app_version=1.2.2" | head -5
# Expect HTTP/1.1 200 (or 302 if template renders) — NOT 422 validation error
```

If 422, the server doesn't yet accept the param. Stop and ship the server half first.

### Step 1 — Create `src/Version.h`

One-file addition, content above. Commit message: `feat: add samplar::kAppVersion single-source-of-truth for version string`

### Step 2 — Wire into OAuthBrowserAuthenticator

Add `#include "../Version.h"` and append the `&app_version=` to the URL builder at line 114.

Commit message: `feat: send app_version on OAuth login for server-side version gating`

### Step 3 — Bump installer version

Edit `installer_sm5.iss` line 15 from `1.2.1` to `1.2.2`.

Commit message: `chore: bump installer version to 1.2.2`

### Step 4 — Local build smoke test

```powershell
cd B:\SANTI\SAMPLE_MONSTA\sample_monsta4\Sample_monsta5
cmake --build build --config Debug -- /m
```

Run the built app, initiate OAuth login, watch the browser URL that opens. It **must** contain `&app_version=1.2.2` at the end.

If the URL is missing the param → check preprocessor expansion, make sure `Version.h` was included.

### Step 5 — Integration test against live server (log mode)

With the server in `VERSION_GATE_MODE=log`, the debug build should log in successfully **and** we should see a server log line like:

```
[version-gate] email=gabriel@example.com version=1.2.2 min=1.2.0 allowed=True
```

Via render-ssh skill:
```bash
ssh $RT "grep 'version-gate' $RP/data/logs/license_server.log | tail -10"
```

If `allowed=False` logs appear from other live clients → those are pre-1.2.2 builds still in the wild. Note their count. Keep `log` mode until the count drops to near zero.

### Step 6 — Release the desktop build

Use the `sm5-release` skill:

```
/sm5-release 1.2.2
```

This runs:
- CMake release build
- Code sign (if configured)
- Inno Setup installer
- Mac .pkg build
- Upload both installers via `r2-upload` skill
- Update `b:/SANTI/license_server/data/releases.json` with v1.2.2 changelog
- Bump download links in `plugins/user_portal/templates/dashboard.html`
- Commit + push to license_server master

### Step 7 — Announce via reminder email (optional)

Once v1.2.2 is live, reuse the bilingual reminder infrastructure we already built to nudge users on older versions. Dynamic targeting: query `broadcast_log` + server `[version-gate]` logs to build a recipient list of users whose most recent OAuth showed `app_version < 1.2.2`.

### Step 8 — Wait 48 hours, then flip server to enforce mode

Monitor the Render logs. When `[version-gate] allowed=False` count drops to < 5% of total logins:

```
# In Render dashboard:
VERSION_GATE_MODE = enforce
```

Redeploy. Any remaining old clients will see:

> **Please update SAMPLAR**
>
> Your version (1.2.1) is no longer supported. Please download the latest version (1.2.2) to continue.
>
> [Download for Windows](https://www.samplemonsta.com/downloads/samplar_Setup_v1.2.2.exe)
> [Download for macOS](https://www.samplemonsta.com/downloads/Sample_Monsta-1.2.2-macOS.pkg)

### Step 9 — Bump MIN_APP_VERSION going forward

Every new SAMPLAR release where we want to hard-deprecate old clients:

1. Build + release v1.2.X as normal
2. Wait 7-14 days for adoption
3. Set `MIN_APP_VERSION = 1.2.X` in Render env
4. Redeploy

No code changes needed — it's a single env var.

---

## What we are NOT doing (YAGNI)

- **Not propagating app_version through `/api/validate`, `/api/activate`, or `/api/licenses/validate-token`.** Those are already version-agnostic and changing them requires client changes on every validation path. For now OAuth-only gate catches 99% of "I just opened the app" scenarios. If we find old clients bypassing the gate by going directly to /api/validate with a cached key, we'll revisit.
- **Not building a "force auto-update" mechanism in the client.** That's a separate discussion (Sparkle on mac, WinSparkle on Windows, etc.) and out of scope for this plan.
- **Not fixing the CMake 2.0.0 / installer 1.2.1 mismatch.** Flagged as TODO in `Version.h` — one day we'll unify.

---

## Rollback plan

**Client side:** If v1.2.2 introduces a regression, release v1.2.3 with the fix. Since the server is in `log` mode during rollout, even broken clients will still log in.

**Server side:** If `enforce` mode locks out legit users:
1. In Render dashboard, flip `VERSION_GATE_MODE=log`
2. Redeploy (takes 60-120s)
3. All users can log in again
4. Investigate logs to see what version matched the block

**Data:** No data migrations. The server change is purely a new query param + env config. No DB schema change. Can be reverted at any moment.

---

## Success criteria

1. ✅ Fresh SAMPLAR v1.2.2 installer sends `app_version=1.2.2` on every OAuth login
2. ✅ Server logs `[version-gate]` line for every OAuth attempt
3. ✅ In `log` mode, no legitimate users are blocked
4. ✅ In `enforce` mode, pre-v1.2.2 clients see the update screen
5. ✅ MIN_APP_VERSION can be raised via env var without code deploy

---

## Checklist for execution

- [ ] Prerequisite: license_server `/auth/app-login` accepts `app_version` query param (server plan, shipped separately)
- [ ] Create `src/Version.h` with `samplar::kAppVersion = "1.2.2"`
- [ ] Edit `src/networking/OAuthBrowserAuthenticator.cpp` lines 1-20 to `#include "../Version.h"`
- [ ] Edit line 114 to append `"&app_version=" + urlEncode(samplar::kAppVersion)`
- [ ] Bump `installer_sm5.iss` line 15 to `"1.2.2"`
- [ ] Build debug, verify URL contains `app_version` param
- [ ] Test against live server (log mode)
- [ ] Check server logs for `[version-gate]` lines
- [ ] Run `/sm5-release 1.2.2`
- [ ] Wait 48h for adoption
- [ ] Flip `VERSION_GATE_MODE=enforce` in Render env
- [ ] Monitor support inbox for update-blocked user reports

---

## Open questions

1. **Should we also block /api/validate calls by version?** (Option C from earlier discussion — more invasive, catches cached-key bypasses)
2. **What's the minimum supported version for the first enforce cycle?** My recommendation: set `MIN_APP_VERSION=1.2.2` and give anyone older 2 weeks to update. Or `1.2.0` to be gentler.
3. **Do we want a Slack/Discord announcement before flipping to enforce?** Probably yes — a 48h heads-up on Discord helps.

---

## Graphify cache used

This plan was built using the existing `b:/SANTI/SAMPLE_MONSTA/sample_monsta4/Sample_monsta5/graphify-out/graph.json` index (3,719 nodes, 6,659 edges). Key nodes consulted:

- `OAuthBrowserAuthenticator.cpp :: startAuthentication()` L21
- `OAuthBrowserAuthenticator.cpp :: run()` L80 (where URL is built)
- `OAuthBrowserAuthenticator.h :: startAuthentication` L59
- `LicenseSystemService.cpp :: startBrowserAuthentication()` L1155 (caller)
- `installer_sm5.iss :: MyAppVersion` L15

No graphify re-extraction needed — existing graph is current as of 2026-04-11.
