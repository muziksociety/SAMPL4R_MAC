# SAMPLAR Revival Report

**Date:** 2026-10-20
**Scope:** Full audit of `b:/SANTI/VST PROJECT/SAMPLAR` (v1.0.0, commit `cf8066ba`, plus uncommitted 1.0.1 changes), compared against SM5, Simple Monsta, LIGHT, RUIDO and VARIA, plus market research.
**Method:** Six parallel read-only audits, each finding traced to file:line. Critical items were re-verified by hand. Release VST3 build run on 2026-10-20 (passed, EXIT=0). Installed-build log analysed.

Detailed findings (each with file:line, quoted code, impact and fix):

| Area | File | Findings |
|---|---|---|
| Realtime audio + plugin lifecycle | [audit_A_realtime.md](audit_A_realtime.md) | A1-A28 |
| S-Mode UI / UX | [audit_B_ui.md](audit_B_ui.md) | B1-B36 |
| Separation + analysis | [audit_C_separation.md](audit_C_separation.md) | C1-C20 |
| Licensing, release, hygiene | [audit_D_release.md](audit_D_release.md) | D1-D23 |
| Sibling projects (what to port) | [audit_E_siblings.md](audit_E_siblings.md) | 26-row port table |
| Market + UX patterns | [audit_F_market.md](audit_F_market.md) | 25+ products |

Secrets that appeared in the raw audits (an embedded key, a server default secret, a committed test licence) are redacted in these copies.

---

## 1. Verdict

The concept is strong and the market gap is real. Only three plugins (Serato Sample, GODLIKE CHOP, FlipCraft) combine stems with a pad sampler, and all three switch stems on or off for the whole sample before chopping. None treats each stem as its own sampler. Samplab shut down on 2026-09-17, so Windows users have one fewer option.

The v1.0.0 code is not ready to sell:

1. **It crashes DAWs.** At least 10 separate ways to use freed memory on editor close, plugin removal or project close. Two SAMPLAR instances share global state.
2. **It forgets the user's work.** Chops and MIDI mappings are never saved in the DAW project. Stems live in `%TEMP%`.
3. **The audio engine is not realtime-safe.** Every MIDI note allocates and copies a whole segment on the audio thread. The UI thread mutates voices while audio is reading them.
4. **Licensing is broken.** Users must sign in through the browser on every launch. The offline check can't verify real tokens, and anyone who extracts the embedded key can forge them.
5. **The UX hides the core idea.** LINK ALL is on by default, so a "vocal chop" plays the full mix. There is no per-chop control strip, no key detection and no visible pad map.

**What works:** the build passes. Native ONNX 4-stem separation works with the correct stem order. BeatThis tempo detection is accurate (84.86 detected vs ~85 actual). OAuth sign-in completes. The S-Mode page, sequencer lanes, drag-out, pitch and MIDI CC mapping exist and mostly function. The foundation is worth keeping. The risk sits in the engine's threading and lifecycle code, not in the concept.

---

## 2. P0: Ship-blockers (fix before anyone else runs it)

Duplicates across the audits are merged; the IDs point to the detailed write-ups.

### 2.1 DAW crashes (lifecycle and threading)

