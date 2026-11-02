# SAMPLAR audit A: realtime audio path, plugin lifecycle, state persistence

Scope: src/plugin/*, src/services/MidiMappingService.*, src/app/{AppState,ServiceLocator,EventBus,ServiceRegistry}, src/audio/*,
plus the call sites they depend on (SModePage, MainWindow/RootComponent, SModeService, SModeSeparationService).
This was a read-only audit: no files were edited and nothing was built. Line numbers are from the working tree as of 2026-10-20.
JUCE version: vendored 8.0.8. Only VST3 is built (CMakeLists.txt, juce_add_plugin FORMATS VST3). The standalone app is a separate SamplarApp (App.cpp).

Live audio path, verified: `SamplarAudioProcessor::processBlock` -> `MidiMappingService::handleNoteOn/Off` + `getNextAudioBlock`
(voices + S-Mode transport, signalsmith-stretch) -> `PluginPreviewService` mixer (no inputs are ever added).
The following are compiled in but never instantiated or referenced from live code: `SequencerAudioSource`, `BufferAudioSource`, `BandProcessor`,
`SoundTouchSource`, `SamplePreviewCache`, `AudioEngine`, `SpectralSeparator`. See A24.

Severity scale: CRITICAL = crash, data loss or a guaranteed dropout in a normal workflow. HIGH = likely crash/glitch, or a core feature that is broken in a DAW.
MEDIUM = audible or functional defect. LOW = hygiene, latent issue, or quality. PLAUSIBLE = the code path is verified but the trigger or magnitude depends on the host or timing.

---

## A1 - CRITICAL - Each MIDI note heap-allocates and copies the whole segment on the audio thread
- **File:** src/services/MidiMappingService.cpp:721, 749-752, 787-799; src/services/smode/SModeSegmentModel.cpp:145-158
- **Evidence:**
  ```cpp
  auto matches = smodeModel_->findAllSegmentsByMidiNote(noteNumber);   // returns std::vector (heap)
  ...
  auto segBuf = std::make_shared<juce::AudioBuffer<float>>(
      stem->audioBuffer.getNumChannels(), segLen);                     // heap alloc, size = segment
  for (int ch = 0; ch < stem->audioBuffer.getNumChannels(); ++ch)
      segBuf->copyFrom(ch, 0, stem->audioBuffer, ch, static_cast<int>(startSmp), segLen);
  ...
  juce::MessageManager::callAsync([this, stemIdx, segIdx, noteNumber, durSec]() {...});  // alloc + PostMessage per match
  ```
  `handleNoteOn` runs inside `processBlock`. Segment length is set by the user. The installed log shows a 9,074,224-sample stereo stem
  (`[DownbeatDetector] ... samples=9074224 ch=2`), so one long segment is up to about 72 MB to allocate and memcpy. With LINK ALL, one note does this 4 times.
  The old voice buffer is also released on the audio thread: `v.buffer = segBuf;` drops the last reference (755). `handleNoteOff` (908-921) has the same vector allocation and per-match `callAsync`.
- **Impact:** Dropouts and crackles on MIDI notes, and they get worse as segments get longer. With large segments, a fast MIDI part can cause continuous xruns in the DAW.
- **Fix:** Store each stem as `std::shared_ptr<const juce::AudioBuffer<float>>` in `StemData` and give `Voice` a `startSample`/`endSample` window into that buffer, with no copy.
  Look up notes from a precomputed `std::array<SmallVec<(stem,seg)>,128>` that is rebuilt on the message thread. Replace audio-thread `callAsync` with a lock-free FIFO that a 30 Hz UI timer drains.
  Release buffers through a deferred-release queue that the message thread empties.

## A2 - CRITICAL - Voices and signalsmith-stretch instances are changed from the message thread while the audio thread uses them
- **File:** src/services/MidiMappingService.h:479 ("Voice pool - audio thread only, no lock needed");
  MidiMappingService.cpp:1323-1356 (playBufferDirect), 1358-1368 (stopAllVoices), 1514-1528 (setSModeModel(nullptr)), 1649-1672 (start/stopSModeTransport), 1413-1438 (handleKeyPress -> handleNoteOn)
- **Callers on the message thread:** SModePage.cpp:1449, 1470, 1516, 2537, 2556, 2589, 2692, 3522-3525, 3531, 3747-3748; SequencerSlotComponent.cpp:775-780; MainWindow.cpp:372-373
- **Evidence:**
  ```cpp
  void MidiMappingService::stopAllVoices() {
      for (auto& v : voices_) { v.active = false; if (v.stretch) v.stretch->reset(); ... } }   // message thread
  // audio thread, same objects at the same time:
  voice.stretch->process(inBuf, inFrames, outBuf, blockOut);   // :448
  s.process(inBuf, inFrames, outBuf, blockOut);                // :615 (stemStretch_, reset by start/stopSModeTransport)
  // setSModeModel(nullptr):
  v.active = false; v.buffer = nullptr;     // :1525-1526, while the audio thread holds `const auto& srcBuf = *voice.buffer;` (:329)
  ```
  The team's own comment at :223-228 records a crash in this area: "observed in stopAllVoices -> DynamicSTFT::moveOutput access violation".
  The comment attributes it to instances that had not been preset. The code shows the more likely cause: `reset()` runs at the same time as `process()` on another thread.
  `allocateVoice()` (:1297) also runs on both threads, so the audio thread and a UI click can be handed the same Voice. `std::shared_ptr` assignment is not atomic.
- **Impact:** Random host crashes (access violations) when the user clicks a segment, plays the sequencer, starts, stops or seeks the S-Mode transport, ejects, or types notes on the computer keyboard while audio is running.
  Without a crash, it shows up as corrupted audio or a silent voice.
- **Fix:** Every UI-side voice or transport operation becomes a command (PlayBuffer, StopAll, StartTransport(pos), StopTransport, DetachModel) pushed to a lock-free SPSC FIFO (`juce::AbstractFifo`).
  `getNextAudioBlock` drains the FIFO at block start and is the only place that touches `voices_`, `stemStretch_` or `reset()`. Buffer lifetimes are handled by the deferred-release queue from A1.

## A3 - CRITICAL - The segment model has no synchronisation between UI edits and audio-thread reads
- **File:** src/services/smode/SModeSegmentModel.cpp:7-22 (clear, setStemCount resize), 38-71 (addSegment push_back / removeSegment erase), 344-358 (restoreSnapshot replaces segment vectors);
  SModeService.cpp:57 (startWorkflow -> model_.clear()), 92-134 (loadStemAudio: resize + move audioBuffer); audio-thread reads at MidiMappingService.cpp:525-547 and 719-752
- **Evidence:**
  ```cpp
  // audio thread
  const auto* stem = smodeModel_->getStem(si);  const float* sL = buf.getReadPointer(0);   // :537-546
  auto matches = smodeModel_->findAllSegmentsByMidiNote(noteNumber);                       // iterates stems_[s].segments
  // message thread, MainWindow.cpp:478-581 (load new song) -> no setSModeModel(nullptr)/stopTransport first:
  smodeService_->startWorkflow(f, sc);   // -> model_.clear(): frees every stem buffer and segment vector
  ```
  `smodeModel_` is a plain `SModeSegmentModel*` (h:473). Nothing (lock, snapshot, epoch) protects reads against chop, delete, undo/redo, LINK-ALL segment replication or loading a new song.
- **Impact:** Use-after-free crashes if the user chops, deletes, undoes, or drops a new song while DAW MIDI or the S-Mode transport is playing. The window is one audio block, and edits happen constantly.
- **Fix:** Publish an immutable `PlaybackSnapshot` (per-stem `shared_ptr<const AudioBuffer>`, segment ranges, note map, mute/solo/volume) to the audio thread via an atomic pointer swap or the A2 command FIFO.
  Rebuild it on the message thread after each edit, and send DetachModel before `clear()` or a reload.

## A4 - CRITICAL - Reloading a DAW project does not restore the chopped kit, and the next save can delete the session
Restore depends on several things, and each one fails on its own:
1. **Segments are never serialized.** The processor `smode` binding (SamplarAudioProcessor.cpp:675-709) saves only `sourceFile`, `stemCount`, `bpm`, `sampleRate` and the stem file paths.
   Segments, MIDI note assignments, per-segment FX/loop, solo/mute/volume, grid offset and locked BPM are not written anywhere. A grep for any segment serializer finds none.
   `ui.smodeBpm`, `smodeSampleRate` and `smodeSegmentData` are restored (719-731) but nothing reads them. The only consumer is MainWindow.cpp:1069-1132, and it uses just the source file, stem files and stem count.
2. **Stems reload only if the editor happens to exist.** `setStateInformation` posts a one-shot `callAsync(... publish("state.restored"))` (SamplarAudioProcessor.cpp:624-626).
   Only the RootComponent subscribes to it (MainWindow.cpp:1057), and only that handler calls `smodeSvc->loadStemAudio(...)` (1109). The RootComponent constructor never checks `pendingSModeRestore`.
   If the plugin window is closed when the project loads (the normal case), the event is lost. The stems never load and DAW MIDI plays nothing, even after the editor is opened.
3. **Saving wipes the session.** With stems not loaded, `svc->isActive()` is false, so the `smode` save clears the node:
   ```cpp
   if (svc == nullptr || ! svc->isActive()) { t.removeAllChildren(nullptr); ... t.removeProperty(...); return; }   // :678-684
   ```
   Open the project, don't open SAMPLAR, save (or let the DAW autosave): the source and stem paths are gone permanently. The same happens if a save lands while a re-separation is running.
4. **The second save loses the stem paths.** The fast restore calls `loadStemAudio()`, which never sets `separation_.stemFiles_`. The save reads `svc->getSeparation().getStemFiles()` (695), which is now empty, so no `<stems>` node is written.
   The next reload goes to full re-separation, which takes minutes, or finds nothing if the source file has moved.
5. **Stems live in %TEMP%** (`FullSongService.cpp:236-237`: `tempDirectory/smode_<millis>`), so Windows disk cleanup or Storage Sense deletes them.
- **Answer to "why only 1 lane":** this is not a lane-persistence bug. Sequencer lanes are not per-stem. SModePage creates one empty lane after stems load
  (SModePage.cpp:1860-1862 `if (sequencerLanes_.empty()) addSequencerLane();`). Per-stem lanes come only from Smart Fill (3574-3583) or "+ SEQ" (897-899).
  The log line `lane 0 captured: slots=0` comes from the editor-close snapshot (installed log lines 906-921) and just means the user had not dropped segments into that lane.
  The data that matters, the segments on the stem waveforms, is not persisted at all (point 1).
- **Impact:** The core product promise fails. Close and reopen the DAW project and the chopped, mapped kit is gone, or the whole session is erased.
- **Fix:**
  - (a) Serialize the full model into the `smode` node: per stem name, file, gain, mute, solo; per segment start, end, midiNote, effects; BPM, lockedBpm, gridOffset, play mode.
  - (b) Restore at processor level without the editor: in `setStateInformation`, start a background load of the stems, then swap them into the model on the message thread and send `setSModeModel`. The UI only mirrors the model.
  - (c) Never wipe the node when inactive. Keep the last deserialized subtree until a new session replaces it.
  - (d) Copy stems to a persistent per-session folder (e.g. `%APPDATA%/Samplar/sessions/<hash>/`) and save those paths. Optionally offer to embed them as FLAC in the chunk.
  - (e) Set `stemFiles_` (or keep the paths in SModeService) in `loadStemAudio`.

## A5 - CRITICAL - Closing the editor can crash the host through callbacks that outlive it
- **File:** SModePage.cpp:3506-3555 and 3717-3757 (sequencer playback chain), 366 (hostTempoSub_), 1104-1120 (destructor), 596/1577/1675/2331/3010 (detached std::threads)
- **Evidence:**
  ```cpp
  *playSlot = [this, laneIndex, ...](int idx) { ... sequencerLanes_[...]->setPlayingSlot(...); ...
      juce::Timer::callAfterDelay(sp.durationMs, [playSlot, cancelFlag, nextIdx]() { if (!cancelFlag->load()) return; (*playSlot)(nextIdx); }); };
  *playheadTick = [this, cancelFlag, ...]() { for (auto& lane : sequencerLanes_) ... callAfterDelay(30, ...); };
  SModePage::~SModePage() { stopTimer(); /* unsubscribes 4 topics */ }   // never calls stopAllSequencerPlayback(), never clears seqPlayingFlag_
  hostTempoSub_ = bus->subscribe("host.tempo", [this](const juce::var& v) { ... applyBpm(hostBpm); });  // never unsubscribed in dtor
  std::thread([this, readPtrs, ...]() { ... juce::MessageManager::callAsync([this, finalBpm]() { if (!smodeService_) return; applyBpm(finalBpm); }); }).detach();
  ```
- **Impact:** Three deterministic crashes:
  - Close the plugin window while a sequencer lane is playing: the next tick dereferences the freed SModePage within about 30 ms.
  - Turn DAW Sync on, close the window, then change or automate tempo in the DAW: `processBlock` republishes `host.tempo`, and `applyBpm` runs on freed memory.
  - Close the window during BPM, onset or quick-assign analysis (a few seconds): the posted lambda touches the freed page.
    The BPM thread also reads raw stem pointers (`readPtrs`), which dangle if the song is ejected or replaced meanwhile.
- **Fix:** In `~SModePage`, call `stopAllSequencerPlayback()` and unsubscribe `hostTempoSub_`. Capture `juce::Component::SafePointer<SModePage>` or a shared alive flag in every deferred lambda.
  Run analysis on a processor-owned `juce::ThreadPool` (joinable, cancellable) on a copied or shared_ptr buffer. Move sequencer playback to the audio thread (A10).

## A6 - CRITICAL - Two instances share one global ServiceLocator, so state crosses between them and freed pointers are reused
- **File:** src/app/ServiceLocator.h:44-64, 85-88; SamplarAudioProcessor.cpp:352-353, 404; SamplarAudioProcessorEditor.cpp:19-20, 96-116; SModePage.cpp:50, 680; MainWindow.cpp:638-646, 655-668
- **Evidence:**
  ```cpp
  ServiceLocator::setAppState(appState_.get());          // processor ctor only: last-constructed instance wins
  ServiceLocator::initialise(&p.getServiceRegistry(), &p.getEventBus());  ServiceLocator::setUIState(&p.getUIState());  // editor ctor (no setAppState)
  static void shutdown() { sRegistry = nullptr; sBus = nullptr; }   // any instance's dtor nulls it for everyone; sUIState/sAppState never cleared
  if (auto* app = samplar::ServiceLocator::getAppState()) stateBinding_ = std::make_unique<ScopedStateBinding>(*app, ...);  // SModePage
  if (auto* bus = samplar::ServiceLocator::getBus()) { bus->unsubscribe("processing.result", resId_); ... }  // RootComponent dtor
  static int midiMode = 0;   // SModePage mono/poly/gate cycle, shared process-wide
  ```
- **Impact with two SAMPLAR instances in one project:**
  - Instance A's SModePage bindings register on instance B's AppState. A's lanes, pitch and mod settings are saved into B's project chunk and not A's. Both pages write the same `smodePage` node, so the last one wins.
  - Delete instance B while A's editor is open, then close A's editor: `ScopedStateBinding::~` calls `unregisterBinding` on the freed AppState and crashes. Reopening A's editor after B is gone registers on the freed AppState and crashes.
  - Deleting B nulls the registry and bus for A. A's UI silently stops working until a visibility change.
  - `sUIState` can point at the deleted instance. The RootComponent destructor and `switchTo` write through it (use-after-free).
  - The RootComponent destructor unsubscribes A's handler IDs on B's bus. Per-bus IDs start at 1 and follow the same order in each instance, so this removes B's live handlers (B's UI stops getting separation progress or results).
  - The locator is re-claimed only in `visibilityChanged`/`broughtToFront`, not on mouse or keyboard input. With two editors open, knobs and pads can drive the other instance's MidiMappingService.
  - Play-mode cycling (`midiMode`) is shared and gets out of sync with each instance. `microFadeEnabledFlag()` is process-global.
  - Each new instance deletes `SAMPLAR_console.log` (SamplarAudioProcessor.cpp:295) and replaces the global JUCE logger (see A21).
- **Fix:** Remove the static locator from the plugin build. Construct `RootComponent`/pages with a `PluginContext&` (registry, bus, AppState, UIState) that belongs to the processor.
  As an interim step:
  - claim all four pointers in the editor constructor and on every focus or mouse-down;
  - in `shutdown()` null only the pointers that still belong to this instance;
  - store the `EventBus&` used to subscribe and unsubscribe from that same bus;
  - make `midiMode` a member that reads from and writes to the service.

## A7 - HIGH - Removing the plugin during a separation leaves a detached thread with a dangling `this`
- **File:** src/services/smode/SModeSeparationService.cpp:62-176 (thread, `->detach()` at 176), 17-23 (shutdown), 180-187 (cancel)
- **Evidence:**
  ```cpp
  separationThread_ = std::make_unique<std::thread>([this]() { FullSongService fullSongSvc(bus_); ... fullSongSvc.separateDrums(sourceFile_, cfg, "smode_"); ... stemFiles_ = foundStems; ... });
  separationThread_->detach();
  void SModeSeparationService::shutdown() { cancelSeparation(); if (separationThread_ && separationThread_->joinable()) separationThread_->join(); }  // never joinable after detach
  ```
  `cancelRequested_` is checked only after the ONNX inference returns. FullSongService already accepts a `shouldCancel` callback (FullSongService.cpp:248), but this path does not pass one.
  The progress lambdas `callAsync([this, ...]{ if (!separating_.load()) ... })` also read the freed object.
- **Impact:** If the user deletes SAMPLAR, closes the project or quits the DAW during the minute-long separation, the thread writes `stemFiles_` and reads `bus_` on freed memory.
  If the host unloads the DLL, the thread may be executing code that no longer exists. The result is a crash, typically some time after the user has moved on.
- **Fix:** Keep the thread joinable. Pass `cancelRequested_` into `cfg`'s shouldCancel so the ONNX loop exits, and `join()` in `shutdown()`.
  Capture a shared alive token, not raw `this`, in every `callAsync`.

## A8 - HIGH - Many async callbacks capture raw `this`, and the `destroyed_` check reads freed memory
- **File:** SamplarAudioProcessor.cpp:511-513, 624-626; AppState.cpp:53-72; MidiMappingService.cpp:654-658, 680-712, 787-799, 863-876, 913-921, 937-945; services destroyed at SamplarAudioProcessor.cpp:407-408
- **Evidence:**
  ```cpp
  juce::MessageManager::callAsync([this]() { eventBus_.publish("state.restored", juce::var()); });   // processor
  juce::MessageManager::callAsync([this]() { if (destroyed_.load(...)) return; bus_.publish(...); }); // MidiMappingService, object deleted by serviceRegistry_.reset()
  ```
  `destroyed_` is a member of the deleted object, so checking it after `serviceRegistry_.reset()` is itself a use-after-free.
- **Impact (PLAUSIBLE timing):** A crash when the host destroys the instance while messages are still queued. Examples: a plugin scanner or validator doing instantiate, setState, destroy; deleting SAMPLAR while it plays notes or the S-Mode transport reaches its end.
- **Fix:** Use `std::shared_ptr<std::atomic<bool>> alive` captured by value, or `juce::WeakReference`, for all deferred lambdas. Better, remove audio-thread `callAsync` entirely (FIFO plus timer, see A1).

## A9 - HIGH - The audio thread does file I/O, takes locks and posts messages when the tempo changes
- **File:** SamplarAudioProcessor.cpp:486, 504, 510-513; MidiMappingService.cpp:495-497; logger chain MainWindow.cpp:465-466 -> MultiLogger.h:22-26
- **Evidence:**
  ```cpp
  if (hostBpm > 0.0 && (wasReRequested || std::abs(hostBpm - lastHostBpm_) > 0.01)) {
      juce::Logger::writeToLog("[SamplarAudioProcessor] Publishing host.tempo: " + juce::String(hostBpm, 3));  // String alloc
      juce::MessageManager::callAsync([this, hostBpm]() { eventBus_.publish("host.tempo", juce::var(hostBpm)); }); }
  void logMessage(const juce::String& message) override { if (fileLogger_) fileLogger_->logMessage(message); console_.appendLine(message); }
  ```
  `juce::FileLogger::logMessage` takes a CriticalSection and opens, writes and closes the file on every line. `DebugConsole::appendLine` takes `std::mutex queueMutex_` (DebugConsole.h:40).
- **Impact:** During tempo automation or a tempo ramp, every block opens the log file, takes two locks and posts a message, which causes dropouts. It also floods the message queue.
- **Fix:** Store host BPM in a `std::atomic<double>` and have a UI timer poll it and publish. Never log from `processBlock`, `getNextAudioBlock` or `handleNoteOn/Off`.

## A10 - HIGH - No real host sync: MIDI timing is quantized to block starts and the sequencer runs on UI timers
- **File:** SamplarAudioProcessor.cpp:461-479, 490-496; SModePage.cpp:3541-3555, 3717-3757 (sequencer driven by wall-clock `callAfterDelay`); MidiMappingService.cpp:128-169 (CC, pitch-bend and aftertouch routing exists only for standalone)
- **Evidence:**
  ```cpp
  for (const auto metadata : midiMessages) { const auto msg = metadata.getMessage();
      if (msg.isNoteOn()) midiMapping_->handleNoteOn(...); else if (msg.isNoteOff()) midiMapping_->handleNoteOff(...); }   // samplePosition ignored
  juce::AudioSourceChannelInfo info(&buffer, 0, buffer.getNumSamples()); midiMapping_->getNextAudioBlock(info);
  ```
  Only `getBpm()` is read. A grep finds no `isPlaying`, `getPpqPosition`, `isNonRealtime`, `setLatencySamples` or `processBlockBypassed` anywhere.
- **Impact:**
  - Every MIDI note starts at the block boundary. At the logged 1024-sample buffer at 48 kHz that is up to 21 ms of jitter, so drum chops sound flammy and loose.
  - In gate mode, a note-on and note-off in the same block kill the voice before it makes any sound.
  - The step sequencer and S-Mode transport do not start or stop with the DAW and do not lock to bars or PPQ. They drift with message-thread load and render incorrectly in offline bounces.
  - Pitch-bend, mod wheel, aftertouch, CC learn and All-Notes-Off from the DAW are ignored in the VST3. They work only in the standalone app.
- **Fix:**
  - Split the block at each event's `samplePosition` (render up to the event, apply it, continue).
  - Move the sequencer into `getNextAudioBlock`, driven by host PPQ and isPlaying, with the BPM fallback used only when there is no playhead.
  - In `processBlock`, forward pitch-wheel, controller, aftertouch, channel-pressure and CC120/123 to the existing `onPitchBend`/`onController`/`onAftertouch` handlers.

## A11 - HIGH - Looped segments never stop on note-off and hang after the DAW stops
- **File:** MidiMappingService.cpp:765, 880-900; SModePage.cpp:333-344; `setLoopGateMode` has no callers (grep)
- **Evidence:**
  ```cpp
  v.looping = seg->effects.loop;                         // Loop button sets this on ALL segments
  if (!sampleGate && !loopGate) return;                  // default one-shot: note-off ignored
  if ((voice.looping && loopGate) || (!voice.looping && sampleGate)) voice.active = false;  // loopGate is never true
  ```
- **Impact:** With Loop on, any MIDI-triggered segment loops forever, even in GATE mode and after the DAW stops. Only a voice steal or a UI stop ends it, and there is no All-Notes-Off handling.
- **Fix:** In GATE mode, release looping voices on note-off. Handle CC120/CC123 and host transport stop by releasing all voices with a short fade.

## A12 - HIGH - Pitched voices are about 30-80 ms late, latency is not reported to the host, and pitched one-shots lose their tails
- **File:** MidiMappingService.cpp:351-371 (bypass when pitch is 0), 549-561 (team comment: signalsmith "~30-80 ms" latency), 469-480 (only one extra block fed after end of sample), 240 and 249 (presetCheaper / presetDefault)
- **Evidence:**
  ```cpp
  const bool voiceUseRealtimeStretch = (... && std::abs(voiceTargetSemi) >= 1.0e-3f);   // unpitched voices bypass, pitched go through STFT
  if (reachedEnd && !voice.looping) { if (voice.eosFedSilence) voice.active = false; else voice.eosFedSilence = true; }  // one block of flush
  ```
- **Impact (magnitude PLAUSIBLE, taken from the code comment):**
  - Any stem with its pitch knob off zero triggers 30-80 ms late compared with unpitched stems and with the DAW grid. Nothing is reported via `setLatencySamples`, so the DAW cannot compensate.
  - At 128-1024-sample buffers, one block of flush is far shorter than the stretcher's latency, so the last tens of milliseconds of each pitched one-shot are cut off, with a click at the hard stop.
  - Moving the knob between 0 and non-zero while a voice plays switches paths: either a gap or a jump.
- **Fix:** Feed silence until `stretch->outputLatency()` (plus input latency) frames have drained. Report a constant latency with `setLatencySamples` and route every voice through the stretcher (or delay the bypass path by the same amount).
  Crossfade when switching paths.

## A13 - MEDIUM - Clicks: voices are cut instantly, and loops wrap without a crossfade
- **File:** MidiMappingService.cpp:728-729 and 826-830 (mono kill), 896 (gate note-off), 1315 (steal), 1362 (stopAllVoices), 491-497 and 410-419 (loop wrap), 512-514 (micro-fade only at natural start and end of the legacy path)
- **Evidence:** `voice.active = false;` everywhere. The realtime-stretch path applies no fade at all.
- **Impact:** Audible clicks on MONO retriggers, GATE releases, voice steals, sequencer steps (every step calls `stopAllVoices`, SModePage.cpp:3522/3531) and every loop iteration.
- **Fix:** Add per-voice release-ramp state (2-5 ms) and fade instead of killing. Do a short equal-power crossfade at loop wrap. Apply the micro-fade in the stretch path as well.

## A14 - MEDIUM - Muting all stems produces a NaN transport position
- **File:** MidiMappingService.cpp:533-543, 636-643
- **Evidence:**
  ```cpp
  if (!stem || !smodeModel_->isStemAudible(si) || ...) continue;   // happens BEFORE maxStemSamples is updated
  ...
  if (newPos >= maxStemSamples) { if (looping) newPos = std::fmod(newPos, static_cast<double>(maxStemSamples)); ... }  // fmod(x, 0) = NaN
  ```
- **Impact:** If the loop is on, muting or soloing so that nothing is audible sets the position to NaN permanently. Unmuting stays silent and the UI playhead shows NaN. If the loop is off, the transport stops the moment all stems are muted.
  The same happens while the model is empty during a reload.
- **Fix:** Compute the length from all stems (or the model's length), and skip advancing when `maxStemSamples <= 0`.

## A15 - MEDIUM - Voice budget and stealing: 16 voices, and the sequencer loop is the first to be stolen
- **File:** MidiMappingService.h:480; MidiMappingService.cpp:1297-1317, 728-729
- **Evidence:** The steal picks `v.position` max. The pre-rendered sequencer mix buffer (SModePage.cpp:3644-3646, 3747-3748) is the longest voice, so it is stolen first. MONO kills every voice, including sequencer and direct-play voices.
- **Impact:** With LINK ALL (4 voices per note), only about 4 notes can sound at once. Playing pads during sequencer playback can stop the sequencer abruptly.
- **Fix:** Use separate pools for sequencer/transport and MIDI. Steal by age (start counter) and prefer released voices. Scope MONO to the MIDI pool.

## A16 - MEDIUM - "markDirty" never marks the VST3 project dirty
- **File:** src/app/AppState.cpp:106-109; JUCE/modules/juce_audio_plugin_client/juce_audio_plugin_client_VST3.cpp:1595-1596
- **Evidence:**
  ```cpp
  void AppState::markDirty() { processor_.updateHostDisplay(); }   // default flags = latency|paramInfo|program
  if (details.nonParameterStateChanged) flags |= pluginShouldBeMarkedDirtyFlag;   // only this marks the project dirty
  ```
- **Impact:** Chopping, mapping or lane edits do not set the host's "modified" flag, so Cubase, Studio One and Bitwig may close without asking to save. If it is fixed naively, `serialize()` itself changes the tree through the snapshot and re-dirties the project on every save.
- **Fix:** Call `updateHostDisplay(juce::AudioProcessorListener::ChangeDetails{}.withNonParameterStateChanged(true))` on the message thread, and suppress it inside `serialize`/`deserialize`.

## A17 - MEDIUM - Play settings are not persisted
- **File:** SModePage.cpp:680-695 (`static int midiMode`), 322-345 (loop), 349-385 (DAW sync); SamplarAudioProcessor.cpp:690 and 720 (bpm saved, never consumed)
- **Impact:** After a reload: POLY/MONO and 1SH/GATE return to POLY 1SH (and the button label can disagree with the service after the editor is reopened). Loop, DAW Sync, grid lock and locked BPM are lost. BPM is re-detected instead of restored.
- **Fix:** Add these fields to the persisted `smode` node (see A4) and restore them into the service.

## A18 - MEDIUM (PLAUSIBLE) - State callbacks run UI code on whatever thread calls get/setStateInformation
- **File:** AppState.cpp:152-170 and 239-260; SamplarAudioProcessor.cpp:614-629 (comment: "setStateInformation may be called from any thread"); SModePage.cpp:57-141
- **Evidence:** `deserialize` calls `e.binding.restore(child)` synchronously, and SModePage's restore does `sequencerLanes_.clear(); addSequencerLane(); resized(); repaint();`.
  `serialize` calls the save lambdas, which read component state (`sequencerLanes_[i]->captureState()`). There is no `MessageManager::existsAndIsCurrentThread()` guard.
- **Impact:** VST3 specifies the UI thread and most hosts follow that. If a host or wrapper calls these from another thread, components are created and read off the message thread, which races or asserts.
- **Fix:** If not on the message thread, keep the tree and defer UI-binding restore with `callAsync` (guarded). Make UI bindings push into the tree on change so `serialize` only streams `root_` under a lock.

## A19 - MEDIUM - Stem WAVs load synchronously on the message thread
- **File:** SModeService.cpp:92-139 (`loadStemAudio`), called from MainWindow.cpp:1109 and from the separation-complete event; SamplarAudioProcessor.cpp:785-831 (midi binding reads files inside setStateInformation)
- **Impact:** Four 9M-sample stereo stems (about 290 MB of float) are decoded on the UI thread, which freezes the host for seconds during project load and after separation.
- **Fix:** Decode on a background job into new buffers, then swap them in on the message thread (combined with the A3 snapshot).

## A20 - MEDIUM - License dialog runs a modal loop inside the editor constructor
- **File:** SamplarAudioProcessorEditor.cpp:23-39; LicenseActivationDialog.cpp (`runModalLoop()` under `#if JUCE_MODAL_LOOPS_PERMITTED`); CMakeLists.txt:678 (`JUCE_MODAL_LOOPS_PERMITTED=1` for the plugin)
- **Impact:** A nested event loop runs inside the host's `createView`. The host can re-enter (close the window, delete the instance, run a project load) while the dialog is open, which can cause hangs or crashes in hosts sensitive to modal loops.
- **Fix:** Show license activation as a non-modal overlay inside the editor, using the async callback.

