# Review round 2 - adversarial correctness review (read-only)

Date: 2026-10-21. Scope: the uncommitted tree after the round-2 work (engine, persistence and schema 2, UI fixes, licensing2, sep2, integration).
Method: four parallel read-only sub-reviewers (engine + processor, persistence + lifecycle, licensing, UI + separation). I then re-read the top findings in the code myself; "verified" means I confirmed the quoted code and the failure path.
Severity: CRITICAL / HIGH / MEDIUM. PLAUSIBLE = depends on timing, host behaviour or deployment state that I could not prove statically.

## 1. Round-1 HIGH status

| ID | Claim | Verdict | Evidence |
|---|---|---|---|
| A1 snapshot UAF | FIXED | **VERIFIED FIXED** | `PlaybackEngine.cpp:156-162`: `switchSnapshotMidBlock` only reloads `cur_`, so `ackSeq_` keeps the block-start seq. `getReclaimBound` (:118-128) uses min(minVoiceSeq_, ack while busy). `minVoiceSeq_` is stored before `inProcess_=false` (:286-287, :322-323). publish and GC share `mutex_` on the message thread. Offline renders use their own engine and pin with shared_ptr. |
| A2/U-H3 cross-instance locator | FIXED | **VERIFIED FIXED** in UI/plugin | UI goes through `ServiceLocator::Context`. The only remaining global lookups are `SamplarAudioProcessor.cpp:95` (survivor claim) and `capture()` defaults. Residual: `UpdateCheckService.cpp:343` reads the global registry from a worker thread (N-16). `Context::getX()` still falls back to the global when a slot is null (`ServiceLocator.h:109-112`): latent, no live path hits it today. |
| U-H2 host.tempo dangling | FIXED | **VERIFIED FIXED** | Subscribe and unsubscribe use the page's own `bus_` (`SModePage.cpp:1280-1286`, `:4202-4219`). |
| A4/U-H1 unload stall + TerminateThread | FIXED | **MOSTLY FIXED** | `jobsCancelled_` is set before the drain (`SModeService.cpp:44`). BPM, key, downbeat, decode and spectral jobs poll it. Residuals: N-9 (validate lock wait of up to 15 s with no cancel), the 20 s drain followed by `workers_.reset()` (`:58-61`) for jobs that ignore the flag, and ORT session creation (S-M3, acknowledged). |
| L-H1 transient error = revoke | FIXED (client) | **CLIENT FIXED, SERVER STILL LOCKS OUT** | Client mapping verified (`LicenseServerClient.cpp:62-100`: non-JSON/5xx/429 map to NetworkError, and a Denied outcome is acted on only after a verified signed denial). But the server signs a denial on a DB error: see N-1. |
| L-H2 OAuth shell query, no timeout | FIXED | **VERIFIED FIXED** | No v1/wmic on the OAuth or message thread. `runBoundedCommand` polls and kills the child. `startAuthentication` never joins. |
| S-H1 cross-process cache delete | FIXED | **VERIFIED FIXED** | File lock + lease (`StemCacheLock.cpp`). The commit keeps a valid existing entry and discards its own partial (`FullSongService.cpp:461-472`). The gate wait is cancellable (`:616`). Residual: N-9. |
| S-H2 missing onnxruntime.dll crashes at load | FIXED | **VERIFIED FIXED** | `ORT_API_MANUAL_INIT` is an interface define on the onnxruntime target (`cmake/extra_analysis.cmake:87`), and `OrtRuntime.cpp:5` raises `#error` without it. The bind runs inside `__try` (`OrtRuntime.cpp:46-84`). |
| U-H4 concurrent auto-transient | FIXED | **VERIFIED FIXED** | `autoChopBusy_` guards `runAutoChop` (`SModePage.cpp:2595`) and disables the visible button (:2580). The result is dropped if `segmentLayoutHash` changed (:2668). Quick Assign matches chops by sample range. |
| A6 ClockGuard empty file (MED) | FIXED | **VERIFIED FIXED** | `ClockGuard::decode` is strict. Residual bypass: N-6. |

## 2. New findings (ranked)

