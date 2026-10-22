# impl_sep2 - separation / analysis hardening

## CANCEL API (final - code against this)

New header-only file `src/services/analysis/AnalysisCancel.h` (already in every target via cmake/extra_analysis.cmake):

```cpp
#include <atomic>
namespace samplar {
struct AnalysisCancel
{
    const std::atomic<bool>* flag = nullptr;   // nullptr = never cancelled
    bool cancelValue = true;                   // *flag == cancelValue means "cancel"

    AnalysisCancel() noexcept = default;
    AnalysisCancel (const std::atomic<bool>* cancelWhenTrue) noexcept;            // implicit: pass a "cancel" flag
    static AnalysisCancel whenFalse (const std::atomic<bool>* aliveFlag) noexcept; // pass an "alive" flag
    bool requested() const noexcept;
};
}
```

Entry points. Existing call sites compile unchanged because the new argument is defaulted:

```cpp
BeatThisDetector::Result    BeatThisDetector::detect (const juce::AudioBuffer<float>&, double sampleRate,
                                                      AnalysisCancel cancel = {});
HybridTempoDetector::Result HybridTempoDetector::detect (const juce::AudioBuffer<float>&, double sampleRate,
                                                         const Config& config = Config(), AnalysisCancel cancel = {});
HybridTempoDetector::Result HybridTempoDetector::detect (const juce::File&, const Config& config = Config(),
                                                         AnalysisCancel cancel = {});
DownbeatDetector::Result    DownbeatDetector::detect (const juce::AudioBuffer<float>&, double sampleRate, double bpm,
                                                      AnalysisCancel cancel = {});
```

- Each Result gains `bool cancelled = false;`. A cancelled result has `valid == false` (Hybrid: `bpm == 0`). Do not apply it.
- No DSP or comb-filter fallback runs after a cancel.
- The flag must outlive the call: capture the owning shared_ptr in the job lambda.

### Call-site changes (owned by others)

- **`SModeService.cpp` ~:222 (runDownbeatDetectionAsync).** The lambda already captures `alive = alive_`.
  - `auto result = DownbeatDetector::detect(*buf, sampleRate, bpm, AnalysisCancel::whenFalse(alive.get()));`
  - `if (result.cancelled) return;` before the callAsync.
- **`SModePage.cpp` ~:3210 (BPM job).** It needs the processor-owned alive flag.
  - SModeService exposes it, e.g. `std::shared_ptr<std::atomic<bool>> aliveFlag() const { return alive_; }`.
  - Capture it in the job and call `detector.detect(buf, sampleRate, cfg, samplar::AnalysisCancel::whenFalse(alive.get()))`.
  - Skip applying the result when `result.cancelled`.
- **`SModeService::shutdown`.** No change needed: `alive_->store(false)` already runs before `workers_->removeAllJobs(...)`. Running jobs now return within 200 ms, so the 20 s drain wait (A4 / U-H1) can drop to about 2 s.

## Results

| Check | Result |
|---|---|
| AnalysisTests, full (53 old checks + 31 new) | 84 checks; the only failure (anti-alias) was a real bug, now fixed. `--sep2-only --quick` after the fix: 30/30 PASS (`sep2_tests1.log`, `sep2_tests2.log`) |
| BeatThis own pipeline vs vendored `BeatThis::process_audio` (75 s) | beats (150), downbeats (75) and beat counts bit-identical |
| Cancel on a 10-min signal, BeatThis at 60 / 400 / 1500 / 5000 ms | 90 / 170 / 72 / 181 ms (budget 200) |
| Cancel on a 10-min signal, other detectors | Hybrid (alive-flag polarity) 182 ms; Downbeat 75 ms; while waiting for another inference 69 ms |
| Real stem (MAQUETA drums) | still 85.000 BPM, downbeat offset 0.0885 s (unchanged) |
| Separation: NaN/Inf input, 191-char song name | finite stems; stem path 146 chars; committed |
| Cache validation | HIT validated. Truncated stem -> `validateCachedStemFile` false, then MISS -> re-separated -> HIT. Source mtime changed -> invalid |
| Out-of-phase input (L = -R) | finite stems (was Inf) |
| 48 kHz input, 23 kHz tone | 21.1 kHz alias vs 1 kHz: -11.6 dB before the fix, -115.4 dB after |
| 3 processes, same song, empty cache | 1 separation + 2 validated hits; every worker's stems intact after 4 s; 1 entry, no leftovers |
| 1 MB cap, 2 processes, 2 songs + a 2 h old entry | old entry evicted; both fresh entries kept (in use); stems intact |
| 2 concurrent SeparationProbe processes, `--cache`, same song (GPU + CPU) | both ALL PASS; one entry; probe B waited on the cross-process gate and then hit |
| SeparationProbe CPU vs GPU, MAQUETA 205 s (`probe_song_sep2.log`) | ALL PASS. GPU vs CPU SNR bass 83.7 / drums 87.1 / other 82.8 / vocals 90.1 dB, the same as the impl_directml baseline. Per-stem RMS (bass 0.08233, drums 0.11177, other 0.04651, vocals 0.10912) is unchanged. GPU wall 52.9 s, CPU 316 s |
| Smoke host, bundle COPY with onnxruntime.dll renamed | `--show-ms 2500` ALL PASS: instantiate, audio, editor x2, state, unload 69 ms |
| Smoke host, intact copy | `--show-ms 2500 --two-instances` ALL PASS |
| Baseline: lead's `build/` bundle copy without the DLL | FAIL instantiate ("Unable to load VST-3 plug-in file"), exit 2 |
| `AnalysisTests --ort-missing-check` from a copy without the DLL | 6/6 PASS. ensureOrtApi false; BeatThis invalid quickly; Hybrid falls back to DSP (120.05); separation returns `runtimeUnavailable` + "Please reinstall SAMPLAR."; describeNextBackend returns "Unavailable (...)" |

