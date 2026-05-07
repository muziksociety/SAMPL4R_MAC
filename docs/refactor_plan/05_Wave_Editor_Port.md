# 05 – Wave Editor (End-to-End Port)

Requires: `Sample_monsta5/docs/refactor_plan/03_UI_Shell_and_Navigation.md`, `Sample_monsta5/docs/refactor_plan/04_Audio_Engine_and_Graph.md`
Provides: the flagship editor feature parity in JUCE.

Scope
- Consolidates functionality from SM4 plugins:
  - `wave_editor_frontend` (UI)
  - `wave_editor_core_sm2` (models, tools, state)
  - `wave_editor_audio_sm2` (playback, cache, export)
  - `wave_editor_effects_sm2` (DSP effects)
  - `sample_waveform_display` (rendering)

Phasing and Reuse Strategy
- Phase 0 (reuse as-is): adopt existing JUCE editor components and host them natively in SAMPLAR (no Qt embedding).
  - Import and wire: `plugins/wave_editor_juce/juce_integration/src/WaveEditorWindow.h`
  - Reuse UI/content: `MainComponent.h` and related subcomponents
  - Reuse preview: `BufferSource.h` for play/stop/gain
  - Remove/ignore Python bridge and Qt wrapper; call editor directly from SAMPLAR services/UI
- Phase 1 (structure alignment): wrap the reused components under `src/ui/WaveEditor/*` and thread state/params through the new service/engine
- Phase 2 (incremental refactor): migrate effects and audio graph to `src/audio/Nodes/*` while preserving sound/UX

Deliverables
- See sub-docs:
  - `05a_WaveEditor_UI.md` – screens, panels, interactions.
  - `05b_WaveEditor_Audio.md` – playback, caching, export.
  - `05c_WaveEditor_Effects_Port_Map.md` – every effect mapped to C++.

Acceptance
- 1:1 UX top flows: load sample, preview, edit chain, export.
- Golden audio parity for core effects within tolerance.
