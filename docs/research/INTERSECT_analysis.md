# INTERSECT Repository Analysis

**Repository:** https://github.com/tucktuckg00se/INTERSECT
**License:** GPL v3.0
**Tech Stack:** C++20, JUCE (submodule), CMake
**Platforms:** Windows, macOS (arm64/x64), Linux

## What INTERSECT Does

INTERSECT is a **sample slicer instrument plugin** (VST3/AU/Standalone) focused on:
- Loading a single audio file and slicing it into MIDI-triggered regions
- Per-slice parameter overrides with a global/slice inheritance model
- Three time-stretching/pitch-shifting algorithms: Repitch, Signalsmith Stretch, Bungee
- Per-voice SVF filter with drive/saturation, key tracking, and filter envelope
- Loop/ping-pong playback with equal-power crossfading
- Live "lazy chop" (real-time slice creation during playback)
- Auto-chop via FFT-based transient detection
- NRPN MIDI controller support for hardware slice editing
- Snapshot-based undo/redo system
- Custom theming system

It is NOT a variation generator or stem separator -- it is a focused sample slicer with high-quality playback.

---

## All Algorithms Found

### 1. Transient Detection (AudioAnalysis.h - `detectTransients`)

**Approach:** STFT-based spectral flux onset detection function with 4-stage pipeline.

**Implementation Details:**
- **Stage 1 - STFT + Spectral Flux ODF:** 1024-point FFT (order 10), 256-sample hop (~5.8ms at 44.1kHz), Hann window. Computes half-wave rectified spectral flux (sum of positive magnitude increases between consecutive frames).
- **Stage 2 - Adaptive Threshold:** Moving median filter (window radius 10 frames, ~58ms lookaround) multiplied by a sensitivity parameter lambda, plus a noise floor delta (1e-4).
- **Stage 3 - Peak Picking:** Local maxima detection with radius 3 frames. Only peaks above the adaptive threshold are kept.
- **Stage 3.5 - Backtrack Refinement:** Walks backward from each peak to find the true transient onset using an envelope-based approach:
  - Moving-max envelope smoothing (0.7ms half-width)
  - RMS noise floor measurement (6ms window)
  - Onset threshold at 10% of (peak - noise)
  - 0.2ms safety preroll after refinement
- **Stage 4 - MIN Post-Filter:** Greedy strongest-first filtering to enforce minimum slice length. Sorts candidates by strength descending, keeps only those that respect the minimum distance.

**Quality Assessment: EXCELLENT (9/10)**
This is a production-grade transient detector. The 4-stage pipeline with backtrack refinement is superior to most open-source implementations. Key strengths:
- Backtrack refinement solves the classic problem of STFT-based detectors reporting onsets late (by the FFT latency)
- Adaptive median threshold is robust against varying signal levels
- Strongest-first MIN filtering is smarter than simple left-to-right suppression
- All constants are well-tuned for musical audio

**SAMPLAR Comparison:**
SAMPLAR's transient detection (if using simple RMS energy thresholding) would benefit significantly from this spectral flux approach. The backtrack refinement alone would improve onset accuracy by 5-10ms.

---

### 2. Zero-Crossing Detection (AudioAnalysis.h - `findNearestZeroCrossing`)

**Approach:** Brute-force search within a configurable window (default 512 samples) for the nearest positive-to-negative or negative-to-positive zero crossing on the mono-summed signal.

**Quality Assessment: GOOD (7/10)**
Simple and effective. Searches both directions from the target position and returns the closest crossing. The 512-sample default search range (~11.6ms at 44.1kHz) is reasonable for most musical content.

**SAMPLAR Relevance:** Directly applicable to slice boundary snapping in S-Mode.

---

### 3. SVF Filter (SvfFilter.h)

**Approach:** State Variable Filter using the topology-preserving transform (Vadim Zavalishin / Andrew Simper method).

**Implementation:**
```cpp
// Core SVF computation
v3 = input - ic2eq;
v1 = a1 * ic1eq + a2 * v3;
v2 = ic2eq + a2 * ic1eq + a3 * v3;
ic1eq = 2 * v1 - ic1eq;
ic2eq = 2 * v2 - ic2eq;
// Outputs: low=v2, band=v1, high=input-k*v1-v2, notch=high+low
```

- Supports LP, HP, BP, Notch modes
- Optional 24dB/oct via cascading two instances
- Coefficient update rate: every 32 samples (kFilterCoeffUpdateInterval)
- Cutoff clamped to [20Hz, 0.49*sampleRate]

