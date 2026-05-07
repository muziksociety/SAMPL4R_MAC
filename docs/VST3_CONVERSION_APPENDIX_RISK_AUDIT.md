# VST3 Conversion Plan — Risk Audit Appendix

**Date:** 2026-03-14
**Auditor:** Risk Analyst Agent
**Based on:** Systematic search of all .cpp/.h files under `src/`
**Status:** FINDINGS — Must be addressed before Phase 1 begins

---

## A1. CRITICAL Architecture Gap: The processBlock Bridge Is Broken as Designed

### Finding

The plan (Section 3.3) describes `SamplarAudioProcessor::processBlock()` calling
`masterMixer_.getNextAudioBlock(info)` where `masterMixer_` is a member of
`SamplarAudioProcessor`. **This member does not exist and cannot exist as described.**

The actual `masterMixer_` lives inside `SampleBrowserPage` — a UI Component:

- `src/ui/Pages/SampleBrowserPage.h:485` — `juce::MixerAudioSource masterMixer_`
- `src/ui/Pages/SampleBrowserPage.h:162` — `juce::AudioSourcePlayer player_`

The audio chain is wired entirely inside the UI page at construction time
(`SampleBrowserPage.cpp:1421-1431`):

```
previewMixer_ -> resampler_ -> masterMixer_
sequencerEngine_             -> masterMixer_
masterMixer_                 -> player_   (AudioSourcePlayer)
player_                      -> deviceManager_ (via addAudioCallback)
```

`player_` registers itself with `AudioDeviceManager` as an `AudioIODeviceCallback`.
There is no central `MixerAudioSource` at the application level — there are three
separate `AudioSourcePlayer` instances, one per UI page:

- `src/ui/Pages/SampleBrowserPage.h:162` — owns the main sequencer + preview chain
- `src/ui/Pages/WavePreviewPage.h:123` — owns WaveEditor preview
- `src/ui/Pages/EngineLabPage.h:45` — owns EngineLab preview

### Why This Breaks the Plan

In VST3 mode there is no `AudioDeviceManager`. `player_.addAudioCallback()` calls
will silently fail or crash. The host calls `processBlock()` directly. The plan's
bridge `masterMixer_.getNextAudioBlock(info)` will never receive audio because
`masterMixer_` is inside an editor component that may not even exist (editors are
created and destroyed on every open/close cycle).

### Required Fix Before Phase 1

The audio ownership must be inverted BEFORE the plugin switch:

1. Move `masterMixer_`, `sequencerEngine_`, `previewMixer_`, `resampler_`, and
   `transport_` out of `SampleBrowserPage` into `SamplarAudioProcessor` (or a new
   `AudioEngine` object owned by the processor).
2. `SampleBrowserPage` receives references to these objects instead of owning them.
3. Only then can `processBlock()` call `masterMixer_.getNextAudioBlock()` safely.

This is **Phase 0 work**, not Phase 1. It must be done before `juce_add_gui_app`
is replaced with `juce_add_plugin`.

**Severity: CRITICAL — the plan's core audio bridge cannot function without this refactor.**

---

## A2. CRITICAL: `JUCEApplication::getInstance()` Called in 11 UI Files

The plan identifies this risk but underestimates its scope. The actual call sites
found in source:

| File | Lines | Usage |
|------|-------|-------|
| `src/ui/Pages/WavePreviewPage.cpp` | 60, 206, 249, 364, 1797, 2213, 2233, 6124 | `dynamic_cast<samplar::App*>` to access ServiceRegistry |
| `src/ui/Pages/SampleBrowserPage.cpp` | 56, 687, 869, 882, 1218, 1230, 1243, 1289, 1454, 1761, 2157, 4118, 4176, 4613 | same pattern |
| `src/ui/Pages/EngineLabPage.cpp` | 53, 69, 152 | same pattern |
| `src/ui/Pages/ChatPage.cpp` | 311, 696 | same pattern |
| `src/ui/MainWindow.cpp` | 48, 230, 257, 317, 584, 850, 1203, 1254, 1376, 1551, 1804, 1815 | systemRequestedQuit + ServiceRegistry access |
| `src/ui/Variation/VariationManager.cpp` | 11, 88 | ServiceRegistry access |
| `src/ui/WaveEditor/WaveEditorView.cpp` | 177, 355 | ServiceRegistry access |
| `src/services/ChatService.cpp` | 61, 673, 683, 693 | ServiceRegistry access |
| `src/services/UpdateCheckService.cpp` | 298 | ServiceRegistry access |
| `src/ui/Components/PatternLibraryDialog.cpp` | 1743, 1762 | ServiceRegistry access |
| `src/ui/Components/SettingsDialog.cpp` | 1165, 1389, 1684, 1718 | ServiceRegistry access |
| `src/ui/Components/SpectralSeparateDialog.cpp` | 491, 528 | ServiceRegistry access |