## A21 - MEDIUM - Logging stops after the first editor close, and logger swaps race across instances
- **File:** MainWindow.cpp:465-466 (RootComponent installs MultiLogger globally), 676 (`juce::Logger::setCurrentLogger(nullptr)` in dtor); SamplarAudioProcessor.cpp:293-302 and 417-418
- **Evidence:** The installed log (`%APPDATA%/Samplar/logs/SAMPLAR_console.log`) ends exactly at `[RootComponent] Destructor complete`. No processor-destructor line ever appears, because the processor's `earlyLogger_` is never reinstated.
- **Impact:** After the user closes the window once, crash diagnostics are gone for the rest of the session. With two instances, `setCurrentLogger` swaps while background or audio threads are logging, which can call a destroyed logger (PLAUSIBLE use-after-free).
- **Fix:** Use one processor-owned, thread-safe logger, ideally with a background writer queue. The editor should register a console listener on it instead of replacing the global logger.

## A22 - LOW (PLAUSIBLE) - Scratch buffer overflow if the host sends a block larger than max(prepared, 4096)
- **File:** MidiMappingService.cpp:260-268, 399-448, 589-615
- **Evidence:** `voiceOutScratch_`/`stemOutScratch_` are sized `maxFrames = max(samplesPerBlockExpected, 4096)`, but `process(..., outBuf, blockOut)` uses `blockOut = numSamples`, which is never clamped. `inFrames` is clamped, so the in and out counts also diverge in that case.
- **Impact:** Heap overflow only if a host breaks the VST3 max-block contract.
- **Fix:** Process in chunks of at most the scratch size.

