# Review round 1 - adversarial correctness review (read-only)

Date: 2026-10-21. Scope: uncommitted change set (git diff HEAD + untracked files).
Method: I read the code directly for engine, processor, MidiMappingService, SModeSegmentModel snapshot, ServiceRegistry, ServiceLocator, SModeService, SModeSeparationService and the editor. Three parallel read-only reviewers covered licensing, separation, and analysis + UI. Their findings are merged below, and I spot-checked the top ones.
Severity: CRITICAL / HIGH / MEDIUM. PLAUSIBLE = depends on timing or host behaviour I could not prove statically.

## A. Realtime engine / lifecycle (reviewed directly)

### A1. HIGH (PLAUSIBLE timing) - Snapshot freed while a voice reads it: mid-block reload raises ackSeq past voices started earlier in the same block
- `src/audio/engine/PlaybackEngine.cpp:318-322` (drainCommands) + `:149-153` (reloadSnapshot) + `:117-128` (getReclaimBound)
```cpp
while (guard++ < 512 && commands_.pop(c))
{
    if (c.snapSeq != 0 && (cur_ == nullptr || c.snapSeq > cur_->seq))
        reloadSnapshot();   // ackSeq_ := newer seq
    applyCommand(c);
}
...
const bool busy = inProcess_.load(...);
uint64_t bound  = minVoiceSeq_.load(...);          // value from the END of the PREVIOUS block
if (busy) bound = std::min(bound, ackSeq_.load(...));
```
- Scenario: the block starts and loads snapshot N. Command A (PlaySegment, seq N) starts voice V, and `V.src` points into `N->stems[]`. On the message thread, the user acts again: `syncSnapshotNow()` publishes N+1 (so N is retired) and pushes command B with seq N+1. The same drain loop pops B, calls `reloadSnapshot()` and sets ackSeq_ = N+1. The 30 Hz timer then runs `collectGarbage()` while the block is still rendering (blocks can be up to 46 ms). It sees busy=true. minVoiceSeq_ is UINT64_MAX (no voices in the previous block), so bound = N+1 and N is freed. `renderVoice` then reads `v.src->ch[0]`, a use-after-free on the audio thread, which can crash the DAW. Freeing the snapshot frees the StemSnap array that `src` points to, even when the audio buffer itself is still shared.
- Fix: never raise ackSeq_ inside a block. Store it once at the start of process(), or keep a per-block `minSeqUsed` and publish `ack = min(blockStartSeq, ...)`. Or publish minVoiceSeq_ (min with the voice's seq) immediately in startWindowVoice. Add an EngineTests case: voice from N, reload to N+1 mid-drain, GC during the block.

### A2. HIGH (PLAUSIBLE, multi-instance) - UI resolves services through the process-global ServiceLocator at call time, which cross-wires instances and leaves a dangling model pointer
- `src/ui/Pages/SModePage.cpp:1395-1400` (onStemsReady), `:3501-3505` (midiService()), `src/ui/MainWindow.cpp:176-186` (onBack), `src/ui/MainWindow.cpp:591-611` (state.restored handler uses `ServiceLocator::getUIState()/getRegistry()`)
```cpp
auto* reg = samplar::ServiceLocator::getRegistry();          // whichever instance claimed LAST
auto* midiSvc = dynamic_cast<samplar::MidiMappingService*>(reg->get("midi.mapping"));
midiSvc->setSModeModel(&model);                              // model = THIS page's smodeService_ model
```
- The locator is re-claimed only on editor construction, visibilityChanged and broughtToFront. Many hosts never call broughtToFront when you click into an already-open plugin window. So if instance A's separation finishes, or the user plays or auditions in A's window, while B holds the claim, A's model is wired into B's MidiMappingService. B then plays A's stems. When A is removed, B's `model_` dangles, and B's 30 Hz timer calls `model_->computePlaybackHash()` on freed memory, which crashes the DAW. The `state.restored` handler has the same root cause: with two open editors during project load, instance A reads B's pending UIState and SModeService, so A's session is not restored. Saving the project afterwards writes an empty S-Mode state for A, and A's session is lost.
- Fix: give RootComponent and SModePage their owning processor's registry, bus and UIState (constructor injection) and stop calling ServiceLocator in action paths. At minimum, look up `midi.mapping` in the same registry that owns `smodeService_`. In MidiMappingService, also check that the model belongs to the same registry (or let the processor's own `smode.workflow.ready` handler do the wiring; it already does, so SModePage:1395 is redundant).