In VST3 mode `JUCEApplication::getInstance()` returns `nullptr`. Every one of
these call sites will silently do nothing (when guarded by `dynamic_cast`) or crash
(when not guarded). The `systemRequestedQuit()` calls in `MainWindow.cpp:48` and
`:1815` will dereference a null pointer and crash the host.

**The plan's "Files to Modify" list (Section 10) only lists 5 files. The actual
count is at least 12 files containing ~50 call sites.**

Required fix: add a global `samplar::ServiceRegistry* g_registry` accessor or pass
the `SamplarAudioProcessor*` reference down through the component tree so components
can reach services without going through `JUCEApplication`.

---

## A3. CRITICAL: `runModalLoop()` — Blocks the Message Thread in a Host

One use of the blocking synchronous modal loop was found:

- `src/ui/licensing/LicenseActivationDialog.cpp:142` — `dialogWindow->runModalLoop()`

In VST3 mode most hosts run the plugin editor on the host's message thread.
`runModalLoop()` spins a nested event loop which is undefined behavior inside a
DAW. Ableton Live and FL Studio will freeze or crash. This is not an async variant.

Required fix: convert to `enterModalState()` with a `ModalCallbackFunction`, or
use `juce::DialogWindow::LaunchOptions::launchAsync()`.

All other modal uses found (`enterModalState` at SampleBrowserPage:3230, :3396,
:6281, :9013; PatternLibraryDialog:1201; PatternPreviewDialog:4521;
CategoryEditorDialog:438; AddCategoryDialog:1092; WavePreviewPage:5363) use async
callbacks and are acceptable, but they still require
`JUCE_MODAL_LOOPS_PERMITTED=0` to be left unset or the preprocessor guard
`JucePlugin_Build_VST3` to skip them in plugin builds.

---

## A4. HIGH: `getCurrentWorkingDirectory()` Used as Data File Fallback

Several files use `juce::File::getCurrentWorkingDirectory()` as a fallback when
the exe-relative path does not exist:

| File | Line | What it loads |
|------|------|--------------|
| `src/ui/Components/StepSequencerComponent.cpp` | 3668, 3676, 3699 | `data/patterns_with_rolls.json`, `data/patterns_rhythmic.json`, `data/latin_patterns.json` |
| `src/ui/Components/PatternLibraryDialog.cpp` | 252 | `data/` directory |
| `src/ui/Components/samplarAnimation.cpp` | 487 | animation backgrounds directory |
| `src/utils/MidiPatternDatabase.cpp` | 44 | pattern database |
| `src/services/FullSongService.cpp` | 788 | changes CWD before launching subprocess |

In VST3 mode the CWD is set by the host (typically the host's installation
directory or `C:\Windows\System32` in some DAWs). The fallback paths will resolve
to wrong locations. Pattern data will silently fail to load and the sequencer will
have no built-in patterns.

The `FullSongService.cpp:788` case is additionally dangerous: it saves the
previous CWD, changes it to launch demucs, then restores it. In a multi-instance
scenario this is a race condition — CWD is per-process, not per-instance.

Required fix: all CWD fallbacks must be replaced with
`File::getSpecialLocation(File::currentExecutableFile).getParentDirectory()` or
the VST3 binary location via `File::getSpecialLocation(File::hostApplicationPath)`.

---

## A5. HIGH: `std::mutex` Lock Inside `getNextAudioBlock()` on the Audio Thread

`SequencerAudioSource::getNextAudioBlock()` acquires `patternMutex_` on every
call (lines 68, 83, 112 of `SequencerAudioSource.cpp`). `std::mutex::lock()` is a
blocking operation — it is forbidden on the audio thread because it can cause
priority inversion on Windows WASAPI and ASIO.

`SamplePreviewCache.cpp` similarly acquires `cacheMutex_` (line 45) from paths
that may be called during audio rendering.

These work reliably today in the standalone because the host thread and the
mutex-contending threads have compatible scheduling. Inside a DAW with a real-time
audio thread (e.g., ASIO with 32-sample buffers), mutex contention will cause
audible dropouts or xruns.

Required fix: replace `std::mutex` with a lock-free mechanism for the audio-thread
path. The simplest approach is `juce::AbstractFifo` for commands and
`std::atomic` flags for play/stop state. Pattern data changes (non-realtime) can
remain mutex-protected but must only be applied via a double-buffer swap that
happens atomically from the audio thread's perspective.