## A23 - LOW - Sample-rate conversion exists but uses linear interpolation
- **File:** MidiMappingService.cpp:759 (`resampleRatio = modelSr / currentSampleRate_`), 395-438 (voice), 531 and 593-606 (transport)
- **Answer:** Yes, 44.1 kHz stems are resampled to the host rate. The installed log shows the host at 48 kHz (`prepareToPlay sr=48000`) and stems at 44.1 kHz.
  The resampler is 2-point linear, which causes aliasing and high-frequency loss.
- **Fix:** Resample the stems once on load to the host rate on a background thread (re-run if `prepareToPlay` changes the rate), or use `juce::LagrangeInterpolator`/windowed sinc.

## A24 - LOW - Dead code and vestigial paths compiled into the plugin
- `SequencerAudioSource`, `BufferAudioSource`, `BandProcessor`, `SoundTouchSource`, `AudioEngine` and `SpectralSeparator` are referenced only from their own files.
- `SamplePreviewCache` is never constructed and `setSampleCache` is never called, so `resolveBuffer` always produces a null buffer (MidiMappingService.cpp:1250-1253).
  The `midiMap_` browser-pad path is unused in SAMPLAR (no caller of mapSampleToNote, lockSampleToNote or registerSampleAtNextFreeNote), and the `midi` binding always saves 0 slots.
