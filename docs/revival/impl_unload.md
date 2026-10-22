# Slow last-instance unload under load (open item 1 of impl_round2_fixes.md)

## Result
- **Root cause:** JUCE's own Direct2D/D3D11 teardown. It runs on the host's message thread when the last `ScopedJuceInitialiser_GUI` goes away. It is not SAMPLAR's destructor, ORT/DirectML, the logger, FontWarmer, or module unload.
- **Fix:** `src/plugin/GpuTeardownOffload.{h,cpp}`. The last DirectX reference is released on a detached thread that pins the module.
- **Repro rate before the fix:** 100%.
  - 6 busy procs: 6/6 over budget, B delete median 8.5 s, max 10.4 s.
  - 8 busy procs: 10/10 over, median 20.6 s.
  - 16 busy procs: 5/5 over (37-57 s) plus 2 hangs.
- **After the fix, 6 busy procs (gate-like load, about 2.5x smoke wall time), 32 runs:** 0 unload budget failures.
  - **B (last instance):** median 77 ms, p95 485 ms, max 897 ms.
  - **A (first instance):** median 42 ms, p95 697 ms, max 1038 ms.

## Reproduction harness
- **`scratchpad/unload_loader.py <runs> <busyProcs> <outdir> [--stacks]`**
  - Starts N Python busy processes at normal priority (8 logical CPUs on this machine).
  - Runs `VST3SmokeHost --two-instances` repeatedly.
  - Parses each step's timing and copies each run's plugin log.
  - Prints median/p95/max and the over-budget count.
- **`--stacks` (new, opt-in, in the smoke host):** `tools/vst3_smoke_host/StackSampler.h`.
  - A TIME_CRITICAL thread suspends the main thread every 5 ms during each instance delete.
  - It unwinds with RtlLookupFunctionEntry/RtlVirtualUnwind, which does no allocation while the thread is suspended.
  - Aggregated stacks are printed after the step has been timed. Plugin frames are printed as SAMPLAR+rva so they survive the unload.
  - Every other thread is dumped if a step runs past 3 s.
  - `profagg.py` and `profsum.py` resolve the frames through `SAMPLAR.map`.
- Off by default, so the gate behaves exactly as before.

## Timeline evidence (before the fix: `unload_stacks1/2`, 16 busy procs)
- The main thread's CPU time stayed flat (2546.9 ms) for 50 s while it sat in the kernel: it was blocked, not starved. Every sample showed this stack:
```
win32u!NtGdiDdDDIDestroyContext / NtGdiDdDDIDestroyAllocation2   (nvwgf2umx = NVIDIA, igd10um64gen11 = Intel iGPU)
d3d11 ... device release
SAMPLAR juce::DxgiAdapter::~DxgiAdapter          (scalar deleting dtor)
SAMPLAR juce::DxgiAdapters::releaseAdapters / ~DxgiAdapters
SAMPLAR std::_Ref_count<juce::DirectX>::_Destroy  (last SharedResourcePointer<DirectX>)
SAMPLAR juce::VBlankDispatcher::~VBlankDispatcher (DeletedAtShutdown singleton)
SAMPLAR juce::DeletedAtShutdown::deleteAll
SAMPLAR juce::ScopedJuceInitialiser_GUI::~ScopedJuceInitialiser_GUI
VST3SmokeHost juce::VST3PluginInstance::~VST3PluginInstance (pluginB.reset())
```
- The wrapper's `ScopedJuceInitialiser_GUI` member is destroyed after `~SamplarAudioProcessor`. This is why the destructor measured 13-41 ms while the delete took seconds.
- The D3D11/D2D device release (one device per GPU, two GPUs here) blocks in the GPU kernel driver, and the block grows sharply with CPU load: about 100 ms idle, 8-10 s at 6 procs, 20 s at 8, 57 s at 16.
- Instance A never triggers this path, because B keeps JUCE initialised.
- The ~4 s figure in round 2 matching JUCE's stopThread timeout was a coincidence.

## Fix (`GpuTeardownOffload`)
- **`registerHolder()`, first line of every processor constructor:**
  - Creates one `juce::DeletedAtShutdown` holder per JUCE lifetime.
  - It is created before any editor or VBlankDispatcher exists. `deleteAll` runs in reverse creation order, so the holder is deleted after VBlankDispatcher.
