# Drum Library (SampleBrowserPage) - Complete Feature Audit

**Document Version:** 1.0  
**Created:** 2026-04-21  
**Scope:** Complete UI element reference for Mentor lesson authoring  
**Source Files:**
- `src/ui/Pages/SampleBrowserPage.h` (lines 34–835)
- `src/ui/Pages/SampleBrowserPage.cpp` (10,466 lines)
- `src/ui/Components/StepSequencerComponent.h` (lines 187–522)
- `src/ui/Components/EqDividerComponent.h` (lines 49+)

---

## 1. Layout Overview

The Drum Library page uses a two-panel desktop layout with a fixed sidebar and expandable content area. When **Library Mode** is active, a collapsible folder tree appears on the left (240px wide); the main sample grid fills the center; and a full-height step sequencer lives at the bottom with an embedded frequency analyzer above it.

```
┌──────────────────────────────────────────────────────────────────┐
│ Header: Batch Info | Selection Info | POLY MONO S:1SH S-Gate... │
├──────────────────────────────────────────────────────────────────┤
│ ┌──────────┐ ┌──────────────────────────────────────────────────┐│
│ │ Folder   │ │                                                    ││
│ │ Tree     │ │     SAMPLE GRID (Tiles with waveforms)            ││
│ │ View     │ │     Each tile: checkbox + waveform + V/recycle    ││
│ │ (240px)  │ │     Engine dots + engine selector (S/C/I)         ││
│ │ [Toggle] │ │                                                    ││
│ │ [Add]    │ │                                                    ││
│ └──────────┘ └──────────────────────────────────────────────────┘│
├──────────────────────────────────────────────────────────────────┤
│ EQ DIVIDER: 5 frequency band buttons + color-coded sample dots   │
├──────────────────────────────────────────────────────────────────┤
│ STEP SEQUENCER (120px tall, resizable)                            │
│ ┌─ Tempo | Bars | Grid | Swing | Buffer | Populate ──────────────┐
│ │ [Kick]     [••••] [••] [••] [••]      G [V]                     │
│ │ [Snare]    [••••] [••] [••] [••]      G [V]                     │
│ │ [Hat]      [••••] [••] [••] [••]      G [V]                     │
│ │ [Clap]     [••••] [••] [••] [••]      G [V]                     │
│ └─ [Play▶][Stop■] [Generate] [Clear] [Learn] [Show Patterns]────┘│
├──────────────────────────────────────────────────────────────────┤
│ FOOTER: [Select All] [Clear All] [Export Selected] [Export Lib]   │
│         [Select by Song] [Show Selected] [HOME]                   │
└──────────────────────────────────────────────────────────────────┘
```

---

## 2. Top-Bar Controls

### 2.1 Batch Navigation

| Control | Type | Member Name | File:Line | Purpose |
|---------|------|-------------|-----------|---------|
| `batchInfo_` | Label | `batchInfo_` | SampleBrowserPage.h:93 | Displays "Batch N of M" for paginated results |
| `<` (prev batch) | TextButton | `prevBatch_` | SampleBrowserPage.h:95 | Decrement batch index, rebuild grid |
| `>` (next batch) | TextButton | `nextBatch_` | SampleBrowserPage.h:96 | Increment batch index, rebuild grid |

**Behavior:** When batch size = 20, shows pages of samples. Navigation persists batch index across folder changes.

---

### 2.2 Selection & Filtering

| Control | Type | Member Name | File:Line | Purpose |
|---------|------|-------------|-----------|---------|
| `selectionInfo_` | Label | `selectionInfo_` | SampleBrowserPage.h:94 | Displays "N selected" (updated on tile click) |
| `showSelected_` | TextButton | `showSelected_` | SampleBrowserPage.h:97 | Toggle "Show Selected Only" mode (filters grid) |

**Behavior:**
- When `showSelectedOnly_` = true, grid displays only selected tiles; button text changes to "Show All".
- Clicking clears batch index (shows from page 1).
- Wired at SampleBrowserPage.cpp:1139.

---

### 2.3 Navigation Buttons

| Control | Type | Member Name | File:Line | Purpose |
|---------|------|-------------|-----------|---------|
| `DRUM LIBRARY` | TextButton | `libraryButton_` | SampleBrowserPage.h:98 | Enter library mode (tree view + organize) |
| `CHAT` | TextButton | `chatButton_` | SampleBrowserPage.h:99 | Navigate to Mentor chat page |

**Behavior:**
- `libraryButton_` toggles library mode; background changes from dark to gold when active (cpp:2520–2524).
- `chatButton_` calls `onGoChat()` callback set by shell (h:48).
- Colors: Gold text for library, Cyan text for chat (cpp:1143–1150).

---

### 2.4 MIDI Voice Mode & Trigger Settings

| Control | Type | Member Name | File:Line | Purpose |
|---------|------|-------------|-----------|---------|
| `POLY` / `MONO` | TextButton | `monoPolyButton_` | SampleBrowserPage.h:100 | Toggle polyphonic (multiple simultaneous) vs monophonic (one sample at a time) preview |
| `S:1SH` / `S-Gate` | TextButton | `sampleTrigButton_` | SampleBrowserPage.h:101 | Sample trigger: OneShot (plays once) vs Gate (plays while key held) |
| `L:1SH` / `L-Gate` | TextButton | `loopTrigButton_` | SampleBrowserPage.h:102 | Loop trigger: OneShot vs Gate |

