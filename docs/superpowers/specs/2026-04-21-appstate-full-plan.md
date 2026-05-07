# AppState Full Implementation Plan

**Date:** 2026-04-21
**Owner:** Echo
**Status:** Phase 1 in progress

## Purpose

Replace ad-hoc JSON blob persistence in `SamplarAudioProcessor::getStateInformation/setStateInformation` with a centralized `juce::ValueTree`-based AppState, covering every page and feature in SAMPLAR. Deliver incrementally across releases so each phase is shippable.

## Architecture summary

- One `juce::ValueTree` root named `"SM5AppState"` with `version` attribute, owned by `SamplarAudioProcessor`.
- Features register a `StateBinding { nodeId, version, save, restore }` (preferably via RAII `ScopedStateBinding`).
- `SamplarAudioProcessor::getStateInformation` ? `AppState::serialize`. `setStateInformation` ? `AppState::deserialize`.
- **Late registration drains**: if a feature registers after `deserialize` already ran, its `restore` is invoked immediately with the buffered child tree. Eliminates the pending-fields anti-pattern.
- **Auto-dirty**: root-level `ValueTree::Listener` calls `processor.updateHostDisplay()` on any tree change.
- **Backward compatible**: legacy JSON blob is detected and imported into the new tree before dispatch.

## Format

```
magic    : 4 bytes    "SMAS"
version  : 2 bytes LE  0x0001
payload  : ValueTree::writeToStream(...)
```

Detection on load:
- First byte `'{'` ? legacy JSON path (import into new tree, then dispatch).
- First 4 bytes `"SMAS"` ? new binary path.

## API (final shape)

```cpp
// src/app/AppState.h
namespace samplar {

struct StateBinding {
    juce::Identifier nodeId;
    int version { 1 };
    std::function<void(juce::ValueTree&)>       save;
    std::function<void(const juce::ValueTree&)> restore;
};

class AppState {
public:
    explicit AppState(juce::AudioProcessor& processor);
    ~AppState();

    // Register a binding. If state has already been deserialized (late open of editor),
    // restore() fires immediately with the buffered child tree (if present).
    // Returns a token used for unregistering (or use ScopedStateBinding RAII).
    int registerBinding(StateBinding binding);
    void unregisterBinding(int token);

    void serialize(juce::MemoryBlock& out);
    void deserialize(const void* data, int size);

    juce::ValueTree getOrCreateChild(const juce::Identifier& nodeId);
    void markDirty();

private:
    juce::AudioProcessor& processor_;
    juce::ValueTree root_;
    struct Entry { int token; StateBinding binding; };
    std::vector<Entry> bindings_;
    int nextToken_ { 1 };
    bool restoreReady_ { false };          // true after first deserialize OR empty root created

    struct RootListener : juce::ValueTree::Listener { /* forwards to markDirty */ };
    RootListener listener_ { this };
};

// RAII helper — registers on ctor, unregisters on dtor.
class ScopedStateBinding {
public:
    ScopedStateBinding(AppState& s, StateBinding b) : state_(s), token_(s.registerBinding(std::move(b))) {}
    ~ScopedStateBinding() { state_.unregisterBinding(token_); }
    ScopedStateBinding(const ScopedStateBinding&) = delete;
    ScopedStateBinding& operator=(const ScopedStateBinding&) = delete;
private:
    AppState& state_;
    int token_;
};

} // namespace samplar
```

**Ownership rule:** every feature that registers MUST unregister in its destructor. Use `ScopedStateBinding` by default.

## Phase 1 — Core infrastructure (no behavior change)

**Goal:** Plumbing exists, compiles, old projects still load. No binding migrated yet — `getStateInformation/setStateInformation` still use the existing hand-rolled JSON path in parallel.

**Files:**
- NEW: `src/app/AppState.h`
- NEW: `src/app/AppState.cpp`
- MOD: `src/plugin/SamplarAudioProcessor.h` — add `std::unique_ptr<AppState> appState_`
- MOD: `src/plugin/SamplarAudioProcessor.cpp` — construct AppState in ctor
- MOD: `CMakeLists.txt` — add AppState.cpp

**Tasks:**
1. Implement AppState with:
   - `registerBinding` / `unregisterBinding` (token-based)
   - `serialize` / `deserialize` (binary magic + ValueTree stream)
   - Legacy JSON detection: if first byte is `'{'`, parse JSON, import `ui`/`smode`/`midi` fields into `root_` as child trees.
   - Late-drain logic: if `restoreReady_` is true at `registerBinding` time and a child exists, call `restore` immediately.
   - RootListener ? `processor_.updateHostDisplay()` on any change.
2. Wire into SamplarAudioProcessor ctor.
3. Add `ScopedStateBinding` RAII helper.
4. **DO NOT** change `getStateInformation/setStateInformation` yet — they still use the existing JSON code. AppState is unused this phase; this is pure plumbing.

**Build + verify:** plugin loads, existing FL project still restores. No regressions.

**Done criteria:**
- Plugin builds
- Plugin loads in FL Studio
- Existing project with saved UI/SMode/MIDI state still restores correctly
- `SAMPLAR_console.log` shows no AppState errors

