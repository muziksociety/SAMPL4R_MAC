# 05a – Wave Editor UI (JUCE)

Requires: `Sample_monsta5/docs/refactor_plan/03_UI_Shell_and_Navigation.md`
Used by: `05_Wave_Editor_Port.md`

Goals
- Reuse the existing JUCE Wave Editor UI first, then evolve:
  - Phase 0 (reuse): host current `MainComponent` inside `WaveEditorView` and wire to services
  - Phase 1 (structure): extract subcomponents gradually to `WaveformView`, `EffectsPanel`, `TransportBar`
  - Phase 2 (refactor): align parameter/state model with SAMPLAR engine while preserving UX
  
- Recreate/align the Wave Editor dialog/view using JUCE Components:
  - Waveform viewport with zoom/scroll, selection, markers.
  - Effects panel with parameter controls and presets.
  - Transport: play/stop, loop, scrub.
  - Status bar: selection time, sample rate, length.

Deliverables
- `src/ui/WaveEditor/`:
  - `WaveEditorView.h/.cpp` – top level view; initially wraps existing `MainComponent`.
  - `WaveformView.h/.cpp` – cached waveform drawing (OpenGL optional).
  - `EffectsPanel.h/.cpp` – parameter controls bound to Engine params.
  - `TransportBar.h/.cpp` – play/loop/scrub; keyboard shortcuts.

Reuse Notes
- Remove Qt/pybind embedding paths; construct `WaveEditorWindow/MainComponent` directly
- Maintain current layout/behavior; defer visual redesign until after parity
- Keep keyboard/mouse interactions identical; bind to SAMPLAR event contracts

Contracts
- EventBus:
  - `wave_editor.open { file, index }`
  - `wave_editor.apply_preset { name }`
  - `wave_editor.export { path, format }`

Acceptance
- Interaction parity with SM4: selection, zoom, parameter editing.
- Smooth redraw, no UI stalls on large files (background decoding).
