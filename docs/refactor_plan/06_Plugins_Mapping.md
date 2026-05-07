# 06 – Plugins Mapping (SM4 → SAMPLAR/JUCE)

Requires: `Sample_monsta5/docs/refactor_plan/02_EventBus_ServiceRegistry.md`, `Sample_monsta5/docs/refactor_plan/03_UI_Shell_and_Navigation.md`
Provides: exhaustive mapping of Python plugins to C++ services/components with migration order.

Legend
- Type: UI, Service, Audio, Utility
- Target: C++ module(s)
- Order: migration sequence

| Plugin | Type | Purpose | Target (C++) | Order |
|---|---|---|---|---|
| ui_shell | UI | Main window, navigation | `src/ui/MainWindow.*` | 1 |
| ui_style_system | UI | Styling/theme | JUCE LookAndFeel + colour scheme | 2 |
| window_drag_handle | UI | Frameless drag | Custom title bar | 2 |
| loading_page / initial_loading_page / loading_animation | UI | Splash/loading | SplashScreen component | 2 |
| app_loading_service | Service | Boot pipeline | App startup tasks | 1 |
| sample_browser_ui / sample_browser_modular | UI | Browser | `src/ui/Pages/SampleBrowser.*` | 5 |
| sample_waveform_display | UI | Waveform widget | `WaveformView.*` | 4 |
| wave_editor_frontend | UI | Editor UI | `WaveEditorView.*` | 4 |
| wave_editor_core_sm2 | Service | Models/tools/state | `WaveEditorService.*` | 4 |
| wave_editor_audio_sm2 | Audio | Playback/export | `PreviewEngine.*`, `ExportManager.*` | 4 |
| wave_editor_effects_sm2 | Audio | DSP effects | `Nodes/*` (see 05c) | 4 |
| wave_editor_backend | Service | Orchestration | merged into WaveEditorService | 4 |
| wave_editor | UI/Service | Legacy modular editor | replaced by SAMPLAR WaveEditorService/View | — |
| wave_editor_juce | Bridge | Legacy embed | replaced by native JUCE | — |
| wave_editor_loading_integration | Service | Wiring | moved to WaveEditorService | 4 |
| wave_editor_theme_sm2 | UI | Theme | LookAndFeel | 3 |
| frequency_analyzer | UI/Audio | Spectrum | `Nodes/FFTAnalyzer.*` + UI | 6 |
| frequency_filter | Audio | Filters | `Nodes/Filters/*` | 5 |
| audio_player | UI/Audio | Simple player | part of Engine/Preview | 3 |
| memory_management | Service | Cache/GC | folded into Cache subsystem | 3 |
| kit_manager | UI/Service | Kits | `Pages/KitManager.*` + service | 7 |
| sample_detection_engine | Service | Detection | `services/Detection.*` | 7 |
| variation_engine | Service | Variations | `services/Variation.*` | 7 |
| variable_sample_generator | Service | Generator | `services/Generator.*` | 8 |
| audio_processing | Service | Offline ops | `services/OfflineOps.*` | 6 |
| audio_separation_sm2 | Service | Demucs etc. | `services/SeparationClient.*` (IPC) | 3 |
| revibe | Audio/UI | Effect | `Nodes/RevibeNode.*` + UI | 6 |
| workflow_dropdown | UI | Workflow switcher | `Pages/Workflow.*` | 6 |
| context_menu_system | UI | Context menus | JUCE PopupMenu wiring | 3 |
| hamburger_menu | UI | Navigation affordance | part of MainWindow | 3 |
| license_activation / license_system / security_ui | Service/UI | Licensing | `services/Licensing.*` + UI | 2 |
| sonar_effect | Audio/UI | Sonar visual/FX | `Nodes/SonarNode.*` + UI (or fold into analyzer) | 6 |

Notes
- Some Python plugins are “thin glue” and will be merged into richer C++ services (e.g., loading_integration → WaveEditorService).
- Migration order ensures core app and editor come first, then browsing, then advanced services.

Prototypes/Reference Services
- `services/juce_audio_core` → superseded by `src/audio/*` (patterns may be reused)
- `services/cpp_audio_core` → superseded by `src/audio/*` (patterns may be reused)
- `services/rust_audio_core` → not targeted in SAMPLAR; document if kept for experiments

Processing Page composition
- Uses: `audio_separation_sm2` (→ SeparationClientService), `audio_processing` (→ OfflineOpsService),
  `variation_engine`, `variable_sample_generator`, and `sample_detection_engine` as task sources.