**Behavior:**
- Cycles through states on click; text changes to reflect current mode.
- Used by step sequencer playback and MIDI trigger system.
- Color: Light green for POLY (cpp:1155), light blue for trigger modes (cpp:1174–1194).
- State not persisted; reset on app restart.

---

### 2.5 Preview Options

| Control | Type | Member Name | File:Line | Purpose |
|---------|------|-------------|-----------|---------|
| `LOOP` | ToggleButton | `previewLoopBtn_` | SampleBrowserPage.h:103 | When checked, clicking a tile loops the sample; unchecked, plays once |
| `DAW` | ToggleButton | `dawSyncBtn_` | SampleBrowserPage.h:104 | When checked, step sequencer syncs tempo to DAW host BPM |

**Behavior:**
- `previewLoopBtn_` affects single-tile playback behavior (cpp:1213).
- `dawSyncBtn_` subscribes to `hostTempoSub_` (h:105) for DAW tempo updates.
- Colors: Cyan for loop (cpp:1213), orange for DAW sync (cpp:1224).

---

### 2.6 Tooltips

| Control | Type | Member Name | File:Line | Purpose |
|---------|------|-------------|-----------|---------|
| Tooltip Window | TooltipWindow | `tooltipWindow_` | SampleBrowserPage.h:106 | 700ms hover delay for all buttons |

---

## 3. Folders Pane (Left Sidebar)

### 3.1 Folder Panel Toggle

| Control | Type | Member Name | File:Line | Purpose |
|---------|------|-------------|-----------|---------|
| Panel Toggle Button | TextButton | `folderPanelToggle_` | SampleBrowserPage.h:119 | Collapse/expand folder tree (width: 240px ↔ 20px toggle strip) |

**Behavior:**
- When collapsed, shows a narrow colored strip with `<` glyph; click to expand.
- When expanded, shows full tree view plus "Add" and context menu buttons.
- State: `folderPanelCollapsed_` boolean (h:117).

---

### 3.2 Folder Tree View

| Control | Type | Member Name | File:Line | Purpose |
|---------|------|-------------|-----------|---------|
| Folder Tree | DroppableTreeView | `folderTreeView_` | SampleBrowserPage.h:110 | Hierarchical folder browser for library organization |
| Root Tree Item | FolderTreeItem | `rootTreeItem_` | SampleBrowserPage.h:111 | Root node for folder tree |

**Behavior:**
- Accepts drag-drop from sample tiles (drag a tile → drop on folder → move/copy sample).
- Right-click folder → context menu (add, remove, rename, classify).
- Single-click folder → load samples into grid.
- Current folder stored in `currentLibraryFolder_` (h:112).
- Folder list persisted in `libraryRootFolders_` (h:811).

---

### 3.3 Folder Operations

**Wired callbacks:**
- `addLibraryFolder()` – browse for new library folder (cpp:~7400).
- `removeLibraryFolder()` – remove selected folder from tree (cpp:~7450).
- `showLibraryContextMenu()` – right-click menu (cpp:~7350).

---

## 4. Sample Grid (Center Panel)

### 4.1 Grid Container

| Control | Type | Member Name | File:Line | Purpose |
|---------|------|-------------|-----------|---------|
| Grid Viewport | Viewport | `gridView_` | SampleBrowserPage.h:123 | Scrollable container for tiles |
| Grid Content | Component | `gridContent_` | SampleBrowserPage.h:124 | Parent component holding all tile objects |
| Tiles Array | OwnedArray<Tile> | `tiles_` | SampleBrowserPage.h:532 | Array of visible tiles (20 per page by default) |

**Behavior:**
- Grid is rebuilt each time batch or filter changes (function `rebuildGrid()` at cpp:~3000).
- Tiles are created incrementally to avoid UI freeze (progressive build at cpp:~2800).
- Grid layout: N columns × M rows (auto-sized based on available width).

---

### 4.2 Per-Tile Structure

Each tile is a custom `Tile` class (SampleBrowserPage.h:205–493) with the following controls:

#### 4.2.1 Selection Checkbox

| Control | Type | Member Name | File:Line | Purpose |
|---------|------|-------------|-----------|---------|
| Checkbox | ToggleButton | `selectToggle_` | Tile.h:316 | Click to include tile in bulk operations |

**Behavior:**
- Callback: `onSelectChanged_` (called when state changes).
- Maintains `selected_` boolean state.
- Colored cyan when checked (cpp:47).

---

#### 4.2.2 Waveform Display

| Control | Type | Member Name | File:Line | Purpose |
|---------|------|-------------|-----------|---------|
| Waveform | AudioThumbnail | `thumb_` | Tile.h:476 | Visual representation of audio (mono or stereo) |

**Behavior:**
- Drawn as an outlined shape in a color determined by frequency/category.
- Updated when tile file changes (setter: `setFile()`, `setDisplayFile()`).
- Color: `waveformColour_` (h:484).

