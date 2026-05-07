# VST3 Mac Porting Guide

**Date:** 2026-03-20
**Latest Commit:** `769140a` + polyphonic MIDI, gate/one-shot modes, tooltip UI
**Platform Reference:** Windows (MSVC 2022, FL Studio)

This document covers all VST3-specific changes made during the Windows stabilization pass. Mac developers should apply equivalent fixes when building the macOS AU/VST3 target.

---

## 1. Crash Fixes (CRITICAL — Apply First)

### 1.1 Logger Use-After-Free (`src/ui/MainWindow.cpp`)

**Problem:** `RootComponent` sets `juce::Logger::setCurrentLogger(multiLogger_.get())` in its constructor. When the editor is destroyed, the destructor body runs first, then member destructors fire. By the time `multiLogger_` (a `unique_ptr`) is destroyed, the global logger still points to freed memory. Next editor open crashes.

**Fix:** Add `juce::Logger::setCurrentLogger(nullptr);` as the **last line** of `~RootComponent()` body (before members auto-destruct).

```cpp
~RootComponent() {
    // ... all other cleanup ...
    juce::Logger::setCurrentLogger(nullptr);  // MUST be last
}
```

### 1.2 LookAndFeel Dangling Pointer (`src/ui/MainWindow.cpp`)

**Problem:** `RootComponent` calls `LookAndFeel::setDefaultLookAndFeel(&lnf_)` where `lnf_` is a member. On destruction, JUCE's Desktop singleton still references the dead LookAndFeel.

**Fix:** Add `LookAndFeel::setDefaultLookAndFeel(nullptr);` as the **first line** of `~RootComponent()`.

### 1.3 AsyncUpdater Pending Callback (`src/ui/Debug/DebugConsole.cpp`)

**Problem:** `DebugConsole` inherits `AsyncUpdater` but had `= default` destructor. A pending async update could fire after destruction.

**Fix:**
```cpp
DebugConsole::~DebugConsole() {
    cancelPendingUpdate();
}
```

### 1.4 Stale EventBus Lambdas (`src/ui/MainWindow.cpp`)

**Problem:** EventBus subscriptions captured `this` but were never unsubscribed. After editor destruction, the bus could invoke dead handlers.

**Fix:**
- Store all `EventBus::HandlerId` values as members
- Unsubscribe all in destructor
- Use `std::shared_ptr<bool> alive_` pattern: capture `alive_` in lambdas, check `if (!*alive) return;` at the top

---

## 2. Plugin Audio Preview (`src/audio/PluginPreviewService.h`, `src/plugin/SamplarAudioProcessor.cpp`)

**Problem:** In VST3 mode, `AudioDeviceService` is intentionally NOT registered (DAW owns audio hardware). The `SampleBrowserPage` audio preview chain (`AudioEngine::connect()`) fails silently — clicks register but no audio plays.

**Architecture:**
```
SampleBrowserPage::masterMixer_
        |
        v
PluginPreviewService::mixer_  (registered as "preview_audio" service)
        |
        v
SamplarAudioProcessor::processBlock()  (mixes into output buffer)
```

**Key files:**
- `src/audio/PluginPreviewService.h` — New file. Wraps `juce::MixerAudioSource`, implements `IService`
- `src/plugin/SamplarAudioProcessor.cpp` — Registers service, calls `prepareToPlay`/`releaseResources`/`getNextAudioBlock` in processBlock
- `src/ui/Pages/SampleBrowserPage.cpp` — `ensureAudioReady()` falls back to adding `masterMixer_` to the preview service when `AudioEngine` fails
- `src/ui/Pages/SampleBrowserPage.h` — Added `bool pluginModeActive_` member

**Mac notes:**
- `PluginPreviewService` is platform-independent, no changes needed
- The `processBlock` mixing code uses standard JUCE buffer operations
- `pluginModeActive_` cleanup occurs at all 3 `audioEngine_.disconnect()` sites in SampleBrowserPage.cpp

---

## 3. External Drag-and-Drop (`src/ui/Pages/SampleBrowserPage.cpp`)

**Problem:** Tile `mouseDrag` used JUCE's internal `DragAndDropContainer::startDragging()` which only works within the JUCE component tree. DAW cannot receive these drops.

**Fix:** Replaced with `DragAndDropContainer::performExternalDragDropOfFiles()` which uses OS-native drag-and-drop (OLE on Windows, NSPasteboard on macOS).

```cpp
juce::StringArray files;
files.add(originalFile_.getFullPathName());
juce::DragAndDropContainer::performExternalDragDropOfFiles(files, false);
```

**Mac notes:**
- `performExternalDragDropOfFiles` is cross-platform in JUCE — should work on macOS without changes
- The internal JUCE drag (for drag-to-folder-tree within the app) was removed in favor of the external drag. If internal drag-to-tree is needed on Mac, you may need to detect whether the drop target is internal vs external

