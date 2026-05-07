# 05b – Wave Editor Audio (Playback, Cache, Export)

Requires: `Sample_monsta5/docs/refactor_plan/04_Audio_Engine_and_Graph.md`
Used by: `05_Wave_Editor_Port.md`

Goals
- Reuse preview chain first, then migrate to SAMPLAR engine:
  - Phase 0 (reuse): keep `BufferSource` playback (play/stop/gain) wired from `MainComponent`
  - Phase 1 (structure): introduce `PreviewEngine` and route UI to it; keep `BufferSource` behind adapter
  - Phase 2 (refactor): replace with `AudioProcessorGraph` nodes and SAMPLAR parameter model

- Implement background decoding and immutable audio cache similar to SM4 `immutable_audio`.
- Playback engine with loop/region, scrub, preview gain.
- Export manager (offline render) with progress and cancel.

Deliverables
- `src/audio/Decode/DecoderPool.h/.cpp` – background decode to float buffers.
- `src/audio/Cache/ImmutableAudio.h/.cpp` – shared storage and slices.
- `src/audio/Preview/PreviewEngine.h/.cpp` – loop regions, play state, gain; adapter for `BufferSource` in Phase 0–1.
- `src/audio/Export/ExportManager.h/.cpp` – file render, progress callbacks.

Reuse Notes
- Keep `plugins/wave_editor_juce/juce_integration/src/BufferSource.h` as the initial preview source
- Surface only the stable API (play/stop/gain/isPlaying) to UI while the engine evolves
- Avoid touching audio thread patterns during Phase 0 to preserve behavior

Acceptance
- Large files decode in background; UI stays responsive.
- Export produces correct length and format; cancel works.