On this machine System32 has the 2.8 KB WinML forwarder `onnxruntime.dll`, so the missing-DLL test hit the realistic "wrong DLL" path. The failure hook now resolves it to the safe stub, and no exception is raised.

## What changed (all files in my ownership)

### 1. Cancellable BeatThis / Hybrid / Downbeat (HIGH, U-H1 / A4)

`BeatThisDetector.cpp` now owns the ORT session and the chunk loop. The vendored `InferenceProcessor` runs with `RunOptions{nullptr}`, which cannot be terminated, and `third_party` is not mine. It reuses the vendored `MelSpectrogram` and `Postprocessor`.

- **Mel spectrogram:** computed in frame-exact slices of 1000 frames (20 s), with a cancel check between slices. Proven bit-identical.
- **ONNX chunks:** one `RunOptions` per detect. A `TerminateWatcher` thread (condition variable, 10 ms poll) calls `SetTerminate()`. There is a cancel check before every chunk, and after resample and post-processing.
- **Shared session:** waiting for the shared session is a cancellable `try_lock` loop.
- **Cache re-check after waiting (U-M9):** the cache is checked again once the lock is acquired, so the BPM job and the downbeat job pay for one inference.
- **Failure handling (U-M9):** a missing model stays a sticky failure. A session-init error is retried after 60 s instead of being permanent.
- **Hybrid and Downbeat:** the token is threaded through; cancel is checked between every stage.
- **Remaining uninterruptible step:** the half-time check after inference (spectral flux, about 0.1-0.2 s on 10 min). The measured worst case is still under 200 ms.

### 2. Stem cache across processes (HIGH, S-H1, plus S-M4 / S-M5)