---

## 4. Performance: Deferred GIF Preload (`src/ui/MainWindow.cpp`)

**Problem:** `processing_.preloadResources()` decoded ~18MB of GIF frames synchronously in the `RootComponent` constructor, causing visible delay when reopening the plugin window.

**Fix:** Deferred to 300ms after construction using `juce::Timer::callAfterDelay`:
```cpp
juce::Timer::callAfterDelay(300, [this, alive = alive_]() {
    if (!*alive) return;
    processing_.preloadResources();
});
```

---

## 5. Asset Deployment (Mac-Specific Action Required)

The following assets must exist at runtime for the VST3. On Windows they live in `%APPDATA%\Samplar\`. On Mac, use `~/Library/Application Support/Samplar/`.

| Asset | Path | Purpose |
|-------|------|---------|
| `CAMPO_optimized.gif` | `BACKGROUNDS/` | Processing page animated background |
| `LAVA_optimized.gif` | `BACKGROUNDS/` | Processing page animated background |
| `REF.png` | Root | Initial page logo |
| `backdrop.png` | Embedded in BinaryData | Initial page backdrop (no deployment needed) |

The `GifAnimatedBackground::loadPreset()` search chain checks:
1. Executable directory
2. `../Resources/` relative to executable
3. **AppData/Application Support directory** (this is the one that works for VST3)

**Mac action:** Create an installer step or first-run copy that places these files in `~/Library/Application Support/Samplar/BACKGROUNDS/`.

---

## 6. Build Configuration

### CMake Targets
- `SM5Plugin_VST3` — Builds the actual `.vst3` bundle (NOT `SM5Plugin` which only builds `_SharedCode.lib`)
- On Mac, also consider `SM5Plugin_AU` for Audio Unit

### Deployment Path
- **Windows:** `C:\Program Files\Common Files\VST3\SAMPLAR.vst3\Contents\x86_64-win\`
- **Mac:** `/Library/Audio/Plug-Ins/VST3/SAMPLAR.vst3/Contents/MacOS/`

---

## 7. Testing Checklist

- [ ] Load plugin in DAW
- [ ] Close plugin window, reopen — no crash
- [ ] Close/reopen 5+ times rapidly — no crash
- [ ] Navigate to Sample Browser, click sample — audio plays through DAW
- [ ] Drag sample tile from browser into DAW channel/timeline — file drops correctly
- [ ] Processing page shows animated GIF background
- [ ] Initial page shows REF.png logo and backdrop.png
- [ ] Close DAW — clean shutdown, no hung process
- [ ] Multiple instances of plugin — each operates independently
- [ ] MIDI notes trigger sample playback from browser tiles
- [ ] Right-click tile ? "Assign to MIDI Key..." ? press key ? sample locks to that note
- [ ] Blue lock icon appears on locked tiles
- [ ] Locked samples survive page changes
- [ ] Right-click locked tile ? "Unlock MIDI Key" removes lock
- [ ] Navigate to Processing page (WavePreviewPage) ? spacebar/click plays audio preview
- [ ] Close/reopen editor ? drum library mode and folder restore correctly
- [ ] MIDI: two keys simultaneously ? both samples play (polyphonic)
- [ ] POLY button toggles to MONO ? new note kills previous voices
- [ ] S:1SH button toggles to S:GAT ? releasing key stops sample playback
- [ ] L:1SH button toggles to L:GAT ? loop plays while held, stops on release
- [ ] Multiple tile animations play simultaneously when triggering multiple MIDI notes
- [ ] Gate mode: tile animation stops immediately on key release
- [ ] Hover over POLY/S:1SH/L:1SH buttons ? tooltip appears after ~700ms
- [ ] Sample rate: click-to-play and MIDI playback have same pitch in VST3 mode

---

## 8. MIDI Mapping Service (`src/services/MidiMappingService.h/.cpp`)

**Purpose:** Maps MIDI notes to sample files for pad-style triggering. Provides an `AudioSource` interface so `processBlock()` can pull audio directly.

**Architecture:**
```
MIDI Note In (processBlock)
        |
        v
MidiMappingService::handleNoteOn()
        |
        v
midiMap_[128] lookup ? Voice pool (16 voices)
        |
        v
