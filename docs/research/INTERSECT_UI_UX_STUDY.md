# INTERSECT UI/UX Study — Comprehensive Analysis

**Repository:** https://github.com/tucktuckg00se/INTERSECT
**Date:** 2026-03-31
**Purpose:** Extract UI/UX patterns applicable to SAMPLAR S-Mode

---

## 1. Component Hierarchy (Full Tree)

```
IntersectEditor (AudioProcessorEditor + Timer)
 |
 +-- IntersectLookAndFeel (global LnF, custom font "Inter")
 +-- TooltipWindow (500ms delay)
 |
 +-- HeaderBar (28px fixed height)
 |    +-- TextButton: UNDO, REDO, PANIC, LOAD, SET
 |    +-- sampleInfoBounds (clickable label area)
 |    +-- FileChooser (async)
 |    +-- Settings PopupMenu (themes, scale, NRPN)
 |
 +-- SliceLane (20px fixed height)
 |    (minimap bar — colored rectangles per slice)
 |
 +-- WaveformView (flex: 1.0, min 180px)
 |    +-- WaveformCache (mipmap-accelerated peak cache)
 |    +-- Drag handlers: 7 modes (edge L/R, loop L/R, draw, move, duplicate)
 |    +-- Overlay hints (timed banners)
 |    +-- Transient markers (dashed lines)
 |    +-- Playback cursors (per-voice)
 |    +-- Crossfade fade region triangles
 |    +-- Middle-mouse scroll+zoom
 |
 +-- ScrollZoomBar (10px fixed height)
 |    (timeline ruler with time labels)
 |
 +-- ActionPanel (22px fixed height)
 |    +-- Items: ADD, LAZY, AUTO, COPY, DEL, ZX, FM, RESEQ
 |    +-- AutoChopPanel (overlay, 34px, on waveform bottom)
 |    |    +-- SENS param cell (drag to edit)
 |    |    +-- MIN param cell (drag to edit)
 |    |    +-- DIV param cell (drag to edit)
 |    |    +-- SPLIT EQUAL / SPLIT TRANSIENTS / CANCEL buttons
 |    +-- ReseqPanel (overlay, 34px, on waveform bottom)
 |         +-- BY POSITION / AS CREATED / CANCEL buttons
 |
 +-- SignalChainBar (94px collapsed / 180px expanded)
      +-- Context bar (26px: tabs GLOBAL/SLICE, info, root note, slices count)
      +-- Module strip (4 modules in a row):
      |    +-- TimePitch module (BPM, PITCH, CENTS, ALGO, REPITCH, STRETCH, etc.)
      |    +-- Filter module (ENABLED, TYPE, SLOPE, CUTOFF, RESO, DRIVE, etc.)
      |    +-- Amp module (ADSR, VOLUME, MUTE GRP)
      |    +-- Playback module (LOOP, ONE SHOT, REVERSE, CROSSFADE, etc.)
      +-- [Expanded] Dual strips: SLICE strip + separator + GLOBAL strip
      +-- Expand/collapse chevron toggle
```

## 2. Layout Strategy

**Fixed-height shell layout using FlexBox:**
```
Base size: 800x400 (kBaseW x kBaseH)
Layout: Column FlexBox, no wrap

HeaderBar:       28px  (fixed)
SliceLane:       20px  (fixed)
WaveformView:   flex:1 (min 180px — gets all remaining space)
ScrollZoomBar:   10px  (fixed)
ActionPanel:     22px  (fixed)
SignalChainBar:  94px collapsed / 180px expanded (fixed)
```

**Resizing strategy:**
- Base window is 800x400
- UI scale via `setTransform(AffineTransform::scale(scale))` — scales entire editor
- Scale range: 0.5x to 3.0x, stored in APVTS parameter AND user settings YAML
- When SignalChainBar expands, `setSize(800, 400 + delta)` resizes window
- No responsive breakpoints — everything scales uniformly

**Key insight:** They do NOT use JUCE's built-in resizable window. Instead, `uiScale` parameter + AffineTransform gives pixel-perfect scaling without layout recalculation.

## 3. Waveform Display — Deep Dive

### Rendering Pipeline
```
paint() -> rebuildCacheIfNeeded() -> drawWaveform() -> drawSlices() -> drawFadeRegions()
        -> paintDrawSlicePreview() -> paintLazyChopOverlay() -> paintTransientMarkers()
        -> drawPlaybackCursors() -> paintOverlayHint()
```

