# Audit E - Sibling projects as port sources for SAMPLAR (read-only, 2026-10-20)

Scope: RUIDO (+6 worktree variants), VARIA, LIGHT ("ili"), Sample Monsta 5 (SM5), Simple Monsta,
BeatMonsta (bonus), shared notes. Nothing was edited or built.

## 0. SAMPLAR baseline (what we are porting INTO)

| Area | SAMPLAR today | Evidence |
|---|---|---|
| Host params / automation | NONE. No APVTS, no AudioParameter*. Nothing is automatable. | grep of src/ ; SamplarAudioProcessor.h:40-44 (1 program, stubs) |
| State | AppState + ScopedStateBinding ValueTree (ui/smode/midi nodes, v1). Stems saved as FILE PATHS only. | SamplarAudioProcessor.cpp:606-628, 632-800 |
| Stem storage | Fresh `%TEMP%/<prefix><millis>` folder per run, no cache. Temp cleanup (Storage Sense) can orphan saved projects. | FullSongService.cpp:236-241 |
| Dead code | encodeBufferRaw/decodeBufferRaw defined, never called | SamplarAudioProcessor.cpp:542,571 |
| Multi-instance | `ServiceLocator` = 4 static inline globals; editor "re-claims" on visibility/focus. No ownership/hand-off. | src/app/ServiceLocator.h:84-88; SamplarAudioProcessorEditor.cpp:19,100-113 |
| Editor sizing | Free resize 850x500..1810x1130, size not display-aware | SamplarAudioProcessorEditor.cpp:52-54 |
| License gate | `runModalLoop()` inside the editor CONSTRUCTOR (JUCE_MODAL_LOOPS_PERMITTED=1 on plugin target) - host-hostile | SamplarAudioProcessorEditor.cpp:24-40; LicenseActivationDialog.cpp:148-150; CMakeLists.txt:678 |
| License crypto | HMAC-SHA256 token, verification key embedded in the binary (symmetric => extractable key can MINT tokens). No PKCE, no ClockGuard. | src/security/SignedTokenVerifier.h:9-17; SamplarVerificationKey.h |
| Release leak | Writes `oauth_debug.txt` to the user's Desktop (destructor + OAuth + DebugConsole, 6 call sites) | LicenseSystemService.cpp:173; OAuthBrowserAuthenticator.cpp; ui/Debug/DebugConsole.cpp |
| Separation cancel | `cancelSeparation()` is EMPTY; worker thread is `detach()`ed so `shutdown()` join is a no-op => use-after-free if plugin unloads mid-separation | SModeSeparationService.cpp:~176-181, 21-22 |
| CPU target | `/arch:AVX2` on app + plugin => STATUS_ILLEGAL_INSTRUCTION on pre-AVX2 CPUs takes the DAW down | CMakeLists.txt:418,749 |
| Undo / presets | None | - |
| QA | Already has tools/release_linter (7 checks + run_all.py) and tests/licensing, tests/mentor. No host smoke test, no UI hit-test sweep, no pluginval. | tools/release_linter/ |

---

## 1. Per-sibling summary

### 1.1 Sample Monsta 5 (b:/SANTI/SAMPLE_MONSTA/sample_monsta4/Sample_monsta5) - SAME ARCHITECTURE, cheapest ports
SAMPLAR forked 2026-05-06 (34bc16cc); ONNX copied 2026-05-18 (cf8066ba, ~= SM5 718596f0/774b4b7c). SAMPLAR has not moved since; SM5 has ~5 months of fixes on identical files (same ServiceLocator/AppState/EventBus/LicenseSystemService shapes => near drop-in).

