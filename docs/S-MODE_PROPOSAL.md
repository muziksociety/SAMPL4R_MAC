# S-Mode: Multi-Stem Sampling Mode — Design Proposal v2

**Date:** 2026-03-22
**Status:** DRAFT v2 — Incorporating Santi's feedback

---

## 1. What Is S-Mode?

S-Mode is a new workflow mode alongside **Drum Stem** and **Full Song** on the Initial Page.
It takes a song, separates it into individual stems (4 or 6), displays all stems as stacked
spectral-colored waveforms, and lets the user **create segments directly on the waveforms
that double as MIDI-triggerable pads** — no separate pad grid needed.

**The segments ARE the pads.** Each segment displays its MIDI note assignment (C1, C#1, etc.)
right on the waveform. Stems can be linked to trigger together or played individually.

---

## 2. User Flow

```
┌─────────────────────────────────────────────────────────────────┐
│                        INITIAL PAGE                             │
│                                                                 │
│   Mode: [Drum Stem ▼]  [Full Song ▼]  [★ S-Mode ▼]            │
│                                                                 │
│                    [ Open File ]                                │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    STEM COUNT DIALOG                             │
│                                                                 │
│   "How many stems?"                                             │
│                                                                 │
│   ┌─────────────────────┐  ┌─────────────────────────────┐     │
│   │      4 Stems        │  │         6 Stems              │     │
│   │                     │  │                               │     │
│   │  • Vocals           │  │  • Vocals       • Piano      │     │
│   │  • Bass             │  │  • Bass         • Guitar     │     │
│   │  • Drums            │  │  • Drums        • Other      │     │
│   │  • Other            │  │                               │     │
│   │                     │  │                               │     │
│   │  Faster             │  │  More detail                  │     │
│   └─────────────────────┘  └─────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                   SEPARATION PROGRESS                           │
│                                                                 │
│   Separating "track.wav" into 6 stems...                        │
│   ████████████████░░░░░░░░░░░░░  52%                           │
│                                                                 │
│   Step 2/3: Running inference...                                │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                      S-MODE MAIN PAGE                           │
└─────────────────────────────────────────────────────────────────┘
```

---

## 3. S-Mode Main Page UI

The core concept: **segments on the waveform ARE the MIDI pads.** No separate pad grid.
Each segment shows its assigned MIDI note directly on the waveform.

```
┌──────────────────────────────────────────────────────────────────────────────┐
│  S-MODE  │  track_name.wav  │  BPM: 128  │  ▶ Play  │  ← Back             │
├──────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│       STEMS + SEGMENTS (spectral-colored waveforms)                          │
│                                                                              │
│  ○ S M  ┌──────────┬──────────┬──────────┬──────────┬──────────┐            │
│  VOCALS │ ░░▓▓██░░ │ ░▓██▓░░░ │ ░░░▓██▓░ │ ░▓▓██░░░ │ ░░▓██▓░ │           │
│         │   C1     │   C#1    │   D1     │   D#1    │   E1     │           │
│         └──────────┴──────────┴──────────┴──────────┴──────────┘            │
│  ○ S M  ┌──────────┬──────────┬──────────┬──────────┬──────────┐            │
│  BASS   │ ░▓▓██▓░░ │ ░░▓██░░░ │ ░▓██▓░░░ │ ░░▓▓██░░ │ ░▓██▓░░ │           │
│         │   F1     │   F#1    │   G1     │   G#1    │   A1     │           │
│         └──────────┴──────────┴──────────┴──────────┴──────────┘            │
│  ○ S M  ┌──────────┬──────────┬──────────┬──────────┬──────────┐            │
│  DRUMS  │ ░░▓██▓░░ │ ░▓▓██░░░ │ ░░▓██▓░░ │ ░▓██▓░░░ │ ░░▓██░░ │           │
│         │   A#1    │   B1     │   C2     │   C#2    │   D2     │           │
│         └──────────┴──────────┴──────────┴──────────┴──────────┘            │
│  ○ S M  ┌──────────┬──────────┬──────────┬──────────┬──────────┐            │
│  OTHER  │ ░▓██▓░░░ │ ░░▓██░░░ │ ░▓▓██░░░ │ ░░▓██▓░░ │ ░▓██░░░ │           │
│         │   D#2    │   E2     │   F2     │   F#2    │   G2     │           │
│         └──────────┴──────────┴──────────┴──────────┴──────────┘            │
│                                                                              │
│  ○ = Link toggle (linked stems trigger together on same MIDI note)          │
│  S = Solo    M = Mute    Waveforms use spectral coloring (reuse existing)   │
│                                                                              │
├──────────────────────────────────────────────────────────────────────────────┤
│  SEGMENTATION                                                                │
│                                                                              │
│  Grid: [Off] [Beat] [Bar] [2-Bar]  │  Snap: [On/Off]  │  Freeform: [On]   │
│                                                                              │
│  Create: Click+Drag on waveform    │  Adjust: Drag edges    │  [✕ Delete]  │
│                                                                              │
│  [Auto: Beat] [Auto: Bar] [Auto: 2-Bar] [Auto: Transient]                  │
│                                                                              │
│  [Quick Assign]  │  [Clear All]  │  [Export WAV]                            │
│                                                                              │
├──────────────────────────────────────────────────────────────────────────────┤
│  MIDI CONTROLS                                                               │
│                                                                              │
│  [POLY/MONO]  [1SH/GAT]  │  Assigned: 20 segments  │  Ctrl+Z Undo         │
│                                                                              │
└──────────────────────────────────────────────────────────────────────────────┘
```

### Segment Interaction Model

**Creating segments:**
- **Click + Drag** on empty waveform area → creates a new segment between drag start/end
- **Auto buttons** (Beat/Bar/2-Bar/Transient) → fills entire stem with segments
- Segments are **per-stem** — each stem has its own independent segments

**Editing segments:**
- **Drag segment edges** to resize
- **Right-click segment** → delete, or select + Delete key
- When **Snap is ON** + grid is active → segment edges magnetize to grid points
- When **Freeform is ON** → no grid, fully manual placement

**Triggering segments:**
- Each segment gets an auto-assigned MIDI note (C1, C#1, D1... ascending across stems)
- **Click a segment** with mouse → plays it (preview)
- **MIDI note input** → triggers the assigned segment
- **Linked stems (○)** → same MIDI note triggers that time range across all linked stems

**MIDI note assignment:**
- Auto-assigned sequentially: first stem's segments get C1→, next stem continues where
  the previous left off
- User can **right-click → reassign note** to any available MIDI note
- Assignment label shown directly on the segment: `C1`, `C#1`, etc.

---

## 4. Feature Breakdown

### 4.1 Stem Separation (REUSE: FullSongService)

| Feature | Detail |
|---------|--------|
| 4-stem | Vocals, Bass, Drums, Other |
| 6-stem | Vocals, Bass, Drums, Piano, Guitar, Other |
| Dialog | Simple "4 Stems / 6 Stems" choice — no model names shown |
| Backend | Reuse `FullSongService` with modified config (keep ALL stems) |
| Output | All stems as `AudioBuffer<float>` in memory |
| File input | Single file only (for now) |

**What changes:** Currently FullSongService only keeps the drums stem. S-Mode keeps ALL.

### 4.2 Tempo Grid (REUSE: TempoAnalyzer)

The grid is a **visual guide + magnetic snap target**, not a slicer itself:

| Grid Mode | Description |
|-----------|-------------|
| **Off** | No grid lines, fully freeform |
| **Beat** | Grid line every beat |
| **Bar** | Grid line every 4 beats |
| **2-Bar** | Grid line every 8 beats |

When grid is active + Snap ON → segment edges magnetize to nearest grid point.
When Freeform is ON → grid is visual only, no magnetic snap.

### 4.3 Auto-Segmentation (REUSE: BeatAlignedSlicer)

One-click buttons that fill the entire stem with segments:

| Button | Action |
|--------|--------|
| **Auto: Beat** | Create segments at every beat boundary |
| **Auto: Bar** | Create segments at every bar boundary |
| **Auto: 2-Bar** | Create segments every 2 bars |
| **Auto: Transient** | Onset detection — segments at transient peaks |

Auto-segmentation replaces any existing segments on that stem.

### 4.4 Manual Segmentation (NEW)

| Action | How |
|--------|-----|
| **Click + Drag** | Draw a segment between two points on the waveform |
| **Drag edge** | Adjust segment start/end by dragging boundary markers |
| **Delete** | Right-click → Delete, or select + Delete key |
| **Snap** | Optional magnetic snap to grid points when dragging |

Segments can overlap or have gaps — the user has full control.

### 4.5 Stem Linking (NEW)

The `○` toggle on each stem controls **linking**:

| State | Behavior |
|-------|----------|
| Unlinked | Stem's segments trigger independently |
| 2+ linked | Hitting one MIDI note triggers that time range across ALL linked stems |

**How it works with segments-as-pads:**
When stems are linked, each linked stem's segment at the same time position shares
the same MIDI note. Triggering it plays all linked stems' audio for that region.

### 4.6 Solo / Mute

- **S (Solo)**: Hear only this stem during preview playback
- **M (Mute)**: Silence this stem during preview playback
- Affects preview/transport only — MIDI triggers always play assigned audio

### 4.7 Spectral Waveform Coloring (REUSE: WavePreviewPage)

Reuse the existing spectral analysis coloring from `WavePreviewPage`:
- `spectralColors_` vector computed via FFT analysis
- `drawSpectralWaveform()` renders frequency-based coloring
- Each stem gets its own spectral analysis (different frequency content per stem)

### 4.8 Transport / Preview

| Feature | Detail |
|---------|--------|
| Play/Pause | Preview full song with solo/mute applied |
| Click segment | Audition that segment |
| Double-click segment | Loop-preview that segment |

---

## 5. Additions & Enhancements

### 5.1 Quick Assign

**[Quick Assign]** button: Auto-assigns MIDI notes (C1, C#1, D1...) to all segments
sequentially across stems. Saves manual assignment when you just want everything mapped.

### 5.2 Export Segments as WAV

**[Export WAV]** button: Exports all assigned segments as individual WAV files:
- `track_vocals_C1.wav`, `track_bass_F1.wav`
- Linked segments export as summed stems: `track_vocals+bass_C1.wav`
- User picks output folder via file dialog

### 5.3 Undo/Redo

Ctrl+Z / Ctrl+Y for segment operations:
- Create, delete, resize, reassign — all undoable
- Simple state stack of segment boundary snapshots

### 5.4 Waveform Zoom & Scroll

For long tracks:
- **Mouse wheel** → horizontal zoom
- **Shift + drag** → scroll/pan
- **Overview minibar** at top showing full track with viewport indicator

### 5.5 Keyboard Shortcuts

| Key | Action |
|-----|--------|
| Space | Play/Pause |
| Delete | Remove selected segment |
| Ctrl+Z/Y | Undo/Redo |
| G | Cycle grid mode (Off→Beat→Bar→2-Bar) |
| S | Toggle snap |
| 1-6 | Solo stem N |

### 5.6 Segment Highlight on Trigger

When a segment is triggered via MIDI:
- Brief flash/highlight animation on the segment (like the existing tile animation)
- Reuse the `midiTileAnims_` pattern from SampleBrowserPage

### 5.7 Right-Click Context Menu on Segments

| Option | Action |
|--------|--------|
| Assign MIDI Note... | Pick specific MIDI note |
| Delete Segment | Remove this segment |
| Solo This Stem | Quick solo |
| Export This Segment | Export single WAV |

---

## 6. Code Reuse Map

```
EXISTING CODE (reuse as-is or with minor mods)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

FullSongService            → Stem separation (mod: keep all stems, not just drums)
TempoAnalyzer              → BPM detection for grid
BeatAlignedSlicer          → Auto-segmentation buttons
MidiMappingService         → MIDI note → audio buffer triggering
SamplePreviewCache         → Audio segment caching & instant playback
Voice / getNextAudioBlock  → Polyphonic playback mixing
MIDI UI (POLY/MONO, 1SH/GAT) → Trigger mode buttons
drawSpectralWaveform()     → Spectral-colored waveform rendering
midiTileAnims_ pattern     → Segment trigger animation
ProcessingPage             → Separation progress display
```

## 6.1 Modular File Structure (BIBLE Architecture)

All S-Mode code follows BIBLE conventions: services own logic, UI owns display,
EventBus connects them. No file exceeds 400 lines. No direct imports between layers.

```
src/
├── services/smode/                         ← Business logic layer
│   ├── SModeService.h/.cpp                 ← Orchestrator: owns stems, coordinates all
│   │                                          ~200 lines. Publishes events, delegates work.
│   ├── SModeSeparationService.h/.cpp       ← Wraps FullSongService for multi-stem output
│   │                                          ~150 lines. Config for 4/6 stems, keep-all mode.
│   ├── SModeSegmentModel.h/.cpp            ← Segment data model + MIDI note assignments
│   │                                          ~250 lines. Undo stack, boundary CRUD, effects.
│   ├── SModeSessionSerializer.h/.cpp       ← .sm5s JSON save/load
│   │                                          ~200 lines. Serialize/deserialize full state.
│   └── SModeExporter.h/.cpp                ← Export segments as WAV files
│                                              ~150 lines. Per-segment + linked-stem export.
│
├── ui/Pages/
│   └── SModePage.h/.cpp                    ← Main S-Mode page layout
│                                              ~350 lines. Owns stem rows, controls, effect strip.
│
├── ui/Components/smode/                    ← S-Mode UI components
│   ├── StemRowComponent.h/.cpp             ← One row: label + S/M/Link + waveform
│   │                                          ~200 lines. Hosts StemWaveformComponent.
│   ├── StemWaveformComponent.h/.cpp        ← Spectral waveform + interactive segments
│   │                                          ~350 lines. Segment drawing, drag, click, animation.
│   ├── SegmentOverlay.h/.cpp               ← Segment boundary rendering + drag handles
│   │                                          ~200 lines. Grid snap, magnetic edges, labels.
│   ├── SegmentEffectStrip.h/.cpp           ← Per-segment pitch/filter/reverse/vol/pan
│   │                                          ~200 lines. Sliders + toggles for selected segment.
│   └── StemCountDialog.h/.cpp              ← 4/6 stem chooser dialog
│                                              ~100 lines. Two-button modal dialog.
```

### File Count & Line Estimates

| File | Lines | Layer |
|------|-------|-------|
| SModeService.h/.cpp | ~200 | Service |
| SModeSeparationService.h/.cpp | ~150 | Service |
| SModeSegmentModel.h/.cpp | ~250 | Service |
| SModeSessionSerializer.h/.cpp | ~200 | Service |
| SModeExporter.h/.cpp | ~150 | Service |
| SModePage.h/.cpp | ~350 | UI Page |
| StemRowComponent.h/.cpp | ~200 | UI Component |
| StemWaveformComponent.h/.cpp | ~350 | UI Component |
| SegmentOverlay.h/.cpp | ~200 | UI Component |
| SegmentEffectStrip.h/.cpp | ~200 | UI Component |
| StemCountDialog.h/.cpp | ~100 | UI Component |
| **TOTAL** | **~2,350** | **11 modules** |

Plus ~400 lines modifications to existing files (InitialPage, MainWindow, CMakeLists.txt).

### Communication Flow (EventBus Topics)

```
"smode.separation.start"      → SModeSeparationService starts separation
"smode.separation.progress"   → Progress updates (0-100%)
"smode.separation.complete"   → All stems ready, payload: stem count + paths
"smode.segment.created"       → New segment added (stemIndex, start, end)
"smode.segment.deleted"       → Segment removed
"smode.segment.modified"      → Segment resized or effects changed
"smode.segment.selected"      → User clicked a segment (shows effect strip)
"smode.midi.trigger"          → MIDI note triggered a segment
"smode.session.saved"         → Session saved to .sm5s
"smode.session.loaded"        → Session restored from .sm5s
```

### Dependency Rules

```
SModeService ──→ EventBus (publish/subscribe)
    │
    ├──→ SModeSeparationService ──→ FullSongService (reuse)
    ├──→ SModeSegmentModel (owns data, no UI knowledge)
    ├──→ SModeSessionSerializer ──→ SModeSegmentModel (read-only)
    └──→ SModeExporter ──→ SModeSegmentModel (read-only)

SModePage ──→ EventBus (subscribe to service events)
    │
    ├──→ StemRowComponent ──→ StemWaveformComponent ──→ SegmentOverlay
    ├──→ SegmentEffectStrip (shown on segment select)
    └──→ StemCountDialog (shown once at start)

UI NEVER imports services directly. Uses:
  - EventBus for communication
  - ServiceRegistry.get("smode") for data queries
```

---

## 7. Implementation Phases

### Phase 1: Pipeline & Navigation
- Add `SMode = 3` to `ProcessMode` enum
- Stem count dialog (4 vs 6)
- Modify `FullSongService` to retain all stems
- `SModeService` skeleton (holds stem buffers + segment state)
- Navigation: Initial Page → Dialog → Separation → SModePage
- Basic SModePage with stacked waveform placeholders

### Phase 2: Waveforms & Segmentation
- `StemWaveformComponent` with spectral coloring (reuse `drawSpectralWaveform`)
- Tempo grid overlay (beat/bar/2-bar lines)
- Click+Drag to create segments
- Drag edges to resize, right-click to delete
- Magnetic snap to grid points
- Auto-segmentation buttons (beat/bar/2-bar/transient)
- Solo/Mute per stem
- Segment preview on click

### Phase 3: MIDI Integration
- Auto-assign MIDI notes to segments (sequential C1→)
- MIDI note labels rendered on each segment
- MIDI triggering via `MidiMappingService` (reuse voice system)
- POLY/MONO + 1SH/GAT mode buttons (reuse existing)
- Stem linking (○ toggle) for combined triggering
- Trigger animation on segments
- Quick Assign button

### Phase 4: Polish & Export
- Export segments as WAV
- Undo/Redo for segment edits
- Waveform zoom/scroll
- Right-click context menus
- Keyboard shortcuts
- VST3 state save/restore for S-Mode

---

## 8. Decisions (Confirmed by Santi)

1. **Per-segment effects**: YES — pitch, filter, reverse per segment. Ship with v1.
2. **Priority**: All 4 phases ship together as one release.
3. **Stem mixing levels**: Adjustable volume per stem when linked.
4. **Save/Load sessions**: YES — save S-Mode state (segments + assignments + effects) to disk.
5. **Max segments per stem**: Unlimited. User maps as many MIDI notes as desired.

---

## 9. Per-Segment Effects

Each segment has its own effect controls, accessible via click-to-select or right-click menu:

| Effect | Range | Description |
|--------|-------|-------------|
| **Pitch** | -24 to +24 semitones | Pitch shift independent of tempo |
| **Filter** | LP/HP with cutoff knob | Low-pass or high-pass filter |
| **Reverse** | On/Off toggle | Play segment backwards |
| **Volume** | 0-100% | Per-segment gain |
| **Pan** | L-R | Stereo positioning |

### UI: Selected Segment Effect Strip

When a segment is selected (clicked), a compact effect strip appears below the stems:

```
┌──────────────────────────────────────────────────────────────────────────────┐
│  SEGMENT: VOCALS seg 3 (C#1)  │  1.2s - 2.4s                               │
│                                                                              │
│  Pitch: [-12──●──────+12]  Filter: [LP ●─────] Cut: [──●────]              │
│  Vol:   [────●──────100%]  Pan:    [L──●────R]  [REV]  [LOOP]              │
└──────────────────────────────────────────────────────────────────────────────┘
```

Effects are saved per-segment and restored with session load.

---

## 10. Session Save/Load

### File Format: `.sm5s` (SAMPLAR Session)

JSON-based session file containing:
```
{
  "version": "1.0",
  "sourceFile": "path/to/original.wav",
  "stemCount": 6,
  "tempo": 128.0,
  "stems": [
    {
      "name": "vocals",
      "segments": [
        {
          "startSample": 0,
          "endSample": 44100,
          "midiNote": 36,
          "effects": { "pitch": 0, "filter": "off", "reverse": false, "volume": 1.0, "pan": 0.0 }
        }
      ],
      "linked": true,
      "volume": 0.8
    }
  ]
}
```

### Workflow
- **Save**: File → Save Session (Ctrl+S) → writes `.sm5s` alongside the source audio
- **Load**: File → Load Session → re-separates if stems not cached, restores all segments + effects
- **Auto-save**: Optional auto-save on every segment change (debounced 2s)

### Stem Caching
Separated stems are cached in a temp folder (`{sourceFile}_stems/`) so reloading a session
doesn't require re-running separation. Cache is invalidated if source file changes (MD5 check).

---

*~55% existing code reuse. Per-segment effects and session save add ~500 lines
to the estimate. Total new code: ~2,500-3,000 lines.*
