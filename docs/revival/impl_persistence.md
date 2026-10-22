# SAMPLAR persistence workstream - implementation report (2026-10-21)

Build: `build_persist/` only (Release). Nothing committed. `build/` untouched.

## API FOR OTHER AGENTS

### ServiceLocator::Context (src/app/ServiceLocator.h) - LANDED
```cpp
struct ServiceLocator::Context {
    ServiceRegistry* registry; EventBus* bus; UIState* uiState; AppState* appState;
    ServiceRegistry* getRegistry() const;   // captured slot, falls back to the live global if null
    EventBus*        getBus() const;
    UIState*         getUIState() const;
    AppState*        getAppState() const;
};
static ServiceLocator::Context ServiceLocator::capture();   // message thread
```
RootComponent (MainWindow.cpp) captures it once (`ctx_`, the editor claims the locator for its own
instance first) and every former `ServiceLocator::getRegistry/getBus/getUIState()` in MainWindow.cpp
now goes through `ctx_`. The page is built by
`makeSModePage<SModePage>(ctx_)`: if `SModePage(EventBus&, const ServiceLocator::Context&)` exists it is
used automatically (`std::is_constructible_v`), else `SModePage(EventBus&)`. So the UI swap is: add that
constructor, store `ctx_`, use it instead of `ServiceLocator::getX()` (incl. ctor bindings, host.tempo
subscribe/unsubscribe, midiService()). No MainWindow edit needed.

### SModeService (src/services/smode/SModeService.h)
- `const std::atomic<bool>* getJobCancelFlag()` - set first thing in shutdown(), before the pool drain.
  SModePage's BPM job must pass `AnalysisCancel(smodeService_->getJobCancelFlag())` to HybridTempoDetector.
- `SessionStatus getSessionStatus()` (Empty / Restoring / Active / MissingAudio), `getSessionMessage()`,
  `isLoadingStems()`, `getStemFiles()`, `createSessionTree()`, `restoreSession(tree)`.
- Events: `smode.session.restoring` (stemCount), `smode.session.missing` (message),
  plus `smode.workflow.ready` now also fires after a project restore.
### SModeSegmentModel
- `SegmentEffects::attackMs / releaseMs (float ms) / triggerMode (0 global, 1 one-shot, 2 gate)`, default 0;
  in undo snapshots (whole Segment is copied), the session, the hashes. The engine/inspector pick them up via SegmentFx.h.
- `setPendingExtraState(var)`: restored page UI state; handed to the page inside `setExtraStateHooks()`
  (start of onStemsReady), pushUndo suppressed while the page restores it.
### SModeSeparationService
- `int getProgressPercent()` (B10 replay).

## Finding -> fix
| ID | Fix |
|---|---|
| A4/B8 | New `src/services/smode/SModeSession.{h,cpp}`: versioned `smode` node (`schema=1`): `<session>` (song path + size/mtime, stem-cache key, stem files, rate, BPM, grid offset, page UI JSON = lanes/slots/bars/subdiv/pattern/grid division/grid lock/loop/link-all) with `<stem>` (link/solo/mute/volume) and `<seg>` (start/end/note/pitch/vol/pan/cutoff/hp/rev/loop/atk/rel/trig); `<play>` (mono, gate, loopGate, transport loop, per-stem pitch + mod routing + velocity curve). Tolerant reader (defaults, clamps, rate rescale, unknown nodes ignored, newer schema best-effort). Pre-schema projects load song+stems. The page's lane JSON is stored as text: the old `smodePage` binding stored DynamicObjects, which `ValueTree::writeToStream` writes as void (lanes were never saved) - that node is now "superseded" (AppState never restores or writes it). |
| A4 rule | `session::SessionState`: while a restore is pending / audio missing, every save writes the pending tree verbatim. AppState: while an off-thread setState's restore is not dispatched yet, serialize streams the loaded tree. |
| B9 | Restore runs in the processor: binding -> `SModeService::restoreSession()`: saved stems (persistent cache) -> decode; else re-separate the song (stem cache hit when possible); else MissingAudio (session kept; loading a file with the same name relinks it). MainWindow's old `state.restored` S-Mode branch and the UIState `smode*` fields are deleted. |
| A17 | Play settings + stem pitch/mod restored into MidiMappingService immediately (MIDI is right even before stems finish). |
| A18 | AppState: off-message-thread deserialize posts the binding restores (alive + sequence guarded); off-thread serialize never runs saves, streams the tree (CriticalSection around root_). Processor 2 Hz watcher keeps the `smode` node fresh. |
| A16 | `markDirty()` -> `updateHostDisplay(ChangeDetails().withNonParameterStateChanged(true))` (message thread hop), suppressed inside serialize/deserialize/snapshots. The watcher fingerprints the session (model incl. every seg field, status, files, page UI JSON, play settings) and marks dirty on change; a finished project restore re-baselines (loading does not dirty). |
| C15/A19 | `loadStemAudio()` decodes on the SModeService pool (chunked, stops on unload), publishes to the model on the message thread, generation-guarded. |
| B10 | MainWindow subscribes permanently (ready/error/progress/restoring/missing, unsubscribed from the same bus in the dtor) and replays `isSeparating()/isLoadingStems()/Restoring` + `getProgressPercent()` when the editor opens. |
| B11/B12 (lead) | One deferred onStemsReady on reopen (sync ctor call removed), skipped if the page already shows that model generation; no self-unsubscribing / overwritten handler ids remain. |
| review A2 | Context (above). The state restore no longer goes through any editor, so it always lands in the instance that received setStateInformation. |
| review A4/U-H1 | `jobsCancelled_` set before the pool drain; stem decode and DownbeatDetector (`AnalysisCancel(&jobsCancelled_)`) honour it. Shutdown logs its time. |

