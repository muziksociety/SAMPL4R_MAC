# Mentor Helper Host Pages

Mapping of workflow sections to their host JUCE Component classes in SAMPLAR.

## Summary Table

| Section id  | Host Component class  | File (path)                        | Notes |
|-------------|----------------------|------------------------------------|-------|
| fullsong    | ProcessingPage       | src/ui/Pages/ProcessingPage.h      | Displays separation progress during FullSong drum extraction |
| drumstem    | WavePreviewPage      | src/ui/Pages/WavePreviewPage.h     | Hosts 4-step drum stem editing workflow (slice, beat grid, processing, sample slicing) |
| smode       | SModePage            | src/ui/Pages/SModePage.h           | Sampler mode with stem sequencer and MIDI learn (confirmed) |
| drumlib     | SampleBrowserPage    | src/ui/Pages/SampleBrowserPage.h   | Drum library browser with category tree and sample grid (confirmed) |

## Evidence & Navigation Flow

### FullSong (→ ProcessingPage)
- **File**: `src/ui/MainWindow.cpp:365-387`
- **Trigger**: User selects "Full Song" mode on InitialPage, loads audio file
- **Flow**: 
  1. InitialPage calls `onOpenFile()` callback with ProcessMode::FullSong
  2. MainWindow shows ProcessingPage (index 2) via `switchTo(2)`
  3. FullSongService runs separation in background thread
  4. ProcessingPage displays animated loading + progress percentage
  5. Once complete, drums stem loads into WavePreviewPage

**Code snippet** (MainWindow.cpp:365-387):
```cpp
if (processMode_ == InitialPage::ProcessMode::FullSong) {
    // Full Song mode: First separate drums, then load into WavePreviewPage
    processing_.resetForNewJob();
    processing_.setInputFile(f);
    processing_.setProcessMode(2);  // FullSong mode
    switchTo(2);
    // ... separation in background thread
}
```

### DrumStem (→ WavePreviewPage)
- **File**: `src/ui/MainWindow.cpp:504-628`
- **Trigger**: User selects "Drum Stem" mode on InitialPage, loads audio file
- **Flow**:
  1. InitialPage calls `onOpenFile()` with ProcessMode::DrumStem
  2. MainWindow detects mode 1 (DrumStem) and sets `processMode_ = 3`
  3. Separation runs; result drum stem loads into WavePreviewPage (index 1)
  4. WavePreviewPage hosts the 4-step workflow: SelectLength → BeatGrid → Processing → SampleSlice

**Code snippet** (MainWindow.cpp:628):
```cpp
mode = (processMode_ == InitialPage::ProcessMode::DrumStem) ? 3 : 2;
```

**WavePreviewPage workflow** (WavePreviewPage.h:26-33):
```cpp
enum class WorkflowStep {
    Step1_SelectLength,    // User selects sample length
    Step2_DownbeatLoops,   // User adjusts beat grid and downbeat
    Step25_Processing,     // User applies pre-slice processing effects
    Step3_SampleSlice      // User sets slice granularity and erases unwanted slices
};
```

### SMode (→ SModePage)
- **File**: `src/ui/Pages/SModePage.h` (class declaration)
- **Trigger**: User selects "S-Mode" on InitialPage, loads audio file, selects stem count (4 or 6)
- **Flow**:
  1. InitialPage calls `onOpenFile()` with ProcessMode::SMode
  2. StemCountDialog appears asking for stem count (4 or 6)
  3. SModeService runs multi-stem separation
  4. SModePage (index 9) becomes visible when `smode.workflow.ready` event fires
  5. Page displays stacked waveforms, sequencer lanes, MIDI learn overlay

**Code snippet** (MainWindow.cpp:449-453):
```cpp
smodeWorkflowReadySub_ = bus->subscribe("smode.workflow.ready", [this, alive](const juce::var&) {
    if (!*alive) return;
    juce::Logger::writeToLog("[MainWindow] S-Mode workflow ready - switching to SModePage");
    smodePage_.onStemsReady(*smodeService_);
    switchTo(9);  // SModePage index
});
```

### DrumLib (→ SampleBrowserPage)
- **File**: `src/ui/Pages/SampleBrowserPage.h` (class declaration)
- **Trigger**: User clicks "Library" or "Browse" on InitialPage
- **Flow**:
  1. InitialPage has two library buttons: onGoLibrary and onGoBrowser
  2. Both call `browser_.enterLibraryMode()` and `switchTo(3)`
  3. SampleBrowserPage (index 3) displays folder tree and sample grid
  4. Users browse, load samples, or manage categories

**Code snippet** (MainWindow.cpp:349-352):
```cpp
initial_.onGoLibrary = [this]{
    browser_.enterLibraryMode();
    switchTo(3);  // SampleBrowserPage index
};
```

## Page Index Mapping

From MainWindow constructor (`pages_.addChildComponent()`):
- Index 0: InitialPage
- Index 1: WavePreviewPage  
- Index 2: ProcessingPage  
- Index 3: SampleBrowserPage
- Index 4: WaveEditorView
- Index 5: JUCE Editor
- Index 6: (unused)
- Index 7: CommunityPage
- Index 8: ChatPage
- Index 9: SModePage

## Design Notes for Mentor Helper Button

- **FullSong mentor** attaches to ProcessingPage during separation (may need to pause/resume animation visibility)
- **DrumStem mentor** attaches to WavePreviewPage and highlights current workflow step (1-4)
- **SMode mentor** attaches to SModePage top-right (similar to existing UI layout)
- **DrumLib mentor** attaches to SampleBrowserPage top-right or near folder tree header
- All pages are Components with direct access to EventBus for event publishing/subscribing