## Phase 2 — Migrate existing sections to AppState

**Goal:** Replace the hand-rolled JSON branches in `getStateInformation/setStateInformation` with three `AppState` bindings: `ui`, `smode`, `midi`. Delete the old JSON-writing code. Legacy JSON reader stays for back-compat.

**Files:**
- MOD: `src/plugin/SamplarAudioProcessor.cpp`:
  - `getStateInformation` ? `appState_->serialize(destData)`
  - `setStateInformation` ? `appState_->deserialize(data, size)`
  - Add three `ScopedStateBinding`s in ctor for `ui`, `smode`, `midi` (or keep them as raw members if owned by the processor).
- MOD: `src/plugin/SamplarAudioProcessor.h` — members for bindings.

**Bindings:**

### `ui` binding
- Node: `<ui loadedFolder="..." loadedFiles="..." libraryMode="1" libraryFolder="..." currentPage="3"/>`
- Save: read `UIState` live fields ? tree properties.
- Restore: tree properties ? `UIState.pending*` fields (keeps existing UI consumers happy until Phase 5+ migrates them).

### `smode` binding
- Node: `<smode sourceFile="..." stemCount="4" bpm="120" sampleRate="44100"><stems><stem file="..."/>...</stems></smode>`
- Save: mirror current JSON logic.
- Restore: write to `UIState.pendingSModeRestore` + related fields.

### `midi` binding
- Node: `<midi><slot note="60" file="..." sampleRate="44100" locked="0"/>...</midi>`
- Save: same as today.
- Restore: `midiMapping_->restoreSlot(...)` per entry (currently inlined in setStateInformation).

**Build + verify:** same project loads identically, saves in new format, reloads correctly. Legacy JSON still imports.

**Done criteria:**
- New saves use SMAS binary format
- Loading a project saved in old JSON format still works
- All three sections restore correctly

## Phase 3 — SampleBrowser persistence (fixes tile shuffle)

**Goal:** Tile order stable across reloads. Persist the ordered file list, batch index, expanded sample index, currently-playing file, library mode toggles.

**Investigation (before implementation):**
- Find where tiles are populated (`SampleBrowserPage::rebuildGrid` / `loadFolder`).
- Identify whether order comes from folder scan (unstable) or a saved list (stable).
- Confirm why current `loadedFiles` field doesn't preserve order.

**Data shape:**
```xml
<sampleBrowser batchIndex="0" expandedSample="-1" currentlyPlaying="">
    <files>
        <file path="C:/samples/a.wav"/>
        <file path="C:/samples/b.wav"/>
        ...
    </files>
    <libraryMode>0</libraryMode>
    <libraryFolder>...</libraryFolder>
</sampleBrowser>
```

**Save:** iterate `tiles_` in order, emit file paths in current on-screen order. Record batch index, expansion index, playing file.

**Restore:** set files list in exact order (bypass folder re-scan), apply batch/expansion/playing.

**Files:**
- MOD: `src/ui/Pages/SampleBrowserPage.cpp/.h` — add binding via `ScopedStateBinding` member, implement `loadFilesInOrder(const juce::Array<juce::File>&)` helper that bypasses directory enumeration.

**Done criteria:** close FL, reopen, tiles in identical order, same batch page showing, same sample expanded, same sample "playing indicator" state (or cleared if ephemeral — user decision).