## Verification (all build_persist, Release)
- Build green: SamplarPlugin_VST3, VST3SmokeHost, SamplarTests (no new warnings in my files).
- SamplarTests: 13 classes, 486 checks, 0 failed. New: `SModeSession (project persistence)` (6 tests: full field round trip through a stream, hash covers every field, tolerant reader, play settings, pending-session save rule, UI-state hand-over) and `AppState persistence` (5 tests: off-thread setState deferral + save-before-dispatch keeps loaded data, off-thread getState runs no binding, newest of two setStates wins, superseded node, dirty flag suppression).
- `VST3SmokeHost --session-roundtrip tests/test_input.wav`: ALL PASS (39 PASS lines). A seeds 8 chops / mute / link / BPM 123.5 / grid 0.125 / GATE / stem pitch +3 / 2 lanes; B (editor never opened): save before restore keeps 8 segments, restore 113 ms, model+play description identical, 6 audible notes equal to A (RMS identical), muted-stem + unassigned notes silent; editor then shows lanes=2 slots=3 division=8, model unchanged; save after editor close keeps 8 segments + lanes. C (stem files gone): re-separation via stem cache, identical. D (song + stems gone): missing-audio reported, save keeps all 8 segments. First run (cold cache) separation took 33.7 s.
- Smoke `--show-ms 2500`: ALL PASS; `--two-instances` x3: ALL PASS (unload 48-70 ms, B 130-207 ms); `--ui-sweep`: ALL PASS.
- NOT measured: unload in the middle of BPM analysis on a 10-min song (needs the page's BPM job to pass the cancel flag - UI agent).

## Needed in files I do not own
1. SModePage: Context constructor (above); pass `getJobCancelFlag()` to the BPM job.
2. SModePage: `onGridOffsetChanged` must call `model.setGridOffsetSeconds(offset)` - a dragged grid offset is never written to the model, so it is not saved (auto-detected offset is; MainWindow replays it to the lanes after every onStemsReady via `smode.downbeat.detected`).
3. SModePage: init the POLY/MONO 1SH/GATE button from `midiSvc->isMonophonic()/isSampleGateMode()` in onStemsReady and drop `static int midiMode` (the engine is restored, the label is not); gridLock button text "LOCKED" in restoreUiExtraState; DAW-sync toggle is not part of captureUiExtraState (not persisted).
4. SModePage: delete the `smodePage` binding (superseded, inert now); `smodeStemPitch`/`smodeStemMod` bindings are redundant but harmless (they save from the service).
5. SequencerLaneComponent::captureState does not store slot `speed`.
6. Missing-audio UI: MainWindow shows a non-blocking AlertWindow for now; replace with an in-page banner (status/message/event above).
7. MIDI CC bindings: not persisted - nothing calls `MidiMappingService::beginCcLearn` (zero callers) and the service has no getter for its bindings (std::function targets). Needs a label->target registry in MidiMappingService first.
8. `SamplarAudioProcessor::lastHostBpm_` is unused (pre-existing, left).

## Files
Changed: src/plugin/SamplarAudioProcessor.{h,cpp}, src/app/AppState.{h,cpp}, src/app/ServiceLocator.h,
src/services/smode/SModeService.{h,cpp}, SModeSegmentModel.{h,cpp}, SModeSeparationService.{h,cpp},
src/ui/MainWindow.cpp, tools/vst3_smoke_host/VST3SmokeHost.cpp.
Added: src/services/smode/SModeSession.{h,cpp}, cmake/extra_persistence.cmake,
tests/smode/SModeSessionTests.cpp, tests/app/AppStatePersistenceTests.cpp.
Debug hook (inert unless set): SAMPLAR_DEBUG_SESSION_SEED / SAMPLAR_DEBUG_SESSION_ACK (processor + MainWindow ack files).
