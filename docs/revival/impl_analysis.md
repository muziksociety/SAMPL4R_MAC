# Analysis workstream - C10 / C11 / C12 / C16 / C17 + KeyDetector

Build: `build_analysis/` only. `SamplarPlugin_VST3` builds clean (Release), and `AnalysisTests` passes **53/53 checks** (exit 0).
Nothing was committed. No files were edited outside my ownership.

## Files
Modified: `src/services/analysis/{BeatThisDetector,DownbeatDetector,HybridTempoDetector}.{h,cpp}`,
`DWTTempoDetector.cpp`, `OctaveCorrection.cpp`, `TempoAnalyzer.cpp`, `TempoRefiner.cpp`,
`src/services/smode/SpectralFluxDetector.{h,cpp}`.
New: `src/services/analysis/AnalysisDsp.{h,cpp}` (shared DSP: downmix, correct-layout spectral flux, FFT autocorrelation,
attack finder, zero-crossing snap), `src/services/analysis/KeyDetector.{h,cpp}`,
`src/services/analysis/tests/AnalysisTests.cpp`, `cmake/extra_analysis.cmake` (adds AnalysisDsp + KeyDetector to
SamplarPlugin/SamplarApp; defines the EXCLUDE_FROM_ALL `AnalysisTests` console target, which copies ORT and beat_this.onnx next to the exe).
`third_party/beat_this/**` is unchanged.

Run the tests: `cmake --build build_analysis --config Release --target AnalysisTests --parallel`, then
`build_analysis/AnalysisTests_artefacts/Release/AnalysisTests.exe [--quick] [--stem file]`. The detector log is written to `AnalysisTests.log` next to the exe.

## Findings -> fix -> evidence

### C10 - downbeat = beat phase only; BeatThis downbeats discarded
- **Re-verified:** `BeatThisDetector.cpp` copied only `bt.beats`, and `DownbeatDetector` clamped the result to `[0, tau)` (one beat).
- **Fix:**
  - `BeatThisDetector::Result` now carries `downbeatTimesSec`, `beatCounts`, `beatsPerBar` and `halvedFromDoubleTime`. `HybridTempoDetector::Result` exposes `beatTimesSec`, `downbeatTimesSec` and `beatsPerBar`.
  - `DownbeatDetector::detect()` keeps its old signature and now runs BeatThis first. Each model downbeat votes for its bar position (beat number mod meter). A near-tie, where the model marks both beat 1 and beat 3, is broken by kick-band plus broadband onset strength.
  - The anchor downbeat is refined to the sample attack (energy step within +-30 ms, then the zero crossing up to 1 ms earlier) and reduced modulo one **bar**.
  - The old comb filter is kept as the fallback (`detectCombFilter`) when BeatThis is missing, its confidence is below 0.5, or it returns fewer than 2 downbeats. The fallback reports `source=CombFilter, beatsPerBar=0` (beat phase only).
  - New API: `detectFromDownbeats(buf, sr, bpm, beats, downbeats, conf)`, which needs no inference.
  - BeatThis results are cached by audio content (FNV hash of shape plus strided samples, last 4 results). The downbeat job therefore reuses the BPM job's inference: 31-65 ms instead of a second inference.
- **Evidence:**
  - Bar offset recovered within 0.03 ms on all 9 synthetic tracks (lead silence plus a pickup beat).
  - This includes the 70 BPM hip-hop and 85 BPM offbeat tracks, where the model marks 2-beat bars and the tie-break picked beat 1 (strength 0.935 vs 0.000).

### C11 - interleaved complex data fed into a real-only FFT
- **Re-verified:** at `TempoRefiner.cpp:56-64` and `OctaveCorrection.cpp:115-122`.
- **Fix:** both files now call `analysis::spectralFluxEnvelope()`. It puts the real frame in the first N floats and zeroes the rest, as `juce::dsp::FFT` expects. OctaveCorrection also downmixes now; before, it read channel 0 only.
- **Evidence:** the test includes a faithful copy of the old code, so each check shows the bug as well as the fix.

