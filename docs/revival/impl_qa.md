# SAMPLAR QA / release infrastructure - implementation report (2026-10-21)

Built only in `build_qa/` (`-DSAMPLAR_BUILD_TESTS=ON`). `build/` was only read (pluginval ran on a temp snapshot copy of its bundle). No git commit/stash/checkout.

## Summary

| Gate step (build_qa, `tools/run_all_checks.ps1 -BuildDir build_qa`) | Result |
|---|---|
| Build: SamplarPlugin_VST3, VST3SmokeHost and SamplarTests | green. The first attempt failed on another agent's in-progress MidiMappingService.cpp; the retry was green. |
| SamplarTests | PASS. 10 test classes, 256 checks, 0 failed, about 65-140 s. Most of that time is the real HWID wmic queries. |
| VST3SmokeHost single run | PASS |
| VST3SmokeHost --two-instances x3 | 1 of 3 FAILED: "B delete instance 5106 ms (budget 1500)". 6 reruns all passed. Root cause below. |
| VST3SmokeHost --ui-sweep | PASS |
| pluginval v1.0.4, strictness 10 | PASS, 0 failures. Ran on both `build/` (snapshot) and `build_qa/`. |
| Release linter (`--bundle build_qa/...`) | 5 of 8 checks clean. The 3 remaining findings are all genuine; see below. |

## 1. Tests (D21)
- **CMakeLists.txt** (only the test parts were touched):
  - Removed `tests/licensing/LicenseSystemTests.cpp` from `SAMPLAR_SOURCES`, so no test code ships in the plugin any more.
  - Replaced the old 0-test SamplarTests block with a pointer comment.
- **`cmake/extra_qa.cmake`** defines `SamplarTests`:
  - It compiles only the sources under test: EventBus, ServiceRegistry, AppState, LicenseSignature, LicenseCache, HardwareFingerprintGenerator, FeatureManager, SModeSegmentModel and HtdemucsIstft, plus pffft.
  - Built with `SAMPLAR_SELFTEST=1`, registered with CTest, and exits non-zero on any failure.
- **`tests/main.cpp`**: runner options are `--list`, a name filter, and `--verbose`. The log of the code under test goes to `%TEMP%/SamplarTests.log`.
- **New tests**:
  - `tests/licensing/LicenseSignatureTests.cpp`: the impl_licensing 4e vector, covering the positive case, tampered payload/signature/key, wrong machine, issued-in-future, verifyLive nonce and staleness, parseTime, makeNonce, and the test-key override.
  - `ClockGuardTests.cpp`
  - `HardwareIdBindingTests.cpp` (D14)
  - `LicenseSystemTests.cpp` (moved and sandboxed). It adds cache rejection for a wrong machine, a tampered file and a corrupt file.
  - `tests/app/{EventBus,ServiceRegistry,AppState}Tests.cpp`. ServiceRegistry covers init in registration order, shutdown and destruction in reverse, and re-register. AppState covers SMAS round trip, late-drain async, unregister-before-drain, AppState destroyed first, legacy JSON, and garbage payloads.
  - `tests/smode/SModeSegmentModelTests.cpp`: add/remove/resize, MIDI, undo/redo including effects, mute and tempo, 50-level cap, extra hooks, and contentGeneration.
  - `tests/utils/DataPathsTests.cpp`
  - `tests/separation/HtdemucsIstftTests.cpp`: port of SM5 453e7733 `tests/unit/HtdemucsIstftTests.cpp`. FFT vs naive iDFT is above 100 dB, in-place output is identical, and the envelope is re-derived.
- **`tests/TestHelpers.h`**: every licence file is sandboxed through `SAMPLAR_CACHE_DIR_OVERRIDE`. Tests never touch the real `%APPDATA%\Samplar`.
- **Finding** (src not owned, so documented in a test comment): a garbage `session.idx` decodes to `0 ^ kMask`, which is a huge high-water mark. ClockGuard then reports "clock rolled back" until an online `reset()` (`src/security/ClockGuard.h` readFile).

## 2. pluginval
- **`tools/run_pluginval.ps1`**:
  - Downloads v1.0.4 if `tools/pluginval/pluginval.exe` is missing. The binary is git-ignored.
  - Runs at strictness 10 by default, with `--timeout-ms` and a wall-clock kill.
  - `-Snapshot` validates a temp copy (without .map/.pdb/logs) so the build DLL is never locked.
  - Writes logs to `tools/pluginval/logs/`.