---

## A6. HIGH: `AudioDeviceManager` Pointer Stored in Three UI Pages

The plan says `AudioDeviceService` needs an `isHostManaged()` guard. The actual
scope is:

| File | Member | Usage |
|------|--------|-------|
| `src/ui/Pages/SampleBrowserPage.h:163` | `deviceManager_` | `addAudioCallback(&player_)` at lines 4122, 4256, 4299, 4638 |
| `src/ui/Pages/WavePreviewPage.h:125` | `deviceManager_` | audio device access |
| `src/ui/Pages/EngineLabPage.h:46` | `deviceManager_` | audio callback registration |
| `src/ui/Components/SpectralSeparateDialog.h:221` | `deviceManager_` | audio callback registration |

All four components call `addAudioCallback()` to register their `AudioSourcePlayer`
with the device manager. In VST3 mode there is no device manager. These calls must
be made conditional on `isRunningAsStandalone()`, or the audio chain must be
restructured as noted in A1.

---

## A7. HIGH: `currentExecutableFile` Resolves to the VST3 Binary in Plugin Mode

Many services use `File::getSpecialLocation(File::currentExecutableFile)` to find
bundled data:

- `src/services/FullSongService.cpp` — lines 471, 565, 1066, 1089, 1232, 1395
- `src/services/DrumSeparatorService.cpp` — lines 59-60
- `src/utils/MidiPatternDatabase.cpp` — line 39
- `src/utils/LogPaths.h` — lines 16-17
- `src/ui/Pages/InitialPage.cpp` — lines 32, 865
- `src/ui/WaveEditor/WaveEditorView.cpp` — line 585
- `src/ui/Pages/WavePreviewPage.cpp` — line 1247
- `src/ui/Components/StepSequencerComponent.cpp` — line 3662
- `src/ui/Components/PatternLibraryDialog.cpp` — line 249

In standalone mode `currentExecutableFile` is `SAMPLAR.exe` and its parent
directory contains the `data/` folder and Python environment. In VST3 mode
`currentExecutableFile` is `SAMPLAR.vst3\Contents\x86_64-win\SAMPLAR
5.vst3`. Its parent directory does not contain `data/`. All these paths will fail.

The Python bridge (`FullSongService`) and demucs CLI (`DrumSeparatorService`) both
depend on finding the portable Python environment relative to the executable. Both
will silently fail in VST3 mode.

Required fix: define a compile-time or runtime path resolver that, in VST3 mode,
walks up from the VST3 binary to the bundle root or checks a user-configurable
data path stored in `userApplicationDataDirectory`.

---

## A8. MEDIUM: State Save Complexity Is Severely Underestimated

The plan lists "state save/restore (full)" as Phase 4 MEDIUM risk. The actual state
that must be persisted for a meaningful VST3 session includes:

**Sequencer state** (owned by `SequencerAudioSource` + `StepSequencerComponent`):
- Per-row sample file paths (absolute), mute/solo/volume/pan per row
- Per-step on/off state, ratchet data, velocity per step
- Tempo (BPM), swing %, pattern length, time signature

**Sample browser state**:
- Current library root folder path
- Current category/subcategory selection
- Selected tile index
- MIDI mapping table (if Phase 2 is done)

**WaveEditor state** (owned by `sm5port::State` — already has its own
save/load mechanism at `src/ui/WaveEditor/sm4port/State.cpp:244`):
- Trim points, loop markers, processing preset selection
- Effect chain settings (Gate, Comp, Limiter parameters)

**Active file paths**:
- Currently loaded song file (`FullSongService`)
- Currently previewed sample

None of this state currently goes through `getStateInformation()`/
`setStateInformation()` because those methods do not exist in the standalone.
When a DAW saves a session and reloads it, the plugin will open empty with no
samples loaded and an empty sequencer grid.

This must be designed and implemented in Phase 1 (stub), Phase 2 (sequencer
patterns + sample paths), Phase 3 (full WaveEditor state), not deferred to Phase 4.

---

## A9. MEDIUM: `FullSongService` Changes Process CWD — Race Condition With Multiple Instances

`src/services/FullSongService.cpp:788`:
```cpp
auto prevCwd = juce::File::getCurrentWorkingDirectory();
// ... sets new CWD for demucs subprocess ...
// restores prevCwd at end
```