| # | Problem | Evidence | Fix |
|---|---|---|---|
| 1 | Separation thread is detached and captures raw `this`. Removing the plugin mid-separation means use-after-free | `SModeSeparationService.cpp:176` `separationThread_->detach()` (C1, A7) | Joinable thread, cancel flag, join in `shutdown()`. Copy the `FullSongSeparationService` pattern / SM5 `cb56199a` |
| 2 | Six detached analysis threads (BPM, onset, quick-assign, spectral, drag-out) post raw `this` | `SModePage.cpp:3010,3042`, `StemWaveformComponent.cpp:400,478`, `SModeService.cpp:187` (C5, B3) | `Component::SafePointer` + shared alive flag; joinable workers |
| 3 | Page destructor leaves model callbacks, the tempo subscription and sequencer timers alive | `SModePage.cpp:1104-1120` vs `:366`, `:1216-1221`, `:3497-3557` (B1, B2, B4, A5) | Unsubscribe everything and stop the sequencer in `~SModePage` |
| 4 | RootComponent leaves update and licence bus subscriptions behind | `MainWindow.cpp:447-454` (B5) | Unsubscribe in destructor |
| 5 | Slot audition timer fires on a deleted slot | `SequencerSlotComponent.cpp:786` (B6) | Stop timer in destructor / SafePointer |
| 6 | UpdateCheckService background thread holds raw pointer; nothing joins it | (D8) | Joinable thread, stop in `shutdown()` |
| 7 | Global static `ServiceLocator` shared by every instance; editor never re-claims `AppState` | `ServiceLocator.h:59-64`, `MainWindow.cpp:655-668` (A6) | Per-processor context passed down the UI. Port SM5 `86e3698a`, `3dcdc3ce`, `be5bd494`, `f1afbc7c` |
| 8 | `/arch:AVX2` crashes the whole DAW on CPUs without AVX2 | `CMakeLists.txt:418,749` | `/arch:AVX` (SM5 `5de0dee8`) |
| 9 | Unbounded memory: ~3.6 GB peak for a 10-min song; `bad_alloc` uncaught, so `std::terminate` | `OnnxSeparationProvider.cpp:323-600` (C3) | Float accumulators, in-place, catch, duration guard |

### 2.2 Audio thread safety

