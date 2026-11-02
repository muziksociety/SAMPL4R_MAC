# SAMPLAR integration round (2026-10-21)

- **Build dir:** `build/`, configured with `-DSAMPLAR_BUILD_TESTS=ON`, VS2022 x64 Release.
- **Repository:** nothing committed, stashed or checked out.
- **Scripts:** the patch scripts are in this folder as `int_*.py`. Backups of the deleted or rewritten HWID files are in `int_backup/`.

## Gate results (final binaries)

| Check | Result | Evidence |
|---|---|---|
| build (Plugin, SmokeHost, SamplarTests, EngineTests, AnalysisTests) | PASS | 0 errors. No new warnings outside the pre-existing C4566 (emoji in old logs) and C4003 (`SAMPLAR_STEM_GUARD`). |
| SamplarTests | PASS | 12 classes, 469 checks, 0 failed. There are 12 classes now because HardwareIdBindingTests was deleted (item 7). |
| smoke single | PASS | `build/qa_logs/20261021_125301` |
| smoke `--two-instances` x3 | PASS | 0 of 3 runs failed |
| smoke `--ui-sweep` | PASS | 0 sweeps with failures. New states `notice_missing_*` and `notice_unavailable_*` at all 3 sizes. |
| pluginval, strictness 10 | PASS | failures=0 |
| release linter | 1 finding: **STALE INSTALLER** | Details below this table. |
| EngineTests (full) | PASS | 28 PASS, 0 FAIL (`int_engine.log`) |
| AnalysisTests (full, not `--quick`) | PASS | 84 checks, 0 failures (`int_analysis.log`) |
| `VST3SmokeHost --session-roundtrip tests/test_input.wav` | PASS | 38 PASS lines (`int_roundtrip2.log`), schema=2 |
| NEW `VST3SmokeHost --bpm-unload <dir>` x3 | PASS | Unload took 347, 296 and 365 ms (budget 1500). BeatThis was cancelled about 550 ms into inference (`int_bpm_final_*.log`). |

**Linter: STALE INSTALLER.** `release_assets/installer/output/SAMPLAR-Setup-1.0.1.exe` dates from May 26 and was built from pre-revival code (impl_qa already flagged it as genuine).
- I did not rebuild it: building a 1.0.1 installer from unreleased code is a release step.
- It must be rebuilt (or deleted) at release time and never uploaded.
- Every other linter check is clean. The dev-path finding my sweep string introduced is fixed.

**Round trip on an unlicensed machine.** This machine has no signed licence, so processBlock output is muted by the A5 gate. The smoke host now reads the plugin's debug ack `licence.txt`:
- When the gate is closed, it asserts that every note of A, B and C is exactly silent ("licence gate mutes every note: 9 silent").
- When the gate is open, it compares audio as before.
- Model, play settings, lanes and grid are still compared for identity.
- The audio-equality branch needs a licensed machine. It passed in impl_persistence before the gate existed.

## Items -> change -> evidence

### 1. SModePage uses ServiceLocator::Context
**Change:**
- `SModePage(EventBus&, const ServiceLocator::Context&)`. The old constructor delegates with `capture()`. MainWindow's `makeSModePage` now picks the new constructor.
- `registry_`, `appState_` and the MIDI-learn overlay registry come from `ctx_`. I removed the overlay's global fallback.
- SettingsDialog and its 3 panels take a Context. `SettingsButton::setContext(ctx_)` is set by RootComponent, and the dialog is closed when the button is destroyed, so it never outlives its instance.

**Evidence:** `grep "ServiceLocator::get(Registry|Bus|UIState|AppState)" src/ui src/plugin` finds only `SamplarAudioProcessor.cpp:95`, which is processor code, not UI.
- `capture()` is called only at construction: MainWindow `ctx_`, the legacy page constructor, and SettingsButton's default.
- `UpdateCheckService::getHardwareId` still uses the global registry. It is a service, not UI, and the machine ID is the same for every instance.

### 2. Cancellation
**Change:**
- **BPM job:** passes `AnalysisCancel(smodeService_->getJobCancelFlag())` (cancel-when-true; set first thing in `shutdown()`). A cancelled result is never applied. Logging moved to the message thread. The "..." animation is reset on every early return.
- **Key detection:** `KeyDetector::detect/computeChroma` gained `AnalysisCancel` and a `cancelled` field, polled every 32 frames.
- **Downbeat and stem decode:** already honour the flag (persistence work).
- **Spectral-colour job:** now cancellable as well (`StemWaveformComponent::computeSpectralColorsAsync(runner, cancel)`, checked every 256 slices). The first bpm-unload run showed this job, not BeatThis, made unload take 3.7 s.