CWD is a process-level property. If two VST3 instances are loaded simultaneously
(one on each track in a DAW), and both invoke FullSongService's demucs path, the
CWD save/restore will interleave non-atomically. The comment at line 1477 already
acknowledges this and uses `CreateProcess` with an explicit `lpCurrentDirectory`
as a workaround for some paths. The CWD mutation path at line 788 must be audited
to ensure it is either eliminated or serialized with a process-level mutex.

---

## A10. MEDIUM: Standalone Regression — `juce_add_plugin` Changes the Standalone Wrapper

When switching from `juce_add_gui_app` to `juce_add_plugin`, the standalone target
is no longer a bare `JUCEApplication`. It becomes a
`juce::StandalonePluginHolder`-wrapped application with these behavioral changes:

1. The top-level window gains an "Options" button for audio/MIDI device selection
   instead of the current custom `SettingsDialog`. Both will exist simultaneously.
2. `JUCEApplicationBase::initialise()` is called with different arguments —
   `samplar::App::initialise()` receives the command-line string, not argc/argv.
3. Window title, icon, and frame are controlled by `StandaloneFilterWindow`, not
   `MainWindow`. Custom title bar code in `MainWindow.cpp` will conflict.
4. The standalone saves its audio device settings in a file named after
   `JucePlugin_Name` in `userApplicationDataDirectory`. If this conflicts with
   existing SAMPLAR settings files, the device configuration will reset on first launch
   after the update.

---

## A11. LOW: `LicenseActivationDialog` Modal Usage Blocks Plugin Host (Duplicate of A3 — Separate Entry for Severity Tracking)

- `src/ui/licensing/LicenseActivationDialog.cpp:142` — `runModalLoop()` is
  synchronous. This is the license activation flow. If a DAW user opens the plugin
  for the first time without a license, the activation dialog will block the DAW's
  message thread entirely. This must become async before VST3 ships.

---

## Summary Table

| ID | Severity | File(s) | Issue |
|----|----------|---------|-------|
| A1 | CRITICAL | SampleBrowserPage.h:485, :162 | masterMixer_ and AudioSourcePlayer live in UI, not processor — processBlock bridge broken |
| A2 | CRITICAL | 12 files, ~50 sites | JUCEApplication::getInstance() returns null in VST3 |
| A3 | CRITICAL | LicenseActivationDialog.cpp:142 | runModalLoop() blocks host message thread |
| A4 | HIGH | StepSequencerComponent.cpp:3668+, PatternLibraryDialog.cpp:252, samplarAnimation.cpp:487 | getCurrentWorkingDirectory() fallbacks break in VST3 |
| A5 | HIGH | SequencerAudioSource.cpp:68,83,112; SamplePreviewCache.cpp:45 | std::mutex acquired on audio thread — priority inversion risk |
| A6 | HIGH | SampleBrowserPage.h:163, WavePreviewPage.h:125, EngineLabPage.h:46, SpectralSeparateDialog.h:221 | AudioDeviceManager pointer in 4 UI components, all call addAudioCallback |
| A7 | HIGH | FullSongService.cpp (6 sites), DrumSeparatorService.cpp:59 + 10 other files | currentExecutableFile path wrong inside VST3 bundle |
| A8 | MEDIUM | No existing getStateInformation | State complexity severely underestimated — sequencer, samples, WaveEditor all need save/restore |
| A9 | MEDIUM | FullSongService.cpp:788 | Process CWD mutation is a race condition with multiple plugin instances |
| A10 | MEDIUM | CMakeLists.txt, MainWindow.cpp | juce_add_plugin changes standalone wrapper behavior |
| A11 | LOW | LicenseActivationDialog.cpp:142 | Duplicate of A3 for standalone async migration tracking |

---

## Revised Phase 0 Requirements

The plan's Phase 0 tasks must be expanded. Before any build system change:

1. **Audio ownership refactor (A1)** — Extract `masterMixer_`, `sequencerEngine_`,
   transport chain from `SampleBrowserPage` into a new `AudioEngine` class owned
   by the processor (or App in standalone). This is the single largest piece of
   work and must happen first.

2. **JUCEApplication accessor replacement (A2)** — Introduce a
   `samplar::ServiceLocator::getRegistry()` static or pass `SamplarAudioProcessor*`
   through the constructor chain. All 12 affected files must be updated.

3. **CWD and exe-relative path audit (A4, A7)** — Define `samplar::DataPaths` helper
   that resolves the `data/` directory correctly in both standalone and VST3 modes.

4. **runModalLoop elimination (A3)** — Convert `LicenseActivationDialog` to async
   before Phase 1.

Items A5 (mutex on audio thread), A8 (state save), A9 (CWD race), and A10
(wrapper behavior) can be addressed in later phases but must be tracked.