- **`retainGpuState()` in the editor constructor:**
  - The holder keeps a 1x1 `NativeImageType` image, i.e. a `Direct2DPixelData` holding a `SharedResourcePointer<DirectX>` (log shows type 1 = native).
  - JUCE creates DirectX for every editor window anyway (VBlank and the D2D context), so this adds nothing.
- **`~Holder` (message thread, inside deleteAll):**
  - Pins the module (`GetModuleHandleEx FROM_ADDRESS`).
  - Moves the image into a detached thread. That thread drops it, which runs the last `~DirectX` and the D3D11/D2D release, then calls `FreeLibraryAndExitThread`.
  - The thread never logs and touches only the image/DirectX it owns.
  - If the pin or the thread cannot be created, it falls back to an inline release, which is the old behaviour.
- **Safety:**
  - No module code runs after unmap.
  - The objects involved:
    - The D2D factory is `MULTI_THREADED`.
    - D3D11 device `Release` is free-threaded.
    - The adapter listener list is a `ThreadSafeListenerList`.
    - The image is never drawn.
  - A new instance created during the release gets a fresh DirectX through the `SharedResourcePointer` weak slot.
  - Residual, the same one accepted for S-M3: if the host drops the last module reference, `DLL_PROCESS_DETACH` runs on that thread after the release. Process exit simply kills the thread.

## Results (all with the fixed build)
| Check | Result |
|---|---|
| 32x `--two-instances`, 6 busy procs (`unload_after6/`) | 0/32 unload budget failures. B: median 77, p95 485, max 897 ms. A: median 42, p95 697, max 1038 ms |
| 32x, 8 busy procs = 100% CPU saturation (`unload_after8/`) | B median 74 ms (was 20.6 s), p95 1754, max 2222 ms. **3/32 over** (see residual) |
| `run_all_checks.ps1 -BuildDir build` | **7/7 PASS** (`build/qa_logs/20261021_185953`): 690 unit checks, 2-instance x3, ui-sweep, pluginval 10, linter clean |
| EngineTests | ALL PASS, 0 failures (`unl_engine.log`) |
| AnalysisTests | First run 91/92: the S-M3 BeatThis cancel latency was 221.6 ms against a 200 ms budget, a timing flake while another agent was active. 2 reruns 92/92 (98.6 and 114.5 ms) (`unl_analysis{,1,2}.log`) |
| `--session-roundtrip tests/test_input.wav` | ALL PASS, 39 PASS lines |
| `--bpm-unload` x3 | PASS: 385, 373 and 413 ms |

## Residual (not fixed, evidence only)
At 8 busy procs the remaining slow deletes, for A as well as B, are inside SAMPLAR's own `shutdownServices`. Aggregated profile (`unload_prof8b`, `profagg.py`):
- **MidiMappingService (about 42% of slow time):** freeing the SignalsmithStretch per-voice FFT buffers (`ZwFreeVirtualMemory`), plus the `~ThreadPool` join (about 10%).
- **SModeService:** worker-pool join (about 17%).
- **juce::FileLogger:** opens and closes the log file for every line. `CloseHandle` costs 10-200 ms under load, about 28% of slow time across SModeService and the processor destructor.

JUCE's `ThreadPool::stopThreads` uses `stopThread(500)`. Under severe load that can reach `killThread`, which is a latent hazard. It was not observed in the logs.

Possible follow-ups:
- Keep the shared log file open, which also helps UI latency.
- Destroy idle, drained pools on a pinned reaper thread.
- Keep the stretchers' memory until module unload.

Also observed and unrelated: "editor reopen #2" exceeds its 500 ms budget under load (3/32 at 6 procs).

Licensing files were not touched.

## Files
- **New:** `src/plugin/GpuTeardownOffload.h`, `src/plugin/GpuTeardownOffload.cpp`, `tools/vst3_smoke_host/StackSampler.h`.
- **Edited:**
  - `src/plugin/SamplarAudioProcessor.cpp`: include plus `registerHolder()`. CRLF/UTF-8 preserved.
  - `src/plugin/SamplarAudioProcessorEditor.cpp`: include plus `retainGpuState()`.
  - `CMakeLists.txt`: plugin `target_sources`.
  - `tools/vst3_smoke_host/VST3SmokeHost.cpp`: `--stacks`, `SampledStep`, `report()`.
- **Rollback:** remove the two calls, the CMake line and the two new files.