### N-1. HIGH (verified, server side) - A database error makes the server sign a denial, so paying users are wiped and muted (L-H1 path still open end to end)
- `license_server/plugins/database_manager/database_service.py` around line 7474, `get_user_by_email`:
```python
except Exception as e:
    logger.error(f"Error getting user by email: {e}")
    return None
```
- Around line 3168, `get_license_machines`: `except Exception as e: ... return []`.
- In `plugins/auth/__init__.py` `_revalidate_core`:
  - `user is None` leads to `_denial("Account not found or inactive", "inactive", ...)` at :2992.
  - Empty machines leads to `_denial(..., "unbound", ...)` at :3028.
  - Both are signed with the client's nonce and HWID.
- Scenario: a Postgres or SQLite hiccup, connection-pool exhaustion or a deploy-time migration during an hourly re-check.
  1. The client's `verifyDenial` passes (correct product, nonce and HWID).
  2. `applyRevalidation` (`LicenseSystemService.cpp:~971-992`) deletes `license_payload` / `signature_v2` and stores `license_valid=false`.
  3. `audioAllowed_` goes false mid-session, so playing tracks and offline bounces go silent.
  4. Recovery waits for the next hourly retry, which needs a working DB.
  5. Every active user who re-checks during the outage is hit.
- Fix:
  - **Server:** these lookups must raise, or return a distinct sentinel, and `_revalidate_core` maps that to 503 (the client already treats 5xx as NetworkError).
  - **Client hardening:** on the first signed denial for a licence that was Valid, lock without wiping and re-check in about 1 minute; wipe only on a second consecutive signed denial.

### N-2. HIGH (verified) - Leftover page bindings `smodeStemPitch` / `smodeStemMod` bring back stale values and override the schema-2 session
- `src/ui/Pages/SModePage.cpp:224-312` registers both bindings in the page constructor.
- `AppState::registerBinding` late-drains `root_.getChildWithName(id)` on every registration once `restoreReady_` is set (`AppState.cpp:49-79`).
- `unregisterBinding` (`:86-100`) does not snapshot before erasing, and the 2 Hz processor watcher snapshots only `smode`.
```cpp
if (restoreReady_ && hasRestore && ! isSuperseded(id)) { ... child = root_.getChildWithName(id).createCopy(); ... e.binding.restore(child); }
```
- **Scenario A:**
  1. The project is saved with stem 0 pitch at +3.
  2. The user sets +5 and closes the editor (the binding is unregistered; the child still says +3).
  3. They reopen the editor: the late drain calls `setStemPitchSemitones(0, +3)`, and the knob and audio revert silently.
- **Scenario B:** change pitch with the editor closed (MIDI CC or automation path) or after closing, then save.
  - `<play>` = +5 is written, but the stale `smodeStemPitch` child = +3 stays in the tree.
  - On reload the page binding restores after the processor binding, so +3 wins.
- These restores also bypass the tolerant reader's clamps:
  - `pbr/mwr/atr` are not clamped;
  - `vc/pbd/mwd/atd` are cast straight to enums;
  - NaN pitch passes `jlimit`.
- Fix:
  - Delete both page bindings and mark both nodes superseded once `<play>` exists.
  - Read the legacy nodes only in the processor restore, through the `clampF` helpers, when `<play>` is missing.

### N-3. HIGH (verified, conditional on the new per-chop release) - Long chop releases defeat lane chokes and steals and force hard voice cuts
- `PlaybackEngine.cpp:670`, `releaseVoice`:
```cpp
v.relStep = juce::jmax(1.0e-6f, v.env / (float) juce::jmax(releaseLen_, v.relLen));
```
- `relLen` can be up to 5000 ms (`applySegFx`, :690-696).
- Every release path uses it:
  - the lane choke in `triggerSlot` (:1165);
  - the oldest-voice steal (:600);
  - the cell-end release (`releaseAt = cellLen - releaseLen_`, :1189, which also ignores `relLen`, so "fade completes at the cell end" is false).
- Scenario: chops with release 1-5 s on a 16th-note lane.
  1. Every step leaves a multi-second tail, so the lane is no longer monophonic.
  2. The 24-slot sequencer pool fills (as does the 40-slot MIDI pool on drum rolls).
  3. `allocVoice` (:606-614) then `killVoice`s the quietest releasing voice with no fade, giving clicks on most steps.