**Quality Assessment: EXCELLENT (9/10)**
This is the gold-standard digital filter topology. The TPT SVF is unconditionally stable, handles modulation well, and provides clean resonance. The 32-sample coefficient update interval is a good balance between accuracy and CPU.

**SAMPLAR Relevance:** If SAMPLAR doesn't already use TPT SVF for any filtering, this is the filter topology to adopt. The coefficient computation and cascading approach are textbook-correct.

---

### 4. Drive/Saturation with Asymmetry (VoicePool.cpp)

**Approach:** tanh waveshaping with asymmetric bias and automatic DC blocking.

**Implementation:**
```cpp
// Saturation constants pre-cached per voice
gain = 1.0 + driveNorm * 12.0;  // up to 13x gain
bias = asymNorm * 0.5;
satOffset = tanh(bias * gain);
// Per-sample: (tanh((x + bias) * gain) - satOffset) / satNorm

// DC blocker (1-pole highpass at 20Hz) engages when asymmetry > 0
dcCoeffR = 1.0 - (2*pi*20 / sampleRate);
```

**Quality Assessment: VERY GOOD (8/10)**
Clean implementation. The asymmetric saturation adds even harmonics (tube-like warmth), and the automatic DC blocker prevents offset buildup. Pre-caching saturation constants per voice avoids redundant computation.

**SAMPLAR Relevance:** Could enhance variation engines that use distortion/saturation. The asymmetry parameter adds character that simple symmetric waveshaping lacks.

---

### 5. ADSR Envelope (AdsrEnvelope.h)

**Approach:** One-pole exponential envelope with pre-computed per-sample coefficients.

**Key Design Choices:**
- Attack targets 1.01 (slightly above 1.0) to reach unity within the specified time rather than asymptotically approaching it
- Release threshold at -76dB (0.00015) -- treats as inaudible, prevents infinite tail
- Coefficient formula: `1 - exp(-5 / (time * sampleRate))` giving ~99.3% convergence in the specified time
- No per-sample exp() calls -- coefficients computed once at noteOn

**Quality Assessment: VERY GOOD (8/10)**
Efficient and well-considered. The 1.01 attack overshoot target is a nice detail that ensures the envelope actually reaches 1.0 in the specified time. The -76dB cutoff is reasonable.

**SAMPLAR Relevance:** If SAMPLAR uses JUCE's built-in ADSR, this custom implementation offers equivalent quality with explicit control over the convergence behavior.

---

### 6. Pitch Shifting - Three Algorithms

#### 6a. Repitch (built-in)
Simple variable-speed playback with linear or cubic interpolation:
```cpp
// Cubic interpolation (Catmull-Rom):
a0 = -0.5*y0 + 1.5*y1 - 1.5*y2 + 0.5*y3;
a1 = y0 - 2.5*y1 + 2.0*y2 - 0.5*y3;
a2 = -0.5*y0 + 0.5*y2;
a3 = y1;
result = ((a0*frac + a1)*frac + a2)*frac + a3;
```
Speed = 2^(semitones/12). Pitch and time are linked.

#### 6b. Signalsmith Stretch (external library)
Independent time/pitch shifting via Signalsmith Stretch library:
- Block size: ~23ms (sr * 0.023), interval: ~6ms (sr * 0.006)
- Supports tonality limiting, formant shifting, formant compensation
- Pre-roll/seek mechanism for instant playback at arbitrary positions
- Source feeding handles loop wrapping with crossfade at boundaries

#### 6c. Bungee (external library)
Granular time-stretching via Bungee library:
- Three grain modes: Fast (-1 hop), Normal (0), Smooth (+1)
- Independent pitch and speed control
- Grain-based analysis/synthesis cycle
- "Unbounded phase" design -- doesn't canonicalize position back into loop range; lets read helpers handle mapping

**Quality Assessment: EXCELLENT (9/10)**
The three-algorithm approach is production-quality. Each serves a distinct use case:
- Repitch: zero-latency, lowest CPU, classic sampler behavior
- Signalsmith: best quality for independent pitch/time, formant preservation
- Bungee: granular character with controllable grain size

The integration is thorough -- all three handle loop wrapping, ping-pong, reverse, and crossfading correctly.

**SAMPLAR Comparison:**
SAMPLAR uses SoundTouch for pitch/time shifting. INTERSECT's approach of offering multiple algorithms is superior for user flexibility. Specifically:
- **Signalsmith Stretch** is significantly higher quality than SoundTouch for pitch shifting (formant preservation, tonality control)
- **Bungee** offers granular character that neither SoundTouch nor Signalsmith provides
- The repitch mode with cubic interpolation is cleaner than basic linear interpolation