### A3. MEDIUM (PLAUSIBLE) - Commands queue up while the host is not calling processBlock, then fire all at once
- `src/audio/engine/RtFifo.h` / `PlaybackEngine.cpp:314-324`. There is no age check, and drainCommands applies up to 512 queued commands at the next block.
- Scenario: FL "smart disable", a deactivated Ableton device, or Reaper's suspend-when-silent. The editor is still live, so the user clicks chops, uses the QWERTY keyboard or presses Play. When processing resumes, every queued NoteOn, PlaySegment and TransportStart fires in the same sample, giving a loud burst (up to 32 voices stacked at full level). If more than 512 commands queue, later NoteOffs are dropped (`pushCommand` only logs), so looped or gated voices stick until CC123 or Stop.
- Fix: stamp each Command with `Time::getMillisecondCounter()`. In drainCommands, drop NoteOn, PlaySegment and PlaySlot commands older than about 100 ms, but always apply stop and note-off commands. Alternatively, have the timer detect "no process for more than 200 ms" and flush the FIFO.

### A4. MEDIUM - Shutdown can block the DAW message thread for 20 s, then terminate a running ONNX thread
- `src/services/smode/SModeService.cpp:47-55`: `workers_->removeAllJobs(true, 20000); workers_.reset();`. Also `MidiMappingService.cpp:96-103` (10 s).
- Pool jobs are plain lambdas and cannot be interrupted. A BeatThis or DownbeatDetector inference, or a segment render, that is still running blocks plugin unload for up to 20 s. After the timeout, `~ThreadPool()` gives jobs another 5 s, then `stopThread(500)` and finally kills the thread (TerminateThread). If the thread holds the CRT heap lock or is inside ORT, the host can deadlock or crash later. The report claims unload stays under 1.5 s, but that only holds when the pool is idle.
- Fix: pass a cancel flag (the existing `alive_`) into the analysis and render functions and poll it between frames or chunks (BeatThis between ONNX windows, renderSegment per block). Keep the wait short (about 2 s). On timeout, leak the pool instead of destroying it.

### A5. MEDIUM - Licence gate is UI-only: the DSP and the DAW session restore run unlicensed
- `src/plugin/SamplarAudioProcessorEditor.cpp` updateLicenseGate only disables `rootComponent_`. `SamplarAudioProcessor::processBlock` has no licence check. MainWindow's `state.restored` fast-restore (`MainWindow.cpp:603-640`) still runs with the root disabled, because components are constructed and subscribed anyway.
- Scenario: an unlicensed or expired user opens a project saved while licensed. The stems reload from the cache and DAW MIDI triggers every chop, the host-synced sequencer included. Only editing is blocked. Decide whether this is intended. If not, gate `renderBlock` on an atomic `licensed` flag that LicenseSystemService sets, and skip the S-Mode restore while unlicensed.

### A6. MEDIUM - ClockGuard: an empty or unparsable `session.idx` decodes to a far-future high-water mark and locks the user out offline
- `src/security/ClockGuard.h:45`: `return f.existsAsFile() ? (f.loadFileAsString().trim().getLargeIntValue() ^ kMask) : 0;`
- `getLargeIntValue()` of "" or garbage is 0, and `0 ^ kMask` = 0x3C5A69E1F00DB44B (about 4.3e18 ms). `rolledBack(now)` is then true forever. Possible causes: AV quarantine, a manual edit, disk errors, or a zero-length file. Every offline validation then fails as "clock rolled back" until a signed online reply calls reset(). That is a paying-user lockout when offline (studio machines are often offline). The registry path checks `isNotEmpty()`, but the file path does not.
- Fix: parse strictly (digits only, non-empty) and treat values outside [2024-01-01, now + 10 years] as "no data" (0) rather than as a high-water mark.