**Open question (decide before Phase 3):** does currently-playing indicator need to persist, or reset on reload? ? Recommend reset (playback isn't resumed anyway).

## Phase 4 — StepSequencer persistence

**Goal:** Full pattern + settings restored.

**Investigation:**
- Check `src/ui/Components/StepSequencerComponent.{h,cpp}` for current internal state.
- Enumerate: pattern (rows × steps × bool/velocity), tempo, swing, row?sample file mapping, mute/solo per row, per-step velocity/probability, pattern length.

**Data shape:**
```xml
<stepSequencer tempo="120" swing="0.5" length="16">
    <rows>
        <row index="0" file="C:/samples/kick.wav" muted="0" solo="0">
            <steps>
                <step index="0" on="1" velocity="1.0"/>
                ...
            </steps>
        </row>
        ...
    </rows>
</stepSequencer>
```

**Files:**
- MOD: `src/ui/Components/StepSequencerComponent.cpp/.h` — binding.

**Done criteria:** edit pattern, close, reopen, pattern identical + playback works.

## Phase 5 — WavePreviewPage persistence

**Goal:** restore loaded sample, zoom, beat grid settings, thresholds, processing enables.

**Scope:**
- `loadFile` path (absolute path of the sample being previewed).
- Zoom controller state (start time, end time, level).
- Beat grid controls (manual BPM override, offset, show-grid toggle).
- Threshold line position (for slicing).
- Step 2.5 controls (whatever toggles/sliders live there).
- Processing-enabled flags per file (currently in UIState — fold into binding).

**Data shape:**
```xml
<wavePreview file="..." visibleStart="0.0" visibleEnd="5.0" zoomLevel="1.0"
             bpmOverride="120" gridOffset="0.0" showGrid="1"
             threshold="-24.0" step25Foo="...">
    <processingEnabled>
        <file path="..."><flag name="trim" enabled="1"/>...</file>
    </processingEnabled>
</wavePreview>
```

**Files:**
- MOD: `src/ui/Pages/WavePreviewPage.cpp/.h`

**Risk:** large surface. Probably split into sub-bindings (`wavePreview.view`, `wavePreview.processing`) for easier testing.

## Phase 6 — ProcessingPage + ProcessingCoordinator

**Goal:** preset selection, enabled processing stages, their configs.

**Data shape:**
```xml
<processing presetName="Default">
    <stages>
        <stage id="trim" enabled="1"><param name="threshold" value="-24"/></stage>
        ...
    </stages>
</processing>
```

**Files:**
- MOD: `src/services/ProcessingCoordinatorService.cpp/.h` — binding owned by the service.
- MOD: `src/ui/Pages/ProcessingPage.cpp/.h` — view state only (selected preset, UI toggles).

## Phase 7 — SMode full, FullSong, Variation

### SMode extensions
Current persistence covers source + stems + bpm. Extend to:
- Section edits / boundaries (user-drawn section markers)
- Stem mute/solo state
- Selected section for playback
- Segment model (already partially in `smodeSegmentData`)

### FullSong
If a full song is loaded: source, stems, detected tempo, sections, sample-rate. Similar to SMode.

### Variation
- Mutation rate, strength, preserve ADSR, default engine.
- Currently global in `VariationService` (saved to disk prefs). Decide: stay global, or per-project override? ? Recommend per-project override when present, fall back to global.

**Files:**
- MOD: `src/services/smode/SModeService.cpp/.h` — extend existing save
- MOD: `src/services/FullSongService.cpp/.h` — new binding
- MOD: `src/services/VariationService.cpp/.h` — new binding

## Phase 8 — Remaining pages + polish

- **InitialPage**: last-used folder, library vs folder mode (probably already covered by `ui` binding).
- **EngineLabPage**: selected engine, lab parameters.
- **MentorPage**: current section/step (or keep cross-session in prefs — decision).
- **ChatPage**: conversation history (decision: per-project or cross-session?).
- **Any overlays** (BeatGrid, ThresholdLine, Zoom): settled by Phase 5 if they're WavePreview children.

**Integration checklist update:** add "AppState binding registered + unregistered" to the integration-checklists skill so every new feature going forward covers this.

## Testing strategy (every phase)

1. Build Release.
2. Copy VST3 + .map to `C:\Program Files\Common Files\VST3\SAMPLAR.vst3\...`.
3. Open FL Studio, load SAMPLAR, perform actions representative of the phase.
4. Save FL project, close FL.
5. Reopen FL project. Verify every item saved is restored.
6. Tail `SAMPLAR_console.log` for binding save/restore bracket logs:
   ```
   [AppState] === serialize ===
   [AppState]   binding 'ui' save ok
   [AppState]   binding 'smode' save ok
   ...
   [AppState] === deserialize ===
   [AppState]   binding 'ui' restore ok
   ...
   ```
7. Also test legacy import: load a project saved with the old JSON format ? should restore via legacy reader.

## Rollback plan

Phase 1 adds infra with no behavior change — rollback = delete AppState.cpp/.h and the ctor line. Phase 2+ change behavior; rollback = revert that commit. Each phase lives on a separate commit.

## Risk register

| Risk | Mitigation |
|---|---|
| Binding captures dangling `this` after feature destroyed | Mandatory `ScopedStateBinding` RAII; destructor unregisters first. |
| Restore-order dependencies (feature A needs feature B restored first) | Register bindings in construction order; late-drain handles out-of-order editor opens. Document explicit dependencies. |
| Legacy JSON project can't load | Keep legacy importer indefinitely; log `[AppState] legacy JSON state imported`. |
| Sample file paths moved on disk after save | Log missing files; skip entry and continue (same behavior as today's MIDI restore). |
| Tree bloat from deep per-step sequencer nodes | ValueTree streaming is fine at 1000s of nodes; profile only if > 10 MB state. |

## Estimated timeline

| Phase | Effort | Ship gate |
|---|---|---|
| 1 | 2-3 h | build green, no regressions |
| 2 | 2 h | existing features work through new plumbing |
| 3 | 2-3 h | tile shuffle fixed |
| 4 | 2-3 h | step seq persists |
| 5 | 3-4 h | wave preview persists |
| 6 | 2-3 h | processing persists |
| 7 | 3-4 h | smode/fullsong/variation persist |
| 8 | 2-4 h | remaining pages + checklist update |

**Total ~20-26 h of focused work.** Can release after Phase 4 with tangible user wins (tile order fixed, step seq persists, existing features still work).

## Conventions

- Every binding logs on save/restore: `[AppState::<nodeId>] save N bytes` / `restore ok|skipped|failed reason`.
- Each binding has `version` attribute in its child tree root; restore checks version and falls back/migrates on mismatch.
- No raw pointers captured in lambdas; use JUCE `SafePointer` for Component captures or capture stable service pointers (services outlive bindings they don't own).