MidiMappingService::getNextAudioBlock() ? mixed into output buffer
```

**Key concepts:**
- **Auto-map mode:** Visible browser tiles auto-map to notes 48-67 (C3-B4, 20 pads)
- **Locked mode:** User assigns a specific sample to a specific MIDI note via right-click ? "Assign to MIDI Key..." ? MIDI learn
- Locked samples are excluded from auto-map to prevent duplicate playback
- `autoMapBrowserPage()` skips both locked slots AND files that are locked elsewhere
- Thread safety: UI thread writes map via `std::lock_guard`, audio thread reads via `try_lock` (never blocks)
- **Polyphonic by default:** Multiple voices play simultaneously (drum pad style). Mono toggle available
- `SamplePreviewCache::Listener` interface for late buffer resolution when preload completes

**Key files:**
- `src/services/MidiMappingService.h/.cpp` — Service implementation
- `src/plugin/SamplarAudioProcessor.cpp` — Registers service, forwards MIDI, calls `getNextAudioBlock`
- `src/ui/Pages/SampleBrowserPage.cpp` — Wires cache, subscribes to events, context menu, lock icon, learn overlay

**Mac notes:**
- Fully platform-independent, no OS-specific code
- MIDI input comes from DAW via `processBlock` MIDI buffer — works on all platforms

---

## 9. MIDI Learn & Lock Icons (`src/ui/Pages/SampleBrowserPage.cpp/.h`)

**Feature:** Right-click a sample tile ? "Assign to MIDI Key..." opens a modal overlay. User presses any MIDI key on their controller. The sample is locked to that note, surviving page changes. A blue lock badge with note name (e.g. "C3") appears on the tile's top-left corner.

**Event flow:**
1. Context menu item 900 ? `MidiMappingService::beginMidiLearnForFile(file)`
2. Overlay shown via `showMidiLearnOverlay(sampleName)`
3. User presses MIDI key ? `handleNoteOn` detects learn mode ? `callAsync` to `lockSampleToNote`
4. `midi.learn.locked` event published ? overlay hidden, all tiles repainted
5. `Tile::paint()` checks `getLockedNoteForFile()` and draws blue badge if locked

**Unlock:** Context menu item 901 ? `unlockNote(noteNumber)` ? tile repainted without badge

**Mac notes:**
- All UI is standard JUCE `Component`/`Graphics` — no platform-specific rendering
- MIDI learn overlay is a child component with click-to-cancel

---

## 10. WavePreviewPage Plugin Audio (`src/ui/Pages/WavePreviewPage.cpp/.h`)

**Problem:** Same issue as SampleBrowserPage (Section 2) — `AudioEngine::connect()` fails in VST3 mode because no `AudioDeviceService` exists. Audio preview on the processing page was silent.

**Fix:** Added `pluginModeActive_` flag and `PluginPreviewService` fallback, same pattern as SampleBrowserPage:

```cpp
// After creating processingSource_:
if (audioEngine_.isConnected()) {
    audioEngine_.setSource(processingSource_.get());  // standalone
} else {
    // Plugin mode: route through PluginPreviewService
    previewSvc->getMixer().addInputSource(processingSource_.get(), false);
    pluginModeActive_ = true;
}
```

**Cleanup points** (must detach `processingSource_` from preview mixer):
- `~WavePreviewPage()` destructor
- `loadFile()` safe teardown (before replacing reader/transport)
- `resetPreview()` teardown

**Mac notes:**
- Same `PluginPreviewService` as SampleBrowserPage — no additional platform work needed

---

## 11. VST3 UI State Persistence (`src/app/ServiceLocator.h`, `src/ui/MainWindow.cpp`)

**Problem:** When VST3 editor is closed and reopened, the UI lost its state — drum library mode, current folder, scroll position.

**Fix:** `ServiceLocator::UIState` now stores:
- `bool libraryMode` — whether drum library view was active
- `juce::String libraryFolder` — path of the selected library folder

`MainWindow::~RootComponent()` saves these values. `MainWindow::attachBus()` restores them, branching between `loadLibraryFolder()` and `loadFolder()`.

**Mac notes:**
- `ServiceLocator` is a static singleton — works identically on all platforms
- State survives editor close/reopen but not DAW session save/load (not persisted to disk)

---

## 12. SafePointer Pattern for Background Threads

**Problem:** `SampleBrowserPage::loadFolder()` spawns a background thread that calls `juce::MessageManager::callAsync` with a raw `this` capture. If the editor closes before the callback fires, `this` is dangling ? crash.

**Fix:** Use `juce::Component::SafePointer<SampleBrowserPage>`:
```cpp
auto safeThis = juce::Component::SafePointer<SampleBrowserPage>(this);
std::thread([safeThis, ...]() {
    // ... background work ...
    juce::MessageManager::callAsync([safeThis]() {
        if (safeThis == nullptr) return;  // editor was destroyed
        safeThis->doStuff();
    });
}).detach();
```

**Mac notes:**
- `SafePointer` is JUCE cross-platform — same pattern applies
- Also used in `MidiMappingService` with `std::atomic<bool> destroyed_` flag for non-Component classes

---

## 13. Polyphonic MIDI & Voice Modes (`src/services/MidiMappingService.h/.cpp`)

**Feature:** MIDI pad triggering now supports polyphonic playback (multiple simultaneous voices) as the default mode, with a MONO toggle to switch to monophonic (new note kills previous voices).

**Voice modes (atomics, audio-thread safe):**
- `monophonic_` — `false` = polyphonic (default), `true` = monophonic
- `sampleGateMode_` — `false` = one-shot (plays to end), `true` = gate (note-off stops playback)
- `loopGateMode_` — `false` = one-shot (plays once through), `true` = gate (loops while held, stops on release)

**Voice struct changes:**
```cpp
struct Voice {
    bool looping = false;   // True = wrap position to 0 instead of ending
    int noteNumber = -1;    // Which note triggered this voice (for gate note-off matching)
};
```

**Gate mode note-off (`handleNoteOff`):**
- Iterates voice pool, kills voices matching `noteNumber`
- Respects per-voice `looping` flag: loop voices killed by `loopGateMode_`, sample voices by `sampleGateMode_`
- Publishes `midi.pad.stopped` event via `callAsync` (audio?message thread) with `filePath` and `padIndex`

**Mac notes:**
- All mode flags are `std::atomic<bool>` — no platform-specific synchronization
- `callAsync` for event publishing is standard JUCE message thread dispatch

---

## 14. Multi-Tile MIDI Animation (`src/ui/Pages/SampleBrowserPage.cpp/.h`)

**Feature:** Multiple MIDI-triggered tiles animate simultaneously (glow effect), matching polyphonic audio playback.

**Architecture:**
- Replaced single-index `midiPlaybackActive_`/`midiPlayingSampleIndex_` with per-tile map:
  ```cpp
  struct MidiTileAnim {
      double startTime;     // juce::Time::getMillisecondCounterHiRes()
      double durationSec;   // sample duration in seconds
      float progress;       // 0.0-1.0 animation progress
  };
  std::map<int, MidiTileAnim> midiTileAnims_;  // sampleIndex ? animation
  ```
- Timer callback processes all entries, fades out completed ones
- `synchronizeTilePlaybackStates()` checks `midiTileAnims_` first, then falls back to click-to-play state
- `midi.pad.stopped` subscription removes animation from map on gate note-off

**Events:**
- `midi.pad.triggered` — adds entry to `midiTileAnims_` (published by `handleNoteOn`)
- `midi.pad.stopped` — removes entry from `midiTileAnims_` (published by `handleNoteOff` in gate mode)

**Mac notes:**
- All animation is standard JUCE `Component::repaint()` driven by `Timer`
- `midiTileAnims_` is message-thread only — no cross-thread concerns

---

## 15. Trigger Mode UI (`src/ui/Pages/SampleBrowserPage.cpp/.h`)

**Feature:** Three toggle buttons in the browser header for MIDI control:

| Button | Default | Toggle | Function |
|--------|---------|--------|----------|
| `POLY` | Green | `MONO` (orange) | Polyphonic vs monophonic voice mode |
| `S:1SH` | Blue | `S:GAT` (orange) | Sample trigger: one-shot vs gate |
| `L:1SH` | Blue | `L:GAT` (orange) | Loop trigger: one-shot vs gate |

**Tooltips:** All three buttons have `setTooltip()` with multi-line descriptions. Requires `juce::TooltipWindow` instance in component tree (added as member `tooltipWindow_` with 700ms delay).

**Layout:** Buttons are 56px wide, placed in the header row after the navigation controls.

**Mac notes:**
- `juce::TooltipWindow` is cross-platform — no additional work needed
- Button colors use hex `juce::Colour` values — renders identically on macOS

---

## 16. Sample Rate Fix for VST3 (`src/ui/Pages/SampleBrowserPage.cpp/.h`)

**Problem:** Click-to-play used `AudioEngine::getSampleRate()` which returned 44100 in standalone but 0 in VST3 mode (no AudioDeviceService). MIDI playback used the DAW's actual rate (e.g. 48000). This caused pitch/speed mismatches.

**Fix:** Added `getEffectiveOutputSampleRate()` helper that falls back from AudioEngine to PluginPreviewService:
```cpp
double SampleBrowserPage::getEffectiveOutputSampleRate() const {
    double rate = audioEngine_.getSampleRate();
    if (rate > 0.0) return rate;  // standalone mode
    // VST3 fallback: get DAW sample rate from PluginPreviewService
    auto* previewSvc = dynamic_cast<PluginPreviewService*>(
        ServiceLocator::getRegistry()->get("preview_audio"));
    if (previewSvc) rate = previewSvc->getSampleRate();
    return (rate > 0.0) ? rate : 44100.0;  // ultimate fallback
}
```

All 6 call sites using `audioEngine_.getSampleRate()` were replaced with this helper.

**Mac notes:**
- `PluginPreviewService::getSampleRate()` is set by `prepareToPlay` in `processBlock` — works on all DAWs
