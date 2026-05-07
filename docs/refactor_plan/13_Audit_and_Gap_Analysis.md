# 13 – Audit and Gap Analysis

Requires: `Sample_monsta5/docs/refactor_plan/06_Plugins_Mapping.md`, `Sample_monsta5/docs/refactor_plan/12_Migration_Checklists.md`
Provides: verification that nothing is left behind and corrections plan.

Coverage Checklist (SM4 plugins)
- [ ] ui_shell → MainWindow
- [ ] ui_style_system → LookAndFeel
- [ ] loading_* → Splash/Loader
- [ ] wave_editor_* (frontend/core/audio/backend/effects/theme/loading) → editor modules
- [ ] wave_editor (legacy modular) → replaced by SAMPLAR WaveEditorService/View
- [ ] sample_waveform_display → WaveformView
- [ ] sample_browser_* → SampleBrowser page
- [ ] frequency_analyzer → FFTAnalyzer node + UI
- [ ] frequency_filter → Filters
- [ ] audio_player → Covered by Engine/Preview
- [ ] memory_management → Cache subsystem
- [ ] kit_manager → KitManager page/service
- [ ] sample_detection_engine → Detection service
- [ ] variation_engine → Variation service
- [ ] variable_sample_generator → Generator service
- [ ] audio_processing → OfflineOps service
- [ ] audio_separation_sm2 → SeparationClient (IPC)
- [ ] revibe → Revibe node + UI
- [ ] workflow_dropdown → Workflow page
- [ ] context_menu_system/hamburger_menu → UI wiring
- [ ] license_activation/license_system/security_ui → Licensing services/UI
- [ ] sonar_effect → Sonar node + UI (or folded into analyzer)

Algorithms (effects) – see `05c_WaveEditor_Effects_Port_Map.md`
- [ ] Compressor
- [ ] Compressor v2 (JUCE baseline)
- [ ] EQ
- [ ] Reverb
- [ ] Delay
- [ ] Mod FX (chorus/flanger/phaser)
- [ ] Distortion/Bitcrusher
- [ ] Limiter/Gates
- [ ] HP/LP Filters
- [ ] Tremolo
- [ ] Frequency Shift
- [ ] Reverse (offline)
- [ ] Pitch/Time (lib)
- [ ] Pitch Bend
- [ ] Revibe
- [ ] Slice/Stutter/Tapestop (offline)

Verification Status (first pass)
- Plugin coverage: COMPLETE (all plugin folders in `plugins/` mapped in `06_Plugins_Mapping.md`, including newly added `wave_editor` and `sonar_effect`).
- Effects coverage: COMPLETE (all files under `plugins/wave_editor_effects_sm2/effects/` mapped in `05c`, including newly added `pitch_bend_effect` and `compressor_v2_juce`).
- JUCE reuse: VERIFIED (existing `plugins/wave_editor_juce/juce_integration/src/WaveEditorWindow.h`, `MainComponent.h`, `BufferSource.h` referenced in `05_Wave_Editor_Port.md`, `05a`, `05b`).
- Event contracts: INITIAL SET captured in `02b_Event_Contracts.md` covering UI/nav, wave editor, context menu, separation, licensing.

Outstanding (to execute during implementation)
- Mark each checklist item with concrete C++ module paths once created under `src/`.
- Add golden audio tests per effect and update pass/fail here.
- Confirm each event producer/consumer pair per plugin and list under `02b_Event_Contracts.md` with thread annotations.

Audit Process
1) For each checklist item, link the corresponding C++ module path and commit ranges.
2) Confirm golden tests exist (where applicable) and pass.
3) Confirm event contracts are preserved (names, payload shapes).
4) Record deviations and rationale; open follow-up tasks.

Corrections
- Maintain a `KNOWN_DEVIATIONS.md` with UX/audio differences and their reasons.
- Schedule follow-ups in `12_Migration_Checklists.md` with owners and dates.
