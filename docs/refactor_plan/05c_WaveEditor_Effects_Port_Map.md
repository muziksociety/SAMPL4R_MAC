- pitch_bend_effect.py -> Nodes/PitchBendNode.h/.cpp
- compressor_v2_juce.py -> Nodes/CompressorNode.h/.cpp (JUCE baseline)
- pitch_bend_effect.py -> Nodes/PitchBendNode.h/.cpp
- pitch_bend_effect.py -> Nodes/PitchBendNode.h/.cpp
- compressor_v2_juce.py -> Nodes/CompressorNode.h/.cpp (JUCE baseline)
- compressor_v2_juce.py -> Nodes/CompressorNode.h/.cpp (JUCE baseline)
- pitch_bend_effect.py -> Nodes/PitchBendNode.h/.cpp
- compressor_v2_juce.py -> Nodes/CompressorNode.h/.cpp (JUCE baseline)
# 05c – Wave Editor Effects Port Map (Python → C++)

Requires: `Sample_monsta5/docs/refactor_plan/04_Audio_Engine_and_Graph.md`
Used by: `05_Wave_Editor_Port.md`

Source (SM4): `plugins/wave_editor_effects_sm2/effects/*`

Port targets (C++/JUCE nodes)
- Compressor: JUCE dsp::Compressor for baseline; custom C++ matching `compressor_effect(_cython)` characteristics.
- EQ: biquad IIR (JUCE IIR + design) or custom RBJ; match `eq_effect(_cython)` bands/Q.
- Reverb: JUCE Reverb with tuned parameters.
- Delay: simple feedback delay with tempo sync.
- Chorus/Flanger/Phaser: JUCE Modulation effects or custom LFO + delay line.
- Distortion/Bitcrusher: waveshapers + sample rate/bit depth reducers.
- Limiter/Noise Gate/Audio Gate: envelope follower + threshold, attack/release.
- Highpass/Lowpass: IIR (Butterworth/Biquad).
- Tremolo: gain LFO.
- Frequency Shift: single sideband/QIFFT (note: complexity; start with ring mod approximation).
- Reverse: buffer reversal (offline render only).
- Pitch Shift/Time Stretch: library option (Rubber Band, SoundTouch) or phase vocoder; realtime quality trade-offs.
- Pitch Bend: time-varying pitch shift (small-window resampler or library-based bend); offline for long/steep bends if needed.
- Revibe: replicate parameters and curve from `revibe_effect.py`.
- Slice Shuffle/Stutter/Tapestop: offline buffer ops; ensure deterministic scheduling.

Mapping Table (file → node)
- `compressor_effect.py` → `Nodes/CompressorNode.h/.cpp`
- `compressor_effect_cython.py` → same node with fast path
- `compressor_v2_juce.py` → `Nodes/CompressorNode.h/.cpp` (JUCE baseline)
- `eq_effect.py` / `eq_effect_cython.py` → `Nodes/EQNode.h/.cpp`
- `reverb_effect.py` → `Nodes/ReverbNode.h/.cpp`
- `delay_effect.py` → `Nodes/DelayNode.h/.cpp`
- `chorus_effect.py` / `flanger_effect.py` / `phaser_effect.py` → `Nodes/ModFX/*`
- `distortion_effect.py` / `bitcrusher_effect.py` → `Nodes/WaveshaperNode.h/.cpp`
- `limiter_effect.py` / `noise_gate.py` / `audio_gate.py` → `Nodes/Dynamics/*`
- `highpass_filter.py` / `lowpass_filter.py` → `Nodes/Filters/*`
- `tremolo_effect.py` → `Nodes/TremoloNode.h/.cpp`
- `frequency_shift_effect.py` → `Nodes/FreqShiftNode.h/.cpp` (note: algorithmic review)
- `reverse_effect.py` → offline export utility
- `pitch_shift_effect.py` / `time_stretch_effect.py` → `Nodes/PitchTime/*` (lib hook)
- `pitch_bend_effect.py` → `Nodes/PitchBendNode.h/.cpp`
- `revibe_effect.py` → `Nodes/RevibeNode.h/.cpp`
- `slice_shuffle_effect.py` / `stutter_effect.py` / `tapestop_effect.py` → `Nodes/OfflineEdits/*`

Porting Guidelines
- Preserve parameter names/ranges; normalize to [0,1] in UI with mapping.
- Audio thread: no allocations; precompute/filter design on param changes (message thread) then atomically swap.
- Provide bypass and latency reporting (where applicable).
- Golden audio tests: render fixed inputs and compare RMS/SNR to Python versions.

External Libraries (optional)
- Rubber Band Library (time/pitch) – GPL/commercial; check licensing.
- SoundTouch (time/pitch) – LGPL.
- FFT: KissFFT/FFTW (license review) or JUCE FFT.