- **Results**: SUCCESS on both bundles, with 0 failures.
- **Observations, not failures**:
  - Cold open takes 6 s (ORT TLS preload, D18).
  - The plugin has 0 parameters.
  - The Steinberg vst3 validator was skipped because no `--vst3validator` path is set.

## 3. Two-instance unload failure (root cause, with log evidence)
- **Where**: `src/services/LicenseSystemService.cpp`.
  - `RevalidationWorker::run` (around line 225) calls `getLegacyHardwareIdCandidates()`, which runs wmic/PowerShell. The log shows `... END === 6 id(s) from 5 adapter(s) in 4724 ms`.
  - `stopBackgroundWork` joins that worker, with `kWorkerJoinTimeoutMs = 15000` (line 24).
  - An instance deleted during those ~4.7 s blocks the host for about 5 s.
- **Why it happens on every launch here**: the server's reply is "NOT signed with the SAMPL4R key" (log: `Re-check inconclusive ... InvalidKey`). The key has not been deployed yet, so the background re-check with legacy IDs runs on every instance start.
- **Fix suggestion** (src not owned): compute the legacy IDs in a cancellable child process, or skip them when the HWID v2 binding already exists. Also shorten the join on unload.

## 4. Release linter (`tools/release_linter`)
- **New `linter_common.py`**:
  - CMake parser covering CMakeLists.txt plus `cmake/extra_*.cmake`, with `set()` variables resolved (`${BEAT_THIS_DIR}`, `${CMAKE_CURRENT_LIST_DIR}/..`).
  - .iss parser.
  - Payload computed as the bundle minus the .iss Excludes.
  - Pinned sha256 values.
  - `--bundle` (or `SAMPLAR_LINT_BUNDLE`).
- **Each check retargeted from SM5 to SAMPLAR**:
  - `check_engines.py` is removed and replaced by `check_smode.py`. It checks that service-name lookups resolve to VST3 registrations, that the required services are registered, that the 9 S-Mode topics each have a publisher and a subscriber, and that the S-Mode sources are compiled.
  - `check_installer.py` checks `samplar.iss`:
    - [Files] source, recursesubdirs, and the Excludes `*.map`/`*.pdb`/`logs`.
    - The `#error` guards.
    - MyAppVersion matches CMake.
    - vc_redist is present.
    - UninstallDelete is set.
  - It also checks the bundle payload:
    - ORT and DirectML sha256 values.
    - `htdemucs_body.onnx` sha256, `beat_this.onnx`, and `samplar_bundle.marker`.
    - The licence files.
    - Forbidden files: map, pdb, logs, `htdemucs.onnx`, `providers_shared`, `tools/`, `BACKGROUNDS/`, `Samples/`, `data/*.json`.
    - Binaries named in the code.
  - `check_runtime_deps.py` walks the imports and delay-imports of SAMPLAR.vst3, recursing into onnxruntime.dll and DirectML.dll. dxcore.dll counts as "optional system", because it is only delay-loaded.
  - `check_hardcoded_paths.py`:
    - The allowlist is now a scrub list. `MuzikSociety` is excused only as a token, so "C:\Users\Muzik" in the same string is still caught.
    - Scans string literals only.
  - `check_staging_sync.py` now looks for:
    - Inputs newer than the binary.
    - Staged resources that differ from their repo sources.
    - A FileVersion that differs from the CMake version.
    - An installer output older than the bundle.
  - `check_sources.py`, `check_events.py` and `check_services.py`: services check VST3 registration vs owned-by-another-service. All three report stale allowlist entries.
  - `run_all.py`:
    - Has `--bundle`.
    - Prints findings per check.
