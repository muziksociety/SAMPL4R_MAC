# SAMPLAR Audit C - Separation + Analysis Backend + Data Paths

Scope: OnnxSeparationProvider, FullSongService, FullSongSeparationService, smode/*, ProcessingCoordinatorService,
analysis/*, DataPaths, OnnxDelayLoad, RecentSongs, stem cache. READ-ONLY audit, no build.
Date: 2026-10-20. Source = working tree on `main` (HEAD cf8066ba).

Legend: VERIFIED = read in code (and/or log). PLAUSIBLE = reasoning/timing-dependent, not reproduced.

## 0. Note on the log evidence

`%APPDATA%/Samplar/logs/SAMPLAR_console.log` is dated **2026-05-17 22:31**, i.e. BEFORE the v1.0.0 commit
(cf8066ba, 2026-05-18 14:30) and before the installed SAMPLAR.vst3 binary (2026-05-26). Several log strings no
longer exist in source:
- "Initializing (ONNX backend + UVR fallback)" -> now `FullSongService.cpp:19` "Initializing (ONNX backend)".
- "Model: htdemucs_ft.yaml" -> now `SModeSeparationService.cpp:39` returns "htdemucs.onnx (4-stem ONNX)".
- "locateOnnxModel diag" / "SEPARATION ONLY (for WavePreviewPage)" -> gone.
So the "stale UVR code path" question is resolved: those strings came from the pre-release build. UVR/Python
remnants that still exist are listed in C19. The garbled progress text (C6) and the dev model path (C7)
are still present in current source.

---

## CRITICAL / HIGH

### C1 - CRITICAL - S-Mode separation thread is detached and captures raw `this`: use-after-free / crash when plugin is removed or project closed mid-separation (VERIFIED)
- File: `src/services/smode/SModeSeparationService.cpp:62-176`, `:17-23`; `src/plugin/SamplarAudioProcessor.cpp:405-413`
- Evidence:
  ```cpp
  separationThread_ = std::make_unique<std::thread>([this]() { ... });   // :62
  separationThread_->detach();                                            // :176
  void SModeSeparationService::shutdown() {
      cancelSeparation();
      if (separationThread_ && separationThread_->joinable())   // always false after detach()
          separationThread_->join();
  }
  ```
  The worker touches `this` after the 4+ minute ORT run: `cancelRequested_.load()` (:88), `stemFiles_ = foundStems` (:154),
  and posts `callAsync([this]{ separating_ = false; bus_.publish(...) })` (:91,103,146,162).
  `SamplarAudioProcessor.cpp:413` claims "All service threads are joined by shutdownAll() above" - false for this service.
- User impact: user closes the DAW project / deletes the plugin while stems are separating -> service is destroyed while
  the thread keeps running -> heap write into freed memory (`stemFiles_`), callAsync lambda dereferences freed
  `bus_`/`separating_`, and if the host unloads the DLL the thread executes unmapped code. DAW crash, possibly
  corrupting the project save that triggered the close.
- Fix: copy FullSongSeparationService's joinable pattern (it was written for exactly this bug, see its header
  comment `FullSongSeparationService.h:27-30`): keep the thread joinable, wire a real cancel (C2), join in shutdown().
  Guard callAsync lambdas with a `std::shared_ptr<std::atomic<bool>> alive` token captured by value.

### C2 - HIGH - Cancellation is not wired at all; user is locked out for the whole separation (VERIFIED)
- Files: `src/services/FullSongService.h:55`, `src/services/FullSongService.cpp:248`,
  `src/services/smode/SModeSeparationService.cpp:66-97,179-186`, `src/ui/MainWindow.cpp:483-509`,
  `src/ui/Pages/SModePage.cpp:1067`
- Evidence:
  - `std::function<bool()> shouldCancel;` is declared and forwarded to `provider_->separate(..., shouldCancel)` but
    **never assigned anywhere** (grep: only read at FullSongService.cpp:248). The provider's per-chunk check
    (`OnnxSeparationProvider.cpp:468`) therefore never fires.
  - `SModeSeparationService` checks `cancelRequested_` only AFTER `separateDrums()` returns (:88).
  - No UI calls `cancelSeparation()` (grep: only from shutdown()). Eject button is hidden until stems load
    (`SModePage.cpp:1067`), and loading another file during separation is refused with
    "Please wait for it to finish" (`MainWindow.cpp:483-494`).
  - FullSongSeparationService has the same gap: its dtor/shutdown `join()` would block the message thread for the
    rest of the separation (currently moot, service is dead code - C19).
- Log: `inference done in 260.69 s (RTF=1.267)` for a 205.76 s song on 4 physical cores -> a 10-min song is ~13 min
  of 100% CPU with no way out except killing the DAW.
- Fix: in SModeSeparationService set `fullSongSvc.shouldCancel = [this]{ return cancelRequested_.load(); };`
  (with the alive-token from C1); also pass an `Ort::RunOptions` and call `runOptions.SetTerminate()` from cancel
  so the current ~7 s chunk aborts immediately. Add a Cancel button on the loading lanes and let Eject/Drop cancel
  the running job instead of refusing.

### C3 - HIGH - Unbounded memory in separate(): ~3.6 GB peak for a 10-min song, and std::bad_alloc is uncaught -> std::terminate kills the DAW on long files (VERIFIED code, sizes computed)
- File: `src/services/separation/OnnxSeparationProvider.cpp:302-305,323,352-353,405-412,595-600,263`
- Evidence (L = samples at 44.1 kHz; 10 min -> L = 26.46 M):
  | buffer | line | type | 10-min size | lifetime |
  |---|---|---|---|---|
  | chL/chR | 302 | float x2 | 212 MB | whole function |
  | refMean0 | 323 | double | 212 MB | whole function (only needed for 2 scalars) |
  | padL/padR | 352 | float x2 | 212 MB | whole function |
  | outL/outR[4] | 405-410 | **double** x8 | 1,694 MB | whole function |
  | sumWeight | 412 | double | 212 MB | whole function |
  | finalL/finalR[4] | 595-600 | float x8 | 847 MB | whole function (pure copy of out*) |
  | writeStemWav buf | 263 | float x2 | 212 MB | per write |
  Peak ~3.6 GB + srcBuf during load (+212 MB, +460 MB for a 96 kHz source) + ORT weights/arena. All allocations
  are outside the only try/catch (which wraps `session_->Run` at :492-554). The SMode worker lambda has no
  try/catch either, so a `std::bad_alloc` escapes a std::thread -> `std::terminate()`.
  After separation, SModeService loads the 4 stems again (847 MB) and BPM/downbeat threads each copy the drum
  stem (212 MB each + mono copies).
- User impact: DJ mixes / long live recordings crash the DAW instead of showing an error; even 5-10 min songs push
  a 32 GB DAW session hard.
- Fix: (1) accumulate in float, not double; (2) compute mean/std in a streaming pass (drop refMean0); (3) free
  chL/chR after building pad; (4) normalise + crop in place and write WAVs straight from out* (drop finalL/R);
  (5) wrap the whole body in try/catch(std::bad_alloc) and return a clean error; (6) add a duration guard
  (e.g. warn > 15 min). A streaming OLA with a 1-segment ring buffer brings peak to ~O(segment) + output.

### C4 - HIGH - Audio thread reads SModeSegmentModel with no synchronisation; dropping a new song while stems are loaded frees buffers the audio thread is reading (VERIFIED race, crash timing PLAUSIBLE)
- Files: `src/services/smode/SModeSegmentModel.h:185-190` (no lock), `SModeSegmentModel.cpp:7-22`,
  `src/services/MidiMappingService.cpp:525-545,719-745`, `src/ui/Pages/SModePage.cpp:4124-4137` (filesDropped),
  `src/services/smode/SModeService.cpp:56-57,99,120`
- Evidence: `MidiMappingService` processBlock iterates `smodeModel_->getStem(si)->audioBuffer` and
  `findAllSegmentsByMidiNote()` (allocates a vector on the audio thread) with a plain raw pointer. The message thread
  mutates the same vectors: `model_.clear()` (startWorkflow :57), `stems_.resize()` (:99), `audioBuffer = std::move`
  (:120), clearSegments/addSegment in auto-segment. `isInterestedInFileDrag` accepts drops at any time
  (`SModePage.cpp:4100`), and the drop path -> `onRequestSeparation` -> `startWorkflow` -> `model_.clear()` never
  calls `setSModeModel(nullptr)`/`stopSModeTransport()` (only the Eject path does, `SModePage.cpp:1882-1889`).
- User impact: drop a second song while the S-Mode transport is playing (or while a MIDI controller fires notes)
  -> audio thread reads freed sample memory -> DAW crash.
- Fix: publish model data to the audio thread through an immutable snapshot (`std::shared_ptr<const StemSet>` swapped
  atomically, old one released on message thread), or a SpinLock/try-lock the audio thread respects. At minimum make
  the drop path run the same detach sequence as unloadCurrentAudio() before startWorkflow().

### C5 - HIGH - Analysis threads are detached and capture raw `this` of SModePage / SModeService -> crash when editor is closed during analysis (VERIFIED)
- Files: `src/ui/Pages/SModePage.cpp:3010-3047` (BPM), `:596-657`, `:1577-1603`, `:1675-1709`, `:2331-2370`,
  `:3973-4000`; `src/services/smode/SModeService.cpp:187-214` (downbeat)
- Evidence: e.g. BPM thread
  ```cpp
  std::thread([this, readPtrs, ...]() { ... detector.detect(...) ...
      juce::MessageManager::callAsync([this, finalBpm]() { if (!smodeService_) return; applyBpm(finalBpm); });
  }).detach();
  ```
  SModePage has no alive token (grep "alive" in SModePage.cpp -> none; MainWindow does use `alive_`). The Hybrid
  detector runs FFT + DWT + BeatThis on the full drum stem (several seconds, see C12); closing the plugin window
  in that window destroys SModePage, then the callAsync dereferences it. `readPtrs` point into model buffers that
  eject/drop (C4) can free while the copy runs. The downbeat thread posts `model_.setGridOffsetSeconds` on a possibly
  destroyed or *re-loaded* model (no generation id -> old song's offset applied to new song).
- Fix: capture `juce::Component::SafePointer<SModePage>` (or shared alive flag) in every callAsync; copy the stem
  buffer on the message thread or hold a shared_ptr snapshot (C4); tag results with a load-generation counter.

---

## MEDIUM

### C6 - MEDIUM - Garbled progress log is a printf format bug with an out-of-bounds read (VERIFIED)
- File: `src/services/FullSongService.cpp:111`
  ```cpp
  juce::Logger::writeToLog(juce::String::formatted("[FullSongService] %d%% - %s", pct, msg.toRawUTF8()));
  ```
- Root cause: on Windows `juce::String::formattedRaw` uses `_vsnwprintf` (`JUCE/modules/juce_core/text/juce_String.cpp:1871-1880`).
  In MSVC wide printf, `%s` means `wchar_t*`. The UTF-8 `char*` is reinterpreted as UTF-16:
  "Qu","eu","ed" -> U+7551 U+7565 U+6465 = "畑略摥" (log line 292 = "Queued"); "Loading ONNX model..." ->
  "潌摡湩⁧乏塎洠摯汥⸮". The reader continues past the 1-byte NUL until it finds a 16-bit zero, so it reads heap
  bytes beyond the string (visible trailing junk "6彴0翸", "Ā"). JUCE's own doc (juce_String.h:935-942) warns against this.
- Impact: unreadable support logs; undefined behaviour on the separation worker thread (an access violation is
  possible if the string sits at a page end). It is the only `formatted(...%s...)` in src.
- Fix: `juce::Logger::writeToLog("[FullSongService] " + juce::String(pct) + "% - " + msg);`

### C7 - MEDIUM - Data/model resolution prefers a `portable_root.txt` pointer over the plugin's own bundle -> installed plugin silently runs on dev/stale assets (VERIFIED)
- Files: `src/plugin/SamplarAudioProcessor.cpp:73-103` (strategy 0: pointer) runs BEFORE `:105-120` (strategy 0.5: own
  bundle `Contents/x86_64-win`); `src/app/App.cpp:93-131` (standalone rewrites the pointer on EVERY launch whenever
  its exe dir has data/ models/ or python/); `src/services/FullSongService.cpp:171-197` (DataPaths models dir is
  candidate #1); `src/services/analysis/BeatThisDetector.cpp:61-88`.
- Evidence: `portable_root.txt` = `B:\SANTI\VST PROJECT\SAMPLAR\build\SAMPLAR_Portable\Samplar_Data`; log lines
  295-297 and BeatThis "Loading ONNX model: B:\SANTI\...\Samplar_Data\models\beat_this.onnx" - the installed plugin
  in Program Files loads both models from the dev tree. The release skill says the pointer mechanism is deprecated
  (2026-05-18) but the code still reads AND writes it. The installer neither deletes it nor needs it (it ships
  models inside the bundle). `hasResources()` only checks for `data/`, not `models/`.
- Impact:
  - Dev machine: "test the installed build" actually tests dev models/data -> release regressions in the bundle are
    invisible (a missing model in the installer would not be caught on this machine).
  - End users who ever extracted a portable build: a stale pointer to a folder that still exists wins forever.
    An old portable with the pre-2026-05-07 ModernMube htdemucs export (2-input contract) makes separation fail
    with "Unexpected model contract"; one without `beat_this.onnx` (pre-05-11) silently disables BeatThis
    (BeatThisDetector has no module-dir `models/` candidate) and tempo falls back to DSP, which gave 112.65 BPM
    for this 85 BPM song.
  - If the folder is deleted the pointer is rejected and it falls through correctly (no crash).
- Fix: move the bundle check (0.5) to the top for VST3; delete the pointer read (or gate it behind a dev define);
  stop App.cpp writing it in release; add `[InstallDelete] Type: files; Name: "{userappdata}\Samplar\portable_root.txt"`;
  add `exeDir/models/beat_this.onnx` as a BeatThis candidate; make `hasResources` require `models/`.

### C8 - MEDIUM - Non-44.1 kHz input is resampled with plain linear interpolation (no anti-alias filter) (VERIFIED)
- File: `src/services/separation/OnnxSeparationProvider.cpp:202-225`
- Evidence: `outChL[i] = L[idx0] + (L[idx1] - L[idx0]) * frac;` - no low-pass before decimation.
- Impact: 48 kHz sources (video/stream rips, most DAW bounces) are fed to Demucs with a sinc^2 roll-off
  (~-1.3 dB @10 kHz, ~-3.3 dB @16 kHz); 88.2/96 kHz sources alias ultrasonic content into the audible band. Stems
  come out duller/noisier than with a proper SRC, and every stem is written at 44.1 kHz regardless of the source.
  Mono (copied to both channels, :199-200), 24-bit, FLAC are handled correctly. >2-channel files: only ch 0/1 are
  used (5.1 centre-channel vocals are lost). MP3 decoding relies on WindowsMediaAudioFormat
  (JUCE_USE_MP3AUDIOFORMAT is 0, `juce_audio_formats.h:99-115`) -> fails on Windows N/KN without the Media Feature
  Pack; .m4a/.aac are not readable at all, although `tools/ffmpeg.exe` is shipped (C19).
- Fix: use `juce::WindowedSincInterpolator` (JUCE 8 is vendored) or r8brain per channel; down-mix >2ch with a
  standard matrix; either wire ffmpeg as a decode fallback or stop shipping it.

### C9 - MEDIUM - Stem output: 16-bit hard-clipped temp WAVs, never cleaned, no stem cache; legacy cache orphaned (VERIFIED)
- Files: `OnnxSeparationProvider.cpp:238-272` (16-bit writer), `FullSongService.cpp:236-241` (temp dir),
  `src/ui/MainWindow.cpp:1080-1132` (restore), `%APPDATA%/Samplar/stem_cache/` (544 MB)
- Evidence:
  - `wav.createWriterFor(stream.get(), 44100, 2, 16, {}, 0)` - JUCE clamps floats to 16-bit; Demucs stems can exceed
    the mix peak on loud masters -> hard clipping, plus undithered 16-bit truncation before further pitch/stretch.
  - Each run writes 4 WAVs to `%TEMP%\smode_<millis>\` (~423 MB for a 10-min song); nothing ever deletes them
    (grep `deleteRecursively` -> only LicenseCleanerUtility).
  - There is NO stem cache in current code (grep `stem_cache` -> none). Project state stores the temp paths;
    `MainWindow.cpp:1080-1132` fast-restores only if they still exist, otherwise silently re-runs the full
    separation (minutes) on project open. Storage Sense/Disk Cleanup or moving the project to another machine
    triggers that. Loading the same song twice also re-separates.
  - `%APPDATA%/Samplar/stem_cache/` holds 12 dirs / 544 MB of `*_htdemucs_6s.flac` from the SM5/Python era
    (last write 2026-05-11) - no code reads, writes, or prunes it.
- Fix: write 32-bit float WAV (or 24-bit) for stems; implement a content-hash keyed cache
  (hash of first/last N MB + size + model id) under `%APPDATA%/Samplar/stem_cache` with an LRU size cap (e.g. 5 GB),
  store cache keys (not temp paths) in project state; delete the legacy 6-stem dirs on first run.

### C10 - MEDIUM - "Downbeat" detector only finds the BEAT phase; bar position (beat 1) is never detected, and BeatThis downbeats are discarded (VERIFIED)
- Files: `src/services/analysis/DownbeatDetector.cpp:66,189,219-226,367-369`; `BeatThisDetector.cpp:142-144`;
  `third_party/beat_this/src/beat_this_api.h:10-13`
- Evidence: comb period `tauSamples = (60.0 / bpm) * sampleRate` (one beat), result clamped into `[0, tau)`;
  `kAnalysisBars = 8` is declared but unused. BeatThis returns `downbeats` and `beat_counts` (1 = downbeat) but
  BeatThisDetector only copies `bt.beats`.
- Impact: grid offset lands on the first kick-phase beat, so 1-bar / 2-bar auto-segments
  (`SModePage.cpp:2246-2276`, "STRICTLY from the grid (first downbeat)") start on beat 2/3/4 whenever the first
  kick is not beat 1 or the track has a pickup. Downbeat offset IS applied to the waveform grid
  (`SModePage.cpp:1840-1857`) and to subsequent auto-segmentation, but auto-segment run before the async result
  arrives uses offset 0. Attack-snap does snap to a zero crossing (:352-362); comment at :346 says "4%" but code
  uses 8% (:331).
- Fix: run BeatThis once, keep `beats` + `downbeats`; set grid offset = first downbeat (mod bar), keep the kick-band
  attack-snap only as sub-frame refinement. Re-run auto-segment when the offset arrives (if user has not edited).

### C11 - MEDIUM - TempoRefiner and OctaveCorrection feed interleaved complex data into a real-only FFT (VERIFIED)
- Files: `src/services/analysis/TempoRefiner.cpp:56-64`, `src/services/analysis/OctaveCorrection.cpp:115-122`
  ```cpp
  fftBuf[i * 2]     = samples[pos + i] * window[i];   // for i < 2048 -> fills all 4096 floats
  fftBuf[i * 2 + 1] = 0.0f;
  fft.performRealOnlyForwardTransform(fftBuf.data()); // expects first N floats = N real samples
  ```
- Impact: the FFT sees only the first 1024 samples (rising half of the Hann window), zero-stuffed -> bin k measures
  frequency 2k*fs/N, the "50-4000 Hz" band is really ~100-8000 Hz, and frames are time-shifted by ~N/4. The
  refiner still roughly works (log: 85.71 -> 84.86, conf 0.365) but precision/robustness is degraded; octave
  correction (used whenever BeatThis fails) works on a wrong ODF. TempoAnalyzer.cpp:209-217 does it correctly.
- Fix: write `fftBuf[i] = samples[pos+i]*window[i]` for i < N, zero the rest (as TempoAnalyzer does).

### C12 - MEDIUM - FFT + DWT detectors always run first and are then thrown away; DWT is O(n^2) per window and was wrong (VERIFIED)
- File: `src/services/analysis/HybridTempoDetector.cpp:25-71` (DSP first), `:73-103` (BeatThis replaces);
  `DWTTempoDetector.cpp:48-60,236-246,344`
- Evidence (log): FFT 112.35, DWT 113.01 (window votes scattered 56/68/85/113/170/215), BeatThis 85.71 conf 0.98 ->
  "picking BeatThis", true ~85. DWT: 3 s windows, 1.5 s hop -> ~136 windows for 206 s; each window does a full
  all-lags time-domain autocorrelation on ~8.3 k samples (~34 M MAC) although only lags for 50-220 BPM are used,
  plus unconditional "Parabolic refinement" log lines (~200 log lines per detection, file-opened per line by
  juce::FileLogger). Estimated several seconds per 3-min song, ~3x for 10 min (PLAUSIBLE - log has no timestamps).
  Only channel 0 is analysed (`buffer.getReadPointer(0)`, :43).
  LTE: `LteWrapper.h` is not included anywhere; loop-tempo-estimator is linked (CMakeLists 476-480, 717-721) but
  never runs - pure dead dependency.
- Fix: run BeatThis first; run FFT/DWT/octave-correction only if BeatThis is invalid or conf < floor. If DWT is kept
  as fallback, compute autocorrelation via FFT and only over [minLag, maxLag]; silence the per-lag logging.
  Remove the LTE link.

### C13 - MEDIUM - ORT is CPU-only, spins all physical cores, normal priority, inside the DAW process (VERIFIED config; xrun impact PLAUSIBLE)
- Files: `CMakeLists.txt:190-192` (`onnxruntime-win-x64-1.20.1.zip` = CPU build), `OnnxSeparationProvider.cpp:92-102`
  (`SetIntraOpNumThreads(physical cores)`, no `session.intra_op.allow_spinning`, `activeEP_ = "CPU"` hardcoded;
  header comment :83 still mentions DirectML), `third_party/beat_this/src/beat_this_api.cpp:34-52`
  (default SessionOptions).
- Impact: RTF 1.27 (log) = slower than real time. ORT's default intra-op spin-wait keeps every physical core busy
  for minutes at normal priority while the DAW's audio thread needs CPU -> dropouts if the user keeps working.
  Two plugin instances separating at once double memory (C3) and oversubscribe cores; `static std::mt19937`
  (:367-368) is shared unsynchronised across instances.
- Fix (cheap): `so.AddConfigEntry("session.intra_op.allow_spinning","0")`, intra = max(1, physical-1), lower worker
  priority (custom thread creation via OrtThreadingOptions), process-wide semaphore = 1 separation at a time,
  `SetOptimizedModelFilePath` to cache the optimised graph (session is rebuilt per separation,
  `SModeSeparationService.cpp:67`). Drop the redundant last chunk: with stride 257985 the 36th chunk only re-covers
  samples chunk 35 already covered (~3%).
  Fix (bigger): DirectML is NOT possible with the shipped CPU onnxruntime.dll. Switching to the
  Microsoft.ML.OnnxRuntime.DirectML 1.20.x build + DirectML.dll (~18 MB) and `AppendExecutionProvider_DML` with
  CPU fallback is a drop-in API change; whether the in-graph STFT/iSTFT ops run on DML or partition back to CPU
  needs a benchmark (PLAUSIBLE large speed-up on any DX12 GPU).

### C14 - MEDIUM - findStemOutputFiles matches stems by substring of the user's song filename (VERIFIED)
- File: `src/services/smode/SModeSeparationService.cpp:188-249`; names written at `OnnxSeparationProvider.cpp:633-638`
- Evidence: match on `containsIgnoreCase("_" + name + "_")` / `endsWithIgnoreCase("_" + name)` over files named
  `<songBase>_<Stem>.wav`. Song "The_Other_Side.wav" -> "The_Other_Side_Bass.wav" contains "_Other_" -> Bass file
  loaded into the Melody lane. Song "Kick_Drums_Loop" -> "Kick_Drums_Loop_Bass.wav" matches "Drums". If a stem is
  missing, the result vector shrinks and `SModeService::loadStemAudio` assigns names by index (:103-112), shifting
  every later lane. Fallback "using all files in order" (:240-246) is alphabetical (Bass, Drums, Other, Vocals)
  but labelled Vocals/Bass/Drums/Melody.
- Stem order itself is correct: provider maps (drums, bass, other, vocals) per the smank export; log confirms the
  file labelled Drums is the drum stem (BeatThis conf 0.98, kick-band downbeat conf 0.387 on it).
- Fix: return `provider_->lastStems()` through FullSongService::Result and use those exact paths; drop the scan.

### C15 - MEDIUM - Stem decode on the message thread (separation complete + project restore) (VERIFIED)
- Files: `SModeService.cpp:64-139` (called from the callAsync bus event), `MainWindow.cpp:1108-1109`
- Impact: 4 full-length WAV decodes (~847 MB for 10 min) block the UI for seconds at the end of each separation and
  on every editor/project reopen. The provider already had the stems in memory.
- Fix: decode on a background thread (or hand the float buffers over directly), then swap into the model (C4).

---

## LOW

### C16 - LOW - BeatThis tempo estimator: mean/regression use array index, biased by missed beats (VERIFIED via log)
- File: `src/services/analysis/BeatThisDetector.cpp:193-261`
- Log: `85.7142 BPM (median=85.7142 mean=79.9368 regression=79.2753 median picked), inliers=249/256`.
  x = array index, so every missed beat (break/fill) stretches the fitted IBI -> 79.9 instead of ~85; the 3% agree
  test then falls back to the frame-quantised median (0.700 s = 35 frames @50 fps). TempoRefiner rescues it (C11).
- Fix: x_k = round((t_k - t_0) / medianIOI) (beat number), then regress / take span over beat numbers.

### C17 - LOW - Transient segmenter places onsets early and never snaps to zero crossings (code VERIFIED, offset size PLAUSIBLE)
- File: `src/services/smode/SpectralFluxDetector.cpp:45-50,173-205`
- Evidence: onset = `frame * hopSize` (window START of a 1024-pt Hann frame) then backtrack searches only BACKWARD
  up to 256 samples for the quietest 32-sample block. A flux peak occurs when the transient is ~1/2-3/4 into the
  window, so onsets land roughly 12-23 ms before the hit (includes the previous hit's tail; MIDI-triggered chops
  feel late). Threshold is `median*1.5` with no absolute floor -> spurious onsets in near-silent stems (partly
  hidden by the RMS pruning in SModePage).
  Playback applies a 2 ms linear fade-in/out for one-shot voices (`src/audio/MicroFade.h:34-55`,
  MidiMappingService.cpp:511-514) but not for looping voices; no slice edges are zero-crossing snapped.
- Fix: map frame f to `f*hop + fftSize/2`, backtrack within [peak - hop, peak + hop] on a short envelope, add an
  absolute flux floor, optionally snap to the nearest zero crossing within 1-2 ms; crossfade loop points.

### C18 - LOW - OnnxDelayLoad: LoadLibrary under loader lock + unconditional log in C:\Users\Public (VERIFIED code; deadlock PLAUSIBLE)
- File: `src/utils/OnnxDelayLoad.cpp:21,161-174`
- Evidence: the TLS callback (DLL_PROCESS_ATTACH, loader lock held) calls `LoadLibraryExW` for onnxruntime.dll -
  explicitly listed by Microsoft as a DllMain deadlock risk (multithreaded plugin scanners). Every load appends to
  `C:\Users\Public\samplar_dll_load.log` (exists, 36 lines); comment says `SAMPLAR_DLL_LOAD_TRACE=0` disables it,
  but no code reads that env var.
- Fix: rely on the dliNotePreLoadLibrary hook only (it runs outside loader lock on first ORT call) and drop the TLS
  preload, or keep it but log only when the env var is set.

### C19 - LOW - Dead code and dead payload still compiled/shipped (VERIFIED by reference counts)
- Unreachable in SAMPLAR:
  - `ProcessingCoordinatorService` (1002 lines): only entry is `MainWindow.cpp:1593-1637`, whose
    `batchExportItems_` is "always empty (preview_ removed)" (`MainWindow.cpp:1412-1415`); no publisher of
    `processing.enqueue`. Still registered (`SamplarAudioProcessor.cpp:319`) and builds its own FullSongService.
    Pulls in LoopDetector, MiniBpm, BeatAlignedSlicer, LoopDivider, OnsetDetector (only referenced from it).
  - `FullSongSeparationService` (+ batch): registered, never started (`MainWindow::startBatchSeparation`
    :1421 has no callers; no other `startSeparation` caller).
  - Zero external references: `TempoCache`, `SpectralSeparator`, `BandProcessor`, `DrumLibraryService`,
    `LteWrapper.h` (+ loop-tempo-estimator link), `DataPaths::getPythonDirectory`, `DataPaths::getDataDirectory`.
  - Remnants: `StemCount::Six` + `stemCount == 6` branches (MainWindow.cpp:578, :1116), 6-stem colours,
    "audio-separator" comments (SModeSeparationService.cpp:195,223; FullSongSeparationService.h:69),
    "Python ZMQ" (ProcessingCoordinatorService.cpp:172), DataPaths still treats `python/` as a resource marker.
    `FullSongService::process/processDrumStem/loadAudioFile/saveAudioFile/onVizProgress/publishError` are used only
    by the dead coordinator or not at all.
- Dead payload in the 808 MB installed bundle: `tools/ffmpeg.exe` 148 MB (0 references in src),
  `data/patterns_with_rolls.json` 56 MB + `latin_patterns.json` (0 references) - present twice
  (`Contents/data` and `Contents/x86_64-win/data`). CAUTION: `data/` is the `hasResources()` marker
  (`SamplarAudioProcessor.cpp:68-71`) - keep the directory or change the marker before deleting the JSONs.
- Fix: delete in one cleanup commit after the above are unregistered; ~260 MB smaller installer.

### C20 - LOW - Misc
- `RecentSongs.h:26-27` doc says missing files are skipped but `get()` includes them (by design per :44-45 comment);
  writeAll is delete+rename (not atomic) and unsynchronised across plugin instances.
- Random shift (`OnnxSeparationProvider.cpp:367-370`) makes two separations of the same file differ slightly
  (matters once a cache exists - key on input, not output).
- `ensureOnnxProviderReady` session reuse is moot: every separation constructs a fresh FullSongService
  (`SModeSeparationService.cpp:67`), so the 300 MB model is parsed + graph-optimised each run; BeatThis (83 MB)
  is reloaded per BPM detection (HybridTempoDetector is a stack local, SModePage.cpp:3019). No leaks found -
  sessions are destroyed with their owners.
- Very short files: separation OK (length >= 2); SpectralFlux returns nothing < 1024 samples; downbeat needs >= 1 bar;
  DWT handles < 3 s. No crash paths found.

---

## Ranked fix order
1. C1 + C2 + C5: joinable threads, real cancel (shouldCancel + RunOptions::SetTerminate), alive tokens.
2. C4: atomic snapshot of stem data for the audio thread; detach before reload.
3. C3: bounded memory + catch bad_alloc + duration guard.
4. C6: one-line log fix.
5. C7: bundle-first resolution, kill portable_root.txt.
6. C9 + C14 + C15: float stems, persistent hash cache, exact stem paths, background decode.
7. C10 + C12 + C16 + C11: BeatThis-first (beats + downbeats), DSP only as fallback, fix FFT input bugs.
8. C8, C13, C17-C19.