**Evidence:**
- The new smoke mode builds synthetic stems with a 600 s drum stem, waits until the BPM job is running, waits 2.5 s more and confirms it is still running, then closes the editor and deletes the instance.
- Results: 296-365 ms (before the spectral fix: 3743 ms).
- The plugin log shows `[BeatThis] CANCELLED during inference (553 ms in)` and `shutdown done in 223 ms`.

### 3. SModePage state and notices
- **Dragged grid offset:** already written to the model (`setGlobalGridOffset`, UI-fixes work). It is saved through the session `gridOffset` and the UI-extra state.
- **POLY/MONO and 1SH/GATE:** `updateMonoPolyLabel()` reads the engine. It now also runs in the constructor, so a project restored without stems shows the right label. It already ran in `onStemsReady`.
- **Missing-audio notice:** the modal alert is replaced by the in-page `SModeNoticeBanner`. It sits under the top bar, the empty-state overlay is laid out below it, and it has **Locate song...** and **Dismiss** buttons.
  - Locate opens a file chooser and calls the new `SModeService::relinkMissingSource(file)`. That re-runs the restore with the new source under any file name; the saved chops are kept and the song is re-separated (cache used if present).
  - MainWindow's `showMissingAudio` now calls the page. The page constructor also shows the notice if the session is already MissingAudio.
- **Sweep checks:** banner placement, overlap, text fit and Locate-button presence. All pass.
- **PNGs I looked at:** `notice_missing_850x500` (two-line message, Locate and Dismiss, LOAD AUDIO below it), `notice_unavailable_1060x724`, and `loaded1_1060x724` (label "POLY 1SH").

### 4. Persistence, schema 2
**Change:**
- **SModeSession:** `kSchema=2`. `<play dawSync>` and `<cc channel cc target/>` children; `PlaySettings::dawSync/ccBindings`. The tolerant reader treats schema 1 as no dawSync and no CC, and drops invalid `<cc>` entries.
- **MidiMappingService:**
  - New `setDawTempoSync/isDawTempoSync`.
  - Bindings are now (channel, cc) -> **named target** instead of a `std::function`. API: `registerCcTarget/unregisterCcTarget`, `getCcBindings`, `setCcBindings`, `debugInjectController`. `beginCcLearn(target, apply)` keeps its signature.
  - A binding whose target is not registered is kept but inert.
- **Processor binding** saves and restores both, and its fingerprint includes them (dirty flag).
- **Page:** the DAW toggle goes through `setDawSync()` and the engine flag, and the page re-subscribes on reopen.
- **Slot speed:** already saved (B18).
- **Removed:** the superseded `smodePage` binding. The `smodeStemPitch/Mod` bindings are kept because they are harmless and old projects still restore pitch from them.

**Evidence:**
- New unit test "schema 2: DAW sync + MIDI CC bindings round trip; schema 1 reads with defaults" (9 checks).
- In the round trip, A is seeded with `dawSync=1 cc=[1:74:stem0.pitch 0:1:stem2.volume]`, and "B/C model + play settings == A" reports identical.

**Gap:** no UI arms CC learn yet (`beginCcLearn` has 0 callers, as before). The bindings persist and fire for any registered target.

### 5. Separation
- **S-M2:** `stemFiles_` is assigned inside the completion `callAsync`, on the message thread.
- **Runtime check:** `SModePage::startLoad` checks `FullSongService::isSeparationRuntimeAvailable()`. If it fails, the page shows "Separation unavailable - please reinstall SAMPLAR." and no job starts. A `workflow.error` with "reinstall", or with no runtime, shows the same notice instead of the modal alert.
- **Restore validation:** on the fast path, `loadStemAudio(files, validateCache=true)` runs `FullSongService::validateCachedStemFile()` on the worker, because it takes a cross-process lock with a wait of up to 15 s. Stale stems fail the load, which then re-separates.
- **Evidence:** round-trip C ("re-separate") passes with a validated cache hit in 109 ms.

### 6. Editor fails closed
`licenseService_ == nullptr` now shows a full-window "could not start its licence check - reinstall" panel and disables the root. The audio gate was already closed: a null flag pointer means silence.

### 7. Dead helpers deleted
**Deleted:**
- `LicenseCache::getAllCachedHardwareIds`.
- The whole v1 / `.hardware_id` subsystem in HardwareFingerprintGenerator:
  - `generateHardwareId` (v1), `verifyHardwareIdConsistency`, `gatherHardwareComponents`, and the CPU / MAC / board / UUID / disk wmic queries;
  - `computeBindingTag`, `getMachineBindingKey`, `getCacheFilePath`, `readCacheFile`, `computeV1Candidates`, `saveCachedHardwareIdToDisk`, `migrateFromExistingHardwareId`, `getAllMacAddresses`.