### Waveform Cache (WaveformCache)
- **Mipmap-accelerated**: SampleData stores pre-computed peak mipmaps at multiple levels
- Cache stores per-pixel `Peak { maxVal, minVal }` arrays
- Cache key: `{ visibleStart, visibleLen, width, numFrames, samplePtr }` — rebuild only on change
- **Three rendering modes based on zoom:**
  1. **Normal zoom (samplesPerPixel >= 8):** Filled path — top envelope left-to-right, bottom envelope right-to-left, closeSubPath, fillPath
  2. **Transition zone (1-8 spp):** Filled path + center-line stroke (1.5px) to prevent disappearing thin fills
  3. **Sub-sample zoom (<1 spp):** Connected line path (strokePath 1.5px) + sample dots (filled ellipses) when >8px per sample

### Zoom/Scroll Model
- `processor.zoom` and `processor.scroll` are atomic floats
- zoom: 1.0 = full view, up to 16384x
- scroll: 0.0-1.0 normalized position
- ViewState computed each paint: `visibleLen = numFrames / zoom`, `visibleStart = scroll * maxStart`
- Cached during paint via `paintViewStateActive` flag to avoid recomputation

### Background/Grid
- Background: `surface0` (darkest surface)
- Center line: `surface4` at 50% alpha
- Quarter lines: `surface4` at 20% alpha

## 4. Per-Slice/Pad UI — The SignalChainBar

This is the most sophisticated component. It's a **parameter cell grid** system.

### Architecture
- **Cell-based layout**: Every parameter is a `Cell` struct with bounds, value, label, drag mapping
- **Dual scope**: GLOBAL (APVTS parameters) vs SLICE (per-slice overrides via lockMask)
- **Module grouping**: 4 modules (TimePitch, Filter, Amp, Playback) each with header + 2 rows of cells
- **Expanded mode**: Shows BOTH slice and global strips simultaneously with a separator
- **Collapsed mode**: Shows one strip, toggled by GLOBAL/SLICE tabs

### Per-Slice Override System ("Lock" System)
- Each slice has a `uint64_t lockMask` with 38+ individual lock bits
- When a bit is set, the slice uses its own value; otherwise falls through to global
- Locked parameters show label in `color5` (red accent)
- Override count shown per-module in header
- Right-click on locked parameter can clear the override

### Cell Types
1. **Param** — Label on top (7.5pt bold), value below (10.5pt). Vertical drag to edit.
2. **Tab** — GLOBAL/SLICE scope toggle with accent underline bar
3. **SetBpm** — Mini button for BPM entry
4. **NoteRangeToggle** — Toggle button for note range mode

### Drag-to-Edit Parameters
```cpp
// Sensitivity: full range in 200px of vertical drag
float sensitivity = (maxVal - minVal) / 200.0f;
float newVal = startVal + deltaY * sensitivity;
// Shift = coarse (5-unit snap), default = 1-unit snap
float snap = coarse ? 5.0f : 1.0f;
newVal = round(newVal / snap) * snap;
```
- **Special: Filter cutoff uses NormalisableRange with skew factor 0.25** for logarithmic feel
- Double-click opens TextEditor for precise value entry

### Layout Math
- Base cell width: 46px
- Cells laid out with weight-based proportional sizing
- `makeRowCells()` distributes cells using weight * baseCellWidth, scaled if total exceeds available width
- Module reference width ensures consistent column alignment across modules

## 5. Parameter Controls — No Knobs or Sliders

**INTERSECT uses NO traditional knobs or sliders.** Everything is text cells with vertical drag.

### The Interaction Model
1. **View**: Label (top, small, bold) + Value text (bottom, larger)
2. **Edit**: Click-drag vertically (up = increase). Shift for coarse.
3. **Toggle**: Click to toggle booleans (ON/OFF)
4. **Cycle**: Click to cycle through choices (LP/HP/BP/Notch, etc.)
5. **Double-click**: Opens inline TextEditor for numeric entry
6. **Right-click** (on locked params): Context menu to clear override

### Value Formatting
```
formatMs(val)       -> "120ms"
formatPercent(val)  -> "75%"
formatGain(val)     -> "+3.2dB"
formatHz(val)       -> "8.2k" or "200"
formatSigned(val)   -> "+12st"
formatBool(val)     -> "ON" / "OFF"
midiNoteName(note)  -> "C4"
```

## 6. Color Scheme and Styling

