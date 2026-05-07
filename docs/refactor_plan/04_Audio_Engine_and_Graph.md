# 04 – Audio Engine and Graph

Requires: `Sample_monsta5/docs/refactor_plan/01_Core.md`
Provides: devices, processing graph, parameter model.

Goals
- Implement device manager, audio I/O, and an `AudioProcessorGraph` (or custom node chain) compatible with wave editor needs.
- Parameter model: atomic parameters with smoothing; UI bindings via lambdas.
- File decode pipeline (background) → ring buffer → engine.

Deliverables
- `src/audio/Engine.h/.cpp`: device init, audio callback, graph wiring.
- `src/audio/Nodes/*`: FileSource, Gain, EQ, Compressor, Reverb, Delay, etc.
- `src/audio/Params.h`: thread-safe parameter wrappers, smoothing helpers.

Acceptance
- Can load and play a file; CPU usage stable; no glitches when changing parameters.
- Unit tests for parameter smoothing and node bypass path.

Links
- Required by `05b_WaveEditor_Audio.md`, referenced in `05c_WaveEditor_Effects_Port_Map.md`.