- Fix: chokes, steals and cell-end releases always use `releaseLen_` (3 ms). Use `relLen` only for note-off and gate release. Make the last-resort kill a forced 1-2 ms fade.

### N-4. HIGH (PLAUSIBLE) - Non-finite or absurd host BPM/PPQ makes the audio thread loop forever (DAW freeze)
- `SamplarAudioProcessor.cpp:569-574` copies `getBpm()` and `getPpqPosition()` with no validation.
- `PlaybackEngine.cpp:1103-1121`:
```cpp
const double bps = host.bpm / 60.0 / sampleRate_;
...
for (int64_t k = lastTrigTick_ + 1;; ++k) {
    int off = (int) std::ceil(((double) k / kTicksPerBeat - b0) / bps - 1.0e-6);
    if (off >= numSamples) break;
    if (off < 0) off = 0;
```
- What breaks the loop:
  - **`bpm = +inf`:** `x / inf` is 0, so `off` is always 0 and the loop never breaks.
  - **NaN ppq:** `(int) NaN` is UB (INT_MIN on x64), clamped to 0, and the loop never breaks. `lastTrigTick_ = (int64_t) ceil(NaN)` is also UB.
  - **Huge finite BPM:** billions of iterations.
- Seen in practice: a host sending garbage at transport start or during a tempo-map edit, or a bridged or wrapped plugin. The audio thread hangs, which freezes the DAW.
- Fix:
  - In processBlock, accept bpm only if `std::isfinite(bpm) && bpm >= 1 && bpm <= 999`, and ppq only if `std::isfinite`.
  - In the engine, cap the loop at about `numSamples * kTicksPerBeat` iterations and resync on non-finite values.

### N-5. MEDIUM-HIGH (verified) - A clock skewed by more than 24 h makes the licence flap every minute (audio toggles on and off)
- `LicenseSystemService.cpp:~920` calls `ClockGuard::reset(signed issued)`. `:754` `ClockGuard::rolledBack(now)` then locks, `:768` sets `nextRetryMs_ = 0`, and the re-check comes back Valid.
- With the local clock more than 24 h behind:
  1. A live reply is now accepted (L-M3 change).
  2. `reset(issued)` puts the mark ahead of local time.
  3. Within 60 s the running-session check reports `clock_rolled_back` and locks.
  4. The immediate re-check returns Valid, and the cycle repeats.
- The effect is muted and unmuted audio every minute, per instance, plus one server request per minute.
- A clock more than 6 h ahead gives `stale == true` after every Valid reply (`lastVerifiedMs_` = signed issue time, :946), so there is one request per minute per instance, forever.
- Fix:
  - Measure staleness and the offline window from the local receive time of a nonce-verified reply, and store the skew.
  - If the signed issue time is more than 1 day off local time, report `kMsgClockBack` and retry hourly instead of going Valid.
  - Never zero `nextRetryMs_` for `clock_rolled_back`.

### N-6. MEDIUM (verified, bypass) - Rollback detection can be defeated mid-session
- `ClockGuard::decode` returns 0 ("unknown") for marks above `now + 10y` (`ClockGuard.h:~55`). `enforceRunningSession` uses only `ClockGuard::rolledBack()` and ignores the cache's `clock_mark`, `lastVerifiedMs_` and any in-memory maximum.
- Attack: while SAMPLAR runs, either set the clock back more than 10 years, or overwrite `session.idx` and the registry value with garbage and then roll back by any amount.
- Result:
  - Expiry is never reached.
  - `now - lastVerified` goes negative, so the offline window never closes.
  - `advance()` then writes the fake time over the real mark.
- A trial or revoked user keeps working until the DAW restarts. Startup is still protected.
- Fix:
  - Keep `maxSeenNowMs_` in memory.
  - Lock when `now + tol < max(highWater, lastVerifiedMs_, maxSeenNowMs_)`.
  - Use a fixed upper bound in `decode` (build date + N years).

### N-7. MEDIUM (verified) - After sign-in on one instance, other instances stay muted for up to an hour
- Each processor has its own `LicenseSystemService` and gate (`SamplarAudioProcessor.cpp:415-416`).
- `LicenseSystemService.cpp:788-790`:
```cpp
if (locked && !hasStoredTokenLocked()) { nextRetryMs_ = now + kRetryMaxMs; return; }
```
- Also `:998-999` (AuthRequired, then an hourly schedule). `:783` returns at `now < nextRetry` before checking for a token.
- Scenario:
  1. A project with 3 SAMPLAR tracks is opened unlicensed.
  2. The user signs in from track 1's window.
  3. Tracks 2 and 3 stay silent for up to 60 minutes unless their own "Try again" is clicked. Users conclude the licence is broken, or sign in again and use up activations.
