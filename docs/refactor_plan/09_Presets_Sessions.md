# 09 – Presets and Sessions

Requires: `Sample_monsta5/docs/refactor_plan/01_Core.md`, `Sample_monsta5/docs/refactor_plan/02_EventBus_ServiceRegistry.md`
Provides: saving/loading of presets and session state.

Goals
- Define JSON schemas for effect chains, editor view state, and app prefs.
- Implement preset manager, session autosave, and migration/versioning.

Deliverables
- `src/services/PresetService.h/.cpp`
- `src/services/SessionService.h/.cpp`
- `resources/schemas/*.json` (versioned)

Acceptance
- Presets round-trip 1:1 with editor parameters; sessions restore view and playback state.

Links
- Consumed by `05a_WaveEditor_UI.md`, `05b_WaveEditor_Audio.md`.

