# SAMPLAR Hardening Roadmap

**Date:** 2026-04-22
**Source:** VaultBoy research notes at `B:\SANTI\VAULTBOY\vault` (ideas/, wiki/, mind/)
**Status:** planning — execute tasks one-by-one, gate each on the previous landing cleanly

Every item has: **description**, **why net positive**, **risks / caveats**, **vault reference**, and an estimate. Check off as we land them.

---

## Phase A — Tempo Detection Speedups (safe, fast ROI)

Tempo drives: beat grid display, beat-aligned slicing, S-Mode segmentation, drum pattern extraction, step sequencer sync, loop detection. **Wrong BPM cascades — must be conservative.**

### [x] A1. Content-hash BPM cache (safest, biggest perceived win) — shipped 2026-04-22
**Description**: SHA-256 hash of the first 64 KB + filesize of each sample → key a JSON cache at `%APPDATA%/Samplar/tempo_cache/<hash>.json` that stores `{ bpm, confidence, detectedAt }`. On analyze, lookup first; run detection only on miss.
**Why net positive**: identical file bytes → identical BPM. Repeat loads of any sample (the common case) = instant. Zero accuracy risk because cached value == value that would be freshly computed.
**Risks**: none material. If user wants re-detection, a "Re-analyze BPM" menu item forces recompute.
**Vault refs**: aligns with `2026-04-07-flac-based-sample-library-compression` caching philosophy.
**Estimate**: 2-3 h.

### [ ] A2. Early-exit on detector consensus
**Description**: `HybridTempoDetector` currently runs DWT + STFT/ODF + LTE. If the first two agree within a tight tolerance (±0.1 BPM), skip the third. If they disagree, run the third as tiebreaker.
**Why net positive**: when detectors agree, the third can't improve accuracy — it just burns time. Tight tolerance protects against false agreement. ~30-50% wall-clock reduction on confidently-detectable tracks.
**Risks**: if existing agreement tolerance is loose (several BPM), early-exit could commit to wrong value downstream (wrong slices). **Must audit the current consensus threshold before enabling.**
**Vault refs**: `2026-04-07-dual-engine-tempo-detection-with-consensus-voting.md`.
**Estimate**: 1-2 h + audit time.

### [ ] A3. Middle-of-file analysis window
**Description**: Rather than analyzing the full audio buffer, sample ~30 s from 25-55% file length. Avoids intros/outros which may have different tempo than the main groove.
**Why net positive**: most tempo detection engines are O(N) or O(N log N) in audio length; clipping to 30 s = linearly fewer cycles. LTE is designed for short loops and actively benefits. Middle-sample avoids false reads on intro fade-ins.
**Risks**: songs with genuine mid-song tempo changes could miss the section you wanted. Minor — no user asked for this currently. Also: tracks < 60 s would use full length (skip the windowing).
**Vault refs**: none directly; common MIR practice.
**Estimate**: 1 h.

### [ ] A4. Audit current consensus tolerance (pre-req for A2)
**Description**: Read `HybridTempoDetector.cpp` and document the exact tolerance the consensus voting uses. Confirm it's tight enough that "agreement" really means "same tempo", not "similar family".
**Why net positive**: unblocks A2 safely. Documents what we actually have.
**Risks**: none — read-only.
**Estimate**: 30 min.

### [ ] A5. Decimate to 22 kHz before detection
**Description**: Anti-alias filter + 2× downsample before feeding the FFT / autocorr / wavelet stages. 22 kHz preserves everything ≤ 11 kHz (well above hi-hat sparkle at 6-10 kHz).
**Why net positive**: FFT cost is O(N log N) → 2× smaller input ≈ 2-2.5× faster. DWT is O(N). No accuracy loss because no meaningful tempo info lives above 11 kHz in real music.
**Risks**: DWT bands above 5.5 kHz that currently carry cymbal onsets would be lost at 11 kHz — that's why we stop at 22 kHz, not 11. Needs a well-designed antialias filter.
**Vault refs**: `2026-04-06-simd-optimized-sample-interpolation-for-sample-monsta`.
**Estimate**: 2-3 h (filter design + test).

### [ ] A6. Parallelize detectors with shared thread pool
**Description**: Run DWT, STFT/ODF, LTE concurrently on a `juce::ThreadPool` sized to `SystemStats::getNumCpus() - 2`. Wait for all, then consensus-vote.
**Why net positive**: on 4+ core machines (≈95% of studio users), wall-clock roughly `max(t_DWT, t_STFT, t_LTE)` instead of `sum`. 2-3× speedup typical.
**Risks**: GeneratorService has 4 workers + audio thread + message thread. Dedicated extra threads would thrash dual-cores. Hence the `-2` sizing.
**Vault refs**: `2026-04-08-sample-monsta-lock-free-audio-graph-and-simd-kernels`, `2026-04-11-realtime-audio-lock-free-sample-monsta`.
**Estimate**: 3-4 h.