- `tests/licensing/HardwareIdBindingTests.cpp` (untracked) and its `extra_qa.cmake` line.

**Kept:** v2, `licenceMachineId`, `runBoundedCommand`.

**Updated:** `LicenseSystemTests` "Hardware fingerprint" now checks that the licence ID is 32 hex, stable, and equal to v2.

### Also fixed: review items found still open
- **L-M3:** `verifyLive` no longer rejects on local clock skew; the nonce proves freshness, and expiry is checked against the signed issue time. A cached licence that fails only because the clock is more than a day behind gets the "check your date" message (`LicenseSignature::isIssuedInFuture`). The signature test was updated: replies with the clock 2 days ahead or 3 days behind are accepted, and nonce or machine mismatches are still rejected.
- **U-M7:** the SequencerSlotComponent popup now uses `withTargetComponent`, `withDeletionCheck` and a SafePointer.

## review_round1 status

| ID | Status |
|---|---|
| A1 HIGH | FIXED (engine work: `switchSnapshotMidBlock` never raises ackSeq). EngineTests A1 cases PASS. |
| A2/U-H3 HIGH | FIXED. Context everywhere (MainWindow, SModePage, overlay, lanes and slots, SettingsDialog); see the grep under item 1. |
| U-H2 HIGH | FIXED. host.tempo is subscribed and unsubscribed on the page's own bus (`setDawSync`, destructor). |
| A3 MED | FIXED (engine). Stale triggers older than 150 ms are dropped, there is FIFO headroom, and voices release after a gap. EngineTests PASS. |
| A4/U-H1 HIGH | FIXED. BPM, key, downbeat, decode and spectral jobs are all cancellable; unload during 10-min BPM analysis takes about 0.3 s. Residual risks are listed below the table. |
| A5/L-M5 MED | FIXED. processBlock and processBlockBypassed are gated; the editor fails closed (item 6). A restore still loads the session while unlicensed, but it is silent; this is intended, so data is never lost. |
| A6 MED | FIXED (`ClockGuard::decode`, tests PASS). |
| L-H1 HIGH | FIXED (licensing2: outcomes plus signed denials; tests PASS). The server change still needs a deploy. |
| L-H2 HIGH | FIXED (bounded commands; no v1 queries; v1 code now deleted). |
| L-M1, L-M2, L-M4, L-M6 MED | FIXED (licensing2: epoch, v2-only binding, encrypted cache only, running-session enforcement, auto re-check). |
| L-M3 MED | FIXED in this round (see above). |
| S-H1 HIGH | FIXED (sep2: cross-process locks and leases, commit never replaces a valid entry; 3-process test PASS). |
| S-H2 HIGH | FIXED (sep2: `ORT_API_MANUAL_INIT`, guarded lazy bind; the no-DLL smoke test passes). |
| S-M1 MED | FIXED (sep2). |
| S-M2 MED | FIXED in this round (item 5). |
| S-M3 MED | NOT FIXED. ORT session creation cannot be interrupted, so unloading during separation model load still waits for it (3-5 s). |
| S-M4, S-M6 MED | FIXED (sep2). |
| S-M5 MED | FIXED. Cache hits are validated (sep2) and restored stems are validated (item 5). |
| U-H4 HIGH | FIXED (UI work: busy flag on the visible button, stale results dropped). |
| U-M5, U-M6, U-M8 MED | FIXED (UI work). U-M8 is also covered by the BPM early returns in item 2. |
| U-M7 MED | FIXED: StemWaveform (UI work) and SequencerSlot (this round). |
| U-M9 MED | FIXED (sep2: cache re-check after the lock, 60 s retry). |

**Residual risks for A4/U-H1:**
- BeatThis ONNX session creation (about 2.1 s, measured) cannot be interrupted, so an unload in that window waits for it. This is the same root cause as S-M3.
- The 20 s drain cap remains only for jobs that ignore the flag: auto-chop/transient, Quick Assign and exports, all of which take a few seconds at most.

## Notes / not done
- **CC learn:** no UI calls it yet (feature gap, pre-existing).
- **`SamplarAudioProcessor::lastHostBpm_`:** still unused. Pre-existing; left in place.
- **Stale installer:** rebuild it at release time.
- **Server:** deploy the licensing2 change.
