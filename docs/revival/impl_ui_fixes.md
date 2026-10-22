# SAMPLAR UI fixes - implementation report (2026-10-21)

Build: `build_ui/` (VS 2022 x64 Release). Targets SamplarPlugin_VST3, VST3SmokeHost, EngineTests: GREEN. Nothing committed.
No edits to src/plugin, src/app, SModeService, SModeSegmentModel, MainWindow, CMakeLists.txt, separation, analysis, licensing.

## Verification
- `VST3SmokeHost --show-ms 2500`: ALL PASS. `--two-instances` x3 (final build): ALL PASS, 3/3.
  (One earlier run had "B delete instance 6.1 s" with 17 compiler processes from other agents running; it did not reproduce.)
- `--ui-sweep`: 18/18 sweeps fail=0, plus new page checks on every sweep. Logs: `scratchpad/ui_sweep.log`, PNG + txt: `scratchpad/sweep_ui/`.
  I looked at: loaded1_850x500.png, loaded1_1060x724.png, loaded4bottom_1810x1130.png, ejected_850x500.png.
- EngineTests (Release): 28/28 PASS, including 7 new checks. Output: `scratchpad/ui_enginetests.txt`. I did not run Debug EngineTests this round.
- Self-test extension (`SModeUiSelfTest.h`, `Callbacks::extraChecks`; each returned line is a FAIL):
  - Key label: visible, at least 40x16, inside the page, overlaps no sibling, and its text fits.
  - After load: LINK ALL off and no stem linked.
  - Chop inspector: laid out with a target after load, hidden in the empty state.
  - The synthetic load now makes 4 chops per stem, assigns each stem's bank and selects Vocals #2, so the inspector's enabled sliders/buttons go through the hit test at all 3 sizes.

## Finding -> fix -> evidence
| ID | Fix | Evidence |
|---|---|---|
| B16 | LINK ALL now defaults to OFF and mirrors the model. Edits only copy to stems the user linked. A click plays that stem's chop directly (not through its note). Linked stems layer by **time**: click, MIDI learn and delete all use the chop at the same time position, never the same index. | page check "LINK ALL is ON after a fresh load" never fired |
| B17 | LOCK is now audible. New `EngineParams::globalSemis` is added to every voice, including exports. `MidiMappingService::setGlobalTransposeSemitones`. The button shows "LK +n". It no longer writes into segment fx. | EngineTests "Global LOCK transpose +12 st": 879.2 Hz (source 440) |
| B18 | Lane grid is `cellsPerBeat` 1/2/4 (x8 = 8ths, x16 = 16ths). The engine clock runs in 16th ticks. `LaneDesc/LaneSnap::cellsPerBeat` added. Slots rescale when the subdivision changes. Old states (4 cells/bar) rescale on restore. Slot `speed` is now saved. | EngineTests "x16 lane: 16 steps, 6000 samples apart": PASS |
| B19 | Grid drag is Ctrl without Shift, so Ctrl+Shift reaches multi drag-out. Grid drag pushes one "Move grid" undo, and the offset is global (model plus all lanes, and in the undo state). | code |
| B20 | Auto-chop progress shows on the visible button ("Analyzing...", disabled). A busy flag blocks a second run. The result is dropped if the chops were edited or undone during analysis. | code |
| B21 | Quick Assign never deletes. Chops are matched by sample range, not index. Each stem gets its own 16-note bank: Vocals C1-D#2, Bass E2-G3, Drums G#3-B4, Melody C5-D#6. The menu shows the ranges. Grid quick-assign also uses the banks. QWERTY uses the Ableton layout (A..; = 17 notes from the base, default C1) and Z/X shift the octave with a range toast. Each held key remembers its own note. | PNG: badges C1..D#1 / E2.. / G#3.. / C5.. |
| B22 | One naming convention everywhere: JUCE default, 60 = C3 (`getMidiNoteName(n,true,true,3)`), used in lane badges, MidiMappingService, the overlay, tooltips and the inspector. | PNG badges |
| B23 | `removedStems_` is in the undo extra state, so undo shows the lane again. A toast reads "Ctrl+Z to restore". | code |
| B24 | `refreshAfterUndoRedo()` re-syncs the UI after undo/redo: LINK toggle and colour, LOCK text and colour, division to the lanes, loop to the engine, S/M colours, grid offset and the inspector. Junk undo entries are gone: BPM pushes only on a real change, host tempo never pushes, focus-lost pushes only if the value changed, "Create segment" pushes only on create, and resize pushes on the first move. | code |
| B25 | MONO/POLY is read from and written to this instance's engine; the static is gone. | code |
| B27 | Lanes no longer grab focus on hover; they grab it on click only. | code |
| B28 | Drop, chooser and recents over a loaded session ask "Replace current session?". A new song clears lanes, inspector, key and BPM. | code |
| B29 | Chops were already done. Pattern WAVs now go to `<userdata>/exports`, with unique timestamped names, 24-bit. | code |
| B30 | Drag-out starts only while the left button is still down. Otherwise the file is kept and a toast says so. | code |
| B31 | Verified: render lambdas read only atomics and the engine export snapshot. WaveDragArea's `sampleRate_` is now set on the message thread. | code |
| B33 | Spectral, BPM, auto-chop, Quick Assign and key detection use `model.getSharedStemAudio()`; no whole-stem copies. | code |
| B7 | Lane menus (header, segment, empty area) use `withTargetComponent` + `withDeletionCheck` + SafePointer. The segment menu re-finds the chop by range. Page menus use the same pattern. | code |
| B11 | `onStemsReady` no longer clears undo history (`model.clear()` on a new song already does). Link state and grid offset are restored from the model on reopen. | code |
| B12 | Page subscriptions are unique per load (it already re-subscribed). host.tempo now uses the page's own bus (U-H2). The remaining issue is in MainWindow (see below). | - |
| Auto-chop | "Auto Chop" (Smart) is the default: transients on Drums, phrases (spacing of at least 1 beat) on Vocals, bars on Bass and Melody. At most 16 chops per stem, the loudest from each of 16 time regions. The grid starts at the detected downbeat. When the downbeat arrives later, the bar chops re-run if untouched (no new undo). Beat/Bar/2-Bar/Transient remain on right-click. | code, logs "auto-chop stem N: ... candidates=X kept=Y" |
| Key | `KeyDetector` runs on Melody plus Bass (shared buffers) on the SModeService pool. The label reads e.g. "Am  8A"; the tooltip has the full name and confidence. It sits next to BPM at all 3 sizes (TopBarSpec.key 66/58/50). | PNGs show "E 12B" on synthetic sines; page check "fits" |
| Nits | The grid combo now shows "1/4" at start. The BPM field resets to "--" on Eject. | ejected_850x500.png |
| Inspector | New `ChopInspectorStrip` row (pitch, gain dB, pan, REV, trigger, ATK, REL) with one undo per gesture. The engine applies per-chop pitch, gain, pan, reverse, trigger, attack and release (fill via `src/audio/engine/SegmentFx.h`) to MIDI notes, audition, sequencer slots and exports. A fx hash triggers snapshot rebuilds. | EngineTests: gain 0.5 + hard-left + reverse maxErr 0; attack 10 ms ramp PASS |

