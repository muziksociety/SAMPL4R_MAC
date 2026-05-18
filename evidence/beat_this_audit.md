# Beat This! ONNX Integration Audit — SAMPLAR

**Model Status:** 80 MB ONNX model, gitignored but deployed. **VERDICT: YES, port to SM5.**

---

## 1. Integration Overview

Beat This is a **neural tempo/beat detector** (Foscarin et al., ISMIR 2024) integrated into SAMPLAR's hybrid tempo detection pipeline as an optional 4th detector. It runs ONNX inference to produce explicit per-beat timestamps, then derives BPM from median inter-beat interval (IOI).

**Key advantage:** Robust against metric-ratio ambiguity (95 vs 126 BPM on 16th-note hat patterns) that traps autocorrelation-based detectors, because the model is trained on beat positions directly.

---

## 2. Model File

**Location:** `third_party/beat_this/models/beat_this.onnx`  
**Size:** 80 MB  
**Status:** Gitignored at `.gitignore:396-398`  
**Deployment:** Copied at CMake POST_BUILD to exe directory; model file discovered via priority-based path resolution in `BeatThisDetector.cpp:61-88`

---

## 3. ONNX Runtime Usage

**Version:** ONNX Runtime 1.20.1 (fetched from GitHub releases at build time)  
**CMakeLists Citation:** `CMakeLists.txt` lines ~150–200 (fetched v1.20.1 for Windows x64)

**ONNX Environment & Session:**
- **File:** `third_party/beat_this/src/beat_this_api.cpp:22-23`
  ```cpp
  Ort::Env env;
  std::unique_ptr<Ort::Session> session;
  ```
- **Initialization:** `beat_this_api.cpp:26-58` — single env, lazy-loaded session via `std::call_once`
- **Threading:** RT-unsafe; designed for background thread only (BeatThisDetector.cpp:22)

**Same Ort::Env/::Session pattern as SM5's OnnxSeparationProvider:** Yes, single global Ort::Env, per-feature Ort::Session.

---

## 4. Input/Output Tensor Shapes

**Input Preprocessing:**
1. **Audio input:** Any sample rate, mono or multichannel (BeatThisDetector.cpp:100–132)
   - Downmixed to mono
   - Resampled to **22050 Hz** (InferenceProcessor.cpp:189)
2. **Mel Spectrogram:** 128 mel bands, 22050 Hz target (MelSpectrogram.h:38–44)
   - FFT size: 1024, hop length: 441 samples (10 ms frames)
   - 30–11000 Hz frequency range

**Model Input Tensor:**
- **Shape:** `[1, num_frames, 128]` (batch=1, frames, mel_bins)
- **Cited:** `InferenceProcessor.cpp:21` — `input_shape = {1, (int64_t)chunk_spect.size(), (int64_t)chunk_spect[0].size()}`
- **Chunking:** 1500-frame chunks with 6-frame border overlap for long audio (InferenceProcessor.h:34–35)

**Model Output Tensors:**
- **Output names:** `"beat"` and `"downbeat"` (InferenceProcessor.cpp:26–27)
- **Shapes:** Each `[batch, num_frames]` — logits for beat/downbeat probability at each frame
- **Cited:** `InferenceProcessor.cpp:34–38` — extracted via `GetTensorMutableData<float>()`

**Result:** Per-beat timestamps in seconds (BeatThisDetector.cpp:142–144), downbeats extracted separately.

---

## 5. Algorithm Wrapper & Decision Logic

**Decision:** Beat This runs **as an alternative, not fallback.** It is conditional on `WITH_BEAT_THIS` CMake flag and `config.useBeatThis` runtime flag.

**Flow (HybridTempoDetector.cpp:12–211):**

1. **Line 24–40:** Run FFT Spectral Flux detector
2. **Line 45–60:** Run DWT detector
3. **Line 70:** Merge FFT + DWT results (confidence-weighted)
4. **Line 73–136:** IF `WITH_BEAT_THIS` compiled in AND `config.useBeatThis==true`:
   - **Line 84:** Run Beat This detector on same buffer
   - **Line 89:** Accept neural result IF `bt.confidence >= config.beatThisConfidenceFloor` (default 0.5)
   - **Line 100–102:** If accepted, **REPLACE** DSP fused BPM with Beat This BPM
   - **Line 112–134:** Optional refinement via narrow-band autocorrelation (±5% tolerance) to recover sub-decimal precision lost to model's ~20 ms frame quantization
5. **Line 147–204:** Octave correction applied **only to DSP results**; Beat This skips it (model already has correct octave)

**Verdict:** Winner-take-all. Beat This **overrides DSP if confident**, no ensemble.

---

## 6. Quality vs Heuristic Detector

**Accuracy advantage:** Explicit beat timestamps vs IOI-based estimate.