---

### 7. Equal-Power Loop Crossfading (VoicePool.cpp)

**Approach:** Cosine-based equal-power crossfade at loop/ping-pong boundaries.

```cpp
// gainMain = cos(t * pi/2), gainXfade = cos((1-t) * pi/2)
// where t = 1.0 - (distanceToBoundary / crossfadeLength)
```

The crossfade source reads from outside the loop region (for normal loops) or from the boundary-adjacent region in the opposite direction (for ping-pong). Distance-to-boundary calculation uses canonical wrapped/reflected positions.

**Quality Assessment: VERY GOOD (8/10)**
Equal-power crossfading is the correct choice for audio loops (maintains perceived loudness). The implementation correctly handles all edge cases: loop wrap, ping-pong turnaround, and pre-loop lead-in phases.

**SAMPLAR Relevance:** If S-Mode has loop points, this crossfading approach would eliminate clicks at loop boundaries.

---

### 8. Voice Allocation and Stealing (VoicePool.cpp)

**Approach:**
1. First pass: find any inactive voice within maxActive range
2. Steal pass: prefer releasing voices with lowest envelope level (score = envLevel, subtract 10 if in Release state)

**Quality Assessment: GOOD (7/10)**
Simple but effective. The release-state bias (-10) ensures releasing voices are stolen before sustaining ones. Max 31 playable voices + 1 reserved preview voice.