Separation (SM5 better on every axis; same htdemucs.onnx md5, same 7.8 s segment / 0.25 overlap, so output quality is identical):
- DirectML GPU path w/ CPU fallback, GPU probe (>=2 GB dedicated VRAM), body model + host iSTFT: `src/services/separation/OnnxSeparationProvider.cpp` (830 vs 665 lines), `GpuAdapterProbe.{h,cpp}`, `HtdemucsIstft.{h,cpp}`, `resources/models/htdemucs_body.onnx` (235 MB), `tools/fetch_onnxruntime_directml.py`. Commits 453e7733, 83a99411 (2026-10-06). Env override `SM5_SEPARATION_DEVICE=cpu`.
- Cancel actually wired: `FullSongSeparationService.cpp:92,245`; cancellable lock wait `FullSongService.cpp:429-439` (cb56199a).
- Hash-keyed stem cache `<userData>/stem_cache/<hash(name+size+mtime+model)>`, incomplete = miss: `FullSongService.cpp:197-252, 385-420, 507-522` (8ca2563d, 5234d89b, b6ce642a).
- Process-wide separation lock (only one separation across instances): a4900a3b.
- AVX instead of AVX2: 5de0dee8 (2026-05-23).
- Not in SM5 any more: S-Mode (deleted 05775818 - "SAMPL4R replaces it"), Beat This (removed 10e0b19a). SAMPLAR's SModeSeparationService / BeatThisDetector / OnnxDelayLoad.cpp are SAMPLAR-only; nothing to port back.

Plugin infra (post-fork, SAMPLAR lacks all):
- Instance-safe ServiceLocator claims + guarded async hops: 86e3698a; hand locator to surviving instance on unload: 3dcdc3ce; `src/app/ServiceLocator.h` (175 lines, `shutdown(owner)` ownership guard at :103-120).
- Thread-safe plugin state: be5bd494 (`src/app/AppState.{h,cpp}`, `tests/unit/AppStateThreadTests.cpp` 93 lines).
- Plugin load 1.9 s -> 0.1 s: f1afbc7c (ServiceRegistry lazy init).
- Display-aware editor sizing + remembered size: 4578d0b7, df5d258a; `src/ui/WindowSizing.cpp`, `SM5AudioProcessorEditor.cpp:20-60`, `tests/unit/WindowSizingTests.cpp` (166).
- License gate + menus in-window (no nested modal loop): b0526074 (`src/ui/Components/InWindowDialog.*`, LicenseActivationDialog.cpp).
- Reopen restores without re-analysis: 5f888d19.
- Commercial hardening (silence logging, strip pdb/map): f37b12f2.