---

#### 4.2.3 Preview Button (V)

| Control | Type | Member Name | File:Line | Purpose |
|---------|------|-------------|-----------|---------|
| V Button | CircleButton | `var_` | Tile.h:468 | Click to open sample preview in full-screen editor |

**Behavior:**
- Custom circle button with tinted ring (h:318–357).
- Click → calls `onOpenPreview()` callback (cpp:2998, 3348).
- Styled cyan ring on black fill.
- Hit-test includes full circle area (not just ring).

---

#### 4.2.4 Regenerate Button (Recycle Icon)

| Control | Type | Member Name | File:Line | Purpose |
|---------|------|-------------|-----------|---------|
| Recycle Button | RecycleButton | `regenerateBtn_` | Tile.h:469 | Click to regenerate variations for this sample (QOL Fix #3) |

**Behavior:**
- Only visible when variation expansion is open for this tile.
- Calls `onGenerate()` callback.
- Styled with recycling icon (3 curved arrows).
- Visibility controlled by `setRegenerateButtonVisible()` (h:277–282).

---

#### 4.2.5 Engine Selector Dots

| Control | Type | Member Name | File:Line | Purpose |
|---------|------|-------------|-----------|---------|
| Simple Dot | EngineDotButton | `simpleDot_` | Tile.h:471 | Select Simple variation engine (green: #00FF88) |
| Creative Dot | EngineDotButton | `creativeDot_` | Tile.h:472 | Select Creative variation engine (pink: #ff69b4) |
| Impact Dot | EngineDotButton | `impactDot_` | Tile.h:473 | Select Impact variation engine (cyan: #00FFFF) |

**Behavior:**
- Three small colored circles (one per engine).
- Click dot → set engine for this sample.
- Only visible when variation expansion is open.
- Selected engine has white highlight ring (cpp:455–458).
- Visibility controlled by `setRegenerateButtonVisible()`.

---

### 4.3 Tile Color-Coding

**Tile displays four types of color information:**

| Color Layer | Source | Purpose | File:Line |
|---|---|---|---|
| Waveform outline | EQ analysis (frequency) | Indicates dominant frequency band (bass/mid/treble) | SampleBrowserPage.cpp:~5700+ |
| Engine tint ring | Engine selection | Tints the waveform based on chosen engine | Tile.cpp:237 |
| Song color border | Batch metadata | Visual grouping of samples from same song | SampleBrowserPage.h:136–145 |
| ML badge letter | ML classification | Single letter (K/S/H/C) indicating drum type | Tile.cpp:250–255 |

**Color Palette for Songs (batch mode):**
```cpp
Coral red    (#FF6B6B)
Teal         (#4ECDC4)
Yellow       (#FFE66D)
Mint         (#95E1D3)
Salmon pink  (#F38181)
Purple       (#6C5CE7)
Pink         (#FD79A8)
Cyan         (#00CEC9)
```

---

### 4.4 Tile Interactions

#### Mouse Events (Tile.cpp:40–217)

| Event | Behavior |
|-------|----------|
| **Left-click waveform** | Play tile (if not selected), or toggle selection (if shift held) |
| **Left-click checkbox** | Toggle selection state |
| **Double-click waveform** | If variations expanded, regenerate; else expand variations |
| **Shift-click** | Multi-select range from last clicked tile to current |
| **Ctrl-click** | Toggle single tile selection |
| **Right-click waveform** | Show context menu: play, preview, edit, classify, export |
| **Right-click V button** | Show engine selector menu |
| **Drag from V button** | Open drag-and-drop MIDI learn overlay |
| **Hover trim handles** | Show start/end trim point indicators (for manual trim mode) |

---

### 4.5 Right-Click Context Menu (Per-Tile)

Menu items depend on tile state:

| Menu Item | Condition | Action | File:Line |
|-----------|-----------|--------|-----------|
| **Play** | Always | Trigger preview playback | Tile.cpp:~1100 |
| **Open Preview** | Always | Call `onOpenPreview()` callback | Tile.cpp:2998 |
| **Edit in Wave Editor** | Always | Call `onEditInWaveEditor()` callback | Tile.cpp:~3150 |
| **Classify As...** | Always | Submenu: Kick / Snare / Hat / Clap / Custom | Tile.cpp:~3200 |
| **Move to Folder** | Library mode | Submenu: available folders | Tile.cpp:~3400 |
| **Export** | Always | Export single sample to user's chosen folder | Tile.cpp:~3500 |
| **Trim** | Always | Enter trim mode (show handles on waveform) | Tile.cpp:~3600 |
| **Reveal in Explorer** | Always | Open folder containing file | Tile.cpp:~3700 |

---

## 5. Frequency Bar (EQ Divider Component)

Located above the step sequencer. A horizontal strip showing 5 frequency band buttons with color-coded sample distribution dots.

### 5.1 Component

| Control | Type | Member Name | File:Line | Purpose |
|---------|------|-------------|-----------|---------|
| EQ Divider | EqDividerComponent | `eqDivider_` | SampleBrowserPage.h:724 | Interactive frequency visualizer + filter panel |

**Layout:** Fixed height ~60px, full page width.

---

### 5.2 Frequency Band Buttons

| Band | Freq Range | Button Color | NeonGlow | Purpose |
|------|------------|--------------|----------|---------|
| **SUB** | 20–50 Hz | Deep purple | #7030a0 | Bass/sub-bass (kick fundamentals) |
| **BASS** | 50–200 Hz | Blue | #0070c0 | Kick/tom body |
| **LOW-MID** | 200–500 Hz | Cyan | #00b8e6 | Low tom/snare body |
| **MID** | 500–2 kHz | Green | #70ad47 | Snare/hi-hat attack |
| **HIGH** | 2–20 kHz | Yellow-Orange | #ffc000 | Hi-hat sizzle/cymbal |

**Behavior:**
- Each button displays a count of samples in that frequency band.
- Clicking a button → filters grid to show only samples in that band.
- Multiple bands can be selected (exclusive or cumulative depending on mode).
- Samples appear as small colored dots above each button.

---

### 5.3 EQ Divider Interactions

| Action | Behavior | File:Line |
|--------|----------|-----------|
| **Click band button** | Apply frequency filter; grid updates | EqDividerComponent.cpp:~400 |
| **Right-click band button** | Show sub-filter menu (kick types, snare types) | EqDividerComponent.cpp:~450 |
| **Hover dot** | Tooltip shows sample name + frequency | EqDividerComponent.cpp:~500 |
| **Click dot** | Navigate to sample in grid (scroll into view) | EqDividerComponent.cpp:~550 |
| **Drag slider (if present)** | Adjust frequency range bounds | EqDividerComponent.cpp:~600 |

---

### 5.4 ML Category Filter Buttons (Below Bands)

If ML model has 3+ training categories, a row of category buttons appears:

| Button | Color | Purpose |
|--------|-------|---------|
| **K** | Green (#00FF88) | Filter Kicks (ML detected) |
| **S** | Pink (#FF69B4) | Filter Snares (ML detected) |
| **H** | Cyan (#00FFFF) | Filter Hi-Hats (ML detected) |
| **C** | Gray (#888888) | Filter Custom categories |

**Behavior:**
- Click → apply filter; grid shows only samples in selected category.
- Multiple categories can be toggled simultaneously.
- ML confidence threshold: 60%+ to show category button.

---

## 6. Step Sequencer (Bottom Panel)

A classic FL Studio–style sequencer for pattern composition and playback. Spans full page width, 120px tall (resizable).

### 6.1 Transport Controls (Top-Left)

| Control | Type | Member Name | File:Line | Purpose |
|---------|------|-------------|-----------|---------|
| Play | TextButton | `playBtn_` | StepSequencerComponent.h:429 | Start playback (arrow glyph: ▶) |
| Stop | TextButton | `stopBtn_` | StepSequencerComponent.h:430 | Stop playback (square glyph: ■) |
| Generate | TextButton | `generateBtn_` | StepSequencerComponent.h:431 | Generate new pattern (uses selected genre + variation) |
| Clear | TextButton | `clearBtn_` | StepSequencerComponent.h:432 | Erase all cells (holds Shift to skip confirmation) |
| Learn | TextButton | `learnBtn_` | StepSequencerComponent.h:433 | Save current row pattern to user database |

**Behavior:**
- Play/Stop: toggle `playing_` state; calls `onPlaybackStart`/`onPlaybackStop` callbacks (h:292–294).
- Generate: triggers pattern generation based on genre + variation slider.
- Learn: saves the currently-hovered row's pattern to `UserPatternDatabase`.

---

### 6.2 Settings Panel (Right Side)

| Control | Type | Member Name | File:Line | Purpose |
|---------|------|-------------|-----------|---------|
| Tempo Label + Editor | Label + TextEditor | `tempoLabel_`, `tempoEdit_` | StepSequencerComponent.h:385–386 | BPM value (e.g., "120") |
| Bars Dropdown | ComboBox | `barsCombo_` | StepSequencerComponent.h:388 | Select pattern length (1–16 bars) |
| Grid Dropdown | ComboBox | `gridCombo_` | StepSequencerComponent.h:390 | Step resolution (1/4, 1/8, 1/16, 1/32) |
| Swing Slider | Slider | `swingSlider_` | StepSequencerComponent.h:392 | Humanize timing (0–75%) |
| Buffer Dropdown | ComboBox | `bufferCombo_` | StepSequencerComponent.h:394 | Audio buffer size for playback |

**Behavior:**
- Tempo: accepts numeric input; updates `tempoBpm_` (h:485).
- Bars: changes `numBars_` and rebuilds grid (h:473).
- Grid: changes step resolution and redraws pattern (h:227–228).
- Swing: applies groove to even-numbered steps (h:486).
- Buffer: passed to audio engine (callback: `onBufferSizeChanged`, h:280).

---

### 6.3 Populate Button

| Control | Type | Member Name | File:Line | Purpose |
|---------|------|-------------|-----------|---------|
| Populate | TextButton | `populateBtn_` | StepSequencerComponent.h:381 | Auto-fill empty rows from library (respects locked rows) |

**Behavior:**
- Click → for each unlocked, empty row, request a sample from the browser.
- Callback: `onRequestSampleForCategory` (h:279).

---

### 6.4 Accordion Panel (Timing, Pattern, Actions)

| Section | Controls | File:Line | Purpose |
|---------|----------|-----------|---------|
| **Timing** | Tempo, Bars, Grid, Swing, Buffer | StepSequencerComponent.h:403 | Tempo and timing controls (collapsed by default) |
| **Pattern** | Genre, Variation slider, Generate button | StepSequencerComponent.h:404 | Pattern library and generation settings |
| **Actions** | Play, Stop, Learn, Clear | StepSequencerComponent.h:405 | Playback and recording buttons |

**Behavior:**
- Each section collapsible (header click to toggle).
- Collapsed section shows a 1-line preview of content.
- Used to save vertical space in compact layouts.

---

### 6.5 MIDI Drag Area & Wave Drag Area

| Control | Type | Member Name | File:Line | Purpose |
|---------|------|-------------|-----------|---------|
| Drag MIDI | MidiDragArea | `midiDragArea_` | StepSequencerComponent.h:395 | Drag to export pattern as MIDI file |
| Drag Wave | WaveDragArea | `waveDragArea_` | StepSequencerComponent.h:396 | Drag to export rendered pattern as audio file |

**Behavior:**
- Click-and-hold → cursor changes; drag to DAW or file manager.
- MIDI export: standard .mid file with note-on/note-off events per row.
- Wave export: renders all playing rows mixed to stereo WAV file.

---

### 6.6 Row Controls (Left Side Per-Lane)

For each of the 4 default rows (Kick, Snare, Hat, Clap), the following are displayed:

| Control | Type | Purpose | File:Line |
|---------|------|---------|-----------|
| Row Label | Label | Displays row name (e.g., "Kick") | StepSequencerComponent.h:239 |
| Waveform Thumbnail | AudioThumbnail | Mini preview of assigned sample | StepSequencerComponent.h:346 |
| S (Solo) Toggle | IconButton | Click to solo this row only | StepSequencerComponent.h:466 |
| G (Generate) Button | ChannelGenerateButton | Generate pattern for this row only | StepSequencerComponent.h:452 |
| V (Volume) Knob | VolumeKnobButton | Drag to adjust row preview volume (-24 to +6 dB) | StepSequencerComponent.h:453 |

**Behavior:**
- Row names come from `cfg_.rowNames` (h:197); can be dynamic.
- Waveform updates when file is dropped onto row (callback: `onFileDropped`, h:276).
- Solo: when any row soloed, only soloed rows play (callback: `onRowVolumeChanged`, h:288).
- Generate: uses pattern library for current row category.
- Volume: logs dB to console; callback passes linear gain to audio engine (h:288).

---

### 6.7 Step Grid (Center Panel)

| Cell | Status | Color | Behavior |
|------|--------|-------|----------|
| **Off** | No note scheduled | Dark gray (#1a1a1a) | Click to toggle on |
| **On** | Note scheduled | Cyan (#00FFFF) | Click to toggle off |
| **Hovered** | Mouse over cell | Brighter variant | Shows tooltip with step/row info |
| **Playhead** | Current step during playback | Bright green (accent) | Displays current playhead position |

**Interactions:**
- **Click cell:** Toggle note on/off (callback: `onCellToggled`, h:275).
- **Right-click cell:** Open ratchet/roll editor (sub-divisions, velocity shaping).
- **Drag across cells:** Paint mode (all dragged cells set to same state).
- **Double-click cell:** Open step editor (for advanced ratchet/roll parameters).

---

### 6.8 Resize Handle

| Control | Type | Location | Purpose |
|---------|------|----------|---------|
| Top Handle | Rectangle | Above sequencer | Drag up/down to resize sequencer height |

**Behavior:**
- Cursor changes to resize cursor when hovering.
- Drag updates `desiredHeight_` (h:519).
- Callback: `onHeightResizeRequested` (h:516).

---

### 6.9 Sequencer Layout Configuration

| Property | Value | File:Line | Purpose |
|----------|-------|-----------|---------|
| Default rows | 4 | StepSequencerComponent.h:194 | Kick, Snare, Hat, Clap |
| Default steps | 16 | StepSequencerComponent.h:195 | 1 bar = 16 steps (1/16 grid) |
| Beats per bar | 4 | StepSequencerComponent.h:196 | 4/4 time signature |
| Row height | 22px | StepSequencerComponent.h:358 | Fixed row height (no scaling) |
| Cell gap | 6px | StepSequencerComponent.h:366 | Spacing between step cells |
| Label width | 120px | StepSequencerComponent.h:353 | Left panel width (collapsible) |

---

## 7. Bottom Action Row

Buttons positioned in a horizontal row at the very bottom of the page.

### 7.1 Bulk Selection Controls

| Control | Type | Member Name | File:Line | Purpose |
|---------|------|-------------|-----------|---------|
| Select All | TextButton | `selectAll_` | SampleBrowserPage.h:128 | Check all tiles on current page |
| Clear All | TextButton | `clearAll_` | SampleBrowserPage.h:129 | Uncheck all tiles |

**Behavior:**
- Wired at cpp:1987, 2006.
- `selectAll_` calls `selectAllOnPage()` (selects only visible tiles).
- `clearAll_` calls `deselectAll()` (clears all selections globally).
- Selection count updates in `selectionInfo_` label.

---

### 7.2 Export Controls

| Control | Type | Member Name | File:Line | Purpose |
|---------|------|-------------|-----------|---------|
| Export Selected | TextButton | `exportSelected_` | SampleBrowserPage.h:130 | Export checked samples to a user-chosen folder |
| Export to Library | TextButton | `exportToLibrary_` | SampleBrowserPage.h:131 | Export checked samples to library folder (creates new subfolder) |

**Behavior:**
- `exportSelected_` calls `showExportDialog()` (cpp:2012).
- `exportToLibrary_` calls `showExportToLibraryDialog()` (cpp:2020).
- Both require at least 1 selection; show warning if none selected.
- Export includes original filename or user-provided prefix.

---

### 7.3 Batch-Specific Control

| Control | Type | Member Name | File:Line | Purpose |
|---------|------|-------------|-----------|---------|
| Select by Song | TextButton | `selectBySong_` | SampleBrowserPage.h:132 | Batch mode only: menu to select all samples from a specific song |

**Behavior:**
- Only visible if `hasBatchMetadata_` = true (batch processing mode active).
- Click → show dropdown menu of song names + counts.
- Select song → filter to show/select only that song's samples (via `filterBySongName()`).

---

### 7.4 Navigation

| Control | Type | Member Name | File:Line | Purpose |
|---------|------|-------------|-----------|---------|
| HOME | TextButton | `backButton_` | SampleBrowserPage.h:125 | Return to previous page (e.g., processing workflow) |

**Behavior:**
- Calls `onBack()` callback (h:47).
- Wired at cpp:1271–1272.
- Typically returns user to initial page or processing page.

---

## 8. Other Controls & Hidden Features

### 8.1 Variation Expansion Grid

When a tile is double-clicked or "Generate" button is clicked, a full-page variation grid appears:

| Control | Type | Member Name | File:Line | Purpose |
|---------|------|-------------|-----------|---------|
| Variation Grid | VariationExpansionGrid | `expansion_` | SampleBrowserPage.h:202 | Full-page grid of 20 variations per engine (3 engines = 60 total) |
| Expanded Sample Index | int | `expandedSample_` | SampleBrowserPage.h:203 | Index of currently expanded tile (–1 = none) |

**Behavior:**
- Grid overlays main sample browser.
- Shows 3 columns (one per engine).
- Each variation is a clickable tile (play preview, drag to export, right-click options).
- Close with Up arrow key or button.

---

### 8.2 State Persistence (AppState Binding)

| State Binding | Purpose | File:Line |
|---|---|---|
| `stepSeqStateBinding_` | Persist step sequencer state (pattern, tempo, bars) | SampleBrowserPage.h:582 |
| `sampleBrowserStateBinding_` | Persist browser state (batch index, filter toggles) | SampleBrowserPage.h:585 |

**Behavior:**
- Registered in constructor; auto-unregisters on destruction.
- Survives app restart (stored in AppState ValueTree).

---

### 8.3 Audio Playback System

| Component | Type | Purpose | File:Line |
|---|---|---|---|
| Audio Engine | AudioEngine | Standalone sample playback | SampleBrowserPage.h:186 |
| Transport | AudioTransportSource | Manages file playback position | SampleBrowserPage.h:185 |
| Preview Mixer | MixerAudioSource | Routes preview + sequencer | SampleBrowserPage.h:591 |
| Sequencer Engine | SequencerAudioSource | Audio-thread-accurate pattern playback | SampleBrowserPage.h:594 |
| Resampler | ResamplingAudioSource | Converts file sample rate to device rate | SampleBrowserPage.h:596 |

**Playback Path:**
1. User clicks tile → `triggerOneShot(file)` or `playFile(file)`.
2. File is either loaded from disk via `reader_` or fetched from `sampleCache_`.
3. Audio is mixed with sequencer output and sent to device.

---

### 8.4 Analysis & Caching

| Cache | Purpose | File:Line |
|---|---|---|
| `analysisCacheByPath_` | Frequency, color, ML category, silence flag (persists across folder changes) | SampleBrowserPage.h:557 |
| `folderCacheByPath_` | Audio file list per folder (for instant re-opening) | SampleBrowserPage.h:565 |
| `eqWaveColorsByPath_` | EQ-analyzed waveform colors (session-only) | SampleBrowserPage.h:534 |
| `sampleCache_` | Pre-decoded audio buffers for instant playback (SamplePreviewCache) | SampleBrowserPage.h:792 |

---

### 8.5 Keyboard Shortcuts (Implicit)

| Key | Behavior |
|-----|----------|
| **Spacebar** | Toggle step sequencer playback |
| **Up Arrow** | Close variation expansion grid |
| **Shift+Click** | Multi-select range |
| **Ctrl+Click** | Toggle single tile selection |
| **Ctrl+A** | Select all tiles on page |
| **Delete** | Delete selected samples (with confirmation) |

---

## 9. Event Bus Subscriptions

SampleBrowserPage listens to the following EventBus topics:

| Topic | Subscriber | Purpose | File:Line |
|-------|-----------|---------|-----------|
| `"host.tempo"` | `hostTempoSub_` | DAW tempo sync (when dawSyncBtn checked) | SampleBrowserPage.h:105 |
| `"engineUI.variation"` | `engineUiSub_` | Handle variation settings changes | SampleBrowserPage.h:702 |
| `"variation.settingsChanged"` | `settingsSub_` | Listen for engine settings updates | SampleBrowserPage.h:703 |
| `"ml.learnPattern"` | `learnPatternSub_` | Learn MIDI pattern from audio loop | SampleBrowserPage.h:704 |
| `"ml.learnAndApply"` | `learnAndApplySub_` | Learn pattern and apply to sequencer | SampleBrowserPage.h:705 |
| `"midi.padTrigger"` | `midiPadTriggerSub_` | Animate tile playback from MIDI trigger | SampleBrowserPage.h:706 |
| `"midi.learnLocked"` | `midiLearnLockedSub_` | Notification: MIDI learn is locked | SampleBrowserPage.h:707 |
| `"midi.padStopped"` | `midiPadStoppedSub_` | MIDI gate note-off (stop playback) | SampleBrowserPage.h:708 |

---

## 10. Recommended Lesson Structure

### **Lesson 1: Introduction to the Drum Library**
**Duration:** 8–10 minutes  
**Learning Goal:** Understand the overall purpose and navigation of the Drum Library page.

**Topics:**
1. What is the Drum Library? (file browser vs library organizer).
2. Key sections: Folders pane, sample grid, frequency analyzer, step sequencer.
3. Navigation buttons: LIBRARY (toggle), CHAT, HOME.
4. The selection info label + "Show Selected Only" toggle.

**Mentor Props:**
- CARD: Show the main layout diagram (folders | grid | sequencer).
- BTN: Highlight LIBRARY, CHAT, HOME buttons.
- ICON: Folder tree icon, grid icon, sequencer icon.

---

### **Lesson 2: Organizing Your Samples with Folders**
**Duration:** 10–12 minutes  
**Learning Goal:** Add, organize, and manage sample folders in library mode.

**Topics:**
1. Entering library mode (click LIBRARY button).
2. Folder tree view: expand/collapse, add folder, remove folder.
3. Folder tree drag-and-drop: drag tile onto folder to move sample.
4. Right-click folder menu: classify folder, set color, organize subfolders.
5. Persistent library folders (saved between sessions).

**Mentor Props:**
- CARD: Step-by-step folder organization flow.
- BTN: "Add Folder", folder toggle button, context menu items.
- ICON: Folder tree, drag icon, color palette.

---

### **Lesson 3: Exploring the Sample Grid & Playback**
**Duration:** 12–15 minutes  
**Learning Goal:** Preview samples, understand color coding, and control playback modes.

**Topics:**
1. Sample tiles: waveform display, selection checkbox, V (preview) button.
2. Per-tile color layers: frequency band (outline), engine tint, song color, ML badge.
3. Playing samples: LOOP toggle, POLY/MONO mode, S:1SH vs S-Gate triggers.
4. Right-click context menu: play, preview, edit, classify, export.
5. Multi-selection: Shift-click range, Ctrl-click toggle, Select All / Clear All.
6. Pagination: batch navigation (< >), show selected only.

**Mentor Props:**
- CARD: Tile anatomy diagram (checkbox, waveform, buttons, colors).
- BTN: V, Recycle, engine dots, POLY/MONO, S:1SH, L:1SH, LOOP, DAW.
- DROPDOWN: Color legend (frequency bands, ML categories).

---

### **Lesson 4: Filtering & Frequency Analysis**
**Duration:** 10–12 minutes  
**Learning Goal:** Use the frequency analyzer to discover and filter samples by spectral content.

**Topics:**
1. EQ Divider component: 5 frequency bands (SUB, BASS, LOW-MID, MID, HIGH).
2. Clicking band buttons to filter samples.
3. Sample dots showing distribution across bands.
4. ML category buttons (K, S, H, C) for ML-detected drum types.
5. Sonar effect (visual feedback on sample discovery).

**Mentor Props:**
- CARD: EQ Divider layout + frequency band chart.
- BTN: SUB, BASS, LOW-MID, MID, HIGH, K, S, H, C buttons.
- ICON: Color-coded frequency bands, sonar effect animation.

---

### **Lesson 5: Creating Patterns with the Step Sequencer**
**Duration:** 15–18 minutes  
**Learning Goal:** Compose drum patterns using the FL Studio–style sequencer.

**Topics:**
1. Step sequencer overview: rows (Kick, Snare, Hat, Clap), steps (16 default), cells (on/off).
2. Playback controls: Play, Stop, tempo BPM, bars, grid resolution.
3. Editing pattern: click cells to toggle notes, drag to paint.
4. Per-row controls: solo, generate, volume knob.
5. Ratchet/roll: right-click cell for advanced editing.
6. Populate: auto-fill rows from library.
7. Swing: humanize timing for groove.
8. Export: drag MIDI or wave file to export.

**Mentor Props:**
- CARD: Step sequencer layout + row anatomy.
- BTN: Play, Stop, Generate, Clear, Learn, Populate, G (generate), V (volume), S (solo).
- SLIDER: Swing, variation slider.
- ICON: Play/Stop glyphs, ratchet icon, drag indicators.

---

### **Lesson 6: Exporting & Advanced Features**
**Duration:** 8–10 minutes  
**Learning Goal:** Export samples and patterns for use in production.

**Topics:**
1. Export Selected: bulk export checked samples to any folder.
2. Export to Library: organize exports into library subfolder.
3. Select by Song (batch mode): filter and export by song origin.
4. Variation expansion: double-click tile to see 60 variations (3 engines × 20).
5. Engine selector dots: choose variation engine per tile.
6. MIDI learn overlay: drag-to-map MIDI controller.
7. Wave editor integration: edit sample in dedicated editor.

**Mentor Props:**
- BTN: Export Selected, Export to Library, Select by Song.
- ICON: Engine dots (Simple, Creative, Impact), expand/collapse, MIDI learn icon.
- DIALOG: Export dialog mockup (folder browse, prefix, numbering).

---

## 11. Unclassified / Uncertain Elements

The following UI elements were **found in code but require user clarification** for accurate lesson authoring:

| Element | Location | Uncertainty | Recommendation |
|---------|----------|-------------|-----------------|
| MIDI Learn Overlay (midiLearnOverlay_) | SampleBrowserPage.h:711 | Is this a full overlay or a small tooltip? When is it triggered? | Ask user: show screenshot of MIDI learn UI in action |
| "Select by Song" menu | SampleBrowserPage.h:132 | Does it show hierarchical list or flat menu? What are the UX affordances? | Ask user: show menu mockup + behavior |
| Trim mode (handles on tile) | Tile.h:305–313 | When is trim mode entered? How are trim points confirmed/applied? | Ask user: test trim flow + provide screenshots |
| Classification override (right-click submenu) | Tile.cpp:~3200 | Does user see all 8 categories or filtered? What happens on selection? | Ask user: test classification UI + show options |
| Ratchet/roll editor (right-click cell) | StepSequencerComponent.cpp:~340 | What does the ratchet editor dialog look like? How many sub-divisions? | Ask user: show ratchet editor mockup |
| Swing slider visualization | StepSequencerComponent.h:392 | Is swing visualized on the step grid (offset cells)? | Ask user: confirm swing visualization in running app |
| Scrollbar in sequencer | StepSequencerComponent.h:357 | When does scrollbar appear? How does it interact with wrapping? | Ask user: test horizontal scroll behavior |
| Learn button (sequencer) | StepSequencerComponent.h:433 | What happens when user clicks Learn? Dialog, toast, background save? | Ask user: test Learn button + show feedback |
| Genre selection dialog | StepSequencerComponent.h:425 | What does PatternLibraryDialog look like? Genres, subgenres, previews? | Ask user: show PatternLibraryDialog mockup |

---

## 12. Summary Statistics

| Category | Count | File Location |
|----------|-------|----------------|
| **Top-bar buttons** | 8 | SampleBrowserPage.h:93–106 |
| **Grid tile controls per tile** | 6 (checkbox, waveform, V, Recycle, 3 engine dots) | Tile.h:316–473 |
| **Bottom action buttons** | 6 | SampleBrowserPage.h:128–132, 125 |
| **EQ divider band buttons** | 5 | EqDividerComponent.h (SUB, BASS, LOW-MID, MID, HIGH) |
| **ML category filter buttons** | 4 | EqDividerComponent (K, S, H, C) |
| **Sequencer row controls** | 5 per row × 4 rows = 20 | StepSequencerComponent.h:239–256 |
| **Sequencer transport buttons** | 5 | StepSequencerComponent.h:429–433 |
| **Step grid cells** | 16 steps × 4 rows = 64 (default) | StepSequencerComponent (grid_, h:331) |
| **Frequency band colors** | 5 | SampleBrowserPage.cpp:~3689+ |
| **Song color palette** | 8 | SampleBrowserPage.h:136–145 |
| **EventBus topics** | 8 | SampleBrowserPage.h:702–708 |

---

## 13. Code References for Implementation

**Key files for future feature additions:**

| Feature | Header | Implementation | Lines |
|---------|--------|-----------------|-------|
| Tile structure & interactions | SampleBrowserPage.h | SampleBrowserPage.cpp | 205–493 / ~38–1200 |
| Step sequencer | StepSequencerComponent.h | StepSequencerComponent.cpp | 187–522 / (large) |
| EQ divider | EqDividerComponent.h | EqDividerComponent.cpp | 49–100+ / (large) |
| Variation expansion | VariationExpansionGrid.h | VariationExpansionGrid.cpp | (separate) |
| MIDI trigger animation | SampleBrowserPage.h | SampleBrowserPage.cpp | 759–765 / (scattered) |
| Folder tree | FolderTreeItem.h | FolderTreeItem.cpp | (separate) |

---

**End of Audit Document**