### [ ] A7. BTrack-style clean-room quick first-guess (UX only)
**Description**: Implement (clean-room, no BTrack code) a causal comb-filter + autocorrelation beat tracker that yields a first-guess BPM in < 1 s of audio. Display it in UI immediately; full pipeline refines in background.
**Why net positive**: perceived speed — user sees a number within 1 second instead of staring at a spinner.
**Risks**: TOTAL CPU work increases slightly (we now run 4 detectors, not 3). First-guess MUST NOT feed beat-aligned slicing — slicing waits for consensus. Add a "provisional BPM" state in the model to prevent this.
**Vault refs**: `2026-04-13-btrack-as-secondary-beat-detector-for-Samplar-tempoana.md` (algorithm reference — license precludes shipping BTrack itself).
**Estimate**: 1-2 days (non-trivial DSP work + careful integration).

---

## Phase B — Measurement Infrastructure (prerequisites)

### [ ] B1. Perfetto profiling integration
**Description**: Instrument SAMPLAR with Google Perfetto tracing. Mark service init, DSP calls, thread hand-offs, file I/O with trace events. Output `.pftrace` files viewable in the Perfetto UI.
**Why net positive**: every subsequent perf decision becomes data-driven. Without it, "optimize the FFT" might be premature if the real bottleneck is disk I/O or mutex contention. This is the *meta* hardening — it makes everything else correct.
**Risks**: tracing overhead must be compiled out in release. Needs a preprocessor guard like `SAMPLAR_ENABLE_TRACING`.
**Vault refs**: `2026-04-06-perfetto-profiling-integration-for-continuous-performance-mo.md`, `2026-04-07-sampply-profiling-for-physical-modeling-and-preamp-optimizat.md`.
**Estimate**: 1-2 days.

### [ ] B2. Golden testing framework
**Description**: For each DSP stage (tempo detection, slicing, variation engines, processing presets), record "golden" input + expected output audio. Diff new builds against goldens — any deviation fails CI.
**Why net positive**: makes DSP refactors *safe*. You can SIMD-optimize a kernel and prove output is bit-identical (or within N dB tolerance). Without goldens, every DSP change ships with "hope it still sounds right".
**Risks**: initial record time is substantial. Maintenance: whenever you deliberately change output, you re-record the golden. Needs discipline.
**Vault refs**: `2026-04-06-golden-testing-framework-for-sample-monsta-5.md`, `2026-04-06-golden-testing-framework-for-sample-monsta.md`.
**Estimate**: 2-3 days initial, ongoing cost per new feature.

---

## Phase C — Audio Thread / Real-Time Safety

### [ ] C1. Lock-free audio graph + SIMD kernels
**Description**: Replace mutex-guarded data flow on the audio thread with single-producer-single-consumer lock-free queues. Vectorize hot DSP inner loops with SIMD (SSE/AVX on x64).
**Why net positive**: lock contention on audio thread = dropouts (audible glitches). Lock-free = guaranteed no priority inversion. SIMD typically 2-4× throughput on DSP loops. Combined: more simultaneous voices, more effects, no xruns.
**Risks**: lock-free correctness is subtle. Needs careful audit + stress tests. SIMD needs architecture fallback (scalar path for non-x64 or old CPUs).
**Vault refs**: `2026-04-08-sample-monsta-lock-free-audio-graph-and-simd-kernels.md`, `2026-04-11-realtime-audio-lock-free-sample-monsta.md`, `2026-04-11-unified-realtime-performance-layer-for-sample-monsta--varia.md`.
**Estimate**: 1-2 weeks (careful work).

### [ ] C2. Lock-free ring buffer template
**Description**: Reusable header-only template `samplar::LockFreeRingBuffer<T, Capacity>` for audio-thread → UI handoffs (level meters, playhead position, transient markers).
**Why net positive**: once the primitive exists, every audio/UI handoff uses it instead of re-inventing with mutexes. Prevents one-off buggy implementations scattered across the codebase. Also a prerequisite for C1.
**Risks**: low — it's a contained primitive with well-known algorithms (wait-free Lamport queue).
**Vault refs**: `2026-04-11-lock-free-audio-ring-buffer-template-for-sample-monsta.md`.
**Estimate**: 1 day (impl + tests).

---

## Phase D — Storage / I/O