- Fix: while locked with no token, check `hasStoredTokenLocked()` / the cache on every 60 s tick, ignoring `nextRetry`. Alternatively, have a process-wide "licence changed" counter that `applyAuthResult` bumps and the timer polls.

### N-8. MEDIUM (PLAUSIBLE) - A stale separation error turns a successful restore inactive and fires "Cancelled" UI handling
- `SModeService.cpp:209-229`:
```cpp
const bool ours = sessionSeparationRunning_;
sessionSeparationRunning_ = false;
active_ = false;          // runs even when !ours
...
bus_.publish("smode.workflow.error", errorMsg);
```
- `restoreSession` cancels a running separation (:307-312), and the fast path decodes cached stems in about 100 ms. The cancelled run's error can arrive after that, since cancel takes 45 ms to 0.7 s.
- The model is then loaded but `isActive() == false`, so editor reconnect and progress replay skip it and downbeat detection refuses to run.
- MainWindow's "Cancelled" branch (`MainWindow.cpp:540-550`) clears the loading state and may start a stale `pendingSeparationFile_`, which replaces the project's song.
- Fix: `if (!ours) { log; return; }` before touching `active_` or publishing.

### N-9. MEDIUM (verified) - New unload stall: restore validation waits up to 15 s on the cross-process lock with no cancel
- `SModeService.cpp:422-424` checks `jobsCancelled_` once, then calls `FullSongService::validateCachedStemFile()`. That calls `acquireLock(lock, "cache lock (validate)", kCacheLockWaitMs /*15000*/, {})` (`FullSongService.cpp:407`) with an empty cancel callback.
- Scenario: the user removes the instance right after loading a project while another process (a second DAW, or the standalone) holds the cache lock during a commit or prune of a large entry. `shutdown()` then blocks the host message thread for up to 15 s.
- Related: the 20 s `removeAllJobs` followed by `workers_.reset()` still ends in a thread kill for jobs that ignore the flag (auto-chop, Quick Assign, exports, ORT session creation).
- Fix: pass `[&]{ return jobsCancelled_.load(); }` into `validateCachedStemFile` and down to `acquireLock`. On a drain timeout, leak the pool rather than destroy it.

### N-10. MEDIUM (verified) - Relink trusts the file name or any chosen file; mismatched audio permanently rewrites the session
- `SModeService.cpp:123-126` relinks on a case-insensitive file-name match. `relinkMissingSource` (:353-372) accepts any existing file. `sourceSize`/`sourceMtime` are saved but never compared.
- `applySessionToModel` (`SModeSession.cpp:316-321`) clamps segments to the new length and silently drops the empty ones. `completeRestore` then discards the pending tree, and the next save overwrites the original chops.
- Fix: compare size/mtime (or a content hash). On a mismatch, warn ("different audio - chops may not line up") and keep the original pending tree until the user confirms. Report the dropped segment count in the banner.

### N-11. MEDIUM (verified) - Transport mute/solo stops working after the host-rate resample lands
- `PlaybackEngine.cpp:205`:
```cpp
if (v.src != nullptr && st.ch[0] == v.src->ch[0]) v.tgainTarget = st.audible ? 1.0f : 0.0f;
```
- Scenario: a 44.1 kHz song in a 48 kHz session, and Play is pressed before the background resample finishes. The new snapshot's `ch[0]` points at the resampled buffer, so the comparison fails and mute/solo clicks are ignored until the transport restarts.
- Fix: compare a stable stem identity (stem index plus source generation in `StemSnap`), not the rendered pointer.

### N-12. MEDIUM (verified) - Engine prepare/release stops transport and sequencer without telling the UI
- `PlaybackEngine.cpp:92-100` (`prepare`) and `:107-116` (`release`) clear `transportActive_`/`seqRunning_` and post no notification. `MidiMappingService::transportUiActive_`/`seqUiRunning_` (:701, :749) stay true.
- Scenario: a sample-rate or device change, or FL/Ableton re-activation, while playing. Play stays lit, the playhead freezes, and the next Play click sends Stop (so the user must click twice).
- Fix: have prepare/release bump an atomic "engine generation" that the 30 Hz timer compares, then clear both UI flags.

