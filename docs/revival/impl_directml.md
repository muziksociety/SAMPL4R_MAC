# SAMPLAR - DirectML GPU stem separation: implementation report (2026-10-21)

Built ONLY in `build_dml/`. No git commit/stash/checkout. `build/` was not touched (only READ for the
smoke host exe and for the baseline installer-size compile).

## Result summary
- Build green: `SamplarPlugin_VST3` and `SeparationProbe` (Release, build_dml). No new warnings in owned files.
  Fixed an LNK4098 (LIBCMT) that the new C target introduced (pffft compiled /MT -> forced /MD).
- Smoke host (build/VST3SmokeHost.exe against build_dml bundle): `--show-ms 2500` ALL PASS, `--two-instances` ALL PASS
  (run twice: after the first build and after the final cancel patch).
- GPU (GTX 970M, DirectML) 7.6x faster than CPU on the reference song; stems equal to the old model at 111-134 dB (CPU)
  and 83-90 dB (GPU).

## Measured (SeparationProbe, SAMPLAR_SEPARATION_SHIFT=0, cache bypassed)
Reference song `MAQUETA COMPLETA.wav` (205.76 s, the audit-baseline song). NOTE: the other agent was compiling during some
runs, and CPU runs now use physical-1 = 3 threads at below-normal priority with spinning off (by design, C13), so CPU
absolute times are higher than the audit's 260.7 s (4 spinning threads, normal priority).

| run | inference | RTF | wall | backend |
|---|---|---|---|---|
| legacy htdemucs.onnx, CPU | 459.5 s | 2.23 | 463.8 s | CPU |
| htdemucs_body.onnx, CPU | 328.1 s (model 320.9, istft 5.5) | 1.60 | 336.5 s | CPU |
| htdemucs_body.onnx, GPU | 43.1 s (model 36.9, istft 5.0) | 0.21 | 54.6 s | DirectML (NVIDIA GeForce GTX 970M) |

Per-stem RMS identical to 5 decimals in all three runs: bass 0.08233, drums 0.11177, other 0.04651, vocals 0.10912.

| compare | bass | drums | other | vocals | max abs diff |
|---|---|---|---|---|---|
| body CPU vs legacy CPU (SNR) | 134.1 dB | 121.1 dB | 110.9 dB | 114.2 dB | 2.3e-6 |
| body GPU vs body CPU (SNR) | 83.7 dB | 87.1 dB | 82.8 dB | 90.1 dB | 3.4e-4 |

Short clip `tests/test_input.wav` (2 s, nearly silent stems): body CPU vs legacy 77-116 dB, GPU vs CPU 37-64 dB (relative
error on stems with RMS ~1e-4; not meaningful), ALL PASS. Stems are 32-bit float WAV (verified: bits=32 float).

Cancel latency (`--cancel-after`):
| when | CPU | GPU |
|---|---|---|
| mid-chunk (15 s / 12 s) | 45 ms total (Run terminated 6 ms after request via RunOptions::SetTerminate) | 711 ms |
| at 2 s (during model load, not interruptible) | 1.48 s (was 6.0 s before the extra cancel checks) | 2.50 s |
No stem files left behind in any cancel case.

Bounded memory, 10.4-min file (3x the reference song, 27.2 M samples), each in its own process:
- GPU: peak working set 569 MB, 133.8 s inference (RTF 0.217), ALL PASS.
- CPU: peak working set 1965 MB (ORT CPU arena ~1.5 GB + 208 MB padded input), 1048 s (RTF 1.70), ALL PASS.
  For comparison the 3.4-min song on CPU peaks ~1.5-1.8 GB: peak is now ~flat in song length. The old code
  allocated ~3.6 GB of buffers alone for 10 min (audit C3).
Duration guard: 20 min max, clear message before any allocation.

Stem cache (`--cache`, test_input.wav, user setting -> GPU): pass 1 MISS -> separated 9.84 s -> committed
(`%APPDATA%/Samplar/stem_cache_onnx/<key>/` with 4 WAVs + entry.json); pass 2 HIT in 0.00 s. Test entry deleted
afterwards. The legacy `%APPDATA%/Samplar/stem_cache` (544 MB) was not read, written or deleted.

Installer size (same samplar.iss, ISCC lzma2/ultra64): lead's current build/ bundle 281.6 MB -> build_dml bundle
243.2 MB = -38.4 MB (-13.6 %). Uncompressed bundle: -68.6 MB model, +2.75 MB onnxruntime.dll, +18.5 MB DirectML.dll.
ISPP guards verified both ways: compile passes on build_dml, aborts on build/ ("Bundle is missing ...DirectML.dll").
build_installer.ps1 preflight verified on build_dml (all sha256 OK).

## Files changed / added
Changed:
- `src/services/separation/OnnxSeparationProvider.{h,cpp}` - SM5 453e7733 port (body model + host iSTFT, DirectML with
  CPU fallback, GPU session released per run, per-chunk GPU->CPU retry) plus: process-wide GPU kill flag; ORT version/
  providers/dll path log; `allowGpu` ctor arg; streamed overlap-add writing 32-bit float WAVs (no full-length
  accumulators, input decoded block-wise straight into the padded buffer); try/catch bad_alloc; 20-min guard;
  CancelWatcher thread -> RunOptions::SetTerminate; cancel checks during decode; spinning off, intra=physical-1,
  ORT pool threads + worker below-normal priority (custom ORT thread fns); thread_local RNG; SAMPLAR_SEPARATION_DEVICE /
  SAMPLAR_SEPARATION_SHIFT; static `describeNextBackend()`, `lastRunBackend()`, `stemFileName()`.