| # | Problem | Evidence | Fix |
|---|---|---|---|
| 10 | Every note-on does `make_shared<AudioBuffer>` + full segment copy + `MessageManager::callAsync`; the old buffer is freed on the audio thread | `MidiMappingService.cpp:749-786` (A1, verified) | Voices play a `[start,end)` window over a shared immutable stem buffer. Note map precomputed. Lock-free FIFO to the UI |
| 11 | UI thread resets voices and stretchers the audio thread is processing (the team's comment at `:223-228` records an access violation) | `MidiMappingService.cpp:1323-1368,1514-1528,1649-1672` (A2) | UI commands go into a lock-free queue that only the audio thread applies |
| 12 | Segment model read by audio with no sync; a new-song drop clears it while playing | `MidiMappingService.cpp:525-547`, `SModePage.cpp:4124` (A3, C4) | Immutable snapshot swapped atomically (RCU-style); free old snapshots on the message thread |
| 13 | Tempo change on audio thread: log write + 2 locks + post, every block under automation | `SamplarAudioProcessor.cpp:510-513` (A9) | Atomic BPM; UI timer publishes |

### 2.3 Losing the user's work

| # | Problem | Evidence | Fix |
|---|---|---|---|
| 14 | Chops and MIDI mappings are never saved in the DAW project | `SamplarAudioProcessor.cpp:670-753` (A4, B8) | Serialise the full segment model + mappings in `getStateInformation` |
| 15 | Restore only happens if the editor is open while the project loads; the next save wipes the session if it didn't restore | `SamplarAudioProcessor.cpp:624-626,678-684`, `MainWindow.cpp:1057-1143` (A4, B9) | Restore in the processor, not the UI. Never overwrite a pending, unrestored session |
| 16 | Stems in `%TEMP%`, 16-bit, hard-clipped, never cleaned; no stem cache; stems matched by filename substring | `FullSongService.cpp:236` (C9, C14) | Persistent cache keyed on (path, size, mtime, model), 32-bit float, one separation at a time (SM5 `a4900a3b`, `b6ce642a`) |
| 17 | Drag-out WAVs go to `%TEMP%` with names reused across songs, so a later drag overwrites audio an older DAW project references | (B29) | Unique names in a persistent export folder |

### 2.4 Licensing and security

| # | Problem | Evidence | Fix |
|---|---|---|---|
| 18 | Browser re-login on every launch: the OAuth JWT is saved as `signed_token`, the verifier expects `payload\|sig`, then the cache is wiped | `LicenseSystemService.cpp:1698-1699,1090`, `SignedTokenVerifier.cpp:164` (D1, verified) | Hotfix: don't store the JWT as `signed_token`, never wipe the cache on a local verify failure. Proper fix: port SM5 RSA flow |
| 19 | Offline token check is symmetric HMAC with the key in the binary: real server tokens fail AND anyone can forge | `SignedTokenVerifier.cpp:178`, `SamplarVerificationKey.h` (D2) | Asymmetric signature (SM5 `LicenseSignature.*`, `ClockGuard.h`). **Rotate `LICENSE_TOKEN_SECRET` on Render** |
| 20 | OAuth callback server on port 9876 listens on all interfaces; `state` is clock-seeded | (D7) | Bind 127.0.0.1, CSPRNG state, verify the callback payload |
| 21 | Debug files on customers' machines: `Desktop\oauth_debug.txt` (6 call sites, 1,077 lines on your machine), `C:\Users\Public\samplar_dll_load.log` | `LicenseSystemService.cpp:173` (D6, verified) | Delete these writes |
| 22 | v1.0.0 installer shipped `SAMPLAR.map` (15 MB symbol map incl. licensing) | installed bundle (D5, verified) | Exclude from staging; sign the installer and the VST3 |
| 23 | Production PRO test licence committed in `src/security/README.md` | (D15) | Revoke it server-side, remove it from the repo |

### 2.5 The uncommitted 1.0.1 changes

| Change | Verdict |
|---|---|
| Rebrand strings (SampleMonsta to SAMPLAR) | Keep. The browser page is still Sample Monsta-branded and there's no purchase link (D17) |
| Version 1.0.1 in CMake + `.iss` | Keep, but 6 other version values disagree (D4). `Version.h:14` still says `1.2.2` |
| `OnnxDelayLoad.cpp` added to `SamplarApp` | Keep |
| Default size 1500x1024 to 1060x724, limits 850x500 | **Do not ship as-is.** Solo/Mute/Link rows get 0 px (B13); the top bar needs ~1200 px (B14) |
| `CLAUDE.md` context-mode block | Keep out of the release commit |

---

## 3. P1: Quality (needed for a good 1.x)

**Audio**
- **No real host sync (A10).** MIDI sample offsets are ignored (up to 21 ms jitter at 1024 buffer). Transport and PPQ are never read. The sequencer runs on UI timers (B36). DAW pitch-bend and CC are ignored in VST3.
- **Note handling (A11, A12).** Looped segments ignore note-off, even in GATE mode. No CC120/123. Pitched voices are 30-80 ms late, latency is unreported and tails are cut off.
- **Clicks (A13).** No fades on cut, no loop crossfade. 16 voices and the sequencer loop is the first to be stolen (A15).
- **Resampling (A23, C8).** Linear interpolation is used both for input to the model and for playback. Use `WindowedSincInterpolator` or r8brain.
- **Six different playback and export paths sound different (B26).** What you audition isn't what lands in the DAW.

**Separation and analysis**
- **Cancel isn't wired (C2).** A 10-min song locks the user out for ~13 min. Use `RunOptions::SetTerminate()`.
- **ORT busy-waits on all cores at normal priority inside the DAW (C13).** It's the CPU-only build, so DirectML needs a runtime swap (SM5 `453e7733`).
- **Downbeat detector finds the beat phase only, not beat 1 (C10).** BeatThis downbeats are discarded.
- **FFT layout bug in `TempoRefiner` / `OctaveCorrection` (C11).** DWT always runs, is O(n^2) per window and was wrong (113 vs 85) (C12). Run BeatThis first; keep DSP as fallback only.
- **Transient slicer cuts ~12-23 ms early (estimate) with no zero-crossing snap (C17).**
- **Garbled progress text in the log (C6, verified).** `FullSongService.cpp:111` passes UTF-8 to `String::formatted("%s")`. Log-only, but it's an out-of-bounds read.

**Release and operations**
- **The update check can never work (D3).** `samplar` isn't in the server's `releases.json`, the client reports `1.2.2`, and the field names don't match.
- **A release can't be rebuilt from git (D9).** Models, patterns and samples are untracked, the post-build copy swallows errors, and the release skill's model preflight isn't in `build_installer.ps1`.
- **Offline grace is unreachable (D10).** The gate requires exactly `Valid`. Deactivate calls an endpoint the server doesn't have. Sign-in times out after 60 s.
- **`portable_root.txt` outranks the installed bundle and survives uninstall (C7, D11).** On your machine the "installed" plugin loads dev models.
- **About 260 MB of 808 MB install is dead payload (C19).** `ffmpeg.exe` (148 MB, zero references, no licence notice), pattern JSONs (56 MB, shipped twice, zero references) and orphaned services. About 130 MB of libtorch/demucs/vcpkg plus `openclaw/` are tracked in git (D20).
- **Release logs contain PII (D13).** Email, the full OAuth URL and the hardware ID.
- **The test target runs zero tests; a licensing test ships inside the plugin (D21).**
- **`CLAUDE.md` describes variation engines that don't exist in `src/` (D22).**

**UI performance**
- **Every lane rescans raw samples per pixel column, 30 times a second, during playback (B32).** Cache the waveform image.
- **Stems decode on the message thread (C15, A19). Large memory spike when stems are ready (B33).**

---

## 4. UX gaps against the vision

Vision: *"sample a song in stems: vocals, drums, melodies, bass, each individually."*

Today it takes 7 clicks to play a vocal chop from a keyboard, one of which (turning LINK ALL off) nothing tells you to do. Even then you have to guess the key.

| # | Gap | Evidence | Proposal |
|---|---|---|---|
| 1 | LINK ALL on by default: editing one stem copies chops to all stems, so a vocal chop plays the full mix | `SModePage.h:555` `linkAllActive_ { true }` (B16, verified) | Default off. Replace with the **Stem Grid** (section 5) |
| 2 | No visible pad map. Three octave conventions. QWERTY reaches only notes 48-71, while Quick Assign starts at 36 | (B21, B22) | Fixed 16-pad bank per stem (vocals C1, bass C2...), on-screen keyboard strip, one octave convention |
| 3 | Auto: Beat on a full song makes ~420 chops of ~2 px | `SModePage.cpp:2256-2276` | Default to transient or bar per stem, cap at 16, "re-roll" with locks |
| 4 | Per-chop pitch, filter, reverse, volume and pan exist in the model but have no UI and playback ignores them. No ADSR or fades | (B UX gaps, B17) | Per-pad strip: pitch, stretch, filter, attack/release, reverse, one-shot/hold, choke. Port Simple Monsta `SliceFx.h` + `SliceKnobSpecs.h` |
| 5 | No key detection (0 references in `src/`); every competitor has it | verified by grep | Chroma + Krumhansl on the melody/bass stems; key-shift to project key |
| 6 | No automatable parameters, presets or kit save | (E: LIGHT) | APVTS per-stem macros + LIGHT `PresetManager.h`; kits saved as session + stem cache refs |
| 7 | Export WAV has no click handler; 6-stem bypassed; lane header buttons do nothing; ~900 lines of dead SM5 UI | (B UX gaps) | Wire or delete |

---

## 5. Differentiators (what makes SAMPLAR worth buying)

From [audit_F_market.md](audit_F_market.md). None of these exists in the competitors reviewed.

1. **Stem Grid: chop once, play four ways.** One marker cuts all four stems at the same timestamp. Each pad stores a time range plus its own stem mix (pad 1 = vocals @1:12, pad 2 = drums+bass @1:12). This is the vision taken literally.
2. **FLIP.** Builds a 4-lane pattern from the song's own stems, pulling chops from different sections, conformed to key and tempo. Locks plus Density/Variation re-roll. Uses stem-aware "find samples" (e.g. drum breaks where the other stems are quiet).
3. **Vocal Chop instrument.** Cuts between syllables, formant-preserving pitch, scale snap. The nearest competitor (Antares Slice) can't extract vocals from a song and costs $24.99/mo.

Positioning: *"Sample the song, not the mix. Four samplers in one, 100% local, no subscription."* Price suggestion: $99, launch $59-69, 30-day full trial.

Ten UX patterns to adopt, with sources, are in `audit_F_market.md` section 2. The most important are one-click re-rollable suggested chops, four slice modes one click apart, key/BPM with key-lock, a per-pad strip, keyboard modes, one-gesture slice-to-MIDI + WAV drag-out, and a short wait before the first chop.

---

## 6. What to port from siblings

From [audit_E_siblings.md](audit_E_siblings.md). SAMPLAR's separation and plugin core is a May 2026 copy of SM5, and SM5 has kept fixing the same files, so most fixes port almost directly.

| Rank | Port | From | Effort |
|---|---|---|---|
| 1 | `/arch:AVX` | SM5 `5de0dee8` | S |
| 2 | Joinable/cancellable separation, clean ORT shutdown | SM5 `cb56199a`; Simple Monsta `core/ml/DrumClassifier.cpp:137-177` | S |
| 3 | Multi-instance fixes + `tools/vst3_smoke_host` (`--two-instances`) | SM5 `86e3698a`, `3dcdc3ce`, `be5bd494`, `f1afbc7c` | S-M |
| 4 | Licensing: remove Desktop debug file, non-modal sign-in, RSA signed licences | SM5 `042b7812`, `b0526074`, `34d398f1`, `894f80ec`, `be797682` (+ `license_server`) | S then M |
| 5 | Persistent stem cache + single-separation lock | SM5 `FullSongService.cpp:197-252,385-522`, `a4900a3b`, `b6ce642a` | M |
| 6 | Per-slice FX, ADSR, pad inspector, 100-step undo | Simple Monsta `core/audio/SliceFx.h`, `Voice.*`, `Adsr.h`, `plugin/ui/SliceKnobSpecs.h`, `PadInspector.*`, `core/session/EditHistory.*` | L |
| 7 | APVTS per-slot params + preset browser + vector knobs | LIGHT `plugin/PluginProcessor.cpp:116-224`, `plugin/PresetManager.h`, `plugin/ui/Theme.h` | M |
| 8 | DirectML GPU separation with CPU fallback | SM5 `453e7733` (`GpuAdapterProbe.*`, `HtdemucsIstft.*`) | L |

QA to copy: RUIDO hit-test sweeps (`tools/selftest/SelfTestMain.cpp:1328-1340`), the LIGHT audio-thread allocation counter + release gate, and Simple Monsta `tools/editor_repro` + Windows CI. No project runs pluginval yet. Borrow RUIDO code from the `00RUIDO-saturate` worktree, because the main checkout is ~49 commits behind.

---

## 7. Roadmap

Each phase ends with a verifiable gate. Effort is in relative units (S/M/L), not dates.

### Phase 0: Stop the bleeding (hotfix 1.0.1)
1. `/arch:AVX`; remove the Desktop and `C:\Users\Public` debug writes; exclude `.map` from staging. **Verify:** grep finds no `userDesktopDirectory` or `Users\\Public`, and the bundle has no `.map`.
2. Licence hotfix: stop storing the JWT as `signed_token`; don't wipe the cache on local verify failure. **Verify:** two launches in a row, no browser prompt.
3. Join the separation and update threads; cancel wired; full `~SModePage` / RootComponent unsubscribe. **Verify:** close the editor and remove the plugin mid-separation, mid-BPM and mid-playback in Reaper + FL; no crash.
4. Revert the window shrink, or fix the B13/B14 layout. **Verify:** RUIDO-style hit-test sweep at the minimum and default sizes.
5. Bundle resources take priority over `portable_root.txt`. Delete ffmpeg + pattern JSONs from staging. One version constant (CMake gives `Version.h` and the `.iss`). Register `samplar` in `releases.json`.
6. **Security:** rotate `LICENSE_TOKEN_SECRET` on Render, revoke the committed test licence, bind OAuth to 127.0.0.1.

### Phase 1: Engine core (makes it a real instrument)
1. Immutable `StemSnapshot` (buffers + segments + mappings) swapped atomically; voices index windows into it (no per-note copy); UI-to-audio command FIFO. **Verify:** LIGHT-style allocation counter reports 0 allocations in `processBlock` across 10k random notes.
2. Per-processor context replaces the static `ServiceLocator`. **Verify:** `vst3_smoke_host --two-instances` with independent sessions, and remove one instance while the other plays.
3. Sample-accurate MIDI, host-transport sequencer on the audio thread, note-off/CC120/123, 2-5 ms fades, latency reporting. **Verify:** offline render matches realtime render bit-for-bit for a MIDI clip.
4. Full session persistence + persistent stem cache, restored without the editor. **Verify:** save, close the DAW, clear `%TEMP%`, reopen with the editor closed, play MIDI; chops sound correct.
5. Memory-bounded separation, float stems, sinc resampling, BeatThis-first tempo, real downbeats.

### Phase 2: Licensing and release pipeline
1. Port SM5 RSA licensing (PKCE, token-exchange, revalidate) + server key pair for SAMPL4R. A working offline grace period.
2. `build_installer.ps1` fails hard on missing models; models fetched from R2 by hash; code signing.
3. QA gate: pluginval level 10, two-instance smoke host, hit-test sweep, pristine-VM install test (`sm5-release-linter` skill).

### Phase 3: Creative relaunch (2.0)
1. LINK ALL off, then **Stem Grid** (per-pad time range + stem mix).
2. Per-pad strip (pitch, stretch, filter, ADSR, reverse, mode, choke); 16-pad banks per stem with an on-screen keyboard.
3. Smart chop: transient / bar / N-equal / manual one click apart; re-roll with locks; zero-crossing snap.
4. Key detection + key-lock.
5. Drag-out everywhere: pad, then WAV; lane, then MIDI; whole stem, then WAV.
6. Kits/presets + APVTS macros (LIGHT).
7. **FLIP** generator, then the **Vocal Chop** instrument.
8. Optional: DirectML GPU path; "chop while separating".

---

## 8. Pre-flight (from global CLAUDE.md)

- **Problem:** Producers want to sample individual parts of a song; today SAMPLAR crashes, forgets their chops, and hides the per-stem idea behind LINK ALL.
- **Why now:** Samplab closed (2026-09-17). Competitors only switch stems on or off for the whole sample. v1.0.0 users are hitting re-login every launch and the update check is dead, so there is no channel to reach them.
- **Scope:** Phase 0 is the minimum that makes the existing build safe to hand out.
- **Success metric:** crash-free sessions (target: zero crashes in the Phase 0 matrix). Then time from load to first played chop (target under 60 s plus separation time, 3 or fewer clicks).
- **Reversibility:** Phases 0-1 are two-way doors. **One-way doors:** switching the licence token format (all v1.0.0 users must re-auth once), rotating the server secret (safe: no shipped client calls validate-token - confirmed by the license-server owner 2026-10-21), and the saved-session format (needs versioning from day one).

## 9. Decisions needed

1. Ship Phase 0 as a 1.0.1 hotfix first, or skip straight to the 2.0 relaunch?
2. Licensing: port SM5 RSA flow now (needs `license_server` + Render work), or hotfix only for now?
3. Default window size: back to 1500x1024, or redesign the layout for 1060x724?
4. GPU (DirectML): worth +235 MB model + runtime swap in 2.0?
5. Delete dead code (ProcessingCoordinator, slicer/loop services, SM5 UI remnants, `src/deprecated`), or keep it parked?