### Theme System
```
ThemeData {
    6 surface levels: surface0 (darkest) -> surface5 (lightest)
    3 text levels:    text0 (muted) -> text2 (primary)
    waveform color
    accent color     (cyan/teal: #3fd8d8 dark, #1a8888 light)
    5 semantic colors:
        color1 = playback module (blue)
        color2 = filter module (amber/gold)
        color3 = amp module (green)
        color4 = output module (purple)
        color5 = overrides/locks/warnings (red)
    16-color slice palette
}
```

### User-Customizable Themes
- `.intersectstyle` text files in user app data
- Simple `key: hexvalue` format (no JSON/XML)
- Backward-compatible with old key names
- `setTheme()` is a global function — no per-component theme passing

### Custom Font
- **Inter** font (Regular + Bold) embedded as BinaryData
- Static typeface pointers shared across all instances
- `makeFont(pointSize, bold)` factory
- `fitFontToWidth(text, maxPt, minPt, width, bold)` auto-sizes text to fit

### LookAndFeel Overrides
- Custom button backgrounds: rounded rectangles with outline, brightness shifts on hover/down
- "Shell buttons": `drawShellButton()` — filled rounded rect + 1px outline
- "Outline only" buttons: transparent fill, only outline visible
- Popup menus: subtle shadow (black 24% offset), rounded highlight with accent tint
- Ticked menu items: accent-colored dot (filled ellipse)
- Tooltips: surface2 bg, surface5 border, text2 text

## 7. Drag and Drop

### File Drop (WaveformView)
- Implements `FileDragAndDropTarget`
- `isInterestedInFileDrag()` — accepts wav/ogg/aiff/flac/mp3
- `filesDropped()` — calls `processor.loadFileAsync(file)`, resets zoom/scroll
- Empty state shows "DROP AUDIO FILE OR CLICK LOAD" prompt
- Wayland fallback: "CLICK LOAD TO OPEN AUDIO FILE" (no DnD support)

### Slice Interactions (WaveformView mouse handlers)
7 drag modes, all handled in a single component:

1. **DragEdgeLeft/Right** — Resize slice boundaries. Uses atomic `liveDragOwner` for thread-safe real-time preview. Snap-to-zero-crossing optional.
2. **DragLoopLeft/Right** — Adjust loop start/length within a slice
3. **DrawSlice** — Alt+drag or +SLC mode: draw new slice region (IBeam cursor)
4. **MoveSlice** — Click inside selected slice, drag to reposition
5. **DuplicateSlice** — Ctrl+click inside slice, drag ghost overlay to position (dashed outline)

### Middle-Mouse Drag (Scroll+Zoom)
- Vertical = zoom (exponential: 1.01^deltaY per pixel)
- Horizontal = scroll (offset in view-fraction space)
- Anchor-point zoom: zoom pivots around mouse position

## 8. Visual Feedback

### Hover States
- **Slice edge handles**: Small rounded rectangles at bottom of waveform, grow taller on hover (24px -> 30px)
- **Loop handles**: Similar but at top of waveform (18px -> 22px)
- **Cursor changes**: LeftRightResize on handles, IBeam in draw mode, PointingHand on ActionPanel
- **ActionPanel items**: Background changes to `surface5` on hover, text to `text1`

### Active/Selected States
- **Selected slice**: Tinted fill (colour at 5% alpha), solid edge lines (42% alpha), "Slice N" label at top
- **Non-selected slices**: Only faint edge lines (12% alpha)
- **Active actions**: Background `surface4`, text `accent` color
- **Tab underline**: 2px accent-colored bar at bottom of active tab

### Playback Cursors
- Vertical line per active voice in `accent` color (70% alpha)
- LazyChop preview voice uses `color5` (red) instead
- Crossfade source position: separate cursor in slice colour

### Transient Markers (AutoChop preview)
- Dashed vertical lines: `accent` at 60% alpha
- Pattern: 4px dash, 3px gap

### Fade Region Visualization
- Triangular overlays showing crossfade zones
- Drawn from slice boundary to fade length, triangular shape indicates fade-in/out
- Slice colour at 18% alpha
- Separate rendering for forward loop, reverse loop, and ping-pong modes

### LazyChop Overlay
- Real-time overlay between chop position and playhead
- `color5` fill at 15% alpha + 50% alpha chop marker line

### Overlay Hint Banners
- Centered banner at bottom of waveform
- `surface2` background (94% alpha), `surface5` border, `text2` text
- Auto-dismiss timer or sticky until action

## 9. Keyboard/MIDI Visualization