## Review round 1 items
1. **A1 (HIGH): fixed.** A mid-block switch (`switchSnapshotMidBlock`) no longer raises ackSeq. Tests: "A1: mid-block switch keeps the older snapshot alive" (freedDuringBlock=0, retired=1) and "freed once no voice uses it": PASS.
2. **A2 / U-H2 (HIGH): fixed in my files.**
   - SModePage, the MIDI-learn overlay, sequencer lanes and slots capture this instance's registry and AppState once, at construction. host.tempo uses the page's own bus.
   - No `ServiceLocator::get*` calls remain in action paths.
   - When `ServiceLocator::Context` lands, it can replace the two capture lines at the top of the SModePage constructor and the overlay constructor.
3. **U-H4 (HIGH): fixed.** The visible button has a busy flag, and stale results are dropped.
4. **A3 (MEDIUM): fixed.**
   - Commands carry a time stamp; NoteOn/PlaySegment/PlaySlot older than 150 ms are dropped, while stops, note-offs and transport commands always apply.
   - The UI keeps 64 FIFO slots free so note-offs always get in.
   - The live engine releases MIDI voices after a processing gap longer than 500 ms.
   - Test "A3: stale trigger dropped, fresh plays": PASS.
5. **U-M5 / U-M6 / U-M7 / U-M8 (MEDIUM): fixed.**
   - Quick Assign matches chops by range.
   - A BPM result is dropped if the BPM was edited meanwhile (edit counter).
   - Menus use SafePointer.
   - Buttons and dots reset on every early return.

## Needed from others
- **Model (persistence agent):** add to `SegmentEffects`, persist them and include them in undo (they are already copied with Segment):
  - `float attackMs {0}; float releaseMs {0}; int triggerMode {0}; // 0 follow global, 1 one-shot, 2 gate`
  - `SegmentFx.h` detects these fields at compile time. Until they exist, the ATK/REL/trigger controls are shown disabled with an explanatory tooltip; the engine side is done and tested.
  - Optionally add effects to `computePlaybackHash` (I hash them separately in MidiMappingService, so this is not required).
- **MainWindow owner (B11/B12):**
  - Drop the synchronous `onStemsReady` at MainWindow.cpp:215 (the deferred one at :409 is enough).
  - Zero `smodeWorkflowReadySub_` after the self-unsubscribe.
  - The restore paths (:630/:654) overwrite the id without unsubscribing first.
- **ServiceLocator::Context:** see review item 2 above.