- **Allowlists re-triaged against the current tree**: orphans 11 → 0, events 24 → 9 (with reasons), services 5 → 0, unshipped 9 → 1 (`htdemucs.onnx` legacy fallback).
- **Current state, `--bundle build_qa/...`**: OVERALL DRIFT with 3 findings, all genuine.
  1. PHANTOM: `src/audio/SamplePreviewCache.cpp` is still listed in CMakeLists `SAMPLAR_SOURCES` but was deleted on disk (another agent). The next configure fails.
  2. STALE BUILD: about 9 inputs changed after the link (other agents' in-progress `src/audio/engine/*` and `analysis/*`).
  3. STALE INSTALLER: `release_assets/installer/output/SAMPLAR-Setup-1.0.1.exe` (480 MB, dated May 26) predates every revival change but carries the 1.0.1 name. Never upload it; rebuild it.
- **Against `build/` (the bundle the .iss ships)**, there are additional genuine findings:
  - `Contents/data/patterns_with_rolls.json` (56 MB) and `latin_patterns.json` would ship. The staging script only cleans `x86_64-win/data`. Fixed for the next build by the extra_qa POST_BUILD cleanup.
  - The absolute PDB path `B:\SANTI\VST PROJECT\...\SAMPLAR.pdb` is baked into the binary. Fixed by `/PDBALTPATH:%_PDB%` in extra_qa.cmake, and verified clean on build_qa.
  - The D19 licences are missing until `build/` is rebuilt.

## 5. D14 hardware ID (`src/security/HardwareFingerprintGenerator.*`)
- **`.hardware_id` format**: it is now `v2:<id>:<tag>`, where tag = SHA-256(salt | id | machine key). The machine key is MachineGuid, falling back to JUCE's device ID. The tag is re-derived on every load.
- **Copied or forged file**: the bound file is ignored and the ID is recomputed from the hardware.
- **Legacy plain 1.0.0 file**: accepted only if it equals the licence-cache ID (which is device-encrypted) through migration, or one of this machine's recomputed v1 candidates (any adapter order). It is then upgraded to the bound format; otherwise it is ignored.
- **Other changes**:
  - The cache path honours `SAMPLAR_CACHE_DIR_OVERRIDE`.
  - The hidden attribute is cleared before a rewrite.
  - The unused `loadCachedHardwareIdFromDisk` was removed.
- **Tests and live check**: covered by HardwareIdBindingTests (9 cases). The live plugin log shows `Loaded machine-bound disk cache`.
- **Limit**: a determined attacker can still compute a tag for a target machine, because this is client-side. The real binding is the server-side HWID v2.

## 6. D19 notices
- **`NOTICE.md`** was rewritten:
  - Component table: ORT 1.19.2 DML (MIT), DirectML 1.15.1 (MS licence terms), Demucs weights (MIT, full text), Beat This! weights (CPJKU MIT, full text), beat_this_cpp, pocketfft, PFFFT, signalsmith-stretch/linear, JUCE 8 commercial with its bundled dependencies, and the Steinberg VST3 SDK licence.
  - A "not shipped" list.
- **`cmake/extra_qa.cmake` POST_BUILD** stages the following into `Contents/Resources/licenses/` (missing source = FATAL_ERROR):
  - NOTICE.md
  - The signalsmith, beat_this_cpp and pocketfft texts
  - JUCE LICENSE plus the VST3 SDK, zlib, png, jpeg, harfbuzz, sheenbidi, FLAC and Ogg texts
- **Verified** in the build_qa bundle (installer check clean).
- **User actions**:
  - Confirm the JUCE 8 commercial licence tier and the signed Steinberg VST3 agreement.
  - `LICENSE.txt` (EULA, not owned by me) section 7 still lists SoundTouch, PyTorch, OpenBLAS, Python and audio-separator. That section is stale.

## 7. Gate script
`tools/run_all_checks.ps1 -BuildDir build_qa [-SkipBuild] [-SkipPluginval]`:
- Steps: build, then SamplarTests, then smoke (single, `--two-instances` x3, `--ui-sweep`), then pluginval, then the linter.
- Each step has a timeout.
- Logs go to `<BuildDir>/qa_logs/<stamp>/`.
- Prints a summary table and exits 1 on any failure.

## .gitignore
- Added `build_qa/`, `build_analysis/` and `build_engine/`.
- Track `tools/run_*.ps1` (`!tools/run_*.ps1`).
- Ignore `tools/pluginval/`, linter reports and `last_*`.
- Added `!cmake/extra_*.cmake`: `*.cmake` was globally ignored, so every agent's `cmake/extra_*.cmake` was untracked.