### MIDI Mapping
- **No visual keyboard display** — slices are mapped to MIDI notes but no piano roll shown
- Root note displayed in context bar (SignalChainBar)
- Per-slice: `midiNote` (low) + `highNote` (high) define note range
- Note range toggle: enables per-slice note range display
- MIDI note names shown using `juce::MidiMessage::getMidiNoteName()`

### MIDI-Driven Selection
- `processor.midiSelectsSlice` flag (FM toggle in ActionPanel)
- When enabled, incoming MIDI note-on selects the corresponding slice
- Visual: selected slice updates in SliceLane and WaveformView

### NRPN Editing
- External MIDI control of slice parameters via NRPN messages
- Channel selection: OMNI or CH 1-16
- "Consume NRPN CCs" option blocks CC passthrough
- Settings stored in user YAML file

### Resequence MIDI
- "RESEQ" button opens panel with BY POSITION / AS CREATED options
- Re-maps slice MIDI notes starting from root note
- Overlay hint confirms action

## 10. Unique UX Patterns

### 1. Lock/Override System (MOST INNOVATIVE)
Every parameter has a global default + per-slice override. The `lockMask` bitfield (38 bits!) controls which parameters are overridden. This is the same concept as CSS specificity — slice values override globals. The UI clearly shows override count per module and highlights locked labels in red.

**Worth stealing for SAMPLAR S-Mode: YES, ABSOLUTELY.**
This is exactly what S-Mode needs for per-pad parameter overrides. The implementation pattern (bitfield mask + dual scope UI) is clean and efficient.

### 2. Text-Cell Parameter Grid (NO KNOBS)
No rotary knobs, no sliders. Every parameter is a label+value text pair edited by vertical drag. This is extremely space-efficient — they fit ~30 parameters in 94px of height.

**Worth stealing: PARTIALLY.** The density is impressive but may be less intuitive for users expecting traditional controls. Good for advanced/compact mode.

### 3. Lazy Chop (Real-Time Slice Creation)
Play audio and tap to create slices at playback position. The overlay shows the region being chopped in real-time.

**Worth stealing: YES.** Great for beat-slicing workflow.

### 4. Snap-to-Zero-Crossing
All slice edge operations can snap to nearest zero crossing for click-free cuts.

**Worth stealing: YES.** Simple but essential for clean slicing.

### 5. Ctrl-Drag Duplicate with Ghost Preview
Drag a slice while holding Ctrl to duplicate it. Shows a dashed-outline ghost at the destination.

**Worth stealing: YES.** Very intuitive drag-to-duplicate.

### 6. Alt-Key Temporary Draw Mode
Holding Alt temporarily enables slice drawing without toggling the +SLC mode. Releasing Alt returns to normal.

**Worth stealing: YES.** Smart modifier key interaction.

### 7. Middle-Mouse Combined Scroll+Zoom
Vertical = zoom, horizontal = scroll, both from single drag gesture. Anchor-point zoom keeps the clicked sample position stable.

**Worth stealing: YES.** Superior to separate scroll/zoom widgets.

### 8. Timer-Based Selective Repaint
Editor runs at 30Hz normally, 60Hz during interaction. Each tick only repaints components that actually changed.

**Worth stealing: YES.** Critical for performance. SAMPLAR should adopt this pattern.

### 9. Mipmap-Accelerated Waveform Cache
Pre-computed peak mipmaps at multiple resolutions. Cache rebuilds only when view parameters change (zoom, scroll, width, sample pointer).

**Worth stealing: YES.** Essential for smooth waveform rendering at all zoom levels.

### 10. Theme System with User Files
Simple text-based theme format, backward-compatible key mapping, themes stored as files in app data.

**Worth stealing: MAYBE.** Nice-to-have but lower priority.

## 11. What NOT to Copy

### 1. Global Theme Singleton
```cpp
static ThemeData globalTheme = ThemeData::darkTheme();
ThemeData& getTheme() { return globalTheme; }
```
This is a global mutable singleton. Works for a single-instance plugin but breaks with multiple instances using different themes. SAMPLAR should use a per-editor theme reference.

### 2. No Component Separation for Parameter Editing
SignalChainBar is a 2000+ line monolith that handles ALL parameter display AND editing in a single `paint()` + `mouseDown/Drag/Up`. This makes it hard to extend. SAMPLAR should use individual reusable components for parameter cells.

