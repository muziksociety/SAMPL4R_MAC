# Round 2 fixes: every review_round2 finding (2026-10-21)

- **Build:** `build/`, VS2022 x64 Release, `-DSAMPLAR_BUILD_TESTS=ON`.
- **Repository:** no git commit, stash or checkout. Server edits are local only, nothing deployed.
- **How the work was split** (separate build dirs, disjoint file ownership, final integration in `build/`):
  - Server: `license_server`.
  - Engine: `build_engine`, reported in `r2_engine.md`.
  - Licensing client: `build_lic`, reported in `r2_licensing.md`.
  - Persistence, separation and S-M3: `build_persist`, reported in `r2_persist.md`.
  - UI, MIDI learn, N-2 page side, N-19, N-20, N-21 page side, N-24 inspector, N-18 UI JSON: done by me in `build_ui`.
- The detailed file:line evidence for each workstream is in the three `r2_*.md` files in this folder.

## Verification (final binaries in `build/`)

| Check | Result | Evidence |
|---|---|---|
| `cmake -B build ... -DSAMPLAR_BUILD_TESTS=ON` + build of Plugin, VST3, SmokeHost, SamplarTests, EngineTests, AnalysisTests | PASS | 0 errors. Only the pre-existing C4566 / C4003 warnings. |
| `run_all_checks.ps1 -BuildDir build` (final) | **PASS, 7/7** | `build/qa_logs/20261021_145602`, details in the next table |
| EngineTests (full) | PASS | 42 PASS / 0 FAIL, up from 28 (`r2_engine_final.log`) |
| AnalysisTests (full) | PASS | 92 checks / 0 failures (`r2_analysis_final.log`) |
| `VST3SmokeHost --session-roundtrip tests/test_input.wav` | PASS | 39 PASS lines, schema=2, ALL PASS (`r2_roundtrip_final.log`) |
| `--bpm-unload` x3 | PASS | Unload in 290, 318 and 245 ms (budget 1500) (`r2_bpm_{1,2,3}.log`) |
| Server pytest (`python -m pytest tests -q`) | PASS | 621 passed / 0 failed, up from 611 |

**Final gate details:**

| Step | Result | Detail |
|---|---|---|
| build | PASS | |
| SamplarTests | PASS | 13 classes, 690 checks, 0 failed (was 12 / 469) |
| smoke single | PASS | |
| `--two-instances` x3 | PASS | 0 of 3 failed |
| `--ui-sweep` | PASS | Includes the new MIDI menus (below) |
| pluginval strictness 10 | PASS | failures=0 |
| release linter | PASS | CLEAN, 0 findings |

### Open item: intermittent slow unload of the last instance (not fixed; evidence below)
Two of the five gate runs had a slow last-instance unload:
- **Gate 1:** two-instances run 3, "B delete instance" 1667 ms. Same gate, ui-sweep "delete instance" 4200 ms. The machine was under heavy load: ui-sweep took 113 s and pluginval 71 s, against 48 s and 23 s normally.
- **Gate 2:** ui-sweep unload took 3609 ms.

Gates 3, 4 and 5 (one of them straight after a forced rebuild) passed, as did 13 standalone smoke runs (unload 29-328 ms).