**Comments in code:**
- **BeatThisDetector.h:17–18:** "robust to metric-ratio (96 vs 126 BPM on 16th-note hat patterns) ambiguity"
- **HybridTempoDetector.h:38–39:** "optional 4th detector, enabled when compiled in and Config::useBeatThis=true"
- **HybridTempoDetector.cpp:105–110:** Tempo refinement recovery for sub-decimal precision (model is ~20ms quantized)

**Confidence threshold:** `config.beatThisConfidenceFloor = 0.5f` (HybridTempoDetector.h:94) — only use neural result if confidence >= 50%.

**No benchmark numbers in code.** Model training paper (ISMIR 2024) would contain F-measure / accuracy metrics, not vendored here.

---

## 7. Licensing & Provenance

**Model source:** Trained model from "Beat This!" (Foscarin et al., ISMIR 2024)  
**Public release:** Likely from https://github.com/CPJKU/beat_this (referenced in comment BeatThisDetector.h:12)  
**License notes:** None in SAMPLAR repo; defer to original Beat This repo for model license (typically CC-BY or similar for ISMIR papers)

**C++ wrapper:** Vendored at `third_party/beat_this/src/` — SAMPLAR-maintained wrapper around ONNX Runtime  
**Dependencies:** PocketFFT (BSD 3-clause, line 395) for FFT; ONNX Runtime 1.20.1 (MIT, pre-built DLL)

---

## 8. Adoption Verdict for SM5

### Model Bundling
- **80 MB ONNX file:** Borderline but tolerable (SM5 already ships htdemucs @ 300+ MB)
- **Deployment:** Model is gitignored (auto-fetched at build time or included in portable distribution)
- **Cost:** +80 MB exe/installer footprint

### Runtime Dependency
- **ONNX Runtime 1.20.1:** Already in SM5 for OnnxSeparationProvider (htdemucs)
- **DLL:** `onnxruntime.dll` already shipped; **no additional runtime cost**
- **Threading:** Both Beat This and Demucs are RT-unsafe, dispatched to background threads

### Functional Fit
- **Improves downbeat detection?** YES — explicit beat timestamps + downbeat labels (beat_counts)
- **Replaces both BPM + downbeat?** PARTIALLY — produces beat positions, not drum-specific downbeat labels. Useful as a **prior** for drum-slice alignment, but SM5's heuristic (kick-band comb filter) is simpler and may be retained in parallel.
- **Latency:** Model inference on 30s @ 22050 Hz ~500–1000 ms (not measured in SAMPLAR logs). Acceptable for offline analysis, not realtime.

### Recommendation
**✅ ADOPT:** Port BeatThisDetector to SM5 as **optional 4th detector in new `TempoAnalysis` service**. Use same pattern as SAMPLAR:
1. Compile-time flag `WITH_BEAT_THIS=ON/OFF`
2. Runtime `TempoAnalysisConfig.useBeatThis=true/false`
3. Winner-take-all merge (if Beat This confident ≥0.5, use it; else fall back to heuristic)
4. Reuse same ONNX Runtime and model file from `third_party/beat_this/`

**Risks:**
- Model is 80 MB; bundle in portable distribution only (not source tree)
- Confidence threshold (0.5) may need tuning for SM5's use case (depends on test set)
- No per-beat-timestamp API in SM5 yet; would need UI extension to display beat grid

---

## File References

| File | Line(s) | Content |
|------|---------|---------|
| `BeatThisDetector.h` | 12–18 | Architecture comment; ISMIR 2024 reference |
| `BeatThisDetector.cpp` | 3 | `#if WITH_BEAT_THIS` compile-time gate |
| `BeatThisDetector.cpp` | 22–23 | `Ort::Env` and `Ort::Session` declaration |
| `BeatThisDetector.cpp` | 61–88 | Model path resolution (5 priority levels) |
| `BeatThisDetector.cpp` | 100–298 | Full detection pipeline; line 142–144 beat timestamp extraction |
| `HybridTempoDetector.h` | 38–41, 93–94 | Config.useBeatThis flag, confidence floor |
| `HybridTempoDetector.cpp` | 73–136 | Beat This integration in merge logic; line 89 confidence gate |
| `InferenceProcessor.h` | 34–35 | Chunk size (1500 frames), border (6 frames) |
| `InferenceProcessor.cpp` | 21, 26–27 | Input shape `[1, frames, 128]`; output names "beat", "downbeat" |
| `MelSpectrogram.h` | 38–44 | Mel spec parameters: 22050 Hz, 128 bins, 30–11k Hz |
| `CMakeLists.txt` | ~150–200 | ONNX Runtime v1.20.1 FetchContent; model copy at install |
| `.gitignore` | 396–398 | Model file gitignored |