QA:
- `tools/vst3_smoke_host/VST3SmokeHost.cpp` (481): load/instantiate, setState from previous run, processBlock burst w/ NaN/Inf check, editor open/close/reopen with budgets, getState, unload, watchdog, `--two-instances` mode (A unloads while B's editor stays open). Directly reusable - SAMPLAR has the same processor shape.
- `tests/unit/` (~60 suites incl. ServiceLocatorContextTests.cpp 38, AppStateThreadTests.cpp, WindowSizingTests.cpp, HtdemucsIstftTests.cpp, OffloadSafetyTests.cpp).
- `tools/license_selftest/` (LicenseSelfTest.cpp, TestSigner, license_vectors.inc), `tools/onnx_benchmark/`, `tools/separation/`.

Licensing (current best for SAMPLAR's code lineage): `src/security/LicenseSignature.{h,cpp}` RSA-PSS v2 enforced (34d398f1, 09-29), `LicenseSigningKey.h`, `ClockGuard.h`, `LicenseCache.cpp` (10-05), HWID v2 + legacy (362b611b), PKCE Authorization-Code OAuth + hardened loopback (894f80ec), WinHTTP statusCode=0 fix (a58391c4), Desktop oauth_debug.txt removed (042b7812), silent Release logging (3db24565), 3-day offline window for non-perpetual (be797682), perpetual fast-path (721fb101).

No APVTS, no presets (`getNumPrograms()=1`), no undo in SM5 either.

### 1.2 Simple Monsta (b:/SANTI/Simple Monsta) - best per-chop engine, undo, QA harness; NO separation
- What: "turns any drum loop into a playable instrument" - VST3 instrument + Standalone (CMakeLists.txt:172-179). Spec explicitly rules out separation (docs/superpowers/specs/2026-08-07-simple-monsta-design.md:14,30); only ONNX = 5.9 MB drum CNN.
- Workflow (the "intuitive" reference): drop loop -> auto slice (SuperFlux) + CNN category tint over spectrogram -> pads (Drum mode = one pad per category with `< 2/5 >` candidate chooser; Slice mode = one pad per slice; 16-pad pages) -> play by click/MIDI/computer keys/piano bar -> shape with ADSR strip + 10 FX tabs, per-pad inspector (level/pan/vel/choke/lock/pitch/stretch) -> drop a sample on a pad to replace (Loop-Hit Picker if it's a loop) -> drag-out: per-pad WAV as heard, transport WAV "cluster stems", MIDI drag with one lane per stem.
- Layout (820x724 base, PluginEditor.cpp:318-340): TransportBar 40px / waveform 40% / AdsrBar+FX tabs 34px / PadGrid w/ page-nav bands / PadInspector 30px / PianoBar 84px.
- Per-chop sound engine (JUCE-free `sm::` core): `core/audio/SliceFx.h` (64; bundle EQ(Pultec)+EQ3+Drive+Lofi+Pitch+Reverse+Formant+Shuffle+Glitch with isNeutral/==), `Voice.{h,cpp}` (170+369), `Adsr.h` (77), `Reverse.h` (63), `Pitch.h`, `SimpleEq.h`, `PultecEq.h`, `Drive.h`, `Lofi.h`, `Transient.h`, `Compressor.h`, `SamplerEngine.{h,cpp}` (141+145), `PadGain.h`, `PadMode.h`, `QuantizeScheduler.h`.
- Per-chop UI: `plugin/ui/SliceKnobSpecs.h` (346; table-driven knob rows, each `set` writes only its own field), `SliceTray.{h,cpp}` (123+383), `AdsrBar.{h,cpp}` (208+600), `PadInspector.{h,cpp}` (85+184), `PadTile.{h,cpp}` (117+659), `PadGrid.{h,cpp}`, `WaveformView.{h,cpp}` (387+2106; drag markers, add/merge cuts, snap to transient), `plugin/ui/fx/*FxPanel.*` (Reverse/Pitch/EQ/...).
- One stem plan feeds WAV drag + MIDI lanes + MIDI drag so they never disagree: `core/export/SliceWavExport.h:102-149` (151), `core/midi/MidiLanes.h` (56).
- Undo: `core/session/EditHistory.{h,cpp}` (81 h) immutable snapshots, depth 100, labelled; Ctrl+Z wired PluginEditor.cpp:398-406.
- RT handoff: `core/audio/SnapshotExchange.h` (153, generation-counted, non-freeing), `core/audio/RtCommandQueue.h` (76).
- State: versioned ValueTree (v12) `core/session/SessionState.cpp:63`; get/set marshalled to message thread (PluginProcessor.cpp:1610-1766); tagged trailing words for UI scale/theme. Embeds source audio as GZIP raw float (SessionState.cpp:183-194) - fine for loops, NOT for 4 full-song stems.
- APVTS: only 6 globals (PluginProcessor.cpp:185-211). No presets.
- Scaling: AffineTransform 25/50/75/100% saved in state (PluginEditor.cpp:384-394).
- ORT lifecycle hardening (SAMPLAR lacks): leaked process-wide Ort::Env, `DisablePerSessionThreads`, spinning off, delay-load + preload from plugin folder (core/ml/DrumClassifier.cpp:137-177; PluginProcessor.cpp:68-112; CMakeLists.txt:248-292). SAMPLAR creates per-provider Env + per-session pools - the exact teardown-hang pattern Simple Monsta fixed.
- QA: doctest `core_tests` (52 files / 315 TEST_CASEs), `tools/editor_repro/ui_harness.cpp` (headless UI harness ~30 groups incl. tooltip audit :2317-2520), `tools/sm_host/` SimpleMonstaHost `--smoke` (close-app / switch-project / module-unload with crash guard + minidumps, SmokeRunner.h:2-28), `.github/workflows/ci.yml` (windows-latest, JUCE 8.0.8, ORT 1.19.2 CPU). No pluginval script, no release script; VM installers named `-vmtest-<hash>` by hand-editing .iss in worktrees/vm-test-build (uncommitted).
- License: OAuth + RSA-PSS signed grant (core/license/RsaPssVerifier.cpp, LicenseStore.cpp, LicenseManager.cpp), `SM_LICENSE_ENFORCE` flag. RUIDO-v2 port pending in worktrees/license-ruido-port.
- Multi-instance caveat: theme colours are mutable inline globals (plugin/ui/Theme.h:47-57, SpectralPalette.h:98) - cosmetic.

### 1.3 LIGHT / "ili" (b:/SANTI/VST PROJECT/00lLIGHT) - best APVTS + preset manager + vector knobs
- APVTS: 789 params (5 global + 49 per band x 16), all `ParameterID{id,1}`, indexed-slot naming "b{n}_*", cached raw pointers, `copyBandParams/resetBandParams` suffix walk (plugin/PluginProcessor.cpp:116-224, 635-673), `withDefaults()` atomic preset load (:707-718). No smoothing; no UndoManager (nullptr).
- State: APVTS XML via copyXmlToBinary + "CRT" child tree for view state (:720-732). No audio.
- Multi-instance: LicenseManager is a per-processor member; UpdateChecker via SharedResourcePointer; only test statics.
- Scaling: single child view scaled with AffineTransform from 1120x700 design, 0.5x..2x (PluginEditor.cpp:258-263, 1149-1159). Size not saved.
- Presets: `plugin/PresetManager.h` (141) `.lightpreset` XML in Documents/ili/Presets, subfolders, next/prev skipping broken files, Windows filename validation. Only real preset browser among siblings.
- UI kit (all vector): `plugin/ui/Theme.h` (297: ThinKnobLNF, OrbKnobLNF rotary + linear bead slider, GemToggleLNF, PopupLNF, paintDrawer), `Moonstone.h` (215: TopBarLNF, SegmentLNF, BandChip), `ReturnPanel.h` (635: CurveEq draggable HP/LP + 3 bells, MiniPanel orb-knob drawer), palette cycling `DiamondSprite.h` (111), `PixelViews.h` (230, 10 palettes), `SpectrumDecor.cpp`, `SpectrumDisplay.*` (220+448), `AuroraField.h`, `CrtPanel.h`. Timer-driven 30 Hz. Skill: ~/.claude/skills/palette-cycling.
- QA: `tests/proc_tests.cpp` (2564, LIGHT_SELFTEST: audio-thread allocation counter :22-37, golden pixel hashes, getComponentAt click-reachability test_preset_bar :1995 / test_low_edge_reachable :1700 / mentor help coverage :1433, test_band_copy_and_reset :194, test_preset_load_atomic :2279), `tests/dsp_tests.cpp` (1131), `tests/mock_host.cpp` (load VST3, process, state round-trip, editor open/close, unload), `build_installer.ps1` (93: tag==version, reject dev-unlock, file version/company, static CRT, notices/EULA, runs mock host, ISCC, SHA256) + docs/RELEASE.md.
- License (core/license, 10-04..10-12): RUIDO-lineage LicenseManager, RSA-PSS 2048, HardwareIDGenerator, ClockGuard, 30/3-day offline, 7-day trial. Cache tag is SHA256(data+hwid), not a true HMAC.

### 1.4 RUIDO (b:/SANTI/VST PROJECT/00RUIDO + worktrees) - best multi-instance hygiene, cards, self-test
- Main worktree is STALE: branch fix/simple-two-states f792b6a (10-03), 19 dirty files, ~49 commits behind master. Variants are git worktrees of the same repo; `-saturate` (feat/module-licensing 4d94195, 10-15) has the best infra; `-shuffle` newest (10-19).
- Params: no APVTS; 11 raw `addParameter` (macro1-8, mix, output, clip) with ParameterID v1; module params are host-invisible atomics reached via macro links (core/rack/INoiseModule.h:95-153). SmoothedValue 30 ms on mix/out; per-module smoothing. `-saturate` solo plugins expose every module param (plugin/solo/ModuleParams.h 133).
- State: binary ValueTree RUIDO_STATE v4 (PluginProcessor.cpp:196-240), RackChain nodes (core/rack/RackChain.cpp:586-670), migration tests. No audio (textures are BinaryData).
- Multi-instance: no mutable globals in shipping build; SharedResourcePointer<pixel::TextLF>; lock-free snapshot swap (RackChain.h:19,95); `alive` tokens guarding async (PluginProcessor.h:84, LicenseManager.h:630). Caveat: N LicenseManagers write the same license.dat + HKCU ClockGuard.
- Sizing: fixed-pixel layout 660x480..1600x1280, no scaling. No presets (sm4port/PresetManager is legacy), no undo.
- UI kit (plugin/ui): FilmstripKnobLF (41), LedRingKnobLF (54), OrbitKnobLF (105), TapeCounterKnobLF (107), LedDigitKnobLF (118), FillSliderLF (108), FeatherFaderLF (78), PixelStyle.h (100); cards ModuleCard.h (259), StackCard.h (683), RackView (328+74), SimplePanel.h (214), SimpleKnob.h (219), LinkableSlider.h (236), ViewSwitch.h (143); `AnimatedBackground.h` (36) base that skips hidden/clipped frames (the Mistake-4 fix) + 11 animated module backgrounds (AirBackground 455, ReverbBackground 432), ChibiStage/PixelParty.
- QA: `tools/selftest/SelfTestMain.cpp` (3014 main / 6454 in -saturate) drives processor + real editor: round-trip/migration, 44.1/48/96k render sweep, UI perf, license, RSA, HWID, and HIT-TEST SWEEPS of card headers (:1328-1340) + license overlay (:1592-1600) with PNG snapshots; 8 DSP test programs (tools/tests, RUIDO_BUILD_TESTS); ASAN build dir; tools/host/HostMain.cpp (496) hot-reload dev host; `-saturate/build_installer.ps1` (110) release gates + installer/ruido.iss.
- License: core/license LicenseManager.h (875; 890 in variants with per-module products), LicenseSignature.cpp RSA-PSS (RUIDO_REQUIRE_LICENSE_SIGNATURE ON), OAuthBrowserAuthenticator.cpp (523), LicenseCache.h (Blowfish, SHA256 tag), HardwareIDGenerator.h v2, ClockGuard.h, 30/3-day offline, 6h revalidate, 14-day trial. Dated 09-27..09-29.

### 1.5 VARIA (b:/SANTI/VST PROJECT/00VARIA/VariaVST) - chop-region editor + crash reporter; weak infra
- Params: 3 raw addParameter, string IDs, no versioning. State: positional MemoryOutputStream blob (PluginProcessor.cpp:410-523) - fragile, do NOT copy. Audio on disk (favourites WAV + .varia.json).
- Multi-instance: several process singletons (LicenseManager::getInstance, BasicPitchKey::get, DrumCnn::get) + liveProcessors() refcount that frees ML models on last instance (fix 72f5b9b for FL hang) - a useful pattern for SAMPLAR's 300 MB htdemucs session.
- Sizing: constrainer 1.8 aspect 864x480..1440x800, size saved globally (PropertiesFile).
- UI: `UI/TakeEditorDialog.h` (339; WaveView drag-select handles, wheel zoom, dbl-click select-all; callbacks onPlay/onLoad/onSelectionChanged) + `Audio/TakeEdit.h` (attack-safe start snap, 5 ms fade-out extract, WAV write) = ready base for a chop-region editor; `UI/FilmstripKnobLF.h` (97, vector or filmstrip), `UI/SettingsSpec.h` (393) + `SettingsPanel.h` (976) data-driven knob grids, `HybridDrumSlots.h` (883, drag-drop slots w/ mini waveforms), `MorphPad.h` (331), GlassAnimationPrerenderer.h (299).
- QA: 31 console test exes (custom check(), not in ctest), golden/fixtures, `Util/CrashReporter.cpp` (minidump + 5 s hang watchdog), ClickTrail click log, `scripts/build_installer.ps1` (keeps PDBs in installer/symbols/v<ver>, strips from package). Possible mismatch: "static runtime" commit 94aba43 vs installer still requiring vcruntime DLLs.
- License (Source/License, 10-12): RSA-PSS, LicenseCache.cpp (329) with a TRUE HMAC-SHA256 + Blowfish (:157,214), HardwareId v1/v2, LicensePolicy.h, ClockGuard, LicenseActivationOverlay (in-window), 15-min recheck.

### 1.6 BeatMonsta (b:/SANTI/VST PROJECT/BeatMonsta) - bonus
Step sequencer extracted from SM5 (v0.4, 2026-06-04): `src/ui/Components/StepSequencerComponent.cpp` (4225) + `RatchetEditorPopup.cpp` (521), `PatternPreviewDialog.cpp` (4795), pattern libraries/classifiers. Relevant only if SAMPLAR's own `SequencerLaneComponent` (src/ui/Components/smode) needs ratchets/probability; otherwise skip.

### 1.7 Shared notes
- `b:/SANTI/VST PROJECT/mistakes.md` Mistake 4 (RUIDO 2026-09-27): cull visuals by real visibility (isShowing + clipped area), never by UI-state flags; sweep real hit points across every header/interactive strip in every state (expanded/collapsed/scrolled/resized). Implemented in RUIDO SelfTestMain (:1328-1340) and LIGHT proc_tests (getComponentAt tests).
- `000CLAUDEBIBLE.md` is the agent-workflow reference (no code assets).

---

## 2. Answers to the six questions

1. Most mature plugin infra: no single sibling has it all. Best-of-breed:
   - APVTS + automation + presets: LIGHT (indexed-slot params, withDefaults, PresetManager.h).
   - Multi-instance safety: RUIDO is cleanest by design, but SM5's ServiceLocator ownership/hand-off fixes are the ones that fit SAMPLAR's identical architecture (plus VST3SmokeHost --two-instances to prove it).
   - Large binary state: Simple Monsta (versioned, message-thread marshalled, embedded gzip audio) for the pattern; SM5 hash-keyed stem_cache for full-song stems (embedding 4 stems = hundreds of MB per project).
   - Editor scaling: SM5 WindowSizing (display-aware + remembered) or Simple Monsta/LIGHT AffineTransform scale.
   - Undo: only Simple Monsta (EditHistory).
   - Port size: SM5 items S-M each (same files); LIGHT APVTS pattern M (new to SAMPLAR); Simple Monsta EditHistory S-M but requires an immutable chop-session snapshot model (M-L).
2. SM5 separation is newer and better (DirectML + CPU fallback, working cancel, stem cache, cross-instance lock, AVX build). SAMPLAR has none of the post-fork separation fixes; same model, same chunking. Nothing to port back from SAMPLAR.
3. UI kit: LIGHT for knob/LNF visuals (OrbKnobLNF/ThinKnobLNF/GemToggleLNF, palette cycling); RUIDO for cards + visibility-aware animated backgrounds; Simple Monsta for the actual per-chop editor (SliceKnobSpecs table + SliceTray + AdsrBar + PadInspector + Reverse/Pitch FX panels, backed by SliceFx/Voice DSP); VARIA TakeEditorDialog for chop-region selection.
4. QA to copy: SM5 VST3SmokeHost (drop-in, two-instance), RUIDO/LIGHT hit-test sweeps (getComponentAt per state), LIGHT audio-thread allocation counter + build_installer.ps1 gates, Simple Monsta editor_repro UI harness + CI workflow, VARIA CrashReporter. pluginval is not scripted anywhere. SAMPLAR already has the release linter.
5. Licensing: for SAMPLAR's code lineage, SM5 Oct-2026 LicenseSystemService/security stack is the best and cheapest (RSA-PSS v2, PKCE, ClockGuard, in-window gate, no Desktop debug file). RUIDO/LIGHT/VARIA LicenseManager is equally strong crypto (VARIA has the only true HMAC cache) but a different architecture (per-processor member) - larger port.
6. Simple Monsta = drum-loop-to-instrument chopper, no separation. Its UX is the model for "intuitive": one-drop -> auto-slice -> pads immediately playable, one strip of tabbed per-pad FX, inspector row, drag-out everywhere, single stem plan driving WAV/MIDI exports, undo.

---

## 3. Port to SAMPLAR

Effort: S < 1 day, M 1-4 days, L 1-2+ weeks. Value: H/M/L.

| # | What | From (file / commit) | To (SAMPLAR) | Effort | Value |
|---|---|---|---|---|---|
| 1 | Build for AVX not AVX2 (DAW crash on older CPUs) | SM5 5de0dee8 (CMakeLists, installer) | CMakeLists.txt:418,749 | S | H |
| 2 | Wire separation cancel; joinable (not detached) S-Mode thread | SM5 cb56199a: FullSongSeparationService.cpp:92,245; FullSongService.cpp:429-439 | src/services/smode/SModeSeparationService.cpp (empty cancelSeparation, detach ~:176), FullSongSeparationService.cpp | S | H |
| 3 | ORT lifecycle hardening (global leaked Env, DisablePerSessionThreads, no spinning) | Simple Monsta core/ml/DrumClassifier.cpp:137-177; SM5 963a2696 | src/services/separation/OnnxSeparationProvider.cpp:90-102 | S | H |
| 4 | Remove Desktop oauth_debug.txt writes | SM5 042b7812 | LicenseSystemService.cpp:173, OAuthBrowserAuthenticator.cpp, DebugConsole.cpp | S | H |
| 5 | In-window license gate (no runModalLoop in editor ctor) | SM5 b0526074: src/ui/Components/InWindowDialog.*, LicenseActivationDialog.cpp, SM5AudioProcessorEditor.cpp | SamplarAudioProcessorEditor.cpp:24-40; LicenseActivationDialog.*; drop JUCE_MODAL_LOOPS_PERMITTED from plugin (CMakeLists.txt:678) | M | H |
| 6 | License crypto upgrade: RSA-PSS v2 signed licences, PKCE OAuth, ClockGuard, HWID v2, 3-day offline window | SM5 src/security/LicenseSignature.*, LicenseSigningKey.h, ClockGuard.h, LicenseCache.cpp; commits 34d398f1, 894f80ec, a58391c4, 362b611b, be797682 | src/security/SignedTokenVerifier.* (replace HMAC), src/networking/OAuthBrowserAuthenticator.cpp, LicenseSystemService.cpp; server side in license_server | M | H |
| 7 | Instance-safe ServiceLocator (owner claims, hand-off on unload, guarded async), thread-safe AppState, fast load | SM5 86e3698a, 3dcdc3ce, be5bd494, f1afbc7c; src/app/ServiceLocator.h (175), AppState.*, tests/unit/ServiceLocatorContextTests.cpp, AppStateThreadTests.cpp | src/app/ServiceLocator.h, AppState.*, src/plugin/SamplarAudioProcessor.* | M | H |
| 8 | VST3 smoke host (load/state/process/editor reopen/two-instances/watchdog) | SM5 tools/vst3_smoke_host/VST3SmokeHost.cpp (481) | tools/vst3_smoke_host/ + samplar-release preflight | S | H |
| 9 | Persistent hash-keyed stem cache + completeness check + cross-instance separation lock; save cache key (not %TEMP% path) in state, re-separate on miss | SM5 FullSongService.cpp:197-252, 385-522; a4900a3b, b6ce642a | FullSongService.cpp:236-241; smode binding in SamplarAudioProcessor.cpp:671-756 | M | H |
| 10 | Per-chop sound engine: SliceFx bundle (EQ/drive/lofi/pitch/reverse/formant) + ADSR + Voice | Simple Monsta core/audio/SliceFx.h, Voice.*, Adsr.h, Reverse.h, Pitch.h, SimpleEq.h, Drive.h, Lofi.h | src/services/MidiMappingService.* Voice struct (:312) - per-slot SliceFx | L | H |
| 11 | Per-chop editor UI: table-driven knob tray, ADSR bar, pad inspector, pad tiles | Simple Monsta plugin/ui/SliceKnobSpecs.h, SliceTray.*, AdsrBar.*, PadInspector.*, PadTile.*, fx/ReverseFxPanel.*, fx/PitchFxPanel.* | src/ui/Components/smode/ (new ChopTray/ChopInspector), SModePage | L | H |
| 12 | Undo/redo for chop edits (immutable snapshots, labelled, depth 100) | Simple Monsta core/session/EditHistory.{h,cpp}, Snapshot.h | new src/services/smode/ChopHistory + SModeSegmentModel | M | H |
| 13 | Lock-free model handoff to audio thread | Simple Monsta core/audio/SnapshotExchange.h (153), RtCommandQueue.h (76) | MidiMappingService slot loads | M | M |
| 14 | APVTS with indexed-slot params for per-stem lanes (s{n}_gain/pitch/cutoff/res/attack/release/reverse...), ParameterID v1, copy/reset, withDefaults | LIGHT plugin/PluginProcessor.cpp:116-224, 635-673, 707-718; tests proc_tests.cpp:194, 2279 | SamplarAudioProcessor.* (new createParameterLayout); keep per-chop data in ValueTree | M | H |
| 15 | Preset browser (user folder, subfolders, next/prev, filename validation) | LIGHT plugin/PresetManager.h (141) | new src/services/PresetService + header bar | S-M | M |
| 16 | Vector knob/toggle LNFs + palette cycling accents | LIGHT plugin/ui/Theme.h (ThinKnobLNF, OrbKnobLNF, GemToggleLNF), DiamondSprite.h, PixelViews.h | src/ui/Style/SamplarLookAndFeel.* | S | M |
| 17 | Visibility-aware animation base (Mistake 4) | RUIDO plugin/ui/AnimatedBackground.h (36) | src/ui/Components/SamplarAnimation.*, GifAnimatedBackground.* | S | M |
| 18 | Display-aware editor sizing + remembered size (or AffineTransform scale menu) | SM5 src/ui/WindowSizing.cpp + tests/unit/WindowSizingTests.cpp (4578d0b7, df5d258a); alt Simple Monsta PluginEditor.cpp:384-394 | SamplarAudioProcessorEditor.cpp:52-54 | S | M |
| 19 | One stem plan driving WAV drag / MIDI lanes / MIDI drag; as-heard 24-bit drag-out to Documents | Simple Monsta core/export/SliceWavExport.h:102-149, core/midi/MidiLanes.h | src/ui/Components/WaveDragArea.*, MidiDragArea.*, SequencerLaneComponent | M | M |
| 20 | Chop-region editor (drag handles, zoom, attack-safe snap, 5 ms fade) | VARIA UI/TakeEditorDialog.h (339), Audio/TakeEdit.h | StemWaveformComponent (src/ui/Components/smode) | M | M |
| 21 | UI hit-test sweep + audio-thread allocation counter self-test | RUIDO tools/selftest/SelfTestMain.cpp:1328-1340; LIGHT tests/proc_tests.cpp:22-37, 1700, 1995 | tests/ (new SModeHitTests) | M | M |
| 22 | DirectML GPU separation (probe, body model, host iSTFT, CPU fallback) | SM5 453e7733/83a99411: OnnxSeparationProvider.*, GpuAdapterProbe.*, HtdemucsIstft.*, tests/unit/HtdemucsIstftTests.cpp, tools/fetch_onnxruntime_directml.py, htdemucs_body.onnx | src/services/separation/*, CMakeLists ORT download (:192), installer .iss, samplar-release preflight | L | H |
| 23 | Release gates (tag==version, no dev-unlock, static CRT, EULA ASCII, PDB archive) | LIGHT build_installer.ps1 (93); RUIDO-saturate build_installer.ps1 (110); VARIA scripts/build_installer.ps1 | 001installer.ps1 / tools/release_linter (add checks) | S | M |
| 24 | Crash reporter (minidump + hang watchdog) | VARIA Source/Util/CrashReporter.cpp | src/utils/ | S | M |
| 25 | Free ML session when last instance closes (refcount) | VARIA PluginProcessor.cpp:94,189 (72f5b9b) | OnnxSeparationProvider owner | S | M |
| 26 | Windows CI (build + unit tests) | Simple Monsta .github/workflows/ci.yml | .github/workflows/ | S | M |

## 4. Recommended order
Safety first (1-5, 7-9: mostly S, all from SM5 on identical files), then licensing (6), then the creative relaunch core (10-12, 14, 11) built on Simple Monsta's engine + LIGHT's param pattern, then GPU (22).

## 5. Caveats
- RUIDO main worktree is ~49 commits behind master; read RUIDO code from `-saturate`/`-shuffle` or master, not the main checkout.
- Simple Monsta embeds audio in state; do not copy that for full songs (4 stems x minutes = hundreds of MB per DAW project). Use SM5 cache + optional "embed stems (FLAC)" toggle.
- SM5 DirectML path adds a 235 MB body model and the DirectML runtime to the installer; samplar-release preflight must check both.
- Exposing every chop as a host param does not scale (4 stems x up to N chops); recommend per-stem lane params in APVTS + per-chop settings in the session ValueTree with EditHistory undo.