### [ ] D1. FLAC sample library compression
**Description**: Convert large sample libraries to FLAC (lossless compression); decode on load. Small samples (< 1 MB) stay WAV to avoid decode overhead.
**Why net positive**: ~40-60% smaller disk footprint. Proportionally faster to load on slow drives (HDD, network). FLAC decode is fast (< 5 ms/MB modern CPU). Users ship smaller updates.
**Risks**: FLAC encode is one-time but CPU-heavy during library build. Need a `juce::FlacAudioFormat` or libFLAC dependency.
**Vault refs**: `2026-04-07-flac-based-sample-library-compression-for-sample-monsta-5.md`.
**Estimate**: 2-3 days.

---

## Phase E — DSP Architecture

### [ ] E1. Cascaded biquad architecture
**Description**: Replace direct-form biquad chains with second-order section (SOS) cascade. Per-stage gain normalization + denormal protection.
**Why net positive**: numerical stability at high Q / aggressive EQ settings. Audibly cleaner filtering. Industry-standard pattern for high-order filters.
**Risks**: audio output changes subtly — needs B2 (goldens) to validate. Otherwise users might perceive "different tone" on updated presets.
**Vault refs**: `2026-04-06-cascaded-biquad-architecture-for-sample-monsta-5.md`.
**Estimate**: 3-4 days + golden validation.

### [ ] E2. Sampler pre-rendering
**Description**: After a variation is generated, asynchronously render the full variation (post all processing) to disk. On playback, stream from the pre-rendered file instead of re-running the engine + processing chain.
**Why net positive**: eliminates all DSP cost during preview playback. Huge for heavy engines (Spectral Clone, DDSP, Power Synth). Aligns with existing disk cache of variation WAVs.
**Risks**: disk space. First-time render cost. Pre-render must happen off the audio thread.
**Vault refs**: `2026-04-07-sampler-pre-rendering-pattern-for-sample-monsta-5.md`.
**Estimate**: 2-3 days.

### [ ] E3. Real-time Rust engine (targeted kernels only)
**Description**: Port specific hot DSP kernels (identified by B1 profiling) to Rust behind a C FFI. Use for memory-safety-critical paths.
**Why net positive**: audio-thread use-after-free bugs = DAW crashes in customer studios. Rust prevents the whole class. Modern Rust compiler sometimes produces tighter code than MSVC on specific kernels.
**Risks**: FFI complexity, build-system changes, team learning curve. DO NOT do wholesale — only hotspots proven by profiling.
**Vault refs**: `2026-04-08-real-time-rust-engine-for-sample-monsta.md`.
**Estimate**: 1-2 weeks per kernel.

### [ ] E4. Adaptive dynamics
**Description**: Auto-adjust compressor/limiter parameters from signal statistics (crest factor, RMS envelope) instead of fixed knobs.
**Why net positive**: new users get sensible processing without dialing. Power users can still override. Reduces "why does this preset sound bad on my sample" complaints.
**Risks**: must be opt-in. If enabled by default it'd override dialled settings. Plus: needs careful smoothing to avoid pumping artifacts when signal statistics shift.
**Vault refs**: `2026-04-07-adaptive-dynamics-for-sample-monsta.md`.
**Estimate**: 3-5 days.

### [ ] E5. Masking-aware variation safety
**Description**: Compute perceptual masking profile of source sample. Score each generated variation against how well it preserves unmasked content. Flag or skip variations below threshold.
**Why net positive**: currently some generated variations are perceptually "garbage" (they mask the source's key frequencies). Automated filter = better default output quality = fewer "why does this variation sound nothing like my sample" complaints.
**Risks**: masking calc is CPU-significant. Compute once per source, not per variation. Threshold tuning requires listening tests.
**Vault refs**: `2026-04-17-sample-monsta-masking-aware-variation-safety.md`.
**Estimate**: 1 week.

---

## Execution order (recommended)

Gate each on the previous landing stable:

1. **B1** (Perfetto) — data-driven from here on
2. **B2** (goldens) — safety net for DSP changes
3. **A4 → A1 → A2 → A3** (tempo safe wins)
4. **C2** (ring buffer primitive)
5. **D1** (FLAC)
6. **A5, A6** (tempo deeper)
7. **C1** (lock-free audio graph)
8. **E2** (pre-rendering)
9. **E1** (cascaded biquad — needs B2)
10. **A7** (BTrack quick-guess, UX polish)
11. **E4, E5** (adaptive dynamics, masking safety)
12. **E3** (Rust kernels — only with B1 evidence)

## Convention

For each task when executed:
- Create a commit with prefix `feat(harden):` or `perf:` as appropriate
- Reference the vault note path in the commit body
- Update this doc to check the box and note any caveats found during implementation