- `src/services/FullSongService.{h,cpp}` - removed dead `process()`, `processDrumStem()`, `publishError()`,
  `loadAudioFile()/saveAudioFile()`, `onVizProgress` (grep: only FullSongService itself used them). Added body-first
  model resolution across all candidates, persistent stem cache (key = lowercased full path + size + mtime + model file
  + format tag; partial dir + atomic rename; entry valid only with entry.json + 4 exact files; 10 GB LRU cap; stale
  `.partial-*` > 6 h removed), process-wide separation gate (try_lock + cancel), Config `deviceOverride`,
  `useStemCache`, `outputDirOverride`; Result `stems` (exact paths), `backend`, `fromCache`, `cancelled`; no exception
  escapes.
- `src/services/smode/SModeSeparationService.{h,cpp}` - uses `result.stems` in Vocals/Bass/Drums/Melody order (C14,
  `findStemOutputFiles` substring scan removed); worker body wrapped in try/catch; `alive_`, join-on-shutdown and the
  `shouldCancel` wiring kept intact.
- `src/utils/SamplarSettings.h` - `useGpuSeparation` (bool, default true).
- `src/ui/Components/SettingsDialog.{h,cpp}` - Audio tab: new "STEM SEPARATION" section (same style as ANALYSIS):
  toggle "Use GPU (DirectML) for stem separation" + read-only label "Backend: GPU: NVIDIA GeForce GTX 970M (DirectML)"
  / "CPU (...reason...)" + "| Last run: ...". Dialog height 550 -> 650. NOT visually verified (no screenshot run).
- `CMakeLists.txt` - ORT moved out of `WITH_BEAT_THIS`, one imported `onnxruntime` target (DirectML 1.19.2 from
  `third_party/onnxruntime-directml`, CPU 1.19.2 zip fallback, sha256 warning, `SAMPLAR_HAS_DIRECTML=1`); option
  `SAMPLAR_ORT_DIRECTML`; `samplar_pffft` static lib (/MD forced); new sources; beat_this_engine links `onnxruntime`;
  plugin/app link onnxruntime + samplar_pffft; `/DELAYLOAD:d3d12.dll`; staging script: ORT/DirectML from configure-time
  paths (no more build/_deps glob), exactly one separation model (stale one deleted), providers_shared removed,
  licenses to `Contents/Resources/licenses/`; `SeparationProbe` EXCLUDE_FROM_ALL target.
- `release_assets/installer/samplar.iss` - layout comment, ISPP `#error` guards, `[InstallDelete]` (htdemucs.onnx,
  providers_shared, tools/, BACKGROUNDS/, Samples/, data/*.json in both data dirs; data/ itself kept).
- `release_assets/build_installer.ps1` - hard-fail bundle preflight (sha256 of ORT/DirectML/body model, required +
  forbidden files).
- `.claude/skills/samplar-release/SKILL.md` - model/runtime table with hashes, sourcing, checks, smoke-test lines.
Added:
- `src/services/separation/GpuAdapterProbe.{h,cpp}` (+ loadedModulePath, thread priority, working-set helpers),
  `src/services/separation/HtdemucsIstft.{h,cpp}`, `src/utils/Fft.h` (SM5, namespace samplar, ASCII).
- `third_party/pffft/**` (verbatim SM5), `third_party/onnxruntime-directml/**` (verbatim sm5-library-p0; hashes match
  the plan), `models/htdemucs_body.onnx` (git-ignored).
- `tools/separation_probe/SeparationProbe.cpp`, `tools/fetch_onnxruntime_directml.py`, `tools/separation/split_htdemucs.py`.
Not changed: `src/utils/OnnxDelayLoad.cpp` (no change needed; DirectML is preloaded by full path at runtime),
`BeatThisDetector.*` (no source change; now links the same 1.19.2 DirectML ORT, stays on CPU EP).

## Left undone / notes
- HtdemucsIstft unit test (SM5 tests/unit) not ported; correctness proven end-to-end by the probe (111-134 dB vs old model).
- tools/release_linter not updated (check_runtime_deps may flag dxcore.dll / DirectML.dll; it still references installer_sm5.iss).
- No DAW/installer-upgrade test, no iGPU-only/VM run, Settings dialog not screenshotted.
- Model load (~3.5 s) cannot be interrupted; worst-case cancel latency is the remaining load time.
- The other agent's build in parallel inflated CPU timings; GPU numbers are robust.

## Changes needed in files I do not own
1. `.gitignore` (lead): add `build_dml/` and `third_party/onnxruntime-directml/` (its headers are currently trackable);
   add exceptions so the new tools can be committed: `!tools/separation_probe/`, `!tools/separation_probe/*.cpp`,
   `!tools/fetch_onnxruntime_directml.py`, `!tools/separation/`, `!tools/separation/*.py`. Commit `third_party/pffft/`.
2. Lead's `build/`: just re-configure/build; the next configure picks the DirectML ORT automatically and the staging
   script deletes `models/htdemucs.onnx` from the bundle. Until then `samplar.iss` intentionally refuses to compile.
3. UI cancel (audit C2 UI half, `src/ui/MainWindow.cpp` ~483-494 / `src/ui/Pages/SModePage.cpp`): instead of refusing a
   new drop "Please wait for it to finish", call `svc->getSeparation().cancelSeparation()` (SModeSeparationService) and
   start the new song after `smode.separation.error` "Cancelled" arrives; optionally show a Cancel button on the loading
   lanes. The backend now aborts within ~50 ms (CPU) / ~0.7 s (GPU).
4. Optional (`SModeService.cpp`, C15): stems are now persistent cache paths and 32-bit float; project state
   (`SamplarAudioProcessor` smode binding) can keep storing these paths - they survive temp cleanup. A 10-min song's
   stems decoded by SModeService are still ~847 MB in RAM (unchanged, outside scope).