**SAMPLAR Relevance:** Same pattern is standard for polyphonic samplers. SAMPLAR likely already has voice management, but the "preview voice" reservation concept (dedicated voice index that's never stolen) is clean.

---

### 9. MIDI Note-to-Slice Mapping (SliceManager.cpp)

**Approach:** Dual mapping system:
- `midiMap[128]`: first slice for each note (fast single lookup)
- `midiMapMulti[128]`: vector of all slices for each note (supports overlapping ranges)

Each slice has: midiNote (low), highNote (high), sliceRootNote (transposition center).
Range transpose: `pitch += (playedNote - sliceRootNote)`

Resequence modes: "BY POSITION" (sorts slices by startSample) or "AS CREATED" (original order).

**Quality Assessment: VERY GOOD (8/10)**
The dual map (single + multi) is efficient. Supporting both single-note and note-range modes per slice is flexible. The resequence feature is a nice UX touch.

**SAMPLAR Relevance:** SAMPLAR's S-Mode MIDI mapping could benefit from the note-range concept (LOW/HIGH/ROOT per pad). This allows velocity layers and chromatic mapping within a single slice.

---

### 10. Waveform Mipmap Rendering (SampleData.cpp + WaveformCache.cpp)

**Approach:** 3-level peak mipmap (block sizes: 64, 512, 4096 samples) built at load time. Rendering picks the finest mipmap level that's coarser than 1 sample but finer than samples-per-pixel.

Sub-sample zoom uses linear interpolation at exact fractional positions.

**Quality Assessment: VERY GOOD (8/10)**
Mipmapped waveform rendering is the correct approach for smooth zooming. Three levels cover a good range of zoom factors. The level selection logic is sound.

**SAMPLAR Relevance:** If SAMPLAR renders waveforms without mipmapping, this approach would significantly reduce CPU usage during zoom/scroll operations.

---

### 11. Sample Loading with Resampling (SampleData.cpp)

**Approach:**
- Asynchronous loading via ThreadPoolJob (off audio thread)
- Lagrange interpolation for sample rate conversion
- Always normalizes to stereo (mono duplicated to both channels)
- Atomic shared_ptr for thread-safe snapshot exchange between UI and audio threads

**Quality Assessment: VERY GOOD (8/10)**
The async loading is essential for a plugin. Lagrange interpolation for resampling is higher quality than linear. The shared_ptr snapshot pattern for lock-free audio/UI data exchange is well-designed.

**SAMPLAR Relevance:** The atomic shared_ptr snapshot pattern is a clean alternative to double-buffering for sharing decoded sample data between threads.

---

### 12. Slice Parameter Inheritance Model

**Approach:** Bitmask-based lock system using uint64_t:
- Each parameter has a LockBit (38 defined parameters)
- `resolveParam(sliceIdx, lockBit, sliceValue, globalDefault)`: returns slice value if locked, global default otherwise
- When editing in SLICE mode, diverging from global automatically sets the lock bit
- Clicking a locked label clears the override

**Quality Assessment: EXCELLENT (9/10)**
Elegant and efficient. The bitmask approach is O(1) per parameter resolution with minimal memory (8 bytes per slice for all 38 lock flags). This is a very clean pattern for the global/local override paradigm.

**SAMPLAR Relevance:** S-Mode could adopt this pattern for per-pad parameter overrides (e.g., individual pad volumes, filters, effects that differ from the global preset).

---

### 13. Undo/Redo System (UndoManager.h + PluginProcessor.cpp)

**Approach:** Snapshot-based -- captures full state (all slices, all parameters) at each undo point. Command queue pattern with lock-free FIFO between UI and audio thread.

**Quality Assessment: GOOD (7/10)**
Snapshot-based undo is simple and correct, though memory-hungry with many slices. The command queue with coalescable commands (CmdSetSliceParam, CmdSetSliceBounds) prevents undo flooding during drag operations.

---

### 14. Filter Envelope with Key Tracking

**Approach:**
```cpp
// Key tracking: ratio = pow(2, (playedNote - rootNote) / 12)^trackPercent
filterKeyTrackRatio = pow(noteRatio, keyTrackNorm);
// Envelope modulation in semitones: cutoff *= 2^(envAmount * envLevel / 12)
cutoff = keyTrackedCutoff * exp2f(filterEnvAmount * envLevel / 12.0f);
```

Filter envelope amount measured in semitones (bipolar, +/-96st = 8 octaves). Key tracking is percentage-based (0% = ignore pitch, 100% = full keyboard scaling).

**Quality Assessment: EXCELLENT (9/10)**
The semitone-based envelope amount is musically intuitive (+12st = one octave of cutoff movement). The key tracking implementation correctly uses exponential scaling. Coefficient update rate of 32 samples keeps it smooth without excessive CPU.

---

## Recommendations for SAMPLAR

### High Priority (Direct Improvements)

1. **Adopt INTERSECT's Transient Detection for Auto-Chop / Smart Fill**
   - SAMPLAR's current approach (if RMS-based) would be significantly improved by the STFT spectral flux + backtrack refinement pipeline
   - The adaptive median threshold is more robust than fixed thresholds
   - The backtrack refinement (Stage 3.5) is the key differentiator -- most implementations skip this

2. **Consider Signalsmith Stretch as an Alternative/Upgrade to SoundTouch**
   - MIT licensed, header-only, significantly better quality for pitch shifting
   - Formant preservation prevents the "chipmunk" effect on large pitch shifts
   - Could coexist with SoundTouch as a quality option

3. **Adopt the Bitmask Parameter Inheritance Model for S-Mode**
   - The `LockBit` + `resolveParam()` pattern is ideal for per-pad overrides
   - Very memory-efficient (8 bytes per pad for 38+ parameters)
   - Clean separation between "pad uses global" vs "pad has override"

### Medium Priority (Architecture Patterns)

4. **Waveform Mipmapping**
   - 3-level peak mipmap (64/512/4096) dramatically improves waveform rendering performance
   - Built once at load time, queried during paint

5. **Equal-Power Loop Crossfading**
   - If S-Mode supports loop playback, use cos/sin crossfading at loop seams
   - Eliminates clicks without volume pumping

6. **Atomic Shared_ptr Snapshot Pattern**
   - Lock-free sample data sharing between audio and UI threads
   - Cleaner than manual double-buffering

### Lower Priority (Nice to Have)

7. **Cubic Interpolation for Repitch Playback**
   - Catmull-Rom 4-point interpolation is audibly cleaner than linear for pitch shifting
   - Minimal CPU cost increase

8. **Drive Asymmetry for Saturation**
   - Adds even-harmonic "tube warmth" character to existing distortion/saturation
   - Automatic DC blocker prevents offset buildup

9. **NRPN MIDI Controller Support**
   - Hardware controller integration via NRPN (CC 96/97 relative encoding)
   - Could enhance S-Mode's MIDI learn with hardware knob editing

### What INTERSECT Does NOT Have (SAMPLAR Advantages)

- No stem separation (SAMPLAR has demucs integration)
- No variation/generation engines (SAMPLAR has 8 engines)
- No pattern/sequencer (SAMPLAR has S-Mode sequencer)
- No FFT spectral analysis for sample classification (SAMPLAR has Smart Fill)
- Single sample per instance (SAMPLAR handles multiple)
- No sample browser functionality

INTERSECT is focused purely on the "slicer + playback" domain and excels at it. SAMPLAR has a much broader feature set but could adopt several of INTERSECT's focused implementations to raise quality in overlapping areas.

---

*Analysis performed: 2026-03-31*
*Source: https://github.com/tucktuckg00se/INTERSECT (GPL v3.0)*
*Files analyzed: 25 source files across src/audio/, src/ui/, src/params/*