### N-13. MEDIUM (PLAUSIBLE) - The 150 ms stale-command drop eats legitimate UI triggers at large buffer sizes
- `PlaybackEngine.cpp:346`: `(int32_t)(nowMs - c.stampMs) > (int32_t) kStaleCommandMs`. A command waits for the next block start, which is 186 ms at 8192 samples / 44.1 kHz (common for mixing buffers and FL "safe" settings). Chop clicks, slot audition and QWERTY notes are then randomly dropped. NoteOffs are never dropped, so there are no stuck notes.
- Fix: threshold = max(150 ms, 3 x the last block duration). Or drop only after a detected stall (`now - lastProcessMs_ > 500`).

### N-14. MEDIUM (PLAUSIBLE) - AppState check-then-lock race: an off-thread setState can be overwritten by a message-thread snapshot
- `AppState.cpp:150` / `:178` test `dispatchPending_` before taking `treeLock_`. `deserialize` swaps `root_` under the lock (:345-361) but sets `dispatchPending_ = true` only at :404, after releasing it.
- Interleaving:
  1. The watcher (or a message-thread `getState`) passes the check.
  2. A worker-thread `setStateInformation` swaps in the project.
  3. The snapshot takes the lock and overwrites the loaded `smode` child with the live (old or empty) session.
  4. The deferred dispatch then restores that old session.
- Result: the project's chops are lost in hosts that call setState off the message thread.
- Fix: set `dispatchPending_` and `++dispatchSeq_` inside the lock scope that swaps `root_`, and re-check `dispatchPending_` after acquiring the lock in both snapshot functions.

### N-15. MEDIUM (PLAUSIBLE) - Dirty flag can be swallowed, so the host may close without a save prompt
- `suppressDirty_` (`AppState.cpp:132`) is one counter shared across threads. An off-thread serialize holding it makes the watcher's `markDirty()` a no-op, and the watcher has already moved its fingerprint forward (`SamplarAudioProcessor.cpp:868-890`).
- The watcher also re-baselines on every tick while `Restoring`, so edits to play settings during a 30 s+ re-separation never dirty the project.
- Fix: make suppression message-thread-only (or thread-local), and move the baseline forward only after the host has been notified.

### N-16. MEDIUM (PLAUSIBLE) - UpdateCheckService reads another instance's registry from a worker thread
- `src/services/UpdateCheckService.cpp:343`: `ServiceLocator::getRegistry()` on `checkThread_`. The plain static can point at instance B, which can unload (`SamplarAudioProcessor.cpp:85-101`) while A's thread calls `licenseSvc->generateHardwareId()`. That is a use-after-free.
- Fix: inject this instance's `LicenseSystemService*` (or the machine ID string) at construction.

### N-17. MEDIUM (PLAUSIBLE, only if 1.0.0 reached users with server binding) - v1-bound machines get a signed "unbound" denial
- The client now sends only the v2 ID (`LicenseServerClient.cpp:115-118`, `OAuthBrowserAuthenticator.cpp:238,472`), and the v1 code was deleted. If any 1.0.0 user was bound by v1 ID, the re-check returns a signed "unbound" denial, so the licence is wiped. The next sign-in uses a second activation slot.
- Fix: confirm on the server whether any SAMPL4R `license_machines` row has no `hwid_v2`. If there are any, add a server-side migration rather than reviving client v1 code.