- The `PluginPreviewService` mixer has no inputs but is still processed every block (SamplarAudioProcessor.cpp:517-531).
- `encodeBufferRaw`/`decodeBufferRaw` are unused (542-599). The stereo input bus is declared on an instrument (26-27).
- **Fix:** Remove these or move them out of the target. This reduces the audit surface and per-block work.

## A25 - LOW - The browser-pad path reuses a voice without resetting its realtime fields
- **File:** MidiMappingService.cpp:833-840
- **Evidence:** `stemIdx`, `startSample`, `endSample`, `eosFedSilence` and `stretch` state are not reset (the S-Mode path at 754-783 does reset them).
- **Impact:** A stolen former S-Mode voice would go through the pitch path with a stale `endSample`. This is latent because the path is currently dead (A24).

## A26 - LOW - Tail length and bypass are not handled
- **File:** SamplarAudioProcessor.h:37 (`getTailLengthSeconds() = 0.0`); no `processBlockBypassed`
- **Impact:** Offline renders can cut stretcher and loop tails. Voices freeze during bypass and resume afterwards with stale state.

## A27 - LOW - Standalone: MIDI arrives on a different thread from the audio callback
- **File:** MidiMappingService.cpp:90-104 and 128-169; 2077 and 2092 (CC learn `apply(v)` callbacks run on the MIDI input thread)
- **Impact:** Same voice race as A2, plus UI callbacks invoked off the message thread. This affects only SamplarApp.
- **Fix:** Push incoming MIDI into a `juce::MidiMessageCollector` and drain it in `getNextAudioBlock`. Marshal CC-learn UI callbacks to the message thread.

## A28 - LOW - Every bypassed (unpitched) stem voice resets its stretcher on every block
- **File:** MidiMappingService.cpp:364-371
- **Impact:** Wasted CPU in proportion to active voices. Reset once when the voice enters bypass instead.

---

### Questions answered
- **What `setStateInformation` restores:** a stem file list and source path only, and only if an editor exists to consume the event.
  Segments, MIDI mappings and play modes are not restored. BPM is saved but ignored. Sequencer lanes and stem pitch/mod are restored, but only into the editor's pages, possibly on the wrong instance (A6).
- **Thread for `setStateInformation`:** the caller's thread. There is no marshalling (A18). The restore event goes to the message thread via `callAsync`.
- **Why the log says "1 lane":** see A4. It is the default empty lane, not a persistence defect.
- **Sample rate:** resampled with linear interpolation (A23). Changing the sample rate re-runs `prepareToPlay`, which kills voices and reconfigures the stretchers, which is fine.
- **Block size:** `prepareToPlay` sizes the scratch buffers. `processBlock` copes with smaller blocks; see A22 for larger ones.
- **Denormals:** `juce::ScopedNoDenormals` is used in `processBlock`. The standalone `AudioSourcePlayer` path has no FTZ (LOW).