New `src/services/separation/StemCacheLock.{h,cpp}` (Win32 only):
- `CrossProcessFileLock`: LockFileEx, released automatically if the process dies.
- `LeaseFile`: DELETE_ON_CLOSE without share-delete. A delete attempt by another process shows whether the holder is alive.
- `toLongPathIfNeeded`: adds the `\\?\` prefix for long paths.

`FullSongService`:
- **Gates:**
  - `.separation.lock` is a cross-process gate around model load and separation, polled and cancellable. It also stops two DAWs fighting over VRAM. The in-process mutex is kept.
  - `.cache.lock` covers lookup with LRU touch, commit and prune.
- **Commit never replaces a valid entry.** If another process committed first, our partial is dropped and theirs is used. An invalid entry is renamed to `.evict-*` and then deleted, so readers never see half-deleted directories.
- **Prune:**
  - skips entries used in the last 30 min and the entry just written;
  - skips `.partial-*` dirs whose writer lease is alive; a dead partial is removed after 10 min (was 6 h);
  - cleans `.evict-*` leftovers.
- **Hit validation:** entry.json must match source path, size, mtime, model and format, and all 4 stems must match the `stemBytes` recorded at commit. Format bumped to `f32wav-v2`, so old dev entries miss once.
- **MAX_PATH (S-M4):** the song name in stem file names is cut to 60 characters, with trailing dots and spaces trimmed.
- **New statics:**
  - `isSeparationRuntimeAvailable()`
  - `validateCachedStemFile(file)`, for stem paths restored from project state; it also touches the entry
- **New Result field:** `runtimeUnavailable`.
- **Test/tool env vars:** `SAMPLAR_STEM_CACHE_DIR` (root) and `SAMPLAR_STEM_CACHE_MAX_MB` (cap).

### 3. NaN / Inf (MEDIUM, S-M1) in `OnnxSeparationProvider.cpp`

- Non-finite input samples are zeroed and logged.
- **refStd guard:** if the mid std is below 0.1 x the stereo RMS (out-of-phase material), the stereo RMS is used instead, with a floor of 1e-4, which also catches NaN. Normal stereo is unchanged, matching the reference normalisation.
- Any non-finite output sample fails the run before anything is written, so it can never be committed.

### 4. Missing onnxruntime.dll (HIGH, pre-existing S-H2)

- **Lazy, guarded ORT binding:**
  - `ORT_API_MANUAL_INIT` is set on the imported `onnxruntime` target's INTERFACE in `cmake/extra_analysis.cmake`, so every consumer gets the same definition (ODR on `Ort::Global<void>::api_`).
  - New `src/services/separation/OrtRuntime.{h,cpp}`: `ensureOrtApi()` binds the API once, inside `__try/__except` for 0xC06D007E/7F, with null and version checks. `#error` fires if the define is missing.
  - Other helpers: `ortUnavailableReason()` and `ortUnavailableUserMessage()`.
  - The separation provider ctor, `describeNextBackend` and BeatThis `acquireEngine` call `ensureOrtApi()` first.
- **`OnnxDelayLoad.cpp` failure hook:** when `OrtGetApiBase` cannot be resolved, it returns a stub that yields a null API base.
  - The import is by ORDINAL 1, its only ORT import (checked with dumpbin).
  - A DLL that cannot be loaded at all is caught by the SEH guard.
  - Trace lines were added (opt-in via `SAMPLAR_DLL_LOAD_TRACE=1`).

### 5. Other MEDIUMs

- **S-H1 (GPU part):** a GPU Run failure that looks like out-of-memory now falls back to CPU for the rest of that run only. The fallback session is dropped so the next run retries the GPU. Other GPU failures still disable it for the process.
- **S-M6:** resampling above 44.1 kHz now streams sequential blocks through an elliptic anti-alias low-pass:
  - 4 biquad sections at 48 kHz, passband to about 20.6 kHz, -90 dB at 22.05 kHz;
  - design failure is refused rather than silently skipped;
  - upsampling maths is unchanged.

### Build and tests

- `cmake/extra_analysis.cmake`:
  - OrtRuntime and StemCacheLock added to Plugin, App, SeparationProbe and AnalysisTests;
  - AnalysisTests now also compiles the separation sources, `OnnxDelayLoad.cpp` and the new `src/services/analysis/tests/SepHardeningTests.cpp`, and is delay-linked so a copy without the DLL starts.
- New AnalysisTests modes:
  - `--sep2-only`
  - `--cache-worker <song> <cacheDir> <id>` (internal)
  - `--ort-missing-check`

## Not done / for others

- **UI "separation unavailable - reinstall" (UI owner).**
  - The error already flows through `smode.separation.error` with the text "Stem separation unavailable - ... Please reinstall SAMPLAR."
  - For a proactive state, SModePage or MainWindow can check `FullSongService::isSeparationRuntimeAvailable()` when a song is dropped and disable the drop zone with that message.
  - The SettingsDialog backend label already shows "Unavailable (ONNX Runtime missing - reinstall SAMPLAR)".
- **SModeSeparationService (S-M2):** assign `stemFiles_` inside the completion `callAsync`, not on the worker. Optionally branch on `result.runtimeUnavailable` for a distinct UI state.
- **Persistence / processor restore (S-M5 second half):** before reloading saved stem paths, call `FullSongService::validateCachedStemFile(path)`. If it returns false, re-separate instead of loading stale stems.
- **Not fixed (S-M3):** ORT session creation (3-5 s, or longer for a DirectML compile) still cannot be interrupted, so unload during model load waits for it.
- **Long source paths:** juce::File may still fail to open a source path over 260 characters if the host is not long-path aware. The cache side is short now.