| Check | New | Old |
|---|---|---|
| 6 kHz tone leaking into the 50-4000 Hz band (6k/1k flux ratio) | 0.0000 | 0.988 |
| Impulse position in the peak frame (expected about 1280) | 1088 | 576 |

  Other C11 checks:
  - TempoRefiner, given a 117.5 hint on a 120 BPM track, returns 120.045.
  - The FFT autocorrelation matches the O(n^2) loop (max error 1.3e-8).

### C12 - DSP detectors always ran first; DWT was O(n^2)
- **Fix:**
  - `HybridTempoDetector` runs BeatThis first. When it is valid with confidence >= 0.5, the result is returned and FFT, DWT and octave correction are skipped. They only run as a fallback; `Config::alwaysRunDsp` runs them anyway for diagnostics.
  - In the fallback, DWT and TempoAnalyzer autocorrelation now use an FFT (O(n log n)) on a mono downmix, and the per-window DWT log lines (about 200 per detection) are gone.
  - OctaveCorrection tie-break: a top-scoring T is no longer replaced by T*2/3 or T*3/4. Ties now prefer the simplest ratio. This bug turned 120 into 80 and 128 into 86 in the tests.
- **Timing on the real stem** (`MAQUETA COMPLETA_(Drums)_htdemucs_ft.flac`, 205.8 s, logger silenced, other agents compiling at the same time):

| Step | Before | After |
|---|---|---|
| Hybrid tempo detection | 68.3 s | 16.45 s, of which BeatThis inference is 16.43 s |
| Downbeat detection | 2.6 s / 1.4 s (two runs) | 0.04 s (cache hit) |
| DSP-only fallback | - | 7.2-9.1 s |

  The remaining cost is ONNX inference. Session options for it are C13 territory; I did not change them.

### C16 - BeatThis tempo regression used the array index
- **Fix:** `BeatThisDetector::estimateTempo()` assigns each beat a number, k_i = k_{i-1} + round(IOI / period), so missed beats are bridged. Non-integer IOIs or gaps longer than 8 beats start a new segment. It then does a joint least-squares fit (common slope, per-segment intercept) over 3 passes, rejecting residuals above max(25 ms, 3 MAD).
- **Evidence:** synthetic beat lists with 12% missed beats, a 16-beat break and 50 fps quantisation. The old index-based mean is shown for comparison:

| True BPM | New fit | Old index-based mean |
|---|---|---|
| 70 | 69.9995 | 59.57 |
| 84.86 | 84.8603 | 70.55 |
| 95 | 95.0008 | 77.11 |
| 128 | 128.0005 | 106.91 |
| 174 | 174.0003 | 136.46 |

  - Real stem: 85.000 BPM with confidence 0.99. The old pipeline gave 84.855 after the refiner.
  - TempoRefiner now runs only when the fit has fewer than 16 inliers.
- **Octave errors:** BeatThis returned double time on two synthetic tracks: 70 hip-hop came back as 140, and 85 offbeat as 170.
  - New check `isHalfTimeFeel`, applied only at >= 120 BPM: if the snare (300-5000 Hz flux) sits only on bar position 3, at >= 1.5x positions 2 and 4, the model's beats are 8th notes, so every other beat is kept.
  - Results: 140 -> 70.004 and 170 -> 84.993. DnB 174, rock and offbeat 140 correctly stay put (snares on 2 and 4).

### C17 - transient onsets 12-23 ms early, no zero-crossing snap
- **Fix in `SpectralFluxDetector`:**
  - Flux frame f covers `[f*hop, f*hop+N)`. The attack is searched in `[f*hop - N - 256, f*hop + N)` with `analysis::findAttackSample`.
  - The score is a 1 ms energy step against the maximum window energy over the previous 10 ms. It is computed on both the raw signal and its first difference, and the larger wins, so a low-frequency kick tail cannot fake a step.
  - A flux floor of 3% of the maximum flux and a -80 dBFS region gate were added.
  - Boundaries then snap to the nearest zero crossing (up to 1 ms back, 0.25 ms forward).
- **MicroFade:** with `preRollMs = -1` (auto, the default), the boundary is placed `kMicroFadeMs` (2 ms) before the attack while MicroFade is ON, so the fade-in ends at the attack and the transient plays intact. With MicroFade OFF there is no pre-roll and the zero-crossing snap alone de-clicks the edge.
- `getLastAttackSamples()` exposes the raw attack positions.
- **Evidence:**