### N-18. MEDIUM - Other licensing and engine items (sub-reviewer findings, spot-read only)
- **Engine, offline exports:** `renderSequencerOffline` / `renderSegmentOffline` (`MidiMappingService.cpp:~769/795`) do not check `isAudioAllowed()`. They are only reachable from the disabled root UI today, but an export job queued before expiry still completes. Add the check inside both.
- **Gate click:** `buffer.clear()` on a gate flip (`SamplarAudioProcessor.cpp:591-592`) is a hard cut or hard start. Use a 5 ms ramp.
- **Bypass:** commands drained during bypass (`processBypassed`) start voices that sound all at once on un-bypass. Ignore trigger commands while `bypassed_` is set.
- **CPU spike:** moving LOCK transpose or a stem pitch off 0 makes every active voice (up to 80) run `outputSeek` pre-roll in the same block. Spread the stretch entries over several blocks.
- **Locate tolerance:** the PPQ locate tolerance is 4 samples (`:1108`). Tempo ramps or FL PPQ rounding release the sequencer pool every block, giving stutter. Use about 10 ms.
- **Encryption key:** an empty `encryption.key` is accepted (`SecureSessionManager::ensureEncryptionKey`). The token is never stored, so sign-in is forced again every offline window. Regenerate the key if it is empty or short, and create it atomically.
- **Unclamped page UI JSON:** `lockedBpm`, `gridOffset` and `lockSemis` in the page UI JSON (`SModePage.cpp:~3845-3871`) are not clamped. `1e999` becomes inf and reaches `setGlobalGridOffset`.
- **Missing `smode` node:** a project with no `smode` node never calls `restoreSession`, so the previous session leaks into it on the next save.

## 3. UI + separation findings

### N-19. HIGH (verified, data loss) - Changing a lane's subdivision, or restoring a pre-B18 project, silently deletes sequencer slots
- `src/ui/Components/smode/SequencerLaneComponent.cpp:1029-1052`:
```cpp
int lastEnd = 0;
for (auto sl : slots_) {
    const int cell = (int) std::floor((double) sl.gridCell * to / fromCellsPerBar);
    ...
    if (cell >= total || cell < lastEnd) continue;   // dropped
```
- The loop assumes `slots_` is sorted by `gridCell`, but it is not:
  - copy appends at the end (`:532`);
  - move edits `gridCell` in place;
  - restore pushes slots in saved order (`:995-996`).
- Scenario:
  1. A lane has a slot at cell 8, then the user copies a slot to cell 0 (appended last).
  2. They switch x4 to x8: the first slot maps to cell 16 (lastEnd 18), and the copy maps to cell 0, which is less than 18, so it is dropped.
- `restoreState` (`:998-1000`) runs the same rescale for lanes saved before B18 (implicit 4 cells/bar) whenever the lane is now x8/x16. It pushes no undo step, so the loss becomes permanent on the next save.
- Fix: sort a copy by `gridCell` before the loop. Drop only a real overlap after rounding, log each drop, and push an undo step on the user-triggered path.

### N-20. MEDIUM (verified) - A stale auto-chop or Quick Assign result clears the busy flag of the current job
- `SModePage.cpp:2660` sets `self->autoChopBusy_ = false; self->updateAutoButton();` **before** the generation check. Eject or a new song also resets the flag (`:2148`).
- Scenario:
  1. Auto-chop runs on song A.
  2. The user loads song B and starts B's auto-chop.
  3. A's late result arrives and clears busy, so the button is re-enabled while B's job is still running.
  4. A second click, or the downbeat re-chop (`:2051` checks `!autoChopBusy_`), starts a concurrent job.
- The layout hash prevents duplicate chops, but the downbeat re-chop can be lost behind a misleading "chops changed meanwhile" toast.
- Quick Assign has the same pattern (`resetQuickAssignButton()` before its generation check).
- Fix: give each job a ticket (`++autoChopTicket_`), and clear busy only when the ticket matches.

### N-21. MEDIUM (verified) - With the ONNX Runtime missing, already-cached songs can no longer be loaded
- `SModePage.cpp:1354-1362`: `startLoad` refuses whenever `!isSeparationRuntimeAvailable()`.
- `FullSongService::separateAudioOnnx` deliberately serves cache hits before it needs the runtime (`:562`, "works even without the ONNX runtime").
- Scenario: AV quarantines `onnxruntime.dll`. Every Recents song shows "reinstall" even though its stems are cached, yet relink (`relinkMissingSource`) still works.
- `MainWindow.cpp:552` also shows the reinstall banner for any error while the runtime is missing.
- Fix: remove the up-front refusal and rely on `res.runtimeUnavailable` from the service.

