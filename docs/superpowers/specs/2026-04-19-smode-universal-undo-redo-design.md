# S-Mode Universal Undo/Redo — Design

**Date:** 2026-04-19
**Owner:** Echo
**Status:** Approved (pending spec review)

## Goal

Make every destructive S-Mode action undoable and redoable, end-to-end, with solid UX feedback. Single Ctrl+Z reverses *anything* the user did to a session — segments, sequencer lanes, tempo, grid, FX, mute/solo, lane patterns — without surprises.

## Non-goals

- Selection state, transport state, and the DAW-sync toggle UI flag are *not* tracked. (BPM changes the toggle produces ARE tracked.)
- No undo across track loads — stacks clear on stem reload.
- No coalescing of rapid value changes (project has discrete clicks/drags only — confirmed with user).

## Existing State

`SModeSegmentModel` already has a snapshot stack (`undoStack_`, `redoStack_`, `kMaxUndoLevels=50`) with `pushUndoState() / undo() / redo()`. Currently snapshots only capture: per-stem `segments`, `linked`, `volume`. Called from 7 sites today. UI buttons (`undoBtn_`, `redoBtn_`) and Ctrl+Z/Y are already wired. We're *expanding*, not replacing.

## Architecture

Single snapshot stack on `SModeSegmentModel`. Every undoable action calls `pushUndoState(label)` *before* mutating; `undo()` swaps current → redo, restores top of undo stack; `redo()` reverses.

UI/lane state that lives outside the model (sequencer lane patterns, grid lock, grid division, loop flag) is captured via two callbacks the page registers with the model:

```cpp
using ExtraStateProvider  = std::function<juce::var(void)>;
using ExtraStateRestorer  = std::function<void(const juce::var&)>;
void setExtraStateHooks(ExtraStateProvider get, ExtraStateRestorer set);
```

The snapshot stack stores `(modelData, extraVar, label)` triples. Single source of truth for undo, no duplicate stacks, no hand-wiring N callbacks per consumer.

## Snapshot Contents

```cpp
struct SModeSnapshot {
    // Per-stem
    struct Stem {
        std::vector<Segment>          segments;
        std::vector<EffectSettings>   segmentFx;   // parallel to segments
        bool                          linked;
        bool                          muted;
        bool                          soloed;
        float                         volume;
    };
    std::vector<Stem> stems;

    // Global model state
    double detectedBpm;
    double sampleRate;

    // SModePage UI state (serialised through the extra-state hook)
    bool   gridLocked;
    double lockedBpm;
    int    gridDivision;
    bool   loopEnabled;

    // Sequencer lanes (one entry per lane)
    struct Lane {
        std::vector<PatternSlot> slots;
        PatternType              patternType;
        int                      numBars;
        int                      slotsPerBar;
        bool                     muted;
    };
    std::vector<Lane> lanes;

    // Metadata
    juce::String label;   // e.g. "BPM change", "Split segment"
};
```

## Action Coverage (the ✓ list)

Every site below pushes an undo entry **immediately before** the mutation. Each site calls `model.pushUndoState("<label>")`.

### `SModePage`
- `applyBpm` — `"BPM change"`
- Grid lock toggle / double / half — `"Grid lock"`, `"BPM ×2"`, `"BPM ÷2"`
- Grid division change — `"Grid division"`
- Quick-assign — `"Quick-assign"`
- Loop toggle — `"Loop toggle"`
- Per-segment FX edits — `"FX: <param>"`
- Stem mute / solo / volume / link — `"Stem <action>"`

### `StemWaveformComponent`
- Segment add / split / merge / delete / move / resize — `"Segment <action>"` (3 sites already exist; add the rest)

### `SequencerLaneComponent`
- Slot assign — `"Slot assign"`
- Shuffle / mutate / slice / clear — `"Lane <action>"`
- Pattern type / bars / subdivision change — `"Lane <param>"`
- Lane mute / remove — `"Lane <action>"`

**Drag handling:** capture at the start of the gesture (mouseDown / drag-start callback), *before* any mutation. Releases without a real change are absorbed by the 50-deep cap; optional optimisation: drop the head if equal to current state.

## Re-entrancy

`pushUndoState` is a no-op while `restoringSnapshot_ == true`, so the act of restoring a snapshot doesn't pollute the stack with downstream side-effect mutations.

## UX Wiring

| Concern | Mechanism |
|---|---|
| Button enabled/disabled | `undoBtn_.setEnabled(model.canUndo())` etc., re-evaluated via model callback `onUndoStackChanged` (fired on push / undo / redo / clear) |
| Tooltip preview | `"Undo: <label> (Ctrl+Z)"` from top-of-stack label; `"Nothing to undo"` when empty |
| Visual confirmation | Brief 1.2s status flash next to the buttons (e.g. `"Undid: BPM change"`) — uses existing tooltip overlay if available, else a small label widget |
| Keyboard | Ctrl+Z (already wired), Ctrl+Y and Ctrl+Shift+Z both bound to redo |
| Event bus | `bus.publish("smode.undo.applied", {label})` and `"smode.redo.applied"` — peripheral components can react without hand-wiring |
| Track load reset | `model.clearUndoHistory()` called on `smode.workflow.ready` |
| During playback | Allowed — undoing during playback simply changes what plays next loop |

## Memory Budget

~20–50 KB per snapshot (segments, FX, and patterns are small POD-like structs; no audio buffers). 50-deep cap → ≤ 2.5 MB worst case. Negligible.

## Logging

Every push, undo, and redo emits one line:
```
[SMode-Undo] push: "BPM change"  (undo=12 redo=0)
[SMode-Undo] undo applied: "BPM change"  (undo=11 redo=1)
[SMode-Undo] redo applied: "BPM change"  (undo=12 redo=0)
[SMode-Undo] history cleared (track loaded)
```
Lets us answer "why is there nothing to undo?" with a log search.

## Files Touched

- `src/services/smode/SModeSegmentModel.h` — expand snapshot struct, add label, hooks, callbacks, `clearUndoHistory()`, `canUndo()/canRedo()`, peek-label
- `src/services/smode/SModeSegmentModel.cpp` — capture/restore implementations
- `src/ui/Pages/SModePage.cpp` / `.h` — register extra-state hooks; add `pushUndoState(label)` calls at every action site listed above; wire enabled-state, tooltip, confirmation flash, event bus broadcast, keyboard
- `src/ui/Components/smode/SequencerLaneComponent.cpp` / `.h` — `pushUndoState()` at slot/pattern action sites (model is already injected)
- `src/ui/Components/smode/StemWaveformComponent.cpp` — fill in remaining segment-action sites

## Risks & Mitigations

| Risk | Mitigation |
|---|---|
| Forgetting to instrument a site | Linter-style grep check post-implementation: every model-mutating method should be preceded by `pushUndoState`. Logging makes runtime gaps visible. |
| Snapshot grows unbounded if user opens huge sessions | Cap at 50 entries (already enforced). Each entry size logged in debug build. |
| `juce::var` extra-state grows fragile if lane schema changes | Version field in extra-state JSON: `{"v":1, …}`. Restorer ignores unknown versions. |
| Restore re-fires UI side effects (e.g. toggle handlers) → infinite loop | Re-entrancy guard `restoringSnapshot_` checked in every `pushUndoState`. UI setters use `dontSendNotification` during restore. |
| User confused by tracking some actions but not others (selection, transport) | Tooltip and confirmation flash always show the *label*, so user sees what was undone. |

## Open Questions

None — all resolved during brainstorm.
