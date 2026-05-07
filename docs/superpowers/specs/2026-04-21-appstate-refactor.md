# AppState Refactor — Centralized Plugin State

**Date:** 2026-04-21
**Trigger:** Crash from dangling AudioThumbnail clients + requests to persist sample-browser order and step-sequencer pattern. Current ad-hoc JSON pattern in `SamplarAudioProcessor::getStateInformation` is reaching its limit (3 sections hand-rolled, pending-fields on UIState, scattered listeners).

## Goals

1. **One source of truth**: a single `juce::ValueTree` owned by `SamplarAudioProcessor`.
2. **Feature-local state**: each feature (ui, smode, midi, sampleBrowser, stepSequencer, …) owns a sub-tree and registers a `StateBinding` with save/restore lambdas.
3. **No pending-fields struct**: restore hands the relevant sub-tree directly to the feature when it's ready.
4. **Backward compatible**: existing JSON projects (ui/smode/midi) still load. New saves use the new format.
5. **Auto-dirty**: any change to the tree calls `updateHostDisplay()` so DAW knows to save.

## Non-goals (this pass)

- Undo/redo. (ValueTree makes it easy later, but not now.)
- APVTS migration. (No parameter-like continuous values today.)
- Cross-feature event coupling changes. EventBus stays as-is.

## Design

### `samplar::AppState` (new, `src/app/AppState.h`/`.cpp`)

```cpp
namespace samplar {

struct StateBinding {
    juce::Identifier nodeId;                    // e.g. "ui", "sampleBrowser"
    int version { 1 };                          // for per-feature migration
    std::function<void(juce::ValueTree&)>           save;     // write into child tree
    std::function<void(const juce::ValueTree&)>     restore;  // read from child tree
};

class AppState {
public:
    AppState(juce::AudioProcessor& processor);

    // Registration — typically called from feature constructors.
    // Registering AFTER setStateInformation has run will immediately replay
    // the restore() with whatever child tree exists (buffered restore).
    void registerBinding(StateBinding binding);
    void unregisterBinding(const juce::Identifier& nodeId);

    // Called by SamplarAudioProcessor::getStateInformation/setStateInformation
    void serialize(juce::MemoryBlock& out);
    void deserialize(const void* data, int size);

    // Direct access for features that want to listen to their sub-tree.
    juce::ValueTree getOrCreateChild(const juce::Identifier& nodeId);

    // Marks the host dirty. Called automatically on any tree change.
    void markDirty();

private:
    juce::AudioProcessor& processor_;
    juce::ValueTree root_ { "SM5AppState" };    // root: version attr + children
    std::vector<StateBinding> bindings_;
    bool restorePending_ { false };             // true between deserialize and first restore drain

    struct RootListener : juce::ValueTree::Listener {
        AppState* owner;
        void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override { owner->markDirty(); }
        void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree&) override         { owner->markDirty(); }
        void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree&, int) override  { owner->markDirty(); }
        void valueTreeChildOrderChanged(juce::ValueTree&, int, int) override          { owner->markDirty(); }
        void valueTreeParentChanged(juce::ValueTree&) override                        {}
    } listener_ { this };
};

} // namespace samplar
```

### Serialization format

- **New format**: 4-byte magic `SMAS` + 2-byte version `0x0001` + `ValueTree::writeToStream()` binary blob.
- **Legacy JSON detection**: if first 4 bytes are `{` `"` or `{` with whitespace, call the legacy importer (imports ui/smode/midi into the new tree, then dispatches normally).
- **Why binary ValueTree?** Faster, smaller, no type coercion issues (bool vs int vs string), native JUCE round-trip.

### Registration flow

```
SamplarAudioProcessor ctor
    owns AppState
    passes &AppState to services that need to persist
    editor/components register their bindings when constructed

DAW: setStateInformation(blob)
    AppState::deserialize parses + stores tree
    AppState iterates currently-registered bindings:
       for each binding, if root_.hasChild(nodeId): binding.restore(child)
    Bindings registered LATER (after editor open) auto-drain on register
    Events on EventBus still fire for features that want to react

DAW: getStateInformation
    AppState iterates bindings:
        for each binding, call binding.save(root_.getOrCreateChild(nodeId))
    AppState serializes tree
```

### Migration plan (4 phases with build+test between each)

**Phase 1 — Core infra (no behavior change)**
- Add `src/app/AppState.h/.cpp`.
- Add `appState_` member to `SamplarAudioProcessor`.
- `getStateInformation` / `setStateInformation` become 3 lines: call `appState_.serialize/deserialize`.
- Legacy JSON reader: parse existing `{ "ui": …, "smode": …, "midi": … }` into the new tree for backward compat.
- **No bindings registered yet** — existing code still uses `getUIState()` + pending-fields path. This phase only proves the plumbing compiles and old projects still load.

**Phase 2 — Migrate existing sections**
- Register bindings: `"ui"`, `"smode"`, `"midi"` in SamplarAudioProcessor ctor.
- Move save logic from `getStateInformation` into each binding's `save()` lambda.
- Move restore logic from `setStateInformation` into each binding's `restore()` lambda.
- `UIState` pending-fields stay for now (they're consumed by UI that hasn't been migrated). The binding's restore() writes to them as a compatibility shim.
- Delete the hand-rolled JSON branches from `SamplarAudioProcessor::getStateInformation/setStateInformation`.

**Phase 3 — SampleBrowser persistence**
- Investigate tile-shuffle root cause (suspect: folder re-scan on restore).
- Register `"sampleBrowser"` binding in `SampleBrowserPage` constructor:
  - `save`: ordered list of file paths, batch index, expanded index, currently-playing file.
  - `restore`: populate ordered file list, skip re-scan (use saved order), jump to saved batch/expanded index.

**Phase 4 — StepSequencer persistence**
- Locate the single `StepSequencerComponent` in SampleBrowserPage.
- Register `"stepSequencer"` binding:
  - `save`: step pattern, tempo, swing, row?sample mapping, mute/solo, per-step velocity/params.
  - `restore`: repopulate pattern, rewire row sources.

## Ownership / lifetime

- `AppState` is a plain member of `SamplarAudioProcessor` ? dies with processor.
- Bindings hold raw lambdas capturing `this` of features. Features MUST call `appState_->unregisterBinding("name")` in their destructor. Ownership rule: *feature that registers, unregisters*. Crash-prevention pattern (see tonight's TimeSliceThread bug — we don't want repeats).

## Testing

Each phase:
1. Build (release).
2. Open FL Studio, load SAMPLAR, do representative actions.
3. Save project, close FL.
4. Reopen project — verify restored state matches saved state.
5. Check SAMPLAR_console.log for save/restore bracket logs on every binding.

## Open questions (decided)

- **Q: keep JSON legacy reader forever?** A: yes, with a flag in the log `[AppState] legacy JSON state imported`. Users' existing projects must load.
- **Q: APVTS now?** A: no, we have no parameter-like values; defer.
- **Q: Save-every-change or save-on-demand?** A: DAW decides. We just call `updateHostDisplay()` so the DAW knows project is dirty.