### N-22. MEDIUM (verified) - Pruning runs while holding the cross-process cache lock; waiters time out after 15 s and then skip validation
- `FullSongService.cpp:487` calls `pruneStemCacheLocked`, which runs `deleteRecursively` on evicted entries, dead partials and `.evict-*` folders under `.cache.lock`.
- In another process:
  - `validateCachedStemFile` on a timeout returns `anyStemFile.existsAsFile()` **unvalidated** (`:407-408`);
  - lookup becomes a MISS, so the song is re-separated;
  - commit fails.
- None of these waits is cancellable, and the validate wait is the unload stall in N-9.
- Fix: inside the lock, only rename to `.evict-*`, and delete after unlocking. On a validate timeout, return false. Pass a cancel hook to the lock waits.

### N-23. MEDIUM - A failed cache commit leaves the session pointing at a `.partial-*` folder that is pruned after 10 min
- `FullSongService.cpp:700-705`: on commit failure the result is `outDir` = the partial, and its lease is released on return.
- The project saves those stem paths. Any process's prune deletes them after 10 min (`:511-515`), and `validateCachedStemFile` accepts the partial until then.
- Result: a slow re-separation on the next project open. Chops survive through the session restore, so this is not chop loss.
- Fix: retry the commit, or have `validateCachedStemFile` reject `.partial-` dirs and re-commit on restore.

### N-24. PLAUSIBLE / low
- **Inspector jumps to another chop:** `findInspectorSegment` (`SModePage.cpp:~4080`) accepts `sg.selected`. After the inspected chop is deleted while another chop is Ctrl-selected, the inspector targets an unrelated chop. Its values are re-read, so nothing is overwritten.
- **Partial deleted mid-run when the lease fails:** if `LeaseFile::acquire` fails (`FullSongService.cpp:658`), the partial is protected only by its age. A CPU run longer than 10 min can be deleted by another process's prune. Do not cache when the lease fails.
- **Stall recovery during slow offline renders:** `stallRecovery_` (`PlaybackEngine.cpp:175-183`) releases the MIDI pool after a 500 ms wall-clock gap. It does not exclude `host.offline`, so a slow offline bounce, or a UI freeze longer than 500 ms, cuts held notes. Skip it when offline.

## 4. Ranked summary (new in round 2)

| # | Sev | Finding | Class |
|---|---|---|---|
| N-1 | HIGH | Server signs "inactive"/"unbound" denials on DB errors; client wipes the licence and mutes audio | licensing lockout (server) |
| N-2 | HIGH | Leftover `smodeStemPitch`/`smodeStemMod` page bindings late-drain stale values and override `<play>` on reload | data loss / wrong state |
| N-19 | HIGH | `rescaleSlotsToGrid` assumes sorted slots, so subdivision change or old-project restore deletes slots | data loss |
| N-3 | HIGH | Per-chop release (up to 5 s) applied to chokes, steals and cell ends; pools fill and are hard-killed | audio glitch |
| N-4 | HIGH (PLAUSIBLE) | inf/NaN/huge host BPM/PPQ gives an infinite loop in `prepareSequencerEvents` | DAW freeze |
| N-5 | MED-HIGH | Clock skewed by more than 24 h makes the lock flap every minute; more than 6 h ahead means a request every minute | licensing lockout / audio toggling |
| N-7 | MED | Sign-in on one instance leaves other instances muted for up to 1 h | licensing lockout (UX) |
| N-8 | MED (PLAUSIBLE) | Stale separation error after a fast restore sets `active_=false` and fires the "Cancelled" UI path | wrong state |
| N-9 / N-22 | MED | Validate lock wait (15 s, no cancel) blocks unload; prune holds the lock; a timed-out validate skips validation | unload stall / stale stems |
| N-10 | MED | Relink by file name or any file; segments clamped or dropped; original session discarded | data loss |
| N-14 | MED (PLAUSIBLE) | AppState check-then-lock lets an off-thread setState be overwritten | data loss |
| N-6 | MED | Mid-session rollback bypass (garbage mark or rollback beyond 10 years) | licensing bypass |
| N-11, N-12, N-13 | MED | Transport mute after resample; prepare/release leave the UI "playing"; 150 ms stale drop at big buffers | audio / UI |
| N-15, N-16, N-17, N-20, N-21, N-23 | MED | Dirty flag swallowed; UpdateCheck reads a foreign registry; v1-bound users; busy-flag ticket; cached songs blocked without ORT; partial-dir stems | various |
