# Serato Sample - Feature Research Summary

**Date:** 2026-03-22
**Purpose:** Competitive analysis for SAMPLAR

---

## 1. Core Features & Workflow

Serato Sample is a VST3/AU/AAX sampler plugin focused on **speed-first sample chopping**. The entire workflow happens in a single window: Load audio → auto-detect tempo/key → sync to DAW BPM → chop → flip → export as MIDI.

**Three-step workflow:** Sync It → Stem & Chop → Flip It

- Drag-and-drop audio loading (any format)
- Automatic BPM detection and time-stretch to host tempo
- Automatic key detection
- Everything saved with DAW session (all cue points, parameters, stems settings)
- Records pad triggers as MIDI clip in DAW for editing/quantizing

---

## 2. Audio Slicing / Chopping

**Four autoset modes for finding slices:**

| Mode | Behavior |
|------|----------|
| **Find Samples** | Algorithmic - analyzes dynamic range and transients to find the best 16 cue points. Prioritizes regions with clear transients and interesting harmonic content |
| **Set Random** | Places cue points at randomly selected transients |
| **Set Slicer** | Creates up to 32 consecutive cue points at regular beat/bar intervals from playhead position. Interval length adjustable with arrows. Similar to Serato DJ's Slicer |
| **Key Shift Pad** | Takes one selected cue point and pitch-shifts it 8 semitones up and 8 down across the 16 pads (chromatic instrument from single slice) |

**Manual slicing:**
- Click anywhere on waveform to set a cue point
- Zoom in/out on waveform for precision
- "Freeze" button locks display to selected cue point for fine adjustment
- Arrow bars to set the range/length of each cue
- Up to 32 cue points total

**Favorite/Trash workflow:** Audition pads → favorite keepers → trash rejects → re-run Find Samples to fill empty slots. Iterative refinement.

---

## 3. MIDI Key Assignment

- **32 pads** mapped to MIDI notes (base at C1 for pad controllers, C3 for keyboard)
- Pads are **color-coded** matching waveform cue point colors
- Triggerable via: computer keyboard (QWERTY in Ableton), MIDI hardware, DAW piano roll
- **Keyboard Mode:** Select any single cue point → toggle keyboard icon → that slice is pitched chromatically across all MIDI notes (base sample at C3). Other cue points remain unchanged
- Playback recorded as standard MIDI clip in DAW

---

## 4. Tempo Detection & Auto-Slicing

- **Automatic BPM detection** on file load - analyzes audio and identifies tempo
- **Auto-sync** to host DAW tempo via Pitch 'n Time algorithm (tempo change without pitch change)
- Key is also auto-detected and displayed
- Global key shift available (±24 semitones) without affecting tempo
- Individual pad key shift also available (±24 semitones)
- Time stretch range: -75% to +300%

---

## 5. Stem Separation (v2.0+)

- **Real-time stem isolation** into 4 stems: Vocals, Melody, Bass, Drums
- Toggle each stem on/off independently
- Fine-tune individual stem levels (not just on/off)
- Can automate stem levels in DAW
- Analysis is quick and seamless on load
- Stems apply to the entire loaded audio file (all cue points affected)
- Praised as "truer to source content" than competitors across genres

---

## 6. Unique UI/UX Patterns

**Single-window design:** Everything visible at once, no tabs/pages/hidden menus.

**Colored waveform:** Frequency-spectrum coloring (low=red, mid=green, high=blue) for visual identification of audio content. Helps identify kicks, hats, vocals by color.

**Pad grid (bottom):** 16 or 32 color-coded pads with visual feedback on trigger. Each pad color matches its cue point marker on the waveform.

**Per-pad parameters panel:** When a pad is selected, adjust:
- Level (volume)
- Filter (resonant lowpass)
- Attack
- Release
- Key Shift (±24 semitones)
- Time Stretch (-75% to +300%)
- Reverse toggle

**"Select All" button:** Apply parameter changes to all pads at once.

**Freeze button:** Locks waveform display to selected cue point (prevents jumping around while adjusting).

**Playback modes (global):**
- Hold mode: plays while key held
- Trigger mode: plays full cue on tap
- Mono mode: one voice at a time (cuts previous)
- Poly mode: up to 16 simultaneous voices

**SLAB hardware controller:** Dedicated Serato MIDI pad controller with native integration.

---

## 7. Manual vs Automatic Segmentation

### Automatic
1. **Find Samples** - AI/algorithm picks best 16 points based on transients + dynamic range
2. **Set Random** - Random transient-based placement
3. **Set Slicer** - Evenly spaced at beat/bar intervals (adjustable length)
4. **Key Shift Pad** - Chromatic spread from single point

### Manual
- Click on waveform at desired position to create cue point
- Zoom waveform for sample-accurate placement
- Drag cue point arrows to adjust start/end range
- Freeze display for focused editing
- Delete unwanted cue points individually

### Hybrid Workflow (Key Differentiator)
The **Favorite + Trash** system enables iterative hybrid workflow:
1. Run auto-detection (Find Samples)
2. Audition all 16 pads
3. **Favorite** (lock) the good ones
4. **Trash** the bad ones
5. Re-run Find Samples → only fills unfavorited slots
6. Repeat until all 16 pads are keepers

This is Serato Sample's killer feature - it turns sample hunting from a chore into rapid iteration.

---

## Key Limitations

- **No looping** (cue points play linearly, no loop points)
- **No built-in effects** beyond filter (relies on DAW effects chain)
- **No waveform editing** (no cut, copy, paste, normalize)
- **No sample layering** (one audio file per instance)
- **Easy to create accidental cue points** (clicking waveform)
- **Single file per instance** (need multiple instances for multiple samples)
- Relatively basic compared to full-featured samplers (NI Kontakt, etc.)

---

## Competitive Positioning vs SAMPLAR

| Feature | Serato Sample | SAMPLAR |
|---------|--------------|-----------------|
| Auto-slicing | Find Samples algorithm | Drum slice service |
| Stem separation | Built-in real-time 4-stem | Via demucs (6-stem) |
| MIDI mapping | 32 pads, keyboard mode | MIDI mapping service |
| Variation generation | None | 8 variation engines |
| Time stretch | Pitch 'n Time (legendary) | SoundTouch |
| Per-pad parameters | Yes (6 params) | TBD |
| Multiple files | One per instance | Multiple samples |
| Workflow speed | Extremely fast | Feature-rich |

---

## Sources

- [Serato Sample Official](https://serato.com/sample)
- [MusicRadar Review](https://www.musicradar.com/reviews/serato-sample-2-review)
- [SampleHunt Ultimate Review](https://samplehunt.com/serato-sample/)
- [DJ TechTools Review](https://djtechtools.com/2017/07/31/review-serato-sample-auvst-plug/)
- [Sweetwater Product Page](https://www.sweetwater.com/store/detail/SeratoSampleV2--serato-sample-2.0-plug-in-with-stem-separation)
