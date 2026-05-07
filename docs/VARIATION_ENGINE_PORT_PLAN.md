# Variation Engine Port Plan (SM4 → SAMPLAR/JUCE)

## Executive Summary
- Port the SM4 Variation system (UI service + async generator + engines) fully to C++ in SAMPLAR using JUCE. No Python bridge.
- Preserve SM2 feature parity for the Simple engine: full parameter set, Advanced genome/mutation logic, ADSR preservation, and DSP features.
- Adopt SAMPLAR event topics and Bible logging across all new code. Use `%TEMP%/variations_sm5` with TTL cleanup.

## Components and Responsibilities
- VariationService (C++): UI-facing orchestration. Subscribes to variation requests/settings/engine changes. Manages per-sample engine selection, session correlation, and publishes generation lifecycle events.
- GeneratorService (C++): Background job queue. Loads audio, invokes engines, writes WAV outputs, publishes progress/ready/error, and cleans temp output dirs.
- Engines (C++): Implement shared `IVariationEngine` and specific engines (Simple, FastSimple/Creative, SpectralClone, DirectMorph, DirectMorphing, PowerSynth, DDSPNoise).
- Genome/Mutation (C++): `AdvancedVariationGenome` + `MutationConfig` mirrored from SM4/SM2.
- UI Integration: `VariationManager` publishes requests to EventBus and listens for ready/progress to update `VariationExpansionGrid` using engine colors.

## SAMPLAR Event Topics (renamed)
- Inbound (UI → VariationService)
  - `sm5.variation.requested` { sampleIndex, samplePath, engine?, count?, sessionId? }
  - `sm5.variation.settings.changed` { mutationRate, mutationStrength, preserveADSR, defaultEngine? }
  - `sm5.variation.engine.changed` { sampleIndex, engine }
- Outbound (services → UI)
  - `sm5.variation.generation.started` { sampleIndex, sessionId, engine }
  - `sm5.variation.progress` { sampleIndex, sessionId, current, total, progress, etaSeconds }
  - `sm5.variation.ready` { sampleIndex, sessionId, files[], engine }
  - `sm5.variation.error` { sampleIndex, sessionId, error, recoverable }
  - `sm5.variation.engine.set` { sampleIndex, engine, availableEngines[] }
  - `sm5.variation.engine.changed.ui` { sampleIndex, engine, regenerating }

## Audio, Files, and Logging
- Audio I/O: JUCE `AudioFormatManager` + `WavAudioFormat`; stereo preserved.
- Output: `%TEMP%/variations_sm5/<sessionId>/var_XX.wav` (32-bit float). TTL cleanup (default 24h, configurable).
- Logging: Apply Bible logging rigor (function entry/exit, step markers, file ops, success/error) with emoji tokens as in `docs/BIBLE_CPP_CONVERSION.md`.
- Log dump file: on app startup, create a single log file and overwrite it each run to always contain the most recent session logs:
  - Path: `%TEMP%/SAMPLAR_console.log`.
  - Mechanism: a MultiLogger mirrors all `juce::Logger` output to both the on-screen DebugConsole and the file (overwriting on startup).
  - Mandate: all new code paths must log function entry/exit and key steps so the log file is sufficient for crash/debug reproduction.

## Engines and Parity Targets
All engines will exist in C++; the Simple engine ships first with full SM2 parity. Others may be staged but registered.
- Common `IVariationEngine`:
  - `setTarget(juce::AudioBuffer<float>&, double sampleRate)`
  - `std::vector<juce::AudioBuffer<float>> generate(int count, std::optional<float> mutationRate, std::optional<float> mutationStrength)`
- SimpleVariationEngine:
  - Core ops: amplitude, time-stretch (resample), envelope curve, ADSR-shaped noise, frequency shift (complex modulation), harmonic emphasis (tanh), attack/decay modifiers, reverse, compression, simple EQ (JUCE dsp), saturation/harmonic enhance.
  - AdvancedVariationGenome + MutationConfig with ADSR protection and categorical params.
- FastSimple/Creative: streamlined DSP chain for fast V-button workflows.
- SpectralClone: FFT-based magnitude/phase shaping (v1: magnitude shaping + noise/exciter placeholder).
- DirectMorph: time/pitch/amplitude/envelope/filter/distortion morphing; JUCE dsp filters & waveshaper.
- DirectMorphing (advanced): same API; staged features behind flags until parity.
- PowerSynth: drum-oriented procedural synthesis (kick/snare/hihat) via JUCE oscillators/noise/envelopes; expand iteratively.
- DDSPNoise: envelope-shaped noise + coloration (initial minimal version).

## UI Integration (V-button)
- `VariationManager` publishes `sm5.variation.requested` with (sampleIndex, samplePath, engine, count=10, sessionId) and subscribes to `sm5.variation.ready/progress` to update the `VariationExpansionGrid`.
- `EngineColorManager` maps engine enum to names and tint colors; `engine.changed.ui` changes color immediately.

## Testing Strategy
- Unit: genome mutation bounds/categorical/ADSR, envelope extraction/application, time stretch, reverse, EQ/compression sanity.
- E2E: V-button triggers 10 variations, 10 WAVs appear, events flow to UI, thumbnails render.

## Phases and Deliverables
1) Phase 1 – Scaffolding + Simple Engine Path
   - Services: `VariationService`, `GeneratorService` with SAMPLAR topics and Bible logging.
   - Temp dir policy `%TEMP%/variations_sm5` + TTL cleanup.
   - Implement `SimpleVariationEngine` (baseline ops) + wire V-button end-to-end.
   - Register services in `App::initialise`.
2) Phase 2 – Additional Engines I
   - FastSimple + Creative; DirectMorph (core subset). JUCE DSP for filters.
   - Unit tests for engines, expand genome usage.
3) Phase 3 – Additional Engines II
   - PowerSynth (drum synths), DDSPNoise (envelope-shaped noise). Performance passes.
4) Phase 4 – SpectralClone + DirectMorphing Advanced
   - FFT-based shaping, advanced morphing; parity audits and optimization.

## Risks and Mitigations
- Realtime: all generation off UI/audio thread; results delivered on Message thread via `MessageManager::callAsync`.
- Performance: preallocate buffers, reuse DSP instances, profile bottlenecks (stretch/FFT).
- Parity: annotate staged features; keep interfaces stable; iterate toward SM4 behavior.

## Decision Record
- Port all engines to native C++/JUCE (no Python IPC).
- Use JUCE DSP throughout (filters/EQ/processing).
- Rename event topics to SAMPLAR schema.
- Full SM2 parameter parity for Simple engine.