### Notes checked and found OK
- Audio thread: no allocation in signalsmith `process`/`outputSeek` (`tmpProcessBuffer` is pre-sized in configure, and the copy-assign of stashed STFT buffers reuses capacity). Blocks larger than the prepared size are chunked (`renderRange`). `ScopedNoDenormals` is set in processBlock and processBlockBypassed. The standalone `getNextAudioBlock` lacks it, but that is the dev-only SamplarApp.
- FIFOs: commands and raw MIDI are MPSC (the producers' SpinLock is never taken by the audio side). Notifications are SPSC: only the audio thread produces and only the timer consumes. Offline engines have their own FIFOs. The VST3 target does not also register an AudioSourcePlayer, so there is only one audio consumer.
- Stem resample job vs new song: the job holds a `shared_ptr` to the source, and results are matched by a `weak_ptr` + rate, so stale results are dropped.
- ServiceRegistry: shutdown and destruction now run in reverse registration order. smode (the last registered) shuts down first and drains its workers before midi.mapping goes away, so drag-out jobs that hold a raw `midiSvc` are safe.
- The editor's licence subscription and callAsync use SafePointer and unsubscribe in the destructor. The AppState and processor callAsync lambdas are alive-token guarded.

## B. Licensing (parallel reviewer; L-H1 and L-H2 spot-checked by me)

What holds up: RSA-PSS runs over exactly the bytes that are parsed. An empty or bad-base64 signature is rejected. The signature covers product, status, hwid, expiry and nonce. A live reply must echo the nonce. An unsigned 1.0.x cache is never trusted. The code I read logs no tokens, codes or OAuth URLs.

### L-H1. HIGH (verified) - Transient server failures are treated as "Revoked", and paying users are locked out immediately with no offline window
- `src/networking/LicenseServerClient.cpp:90-93`
```cpp
const bool verdict = statusCode < 400 && json.hasProperty ("license_valid");
...
else if (statusCode == 404 || verdict)   r.outcome = RevalOutcome::Revoked;
```
- The server's `_revalidate_fail()` (`license_server/plugins/auth/__init__.py:2844`) returns HTTP 200 with `{"success":false,"license_valid":false}` for "Invalid access token", "Access token too old", "Service unavailable" and "Account not found" (lines 2915-2925). A 404 with a non-JSON body (route missing during a deploy, CDN error page) is also Revoked (line 74). `LicenseSystemService.cpp:844-865` then deletes `license_payload`/`signature_v2`, writes `license_valid=false`, sets `lastVerifiedMs_=0` and shows "revoked or refunded". A DB outage or a JWT secret rotation locks every user at their next re-check. Because `lastVerifiedMs_==0`, `enforceOfflineWindow()` never re-checks automatically (see L-M6).
- Fix: Revoked only when `success==true && license_valid==false`, ideally only when signed. Map `success:false` to AuthRequired and a non-JSON 404 to NetworkError. Server side: sign revocations, or return a distinct status for transient failures.

### L-H2. HIGH (verified) - The sign-in thread runs shell queries with no timeout; joining it freezes the DAW UI and can TerminateThread
- `src/networking/OAuthBrowserAuthenticator.cpp:204-207` always calls `getLegacyHardwareIdCandidates()`, which runs wmic or PowerShell through `ChildProcess::readAllProcessOutput()` (`HardwareFingerprintGenerator.cpp:388`). That call blocks until the pipe closes, so the later `waitForProcessToFinish(3000)` is useless.
- `startAuthentication:118-121` and `cancelAuthentication` call `stopThread(15000)` on the message thread. Cancel then Sign in again freezes the host for the length of the shell query. If WMI hangs during plugin unload, JUCE force-kills the thread (TerminateThread), which can leave the Logger or heap locks held.
- Fix: drop the legacy recompute (the RevalidationWorker already does: "SAMPLAR never shipped"), or read with a timeout and kill the child. Never block-join from `startAuthentication`.

### L-M1. MEDIUM - A stale re-check result can undo sign-out or wipe a fresh sign-in
- `deactivateLicense()` (`LicenseSystemService.cpp:1156-1173`) neither cancels the in-flight re-check nor bumps an epoch, and `applyRevalidation` has no generation check. A Valid reply after sign-out rebuilds the cache and session (826-835). A Revoked reply (see L-H1) computed with the old token deletes the payload just stored by a new browser sign-in.
- Fix: keep an epoch counter that sign-in and sign-out bump, and drop results from an older epoch. Cancel the worker in `deactivateLicense`.

### L-M2. MEDIUM - The machine binding can be satisfied from local, forgeable data
- `thisMachineIdsLocked()` (`:688-695`) accepts the v1 id even when a live v2 id exists. The v1 id comes from `.hardware_id` (its tag uses a salt baked into the binary) or from the cache. `LicenseCache::decryptData` accepts plaintext `licenseData` when the `encrypted` flag is missing, and the HMAC key is derivable on the target machine. A cloned cache that carries machine A's v1 id plus A's signed payload verifies on machine B.
- Fix: when v2 is available, require the signed hwid to equal the v2 id recomputed from the OS. Reject unencrypted cache blobs.

### L-M3. MEDIUM - A clock off by more than 24 h means a paying user can never activate, and the error message is misleading
- `verifyLive:250` (stale when the clock is ahead) and `verifyStored:231` (issued in the future when it is behind) both produce `kMsgUnverifiedReply`. Fix: for live replies, rely on the nonce for freshness, use the signed `issued` time for `ClockGuard::reset`, and show the clock message.

### L-M4. MEDIUM - Expiry is not enforced mid-session
- `enforceOfflineWindow()` (`:655-686`) ignores `currentLicense_.expiresAt`. A trial that expires stays Valid for up to 72 h in a long session with the server blocked. Fix: check expiry and `ClockGuard` in the timer.

### L-M5. MEDIUM - The gate covers the UI only (same as A5)
- The editor also fails open if `licenseService_` is null (`SamplarAudioProcessorEditor.cpp:42`).

### L-M6. MEDIUM - No automatic re-check after a lock-out
- `lockOut` sets `lastVerifiedMs_=0`, and `enforceOfflineWindow:666` returns before `revalidateAsync()`. A user who launched offline stays locked after reconnecting until they click "Try again" or restart. Fix: re-check from the timer while not Valid and a token exists.

## C. Separation / stem cache (parallel reviewer; S-H1 and S-M1 spot-checked)

What holds up: GPU-to-CPU fallback is reachable at session creation and mid-run. A cancelled run is not counted as a GPU failure. The cancel watcher is joined before `runOptions_` and the session are destroyed. The DirectML.dll identity check falls back to CPU if a foreign DirectML is already loaded. The cache write is atomic (partial dir + entry.json + rename). Overlap-add and iSTFT indexing are in bounds; short and mono input are handled.

### S-H1. HIGH (PLAUSIBLE) - The separation gate is per process, so the cache entry gets deleted while another process reads it
- `FullSongService.cpp:21-25` (function-static std::mutex) and `:264-268`:
```cpp
if (entryDir.exists() && !entryDir.deleteRecursively()) { ... return false; }
if (!partialDir.moveFileTo(entryDir)) ...
```
- Two processes (two DAWs, standalone plus DAW, or Bitwig/Reaper/FL sandboxed or bridged hosts) separate the same song. Process A commits and starts `loadStemAudio`. Process B commits next and `deleteRecursively()` deletes A's stems that are not currently open. A then loads only some stems, logs "failed to load" and still publishes `workflow.ready`, so a lane is silent. Concurrent DirectML sessions can also run out of VRAM, and `disableGpuForProcess` (`OnnxSeparationProvider.cpp:342, 961`) then turns the GPU off for the rest of the session.
- Fix: use a named cross-process mutex for the gate. In commit, keep an existing valid entry and delete your own partial. Do not disable the GPU permanently on OOM.

### S-H2. HIGH (pre-existing, PLAUSIBLE) - A missing onnxruntime.dll crashes the host at plugin load
- `ORT_API_MANUAL_INIT` is not defined, so `OrtGetApiBase()->GetApi()` runs in a static initializer and fires the delay-load during DLL init. If AV has removed the DLL, the delay-load SEH exception 0xC06D007E is raised with no `__try`, and the plugin scan or load crashes the DAW. Fix: define `ORT_API_MANUAL_INIT`, call `InitApi()` lazily inside `__try`, and null-check the result.

### S-M1. MEDIUM (verified) - NaN/Inf or L = -R input produces garbage stems that are cached permanently
- `OnnxSeparationProvider.cpp:722-723`: `if (refStd < 1.0e-8) refStd = 1.0e-8;` is false for NaN, so NaN propagates into all four stems, which are then committed to the cache. Out-of-phase stereo gives mid = 0, a 1e8 gain, and Inf output. Fix: zero non-finite input, floor refStd at about 1e-4 using the RMS of both channels, and reject non-finite model output before commit.

### S-M2. MEDIUM - Race on `stemFiles_`, written by the worker thread (`SModeSeparationService.cpp:170`)
- Readers: the state-save lambda (`SamplarAudioProcessor.cpp:689`, possibly on a non-message thread). The current path is mostly shielded because `active_` is false during separation. Fix: assign it inside the completion callAsync.

### S-M3. MEDIUM - Unload blocks on ORT session creation (3-5 s on CPU, more for a DirectML graph compile), which cannot be cancelled. This exceeds the 1.5 s unload budget.

### S-M4. MEDIUM - MAX_PATH: `%APPDATA%\Samplar\stem_cache_onnx\<hash>.partial-<ts>\<songname>_Vocals.wav`. Song names over about 150 characters fail on every attempt. Fix: use fixed stem file names inside the hashed dir.

### S-M5. MEDIUM - A cache hit never compares entry.json (source, size, mtime) with the input, and the key is a non-cryptographic hashCode64. Saved state also reloads stem paths directly, so stale stems load after the source changes. Fix: validate entry.json on lookup.

### S-M6. MEDIUM (quality) - 48 kHz and higher input is downsampled to 44.1 kHz by linear interpolation without an anti-alias filter (`OnnxSeparationProvider.cpp:532-555`).

## D. Analysis + UI (parallel reviewer; U-H2 and U-H4 spot-checked)

What holds up: the analysis DSP is guarded against silence, very short input and empty input. The BeatThis cache is in memory only, keyed on a content hash (shape + rate + about 8k sampled values), so no file is written and an edited file gets a new key. The ORT session runs under `runMutex`.

### U-H1. HIGH (PLAUSIBLE) - BeatThis inference outlasts the 20 s worker drain, then the thread is killed (same root cause as A4)
- impl_analysis measured 16.4 s for a 205 s stem, so a 6-10 min song exceeds 20 s. Unloading during BPM detection then blocks the message thread for about 25 s and TerminateThreads a thread inside ORT. Fix: make inference cancellable with `RunOptions::SetTerminate` or a chunk loop that checks a flag.

### U-H2. HIGH (verified) - The host.tempo subscription is unsubscribed from whatever bus is current, not the bus it was added to, which leaves a dangling `this`
- `SModePage.cpp:417-426` subscribes on `ServiceLocator::getBus()` at click time. The destructor at `:1260-1263` unsubscribes from `ServiceLocator::getBus()` at destruction time. If another instance owns the locator at destruction, the handler stays on the original bus and the next `host.tempo` publish from that instance's MidiMappingService timer calls into the freed page. Fix: store the bus pointer used to subscribe, or use the page's own `bus_`.

### U-H3. HIGH (PLAUSIBLE) - Cross-instance `setSModeModel` = A2.

### U-H4. HIGH (verified) - Auto-Transient can run twice at once and duplicate segments
- `SModePage.cpp:2519` disables the hidden `autoTransientBtn_`, but the visible trigger (`autoBeatBtn_`, `:562`) stays enabled. A second click, a mode switch or Undo during analysis runs concurrent jobs whose results `addSegment` on top of each other. `contentGeneration` only changes in `clear()`, so the stale check passes. Fix: disable the real trigger and use an edit-generation counter that every segment mutation bumps.

### U-M5. MEDIUM - Quick Assign uses `(stem, segIdx)` captured before the job, and any edit or undo during the job makes it assign or delete the wrong segments, including deleting audible ones. Same fix as U-H4.
### U-M6. MEDIUM - A late BPM detection result overwrites a BPM the user typed or the DAW synced (`SModePage.cpp:3233-3243`). With the grid locked, it also transposes every segment.
### U-M7. MEDIUM (PLAUSIBLE) - PopupMenu callbacks in `StemWaveformComponent.cpp:1572` and `SequencerSlotComponent.cpp:360` capture raw `this` with `withTargetScreenArea`. If a lane is rebuilt while the menu is open, picking an item is a use-after-free or acts on the wrong index. Fix: capture a SafePointer, use `withTargetComponent`, and re-validate the index.
### U-M8. MEDIUM - UI stuck after Eject or a dropped result: the BPM "..." animation and the "Analyzing..." state are never reset on early returns (`SModePage.cpp:3195, 700, 726`).
### U-M9. MEDIUM - BeatThis checks the cache, then runs inference without holding the lock, so two jobs on the same stem both run the full inference. `gLoadFailed` (`BeatThisDetector.cpp:278`) disables BeatThis for the rest of the process after a single init failure.

## Ranked summary
1. A1 HIGH - snapshot use-after-free on the audio thread (mid-block ackSeq raise)
2. A2/U-H3 + U-H2 HIGH - multi-instance ServiceLocator cross-wiring, leaving a dangling model or handler (crash after removing one instance) and losing restored state
3. L-H1 HIGH - transient server errors treated as Revoked, locking out paying users
4. A4/U-H1 HIGH - 20 s unload stall, then TerminateThread inside ORT
5. L-H2 HIGH - OAuth shell query with no timeout, freezing the UI or TerminateThread
6. S-H1 HIGH - cross-process cache commit deletes stems in use
7. U-H4 HIGH - concurrent auto-transient jobs duplicate segments
8. S-H2 HIGH (pre-existing) - missing onnxruntime.dll crashes at load
9. MEDIUMs: A3 stale command burst, A5/L-M5 UI-only gate, A6 ClockGuard empty-file lockout, L-M1 stale re-check, L-M2 hwid binding, L-M3 clock skew, L-M4 expiry, L-M6 no auto re-check, S-M1 NaN cached, S-M2-M6, U-M5-M9