| Test | New | Legacy pipeline (reproduced in the test) |
|---|---|---|
| 72 isolated transients over -70 dB noise | max error 0.068 ms, mean 0.011 ms | - |
| Dense 95 BPM loop, 129 hits | 121 matched, 98% within 1 ms, mean +0.06 ms | 119 matched, mean -10.44 ms, 0% within 1 ms |

  Other C17 checks:
  - Every boundary is on a zero crossing when one exists within 1 ms.
  - With MicroFade ON, all boundaries sit 2 ms before the attack.
  - A near-silent stem produces 0 onsets.

### KeyDetector (new, analysis only)
`samplar::KeyDetector::detect(const juce::AudioBuffer<float>&, double sr) -> Result`
- `Result` fields: `tonic` (0 = C .. 11), `mode` (Major/Minor), `confidence`, `correlation`, `secondTonic`, `secondMode`, `secondCorrelation`, `chroma[12]`, `valid`. Helpers: `getName()` (e.g. "A minor") and `getCamelot()` (e.g. "8A").
- Also available: `computeChroma()` and `detectFromChroma()`.
- **Method:**
  - Peak-picked chroma from an STFT of about 186 ms, over 65 Hz - 5 kHz, with parabolic peak frequency, a cents weight and log magnitude. Frames are L1-normalised and silent frames are skipped.
  - Key choice: Pearson correlation against the Krumhansl-Kessler profiles in all 24 keys.
- **Tests:** 8 of 8 synthetic progressions correct (C, G, Eb and B major; A, F#, D and C# minor; r between 0.83 and 0.88). Camelot codes are correct, and silence returns invalid. Runs in about 1 s on a 206 s stem.
- **UI (later):** run it on a worker over the full mix, or over Melody plus Bass (not the drum stem), then publish the key and Camelot code.

## Call-site changes needed elsewhere (not my files)
1. **None required for C10.** `SModeService::runDownbeatDetectionAsync` -> `DownbeatDetector::detect(buf, sr, bpm)` is now BeatThis-first and served from the cache. `Result::beat1Sample` is now modulo one **bar** instead of one beat; the field and its meaning as a grid offset are unchanged.
   - Optional for `SModeService`: also publish `beatsPerBar` and `source` on `smode.downbeat.detected`.
   - Optional for the SModePage BPM job: call `DownbeatDetector::detectFromDownbeats(buf, sr, finalBpm, result.beatTimesSec, result.downbeatTimesSec, result.beatThisConfidence)` directly.
2. **SModePage (audit C10 note):** re-run the bar-based auto-segment once the downbeat offset arrives, if the user has not edited the segments. Today an auto-segment that runs before the result uses offset 0.
3. **SModePage transient auto-segment:** no change needed. Boundaries now include the MicroFade pre-roll automatically. The UI could also expose `Settings::preRollMs` and `snapToZeroCrossing`.
4. **Loop voices** still get no crossfade at loop points (audit C17). That is in `MidiMappingService` / `SequencerAudioSource`, not my scope.

## Limitations / notes
- **DSP fallback** (only used without a confident BeatThis result) is right on 6 of 9 synthetic tracks; the other 3 (70->140, 140->93, 174->87) have the octave/metric ambiguity inherent to density heuristics, so the test reports them as INFO rather than checks.
  - On the real stem the fallback alone gives 113.4 instead of 85, the same 4/3 error FFT and DWT made before.
- **Real-stem downbeats from BeatThis are noisy.** Spacings: 1 beat x27, 2 x58, 3 x3, 4 x22.
  - Votes over the 4 bar positions were 53/19/32/9, a near-tie that the kick/crash tie-break settled 0.64 vs 0.21. Grid offset 0.0885 s, anchored on the downbeat at 14.206 s, which sits on a detected attack 6 ms from the model time. Confidence 0.46.
  - The old comb filter gave beat phase 0.186 s. There is no ground truth for beat 1; it needs an ear check in the app.
- **BeatThis speed:** about 16 s for 206 s of audio under load. The ORT session options are untouched (C13).
- **Small extras:** the ONNX session is now process-wide (weak_ptr shared, freed on the worker thread when the last detector dies) instead of loaded on every BPM job.
- **Leftover:** `kAnalysisBars` in `DownbeatDetector.cpp` was already unused before this change; I left it in place.