### 3. No Undo Integration in UI Components
Undo is handled via `processor.pushCommand(CmdUndo)` — entirely processor-side. The UI has no undo state awareness beyond snapshot version numbers. This works but makes it impossible to show undo/redo previews.

### 4. Raw Atomic Communication Between UI and Audio
```cpp
processor.liveDragOwner.compare_exchange_strong(...)
processor.liveDragBoundsStart.store(...)
```
This is correct for real-time safety but tightly couples UI to processor internals. SAMPLAR should use a more structured command queue.

### 5. Fixed Base Window Size
800x400 with no aspect ratio flexibility. The AffineTransform scale works but limits layout options. SAMPLAR already has a better approach with responsive pages.

### 6. No Visual Keyboard/Pad Display
For a drum sampler, the lack of a visual pad/keyboard display is a significant omission. SAMPLAR's S-Mode pad grid is a better approach for this use case.

## 12. Summary — What to Steal for SAMPLAR S-Mode

### HIGH PRIORITY (implement these)
| Pattern | What | How |
|---------|------|-----|
| Lock/Override System | Per-slice param overrides with bitfield mask | Adapt for per-pad overrides in S-Mode segments |
| Mipmap Waveform Cache | Multi-level peak mipmaps for fast rendering | Port WaveformCache + SampleData mipmap system |
| Anchor-Point Zoom | Zoom pivots around mouse position | Apply to StemWaveformComponent |
| Snap-to-Zero-Crossing | Clean slice boundaries | Add to S-Mode segment editing |
| Timer-Based Selective Repaint | 30/60Hz adaptive, per-component dirty flags | Apply to S-Mode page timer |
| Playback Cursors Per Voice | Show voice positions on waveform | Add to StemWaveformComponent |

### MEDIUM PRIORITY (nice to have)
| Pattern | What | How |
|---------|------|-----|
| Ctrl-Drag Duplicate | Ghost preview on duplicate | Add to segment drag interaction |
| Alt Temporary Draw Mode | Modifier key for temporary mode switch | Add for segment creation |
| Lazy Chop | Real-time playback-based slicing | Consider for S-Mode workflow |
| Fade Region Triangles | Visual crossfade indicators | Add to loop point visualization |
| Overlay Hint Banners | Contextual guidance text | Reuse for S-Mode user hints |

### LOW PRIORITY (skip for now)
| Pattern | What | Why Skip |
|---------|------|----------|
| Text-Cell Param Grid | No-knob parameter editing | SAMPLAR already has good parameter UI |
| Theme File System | User-editable themes | Lower priority feature |
| NRPN Editing | External MIDI control of params | SAMPLAR has MidiMappingService already |

---

## File Index (INTERSECT source)

| File | Lines | Purpose |
|------|-------|---------|
| `src/PluginEditor.h/cpp` | 57 / 559 | Top-level editor, FlexBox shell layout, timer, theme management |
| `src/ui/WaveformView.h/cpp` | 122 / ~1100 | Waveform rendering, slice interaction, 7 drag modes |
| `src/ui/WaveformCache.h/cpp` | 20 / 117 | Mipmap-accelerated peak cache |
| `src/ui/SignalChainBar.h/cpp` | 227 / ~2050 | Parameter grid, lock/override system, dual scope |
| `src/ui/SliceLane.h/cpp` | 18 / 205 | Minimap slice overview bar |
| `src/ui/HeaderBar.h/cpp` | 32 / 364 | Header with file info, buttons, settings menu |
| `src/ui/ActionPanel.h/cpp` | 55 / 481 | Action toolbar, auto chop panel, reseq panel |
| `src/ui/ScrollZoomBar.h/cpp` | 25 / 146 | Timeline ruler with combined drag scroll+zoom |
| `src/ui/AutoChopPanel.h/cpp` | 53 / 351 | Transient detection panel with drag-to-edit params |
| `src/ui/IntersectLookAndFeel.h/cpp` | 57 / 308 | Custom LnF, Inter font, button/menu/tooltip rendering |
| `src/ui/ThemeData.h` | 252 | Theme struct with dark/light defaults, file parsing |
| `src/ui/UIHelpers.h` | 42 | Drag value computation, zoom factor math |
| `src/audio/Slice.h` | 134 | Slice data model with 38+ lock bits |
| `src/params/ParamIds.h` | 43 | APVTS parameter ID strings |
| `src/params/GlobalParamSnapshot.h` | 119 | Global parameter snapshot loader |
| `src/Constants.h` | 18 | Global constants (MIDI, filter, etc.) |
