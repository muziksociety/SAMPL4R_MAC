# SAMPLAR dead-code deletion plan (verified inventory)

Target: `b:/SANTI/VST PROJECT/SAMPLAR`. Shipping artefact: `SamplarPlugin_VST3` (FORMATS VST3 only).
Read-only analysis. Nothing was edited or built.

Snapshot taken 2026-10-20 22:50-23:10. **The working tree is being edited right now by at least three
other workstreams** (licensing port, audit-A realtime fixes, audit-C/D separation and release fixes).
During this analysis these files changed under me: CMakeLists.txt, MainWindow.cpp, SamplarAudioProcessor.{h,cpp},
MidiMappingService.{h,cpp}, SModePage.cpp, FullSongService.cpp, LicenseSystemService.cpp, and others
(`git status`: 46 modified, 6 new untracked licensing files).
=> Every line number below is a SNAPSHOT. Each block also has a symbol or text anchor. Re-locate by
   anchor before editing, and run each batch on top of the committed results of those workstreams.

Method: an include graph over src/tests/tools (`#include` resolution), symbol reference counts with comments
stripped (excluding a symbol's own files), service registry names (`registerService("x")` vs `get("x")`),
an EventBus publish/subscribe topic inventory, CMake `SAMPLAR_SOURCES` vs files on disk, git tracked sizes
(`git ls-tree -r -l HEAD`), and measured sizes of the built bundle under build/.../Samplar.vst3.

---------------------------------------------------------------------------------------------------

## 0. Summary

| Item | Count |
|---|---|
| Whole source files to delete (B1-B4) | 68 files, 15,584 lines (B1 44 files/9,018, B2 14/4,516, B3 9/2,003, B4 1/47) |
| Plus deferred B6 | 2 files, 707 lines (SamplePreviewCache) |
| Code blocks inside live files (B1-B5) | ~1,580 lines (MainWindow ~920, SModePage ~245, MidiMappingService `#if 0` 189, processor ~110, FullSongService ~110, misc) |
| Optional or deferred blocks (B6/B7) | ~600 lines (browser-pad MIDI path ~450, nav sidebar and UIState legacy ~100) |
| CMake | ~150 lines in B1-B3 (3 network FetchContent deps go: nlohmann_json, SoundTouch, LTE subproject) |
| Tracked repo junk to `git rm` | 29,312 of 29,839 tracked files, about 237 MB of 244 MB at HEAD |
| Bundle payload removed | 342.8 MiB of the 806.9 MiB installed bundle: ffmpeg 141.7 + pattern JSONs 53.7 (already done, uncommitted) + BACKGROUNDS 141.3 + Samples 6.1 (B3) |

Batch order (the build stays green after each one):
**P0 -> B0 -> B1 -> B2 -> B3 -> B4 -> B5 -> (B6 deferred) -> (B7 optional) -> (B8 decision) ; L = licensing, coordinate.**

Verification after every batch:
1. `cmake -B build -G "Visual Studio 17 2022" -A x64` then `cmake --build build --config Release --target SamplarPlugin_VST3`
2. `python tools/release_linter/run_all.py`
3. DAW smoke test: load song, run separation (lane bars), Auto: Beat, Quick Assign, play from MIDI keyboard,
   drag a chop out, save the project, close the editor, reopen, reload the project.
4. Grep the log `%APPDATA%/Samplar/logs/SAMPLAR_console.log` for `ERROR|WARNING|not registered`.

---------------------------------------------------------------------------------------------------

## P0. Prerequisites (do first; these are not deletions)

1. **DebugConsole is not in git.** `.gitignore:82 Debug/` matches `src/ui/Debug/`, so
   `src/ui/Debug/DebugConsole.{h,cpp}` and `DebugFlags.{h,cpp}` are untracked. DebugConsole is compiled
   (CMake 334) and included by MainWindow.cpp:18 and MultiLogger.h. **A fresh clone does not build.**
   Fix: change the pattern to `/Debug/` (or add `!src/ui/Debug/`), then `git add src/ui/Debug/DebugConsole.*`.
   Do NOT add DebugFlags (it is deleted in B1) or GrayDetectionDebugDialog (deleted in B1).
2. Land or rebase on the in-flight workstreams first. These files overlap with this plan:
   MainWindow.cpp (B5 handler-id fix just added), SamplarAudioProcessor.* (A8 aliveToken),
   MidiMappingService.* (A fixes), SModePage.cpp (B3 fix: someone just converted the DEAD
   `dragSequencerAsWav` to `runBackground`, which is wasted effort because that function is deleted in B5),
   FullSongService.cpp, and the SModeSeparation and licensing files.
3. The ffmpeg and pattern-JSON payload removal is **already done in the uncommitted CMakeLists.txt and samplar.iss**
   (staging now writes `data/samplar_bundle.marker` and removes stale `data/*.json` and `tools/`). Commit it
   together with the B3 payload items.

---------------------------------------------------------------------------------------------------

## B0. Tracked repo junk, no build impact (`git rm`; can run any time)

Verified: none of these paths is referenced by CMakeLists.txt, src/, the installer, `release_assets/`
or the release linter.

| Path | Files | Tracked size | Command / note |
|---|---|---|---|
| third_party/libtorch | 9,391 | 39.06 MB | `git rm -r` (SAMPLAR uses ONNX) |
| third_party/demucs.cpp | 2,707 | 33.99 MB | `git rm -r` |
| third_party/vcpkg | 9,499 | 12.27 MB | `git rm -r` |
| third_party/openblas | 10 | 1.94 MB | `git rm -r` |
| third_party/onnxruntime (1.19.2 GPU headers) | 25 | 0.89 MB | `git rm -r`; the build uses FetchContent ORT 1.20.1 (`${ORT_ROOT}/include`) |
| third_party/perfetto | 22 | 0.39 MB | `git rm -r`; Tracing.cpp writes Chrome-trace JSON itself |
| (subtotal J1) | 21,654 | 88.52 MB | |
| openclaw/ | 4,710 | 37.89 MB | a different project: `git rm -r --cached openclaw` + add to .gitignore (keeps the disk copy) |
| JUCE/ (partial vendored copy) | 2,704 | 36.03 MB | It lacks `modules/juce_gui_basics`, so CMake 59 skips it and the build ALREADY uses `../JUCE` (build/JUCE_fallback). Safe to `git rm -r`, but run it as its own commit: confirm configure prints "Using fallback JUCE", and document the out-of-repo `../JUCE` dependency (or pin it with FetchContent). |
| 4 UUID-named PNGs at root | 4 | 10.47 MB | 5546c120..., 5e06a599..., 917a6213..., bcaed40b... (0 refs) |
| loading_transparent.apng, loading animation.mp4, monsta.gif, loading_transparent.gif, loading_transparent-ezgif.com-effects.gif | 5 | 24.67 MB | 0 refs (the "loading animation" string hits are code comments) |
| .axon/kuzu | 1 | 3.30 MB | tool DB |
| .sm5_real_orphans.json, .sm5_suspicious.json, .sm5_weak_nodes.json, SERVICELOCATOR_FEASIBILITY.json | 4 | 0.16 MB | analysis leftovers |
| test_batch_ui.cpp, test_progress_implementation.cpp, test_progress.ps1, copy_fix.ps1, fix_file.ps1 | 5 | <0.01 MB | 0 refs |
| 000portable.ps1, 001installer.ps1, installer_samplar.iss (v1.3.0 Program Files layout) | 3 | 0.07 MB | referenced only by .claude/settings.local.json allow-rules and old workflow JSON |
| render.yaml, license_server/ (app.py, requirements.txt) | 3 | 0.01 MB | SM5 license-server deploy, not the build |
| BUILD_STATUS.md (Demucs/UVR progress), batches/batch_queue.json, portable_fixes/, temp/diffs/, python_bridge/ | 7 | 0.06 MB | SM5/UVR/Python era. python_bridge is copied only by the SamplarApp POST_BUILD (removed in B1). |
| scripts/legacy/ (25 files), scripts/sm5cli.ps1 | 26 | 0.17 MB | SM5 build/installer scripts |
| tools/tutorial_gen/, resources/mentor/mentor_lessons.json, assets/games/fmd_assets.json | 7 | 0.23 MB | Mentor and Games features were removed |
| plugins/REF.png | 1 | 2.37 MB | 0 refs in src (InitialPage was deleted). Remove CMake 591-597 in the same commit (B1). |
| tests/mentor/MentorServiceTests.cpp, tests/test_*.py (8 files: UVR, GPU, Demucs, "Sample Monsta animation") | 9 | 0.04 MB | SM5 leftovers; includes the nonexistent src/mentor/ |
| src/deprecated/ (Chat*) + src/ui/Components/SettingsDialog.h.new | 6 | 0.09 MB | not compiled (also listed in B1) |

Untracked, local-disk-only clutter (not git; no build use after the uncommitted staging change): `nul`,
`SM5_Portable_2026-02-16.zip`, `SM5_Portable_2026-02-18.rar`, `SM5_Portable_2026-02-18/` (it was only the
pattern-JSON source), `test_progress_implementation.exe`, `*.csv` traces, `third_party/libtorch-cpu|libtorch-cuda|libtorch.zip|demucs.onnx`,
`third_partyopenblas/`, `tools/LARS`, `tools/MSS_ONNX_TensorRT`, `portable_cache/ffmpeg/`.
**KEEP `portable_cache/vc_redist.x64.exe`** (samplar.iss:89). Keep `models/htdemucs.onnx` and
`third_party/beat_this/models/beat_this.onnx` (both git-ignored by `*.onnx` but staged by CMake 846-847).

Note: `git rm` does not shrink .git history. Use `git filter-repo` before any public push (D20).

---------------------------------------------------------------------------------------------------

## B1. Pure orphans + preprocessor-dead code + dead CMake dependencies

Every file here has zero includers (include graph) and zero symbol references outside its own files.

### (a) Whole files to delete
| File | Lines | Evidence |
|---|---|---|
| src/audio/AudioEngine.h | 125 | not in CMake, 0 includers |
| src/audio/BufferAudioSource.h | 238 | not in CMake, 0 includers |
| src/ui/Components/ClassificationProgressDialog.h | 84 | 0 includers |
| src/utils/DebugLogger.h | 284 | 0 includers |
| src/services/analysis/LteWrapper.h | 423 | 0 includers (the only user of the LTE library) |
| src/ui/Debug/GrayDetectionDebugDialog.{h,cpp} | 48+236 | not in CMake, untracked (ignored) |
| src/deprecated/{ChatPage,ChatService}.{h,cpp}, README.md | 2,522 | not compiled; README says "delete after next release" |
| src/ui/Components/SettingsDialog.h.bak (untracked), SettingsDialog.h.new | 433+433 | stale copies |
| tests/mentor/MentorServiceTests.cpp | 75 | not built; includes the nonexistent src/mentor |
| src/audio/Preview/SoundTouchSource.{h,cpp} | 184+12 | 0 includers |
| src/audio/SequencerAudioSource.{h,cpp} | 261+567 | 0 includers |
| src/audio/SpectralSeparator.{h,cpp} | 191+298 | 0 includers |
| src/audio/BandProcessor.{h,cpp} | 94+214 | 0 includers |
| src/services/DrumLibraryService.{h,cpp} | 136+325 | 0 includers, never registered; publishes `drumLibrary.changed`, which nobody subscribes to |
| src/services/analysis/TempoCache.{h,cpp} | 53+139 | 0 includers |
| src/utils/MidiFileGenerator.{h,cpp} | 78+220 | 0 includers |
| src/ui/Components/MidiDragArea.{h,cpp} | 54+155 | 0 includers (WaveDragArea is the live one) |
| src/ui/Components/CollapsibleSection.{h,cpp} | 55+136 | 0 includers |
| src/ui/Components/SamplarAnimation.{h,cpp} | 104+544 | 0 includers (the only user of 100 MB of BACKGROUNDS PNG/GIF) |
| src/ui/Style/DropZoneLookAndFeel.{h,cpp} | 14+40 | 0 includers |
| src/ui/Style/TransparentHoverButtonLookAndFeel.{h,cpp} | 13+30 | 0 includers |
| src/ui/Theme/EngineColorManager.h | 83 | 0 includers |
| src/ui/Style/Sm5LookAndFeel.{h,cpp} | 19+82 | only an `#include` in MainWindow.cpp:21; never instantiated (SamplarLookAndFeel does not derive from it) |
| src/ui/Debug/DebugFlags.{h,cpp} | 9+7 | write-only: the flag is stored at MainWindow.cpp:767 and never read |

### (b) Blocks inside live files
- MainWindow.cpp:21 `#include "Style/Sm5LookAndFeel.h"`; :23 `#include "Debug/DebugFlags.h"`;
  :767 `samplar::DebugFlags::enabled().store(debugVisible_);`
- MainWindow.cpp:157-162 `class NudgeTimer` and :1379 `NudgeTimer nudgeTimer_;` (never used)
- MidiMappingService.cpp:1725-1913 `#if 0 ... renderPitchedStem ... #endif // 0` (189 lines, preprocessor-dead;
  it is also the last SoundTouch user)

### (c) CMakeLists.txt (snapshot line numbers)
- 88-96 nlohmann_json FetchContent (0 users in src or beat_this) + 481 and 722 `nlohmann_json::nlohmann_json`
- 98-113 Loop Tempo Estimator block + 485-490 (SamplarApp) + 725-729 (SamplarPlugin) (LteWrapper.h is the only consumer; GPL; D19)
- 115-118 SoundTouch comment + `option(WITH_SOUNDTOUCH ...)`; 153-176 SoundTouch FetchContent (codeberg);
  455-457, 493-496, 695-697, 731-734 SoundTouch include/link/define
- 119 `option(WITH_ZEROMQ ...)` + 532-542 ZeroMQ block (0 `WITH_ZEROMQ` users)
- 439-450 Intel IPP find_path (0 `INTEL_IPP_AVAILABLE` users)
- 544-557 SamplarApp POST_BUILD copy of python_bridge
- 591-597 SamplarApp POST_BUILD copy of plugins/REF.png
- 677-681 `set_property(SOURCE src/app/App.cpp ... SAMPLAR_SKIP_APP_MAIN=1)`. The macro is read nowhere; App.cpp guards
  `START_JUCE_APPLICATION` with `JucePlugin_Build_VST3`.
- SAMPLAR_SOURCES: 254, 256-262 (incl. the comment at 258), 281-282, 296-297, 335, 336, 338-339, 346-350, 353-354, 365-367
- Optional: 480 and 721 `juce::juce_opengl` (0 OpenGL users in src; shrinks the binary).

### (d) Registrations / subscriptions
None in this batch.

### (f) git rm
B0 rows marked "(also B1)" plus `third_party/loop-tempo-estimator/` (148 files, 0.59 MB) once the LTE CMake lines are gone.

### Also update
`tools/release_linter/intentional_orphans.txt` (remove the GrayDetectionDebugDialog, deprecated, and Games entries),
`intentional_unregistered_services.txt` (DrumLibraryService, ChatService, and the SM5-only names),
`intentional_orphan_events.txt` (chat.*, sm5.variation.*, variation.settings.changed, drumLibrary.changed),
NOTICE.md (remove the SoundTouch and LTE sections).

---------------------------------------------------------------------------------------------------

## B2. Dead services: ProcessingCoordinatorService + FullSongSeparationService and their consumers

Evidence:
- `ProcessingCoordinatorService` is registered (processor :324-325, App.cpp:169-171) but only triggered through
  `processing.enqueue`, which nothing publishes, or through `enqueue()` called from `MainWindow::processNextBatchFile`.
  That function runs only when `batchExportItems_` (always empty, :1428-1430) has items and
  `batchFinalProcessingActive_` (never set true) is set.
- `FullSongSeparationService` is registered (processor :340-343) but is started only by `MainWindow::startBatchSeparation`
  (:1436), which has 0 callers. `startSeparation()` has 0 callers.
- PCS is the only user of LoopDetector, LoopDivider, BeatAlignedSlicer, OnsetDetector (via BeatAlignedSlicer) and MiniBpm.
- **License bonus:** MiniBPM (third_party/minibpm) is **GPL** ("Copyright Particular Programs ... GNU General Public
  License"). It is linked into the commercial plugin through PCS, and NOTICE.md does not mention it. This batch removes it.

### (a) Whole files
src/services/ProcessingCoordinatorService.{h,cpp} (80+1002), src/services/FullSongSeparationService.{h,cpp} (116+388),
src/services/analysis/LoopDetector.{h,cpp} (102+495), LoopDivider.{h,cpp} (151+382), BeatAlignedSlicer.{h,cpp} (211+484),
src/utils/OnsetDetector.{h,cpp} (79+189), third_party/minibpm/MiniBpm.{h,cpp} (144+693). Total 4,516 lines.

### (b) Blocks inside live files
SamplarAudioProcessor.cpp:
- :7 `#include "../services/ProcessingCoordinatorService.h"`, :11 `#include "../services/FullSongSeparationService.h"`
- :324-325 `registerService("processing.coordinator", ...)`; :340-343 comment + `registerService("fullSong.separation", ...)`

App.cpp: :9 PCS include; :169-171 PCS registration (3 lines + log lines).

MainWindow.cpp (anchors in quotes):
- :5 PCS include, :6 FullSongService include, :7 FullSongSeparationService include, :8/:9/:11/:12 "SAMPLAR: removed" comment lines
- :196-197 "InitialPage, CommunityPage, MentorPage removed" remnant log
- :199-303 the whole `if (auto* bus = ...getBus())` block: "ui.navigate mentor routing removed" log plus the 5 `fullSong.separation.*`
  subscriptions (progress/message/complete/batch.complete/error)
- :305-363 "FullSong separation: RESUME on editor reopen" block
- :664-675 destructor unsubscribes for `ui.navigate`, `wave_editor.open`, `processing.viz/progress/result`, and the 5 `fullSong.*` topics
  (keep :676-682, the B5 handlers)
- :778-782 `isBatchProcessingActive()` (keep the `public:` at :777)
- :787-1032 attachBus: the whole `if (auto* busPtr = ...)` block with the `processing.viz` / `processing.progress` /
  `processing.result` handlers (three `&& false` branches, `switchTo(3)`/`switchTo(0)` to removed pages)
- members: :1362 `mentorNavSub_`, :1382-1384 `progId_, resId_, vizId_, waveEditorId_`, :1395-1396 `fullSongService_`
  (never constructed; it would null-deref if the batch path ever ran), :1402-1408 `fullSongSepService_` + 5 `fullSong*Sub_`,
  :1413-1434 `separationThread_`, `separationInProgress_`, `separationAlreadyDone_`, all `batch*` state, `BatchExportItemStub`
- :1436-1799 `startBatchSeparation`, `separateNextBatchFile`, `onBatchSeparationComplete`, `processNextBatchFile`,
  `onBatchFileProcessed`, `onBatchFinalProcessingComplete` (364 lines)
- :1858-1891 `MainWindow::closeButtonPressed`: drop the batch-confirmation branch (:1860-1888) and keep `requestQuit()`
  (standalone only)
- :28 `#include <thread>` (only separationThread_ used it)

FullSongService (KEEP the class; it is the live ONNX engine). Remove the methods only PCS used, or nobody used:
- .cpp :27-61 `process()`, :90-107 `processDrumStem()`, :116-118 `publishError()`, :120-167 `loadAudioFile()` + `saveAudioFile()`
- .h :57-58 `process`, :64-65 `onVizProgress`, :76 `processDrumStem`, :80 `publishError`, :83-84 `loadAudioFile/saveAudioFile`
  (C19; recheck: FullSongService.cpp is being modified)

### (c) CMakeLists.txt
263 (PCS .cpp), 265-266 (FullSongSeparationService), 298-301 (LoopDivider, BeatAlignedSlicer), 368-370 (OnsetDetector +
comment), 384-387 (MiniBpm + LoopDetector + comments). Keep 264 FullSongService.cpp.

### (d) Registrations / subscriptions removed
`registerService("processing.coordinator")` x2 (processor, App), `registerService("fullSong.separation")`;
subscriptions `fullSong.separation.{progress,message,complete,batch.complete,error}`, `processing.{viz,progress,result}`,
and the PCS-internal `processing.enqueue`; the dead unsubscribes `ui.navigate` and `wave_editor.open` (their handles were always 0).

### (f) git rm
third_party/minibpm/ (2 files). Release linter: drop `processing.*` from intentional_orphan_events.txt.

---------------------------------------------------------------------------------------------------

## B3. ProcessingPage overlay, StemCountDialog, 6-stem remnants, BinaryData, BACKGROUNDS/Samples payload

Evidence: ProcessingPage is never made visible in SAMPLAR. Its only `setVisible(true)` calls were in the FullSong
resume (gone in B2) and at :579, which is commented out. It still runs a 60 Hz timer and preloads a 22 MB TILESET
plus GIFs 300 ms after every editor open (B34). `StemCountDialog::show` bypasses itself and always calls back with 4
(StemCountDialog.cpp:82-92). Samples/ has 0 runtime references (the only `Samples` string is DrumLibraryService's
user-folder scan, deleted in B1). BACKGROUNDS is read only by ProcessingPage (TILESET.png), GifAnimatedBackground
(CAMPO/LAVA_optimized.gif) and SamplarAnimation (B1).

### (a) Whole files
src/ui/Pages/ProcessingPage.{h,cpp} (134+608), src/ui/Components/LoadingAnimation.{h,cpp} (59+123),
src/ui/Components/GifAnimatedBackground.{h,cpp} (91+630), src/services/BeatAnalysisTypes.h (229; its last includers were
PCS, TempoCache, ProcessingPage and the MainWindow stub), src/ui/Components/smode/StemCountDialog.{h,cpp} (35+94).

### (b) Blocks inside live files
MainWindow.cpp:
- :10 ProcessingPage include, :14 StemCountDialog include
- :192-194 `addChildComponent(processing_)` + log
- :481-482 comment "Shows StemCountDialog ... ProcessingPage overlay"
- :518-520 and :588: replace `samplar::StemCountDialog::show(this, [this, alive, f](int stemCount) { ... });` with the
  body inlined, `const int stemCount = 4;`
- :538, :554 `processing_.setVisible(false);`; :566 `processing_.setProgress(...)` (KEEP the rest of the live
  `smode.separation.progress` handler, which drives the lane loading bars)
- :574-579 `processing_.resetForNewJob/setInputFile/setProcessMode` + the commented setVisible
- :584-586 `(stemCount == 6) ? StemCount::Six : StemCount::Four` -> `StemCount::Four`; same at :1129-1131
- :591-592 `processing_.onBack`; :594-602 deferred `processing_.preloadResources()` (GIF preload)
- :735-736 `processing_.setBounds(...)` + comment; :753-754 stale comments
- :1358-1360 `ProcessingPage processing_;` + comments
SModeSeparationService.h:28 `Six = 6` enum value; SModeSeparationService.cpp:43 comment.
DataPaths.h:82-111 `getBackgroundsDirectory()` (0 callers after B1). Optional: `getPythonDirectory()` :76-80 and
`getDataDirectory()` :64-68 (0 callers).

### (c) CMakeLists.txt
- SAMPLAR_SOURCES 333 (ProcessingPage.cpp), 357-360 (LoadingAnimation, GifAnimatedBackground), 374-375 (StemCountDialog),
  388-389 (BeatAnalysisTypes + comment)
- juce_add_binary_data: 620 comment, 622 `backdrop.png`, 623-624 stale REF/InitialPage comment, 626-627
  `loading_spritesheet.png`, 633-640 stale comments. KEEP 629-631 `assets/samplar_logo.png` (SModePage.h:33).
- SamplarApp POST_BUILD: 575-589 BACKGROUNDS copy, 599-612 Samples copy
- VST3 staging: 829-830 layout comments, 856-859 BACKGROUNDS + Samples CopyTree; add `'BACKGROUNDS','Samples'` to the
  stale-removal `foreach` at 855 so existing build bundles are cleaned.

### (e) Installer / staging payload
| Payload | Size in bundle | Runtime refs | Status |
|---|---|---|---|
| tools/ffmpeg.exe | 148,628,992 B | 0 in src (decoding is JUCE AudioFormatManager) | already removed in uncommitted CMake 848-855; commit it |
| data/patterns_with_rolls.json + latin_patterns.json | 56,305,470 B | 0 in src | already removed; `data/samplar_bundle.marker` keeps the marker dir (an Inno `recursesubdirs` without `createallsubdirs` would drop an EMPTY data/, so the marker file is required) |
| BACKGROUNDS/ (13 files: TILESET 22 MB, CAMPO.gif 45 MB, LAVA.gif 51 MB, *_optimized.gif, 8 PNGs) | 148,177,830 B | only ProcessingPage, GifAnimatedBackground, SamplarAnimation | B3 |
| Samples/ (808 + GM wavs) | 6,380,779 B | 0 | B3 |
| Contents/data (54 MB duplicate at the bundle root, from an older installer) | on installed machines only | 0 | see below |

Upgrade gotcha: Inno does not delete files from the previous version when it upgrades in place. Add to samplar.iss:
```
[InstallDelete]
Type: filesandordirs; Name: "{app}\Contents\x86_64-win\tools"
Type: filesandordirs; Name: "{app}\Contents\x86_64-win\BACKGROUNDS"
Type: filesandordirs; Name: "{app}\Contents\x86_64-win\Samples"
Type: files;          Name: "{app}\Contents\x86_64-win\data\*.json"
Type: filesandordirs; Name: "{app}\Contents\data"
```
Do NOT delete `{app}\Contents\x86_64-win\data\` itself. It is the `hasResources()` marker (SamplarAudioProcessor.cpp:69-72),
`DataPaths::getAppDirectory` (DataPaths.h:48) and the App.cpp:107 pointer heuristic depend on it.
Result: about 343 MiB less on disk (807 -> ~464 MiB installed). The installer shrinks less, because the ONNX models
(369 MiB) dominate and compress poorly (estimate: 481 MB -> ~350 MB; measure after the build).

### (f) git rm
BACKGROUNDS/ (9 tracked PNGs, 27.76 MB; the GIFs are git-ignored), backdrop.png (2.76 MB), loading_spritesheet.png (2.00 MB),
assets/808_preview_kit/LICENSE_TR808.txt (license for the dropped Samples). Subtotal 32.52 MB.

### Also update
`.claude/skills/samplar-release/SKILL.md:128` (remove `BACKGROUNDS\`, `Samples\`, `tools\ffmpeg.exe`);
samplar.iss header comment lines 17-19; the CMake 820-830 layout comment.

---------------------------------------------------------------------------------------------------

## B4. Processor: unused preview mixer and raw-buffer helpers

Evidence: `"preview_audio"` is registered (processor :345-352), and nothing calls `get("preview_audio")` or `getMixer()`
(SampleBrowserPage was deleted). The mixer has no inputs but is processed every block (A24).
`encodeBufferRaw`/`decodeBufferRaw` are static, with 0 callers.

- delete src/audio/PluginPreviewService.h (47; header-only, not in CMake)
- SamplarAudioProcessor.cpp: :12 include; :345-352 registration; :438-442 prepareToPlay block; :452-453 releaseResources
  lines; :522-537 processBlock "3. Mix in sample browser preview audio" block; :544-605 "FLAC encode/decode helpers"
  (`encodeBufferRaw`, `decodeBufferRaw`)
- SamplarAudioProcessor.h: :12 `class PluginPreviewService;`, :79-80 `pluginPreview_`, :102-103 `previewBuffer_`
- (d) remove `registerService("preview_audio")`
- Optional, same batch: the processor's standalone-only branches are dead because this target is VST3-only and SamplarApp
  never instantiates the processor: :3 AudioDeviceService include, :316-322 `if (isRunningStandalone()) registerService("audio")`,
  :375-395 "Standalone MIDI" block. Leave the `isRunningStandalone()/isRunningAsPlugin()` helpers if you want
  `FORMATS Standalone` back later.

---------------------------------------------------------------------------------------------------

## B5. SModePage dead functions and widgets (anchors; SModePage.cpp is being edited)

- SModePage.cpp :3860-3934 `// -- Play Lanes Chained` + `SModePage::playLanesChained()` (0 callers)
- SModePage.cpp :3966-4101 `// -- Drag Sequencer as WAV` + `SModePage::dragSequencerAsWav()` (0 callers). Stop
  fixing it (the B3 fixer just edited it); the live drag path is `dragSeqWavArea_.onRenderPattern` (:996+).
- SModePage.h :615 and :617 declarations; :611-612 `playChainBtn_`, `stopAllSeqBtn_` + SModePage.cpp :990-994 (hidden,
  no onClick)
- SModePage.h :129-146 `class StemStatusIndicator` (0 refs)
- `exportBtn_` (h:544; cpp :416 entry in the button-styling list, :1081 tooltip, :2194 setVisible(false)). It has no
  onClick and no SModeExporter.
- Optional (B5b, small value, touches live functions): hidden-but-constructed widgets `autoBarBtn_`, `auto2BarBtn_`,
  `autoTransientBtn_` (but `autoSegmentByTransient()` is LIVE via `autoBeatBtn_` right-click mode, and it writes busy
  text to the hidden `autoTransientBtn_`. Retarget it to `autoBeatBtn_` instead of deleting it), `trigModeBtn_`,
  `transportBtn_`, `titleLabel_`, `trackNameLabel_`, `bpmLabel_` (still written), `controllerAreaLabel_` (h:519-591).
- Unused helpers (decl + def only, 0 calls): StemWaveformComponent::`flashSegment`, `getPitchSemitones`;
  SequencerLaneComponent::`fillWithSegments`, `setSlot`; SModeSegmentModel::`findSegmentByMidiNote`, `getLinkedStems`,
  `setStemVolume`; PatternGenerator::`applySubSlicing`, `getRequiredRoles`, `randomPitch`, `randomReverse`;
  WaveDragArea::`isReadyToDrag`; TempoAnalyzer::`detectBPM`. `StemWaveformComponent.h:256 showMidiNotePickerMenu` is
  declared but never defined: delete the declaration.

---------------------------------------------------------------------------------------------------

## B6 (DEFERRED, medium risk). MidiMappingService browser-pad path + SamplePreviewCache

Evidence: these have 0 external callers: `mapSampleToNote`, `clearMapping`, `clearAllMappings`, `autoMapBrowserPage`,
`mapSequencerChannels`, `setSampleCache`, `lockSampleToNote`, `unlockNote`, `isNoteLocked`, `getLockedNoteForFile`,
`noteForSample`, `registerSampleAtNextFreeNote`, `beginMidiLearn(int)`, `beginMidiLearnForFile`, `getOccupiedSlots`.
`SamplePreviewCache` is never constructed (A24). The subscription `browser.page.changed` (.cpp :40-56, unsubscribed at :74-78)
has no publisher. So `midiMap_` is always empty, and the "midi" AppState binding always saves 0 slots.
Why deferred: it touches `handleNoteOn`/`handleNoteOff` on the AUDIO THREAD (midiMap_ branches around .cpp :815-877 and
:934-946, the MIDI-learn-to-file branch :700-712), and audit-A fixes (A2/A11/A25) are editing those same functions now.
Do it after A lands, then verify MIDI with a DAW and the computer keyboard.
- files: src/audio/SamplePreviewCache.{h,cpp} (213+494); CMake 255
- .h: :9 include, :47 `public SamplePreviewCache::Listener`, :69-90, :99-132 (locked assignments, MIDI learn for files),
  :214-234 `SlotInfo/getOccupiedSlots/SlotMeta/getOccupiedSlotsMeta/restoreSlot`, :346 `resolveBuffer`, :472 `cache_`,
  :477 `midiMap_`
- .cpp: :955-1195 (mapSampleToNote .. registerSampleAtNextFreeNote), :1209-1224 (beginMidiLearn, beginMidiLearnForFile),
  :1249-1297 resolveBuffer, :1557-1646 (slot snapshot/restore), the cache_ teardown in shutdown (:80-85)
- processor: the "midi" binding (SamplarAudioProcessor.cpp :762-843). Old projects stay loadable: AppState keeps
  unregistered subtrees in root_ (late-drain design).
- dead publishes that disappear: `midi.pad.triggered`, `midi.pad.stopped`, `midi.learn.locked`, `midi.learn.waiting`
  (drop it from intentional_orphan_events.txt)

## B7 (OPTIONAL). Debug nav sidebar + legacy UIState page fields
- MainWindow.cpp :1337-1351 `struct NavModel` (lists "Initial", "Community", "Mentor"; selecting a row calls the no-op
  `switchTo`), :1354-1356 `nav_`, :183-187 nav setup, the nav column in resized() (:698-731), :768 `nav_.setVisible`.
  Keep F1x10 -> DebugConsole. LicenseStatusWidget is laid out in the nav column, so coordinate with the licensing port.
- `switchTo()` (:1160-1184) and its calls (:623, :1051, :1113, :1138, :1152) only maintain `UIState.currentPage/previousPage/
  loadedFolder/libraryMode/libraryFolder/hasSavedState` + `pendingPage/pendingFolder/pendingLibraryMode/pendingLibraryFolder`
  (ServiceLocator.h:17-32) for the "ui" binding (processor :640-673). All of these are meaningless with one page. Do this
  together with the B8/B9 persistence rework (audit B8 wants real segment persistence; `smodeSegmentData` and `smodeBpm` are
  also written but never read).

## B8 (DECISION). SamplarApp standalone. See section 6.

---------------------------------------------------------------------------------------------------

## L. Licensing dead code: COORDINATE WITH THE LICENSING PORT (do not touch now)

The port is live in the tree: LicenseSystemService was rewritten during this analysis, `activateWithKey` and the
`license.validate`/`license.activate` subscriptions are already gone, and LicenseSignature.*, LicenseSigningKey.h,
ClockGuard.h and LicenseGateOverlay.* are new untracked files. Recheck everything below after the port lands.
- `tests/licensing/LicenseSystemTests.cpp` compiled INTO the shipping plugin (CMake 332), so a static juce::UnitTest
  registers inside the DAW. Move it to `SamplarTests` (CMake 879-892, which today compiles 0 tests).
- `src/licensing/FeatureManager.{h,cpp}` (48+117; CMake 325-326) + `featureNameMap()` + `featureManager_` +
  `LicenseSystemService::isFeatureEnabled`. The only caller is the `license.check_feature` subscription, which has no publisher.
- Subscriptions with no publisher: `license.check_feature`, `app.startup`. Published, never subscribed:
  `license.feature_result` (+ `license.validation_result`, `license.activation_result` if still present).
- `LicenseSystemService::isHardwareValid()` (0 callers) -> `HardwareFingerprintGenerator::verifyHardwareIdConsistency()`
  (used only there and in the tests). D14 says it SHOULD be wired into the gates; decide fix vs delete.
- `src/security/LicenseCleanerUtility.{h,cpp}` in SAMPLAR_SOURCES (CMake 314-315) is dead inside the plugin; the only
  plugin include is unused (DebugConsole.h:6). It is needed only by the `SamplarLicenseCleaner` tool (section 6).
- `src/ui/assets/license_placeholder.svg`: 0 references. `src/security/README.md`: documentation, verify it still matches.
- `JUCE_WEB_BROWSER=1` (CMake 419, 684): no `WebBrowserComponent` user (OAuth uses `URL::launchInDefaultBrowser`).
  Set it to 0 unless the new port embeds a browser.
- LicenseStatusWidget is only reachable through the F1x10 debug sidebar (B7).

---------------------------------------------------------------------------------------------------

## 5. Looks dead, but is LIVE (do not delete)

| Thing | Why it is live (evidence) |
|---|---|
| src/ui/Debug/DebugConsole.{h,cpp} | compiled (CMake 334) + MainWindow member + MultiLogger sink. Untracked because of `.gitignore Debug/` (P0). |
| MultiLogger.h | the global logger while the editor is open (MainWindow :465-466) |
| data/ (bundle and repo root) | the install marker for `hasResources()`, `DataPaths::getAppDirectory`, the App.cpp pointer write and the processor walk-up. Repo data/ holds only data/strip_sources.py (pattern-JSON tool, dead). If you delete it, replace it with a data/README.txt marker, or the SamplarApp POST_BUILD copy and the CWD fallback lose data/. |
| FullSongService | name is SM5, but it is the live 4-stem ONNX engine: `SModeSeparationService` calls `separateDrums` -> `separateAudioOnnx`/`ensureOnnxProviderReady`/`locateOnnxModel`. Only the methods listed in B2 are dead. |
| TempoAnalyzer, DWTTempoDetector, OctaveCorrection, TempoRefiner, BeatThisDetector | live through `HybridTempoDetector` (SModePage BPM detection). C12 is about wasted work, not dead code. |
| DownbeatDetector, SpectralFluxDetector (smode), PatternGenerator, MicroFade.h | live (SModeService.cpp, SModePage.cpp, SequencerLane, MidiMappingService). MicroFade is also included by the dead SequencerAudioSource, so do not delete it with B1. |
| WaveDragArea | live (SequencerLaneComponent); MidiDragArea is the dead one |
| `smode.separation.progress`, `smode.workflow.ready/error`, `state.restored`, `update.available`, `license.status.reminder`, `host.tempo(.request)` handlers in MainWindow and the processor | live topics with live publishers |
| ExitButton/PinButton/DragButton, `samplar::MainWindow` (DocumentWindow), `closeButtonPressed` | live in SamplarApp only (hidden when `getUIState() != nullptr`) |
| AudioDeviceService + the SettingsDialog audio/MIDI-device panels, `MidiMappingService::connectToDeviceManager` / `handleIncomingMidiMessage` | standalone-only, compiled in the shared sources; dead in the VST3 but needed while SamplarApp exists (B8) |
| UpdateCheckService / UpdateDialog / NotificationButton | live code path (D3 says it is functionally broken, not dead) |
| OnnxDelayLoad.cpp | not in SAMPLAR_SOURCES, but added via `target_sources` at CMake 790 (required for the System32 ORT stub) |
| third_party/beat_this, signalsmith-stretch, signalsmith-linear | referenced by CMake 124-133, 184-248 |
| assets/samplar_icon_512.png/_64.png, assets/samplar_logo.png, logo.png | ICON_BIG/SMALL, BinaryData (SModePage.h:33); logo.png is the source for assets/generate_icon.py |
| src/app/SamplarVersion.h.in, src/Version.h | configure_file input; Version.h is included by OAuth and UpdateCheck (D4 version unification, not deletion) |
| tests/main.cpp | SamplarTests target |
| `SequencerSlotComponent::keyPressed` (.cpp:805) | unreachable (`setWantsKeyboardFocus(false)` at :12), but it is the intended ESC-cancel for drag preview: fix rather than delete |
| `isRunningStandalone()` guards in the processor resource-path code | the registry, Program Files and breadcrumb strategies (processor :124-240) never match the current installer (D11), but they belong to the D11 resource-lookup redesign. Do not touch them in this pass. |
| .beads/, beads-bin/, AGENTS.md, IDENTITY.md, SOUL.md, MEMORY.md, .serena/, .claude/ | agent and tooling config, not build junk. User decision. |
| VERSION.txt | referenced only by scripts/legacy, but D4 (single version source) may adopt it. Decide in D4. |
| portable_cache/vc_redist.x64.exe | samplar.iss:89 |

## 6. SamplarApp and SamplarLicenseCleaner

**SamplarApp (standalone, dev-only).** Gated by `SAMPLAR_BUILD_STANDALONE` (OFF in build/CMakeCache.txt). The last
artefact is build/SamplarApp_artefacts/Release/SAMPLAR.exe from 2026-05-23, so it has not been built for 5 months.
It compiles the same `SAMPLAR_SOURCES` + App.cpp; the uncommitted CMake also adds OnnxDelayLoad.cpp to it. Buildability
could not be verified (read-only). Expected to build if the plugin builds, because App.cpp's only extra dependencies are
AudioDeviceService, MainWindow and LicenseActivationDialog, which are already compiled. Known gaps: there is no
models/htdemucs.onnx POST_BUILD copy, so separation depends on the DataPaths CWD/AppData fallbacks; there is a MIDI thread
race (A27); and App.cpp:92-131 rewrites `portable_root.txt` (D11).
- Recommended minimum: keep it as a debugging harness, but move `src/app/App.cpp` out of `SAMPLAR_SOURCES` (CMake 252)
  into `target_sources(SamplarApp ...)` and delete 677-681. App.cpp then stops being compiled into the VST3.
- If dropped (B8): delete App.{h,cpp} (366) and AudioDeviceService.{h,cpp} (500); remove CMake 252-253, 399-614, 643-645,
  752-754, 791-793, 807-815; delete MainWindow's DocumentWindow class (:1802-1929, i.e. ctor, closeButtonPressed and the 4
  file-drag forwarders; KEEP `createRootComponent()` at :1931-1937 and the `class MainWindow` decl in MainWindow.h is then
  reduced to the factory) + Exit/Pin/Drag buttons (:37-155) + the
  `!isPlugin` block (:428-434); the SettingsDialog audio-device and MIDI-device panels; MidiMappingService
  `connectToDeviceManager/disconnectFromDeviceManager/setMidiChannelFilter/handleIncomingMidiMessage` + `MidiInputCallback`
  (which also closes A27); and the unused `#include "../app/App.h"` in MainWindow.cpp:2, UpdateCheckService.cpp:3 and
  SettingsDialog.cpp:4 (compile-verify, they may lean on its transitive EventBus.h include).
  Cost: there is no longer a non-DAW way to run the UI.

**SamplarLicenseCleaner.** Always configured (CMake 868-874, not gated) but never built in this build dir
(the artefacts folder has only JuceLibraryCode). build_installer.ps1 builds only `--target SamplarPlugin_VST3`, so it is
never shipped. It is buildable in principle (juce_core + 2 files). It deletes `%APPDATA%/Samplar/license_cache.json`,
`license_failures.json` and `sessions/`, and those paths must match the new licensing storage. Licensing port decides:
gate it behind `option(SAMPLAR_BUILD_TOOLS OFF)` or delete tools/license_cleaner + LicenseCleanerUtility.

## 7. Notes for the implementer
- One commit per batch; batch B3 needs a full Release build + an installer build, because the staging output changes.
- Dead topics after B1-B6 that still need allowlist cleanup: see each batch's "Also update".
  `smode.undo.applied`, `smode.redo.applied`, `smode.stem.pitch.changed`, `midi.cc.bound` and `smode.midi.learn.waiting`
  are publish-only hooks. They are harmless and may be wanted later; not in scope.
- Observed bug while reading (not a deletion): MainWindow :563 `bus->subscribe("smode.separation.progress", ...)` does not
  store its handle, so every song load adds one more subscription that is never removed (relevant to audit B4/B5 owners).
- NOTICE.md/LICENSE.txt: after B1/B2, remove SoundTouch and LTE. MiniBPM (GPL) was never listed; it goes away in B2.