What the temporary timestamps showed (in the destructor and around the host's `reset()`, since removed):
- SAMPLAR's own destructor body, including the shutdown of every service, always took **13-41 ms**.
- Every service logs a shutdown time under 10 ms.
- So the extra time is spent **after** `~SamplarAudioProcessor`'s body. That is JUCE's VST3 wrapper / last-instance global teardown (DeletedAtShutdown, MessageManager, TimerThread `stopThread`; 3.6 to 4.2 s is close to JUCE's 4 s stopThread timeout).
- No SAMPLAR member can block at that point.

This needs a profiler run (ETW / WPR) on a loaded machine. The gate does not catch it reliably.

## MIDI-learn UI: PNGs inspected
- `ui_sweep/midimenu_pitch_bound_1060x724.png`: the menu opens under the Vocals pitch knob, with the header "MIDI: Vocals pitch" and the items "MIDI Learn" and "Clear MIDI (CC 74 ch 1)". The status line reads "MIDI: self-test pitch <- CC 74 ch 1".
- `ui_sweep/midimenu_insp_bound_1060x724.png`: the menu opens under the inspector's GAIN control, with the header "MIDI: Selected chop gain" and the items "MIDI Learn" and "Clear MIDI (CC 7 ch 2)". GAIN shows 6.0 dB after the injected CC = 127.
- **Unbound variants:** "Clear MIDI" is disabled.
- **Sweep steps (all PASS):**
  - pitch menu, unbound and bound;
  - pitch learn (semi=6.00, knob=6.00, info "CC 74 ch 1");
  - inspector menu, unbound and bound;
  - inspector learn (vol 0.000 -> 1.995, info "CC 7 ch 2");
  - clear (no bindings left; tooltips back to normal);
  - the 4 menu sweeps (fail=0).
- **Bug the first gate caught, now fixed:** `ChopInspectorStrip::setFromController` checked `isEnabled()`. That is false under the licence-locked root, so inspector CCs were ignored. It now checks the strip's own envelope flag.

## Findings: status and evidence

| ID | Status | Change + evidence |
|---|---|---|
| **N-1** (server) | FIXED | **Server:** a new `DatabaseUnavailableError` (`core/db_errors.py`) is raised by `get_user_by_email`, `get_license_machines` and `get_licenses_by_user_id`. A failed bind returns `db_error`. `_revalidate_core`, family pickup, login-with-hardware and validate-token map it to an **unsigned 503** with `Retry-After: 60` and the body `{"error":"service_unavailable","reason":"service_unavailable","cause":"db_unavailable"}`. `reason` keeps the documented contract; `cause` carries the DB reason. A global handler in `app.py` catches every other endpoint. Genuine not-found answers still get signed denials. 10 new tests in `tests/test_revalidate_db_unavailable.py`. **Client:** the first signed denial only locks (licence and token kept, pending marker persisted) and re-checks in about 60 s. A second denial at least 50 s later wipes. `testPendingDenial` 18/18. |
| **N-2** | FIXED | **Page:** the `smodeStemPitch` / `smodeStemMod` page bindings and their members are deleted. Knobs mirror the service (they are initialised when created, then synced on the 30 Hz timer with no drag fight). **Processor:** both nodes are marked superseded (never written again). `selectPlaySettings` makes `<play>` win and reads the legacy nodes only when `<play>` is absent, through the clamping helpers (NaN, inf, out-of-range enums). Tests: "N-18 legacy-only ..." 27 checks, "`<play>` wins over stale legacy" 3. |
| **N-3** | FIXED | `fastReleaseVoice` (3 ms) handles chokes, steals, cell ends (which now end exactly at the cell end), pool stops and the stretch tail. `relLen` is used only for note-off and CC123. 4 slots are kept as headroom. The last-resort cut hands the voice's last sample to a 1.5 ms declick ramp. EngineTests: 16th lane with 5 s release stays monophonic (forcedKills=0); a 48-note roll triggers 48/48 with no starvation; an exhausted pool has a max step of 0.015. |
| **N-4** | FIXED | `sanitizeHostInfo` runs in both the processor and the engine: BPM must be finite, > 0 and is clamped to 20..999; PPQ must be finite with \|ppq\| <= 1e7. The loop is bounded, comparisons are done in double before any cast, and `lastTrigTick_` resyncs. EngineTests: 14 garbage cases (NaN, inf, 1e300, 1e18, 0, negative), worst block 0.03 ms, output finite. |
| **N-5** | FIXED | Staleness is measured from the local receive time, and the skew is stored. A skew over 1 day means "check your date" with a locked state, and retries back off 1/5/15/60 min (never zeroed). The ClockGuard mark is never set ahead of local time. `testClockSkewNoFlapping` 25/25, `testClockAhead` 13/13. |
| **N-6** | FIXED | The rollback check also uses an in-memory `maxSeenNowMs_`. `ClockGuard::decode` uses a fixed range (2024 to build year + 30), and `advance` never lowers the mark. `testRollbackMaxSeen` 7/7, `testClockGuardFixedRange` 9/9. |
| **N-7** | FIXED | A process-wide `std::atomic<uint64_t>` licence generation (trivially destructible, so safe at DLL unload) is bumped by sign-in, re-check, wipe and sign-out. Every instance compares it on each tick and reloads. Locked-with-no-token instances also poll the cache. `testNotifierTwoInstances` 9/9. |
| **N-8** | FIXED | `SModeService` `onSeparationError`: `if (!ours) { log; return; }` before touching `active_` or publishing. |
| **N-9 / N-22** | FIXED | Every cache-lock wait reachable from unload is cancellable. A validate timeout or cancel returns false. Prune only renames to `.evict-*` under the lock and deletes after unlocking. A shutdown drain timeout leaks the pool instead of destroying it. AnalysisTests: "validate blocked on the cache lock is cancelled in 153 ms". `--bpm-unload` 245-318 ms. |
| **N-10** | FIXED | A content fingerprint (FNV of length + first and last 64 KB, saved as `sourceFp`, schema-tolerant), with a size fallback and a duration check. On a mismatch nothing is applied, the pending tree is kept verbatim, and the banner says "Different audio - chops may not line up ... (N shortened, M removed). Your chops are kept. Choose the same file again to use it anyway." Choosing the same file again confirms. `applySessionToModel` reports clamped and dropped counts. Test "N-10 ..." 16 checks. |
| **N-11** | FIXED | `StemSnap::sourceId` gives a stable identity (generation + model buffer, set before the resample override). EngineTests N-11 x2 PASS. |
| **N-12** | FIXED | The engine `lifecycleGen_` is bumped by prepare and release. The MidiMappingService timer clears both UI flags, publishes `smode.transport.stopped` (already handled by SModePage) and pushes Stop commands. EngineTests N-12 PASS. |
| **N-13** | FIXED | The stale threshold is max(150 ms, 3 x the largest of the last two block durations); NoteOffs are never dropped. EngineTests: at 8192 samples, a 190 ms old trigger plays and a 2 s old one is dropped. |
| **N-14** | FIXED | `dispatchPending_` and `dispatchSeq_` are set inside the lock scope that swaps `root_`. Both snapshot functions take the lock and then check the flag. A 300-round stress test passes, with the caveat that it was not re-run against the old code. |
| **N-15** | FIXED | Suppression is a thread_local per-AppState stack. The watcher moves its baseline only after `markDirty()` returns true. During a restore only the session part is re-baselined, so play edits and relinks still dirty the project. Test "off-thread save never swallows markDirty" PASS. The watcher itself is covered only by the round trip. |
| **N-16** | FIXED | UpdateCheckService gets the machine ID on the message thread (the process-wide `licenceMachineId()`) and passes it by value to the worker. The global registry lookup is removed. |
| **N-17** | NOT FIXABLE CLIENT-SIDE, decision needed | The server has no `hwid_v2` column (`hardware_id` + `hwid_version`), and the client sends only the v2 ID. A machine bound by 1.0.0 with the v1 ID would get a signed "unbound" denial; with N-1 that is now a soft lock first, then a wipe. Production could not be checked locally. Before release, query the production `license_machines` table for SAMPL4R rows with `hwid_version=1`. If any exist, either migrate them server-side or restore the client's v1 computation (backup in `int_backup/`) and send it in `legacy_hardware_ids`, which the server already migrates. |
| **N-18** engine items | FIXED | Offline exports refuse while the gate is closed (checked before and after the render). The gate flip is a 5 ms ramp, and the standalone path is now gated too. Triggers drained during bypass are ignored. Locate tolerance is 10 ms. The CPU spike is limited by `kMaxStretchEntriesPerBlock`=8 (trade-off: with 80 voices the last ones reach the new pitch up to about 116 ms late). EngineTests x4 PASS. |
| **N-18** encryption key | FIXED | An empty, short or garbage key is regenerated, with atomic create (hard link) or replace (rename). `testEncryptionKeyRegen` 29/29. |
| **N-18** UI JSON | FIXED | `restoreUiExtraState` handles `lockedBpm` / `displayBpm` (non-finite -> 0, else clamped to 20..999), `lockSemis` (clamped to -24..24, and 0 when unlocked), `gridOffset` (non-finite ignored, else clamped to 0..3600 s) and `removed` (only 0..MAX_STEMS-1). Lane restore snaps bars and subdivision to valid values, clamps BPM, and makes slot fields finite and in range. |
| **N-18** missing `smode` node | FIXED | `StateBinding::restoreWhenMissing` resets the session to default play settings when the node is absent. AppState test 7 checks. |
| **N-19** | FIXED | New pure function `rescaleLaneSlots` (`src/ui/Components/smode/LaneGridRescale.h`). It sorts by (new cell, filled first, old cell, index). Same-cell collisions merge deterministically: the earliest filled slot stays, the rest are reported and logged one by one. An overlap shortens the earlier slot instead of dropping the next. Out-of-range slots are reported. Upscaling, which includes the pre-B18 migration, is lossless. The subdivision change pushes undo first and shows "N slot(s) merged ... (Ctrl+Z to undo)". The restore path logs the migration and normalises same-grid lanes. `tests/smode/LaneGridRescaleTests.cpp`: 7 tests, 40 checks PASS, including the exact review scenario. |
| **N-20** | FIXED | `autoChopTicket_` / `quickAssignTicket_`: only the newest job clears busy or resets the button, and a new song or eject bumps both. A downbeat that arrives during a chop job is deferred and re-runs once that job's result lands; dropped results never re-chop. |
| **N-21** | FIXED | The up-front refusal in `SModePage::startLoad` is removed; it now only logs whether the runtime is available. The service serves cache hits before `ensureOrtApi`. MainWindow shows the reinstall notice only for `FullSongService::isReinstallError(err)`. |
| **N-23** | FIXED | A failed commit is retried once, then copied to a stable `<key>.copy-*` entry with `entry.json`. If that fails too, the result is a clear error and the partial is deleted. `validateCachedStemFile` rejects `.partial-*` and `.evict-*`. AnalysisTests x3 PASS. |
| **N-24** inspector | FIXED | `findInspectorSegment`: exact range first, then a search by range. The resize fallback requires the same index, selected, **and** overlap with the inspected range, so the inspector never jumps to another chop after a delete. |
| **N-24** lease | FIXED | If the lease fails, a heartbeat (`.alive` create and delete each chunk) keeps the partial's mtime fresh, so prune never deletes a live run. |
| **N-24** stall recovery | FIXED | Skipped when `host.offline`. |
| **S-M3** | FIXED (documented) | `createOrtSessionCancellable` (`OrtRuntime.cpp`) creates the session on a detached Win32 thread. That thread pins the plugin module (`GetModuleHandleEx FROM_ADDRESS`) and exits through `FreeLibraryAndExitThread`, owns everything it touches, and never logs. The caller polls cancel every 25 ms. Abandoned sessions are freed on that thread, and the next creation waits (cancellably) for them. Measured: htdemucs cancel in 12-22 ms, BeatThis in 125-160 ms. **Rejected:** a join in a static destructor (deadlocks under the loader lock) and a bounded wait (still blocks). **Residual:** an abandoned load keeps using CPU, RAM and VRAM for up to about 5 s after unload. If the last module reference is dropped on that thread, DLL_PROCESS_DETACH runs there. |
| **MIDI CC learn UI** | DONE | Details in the next section. |

### MIDI CC learn UI
- **Right-click menu:** "MIDI Learn" / "Cancel MIDI Learn" / "Clear MIDI (CC n ch c)" on every stem pitch knob and on all 7 chop-inspector controls (PITCH, GAIN, PAN, REV, trigger, ATK, REL).
- **Mechanics:** new `MidiLearnControls.h` (`MidiLearnable<Slider/TextButton>`). A right-click opens the menu instead of dragging or clicking.
- **Tooltip:** shows "MIDI: CC 74 ch 1 (right-click to change)".
- **Targets:**
  - `stem<N>.pitch` is built into MidiMappingService, so it works with the editor closed.
  - `chop.<param>` acts on the selected chop while the editor is open. The binding is kept and saved when the editor is closed, but does nothing until it reopens.
- **New MidiMappingService API (additive):** `clearCcBindingsForTarget`, `describeCcBindings`, `getCcLearnTarget`, `getCcBindingsGeneration`. A re-learn replaces the target's old CC.
- **Persistence:** bindings are saved through the existing session `<cc>` entries, and the project is marked dirty on learn and clear.
- **Undo:** one undo step per CC gesture (a pause over 1 s starts a new one).
- **Editor close:** the destructor unregisters the inspector targets and cancels a learn that is still armed.
- **ui-sweep:** new `midi|<step>` verb, menu windows drawn into the PNGs, and 15 new PASS steps.

## Not done / follow-ups
1. **Intermittent last-instance unload of 1.7-4.2 s under load:** it happens after SAMPLAR's destructor (see above) and needs profiling.
2. **N-17:** the production DB must be checked before release (see the table).
3. **Server, SQLite:** the dev schema lacks `licenses.granted_by`, so `get_licenses_by_user_id` now raises, and revalidate returns 503 on SQLite dev setups. Postgres in production is unaffected. `is_hardware_blocked` and the Gold-expiry lookup still fail open; neither produces a denial.
4. **Server deploy:** the server change still needs a deploy. It was not done, as instructed.
