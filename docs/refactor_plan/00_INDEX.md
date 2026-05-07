# SAMPLAR – JUCE/C++ Refactor Plan (Index)

Purpose: deliver a 1:1 rebuild of SAMPLAR (SM4) in native C++/JUCE while preserving BIBLE.MD architecture (services, event bus, plugins, contracts). This index ties all plan documents together and enforces dependencies between them.

Read first:
- `Sample_monsta5/docs/BIBLE_CPP_CONVERSION.md` – BIBLE→C++/JUCE mapping and directory layout.
- `Sample_monsta5/docs/PYTHON_ML_INTEGRATION.md` – Demucs/ML via IPC strategy and contracts.

Plan modules (in order):
1. `01_Core.md` – App core, lifecycle, config. Requires: BIBLE_CPP_CONVERSION.md
2. `02_EventBus_ServiceRegistry.md` – Pub/Sub + service wiring. Requires: 01_Core.md
3. `03_UI_Shell_and_Navigation.md` – JUCE UI shell mirroring `ui_shell`. Requires: 02_EventBus_ServiceRegistry.md
4. `03b_Initial_Page.md` – initial/welcome/loading page parity. Requires: 03
5. `03f_Wave_Preview_Page.md` – quick preview with waveform, selection, markers. Requires: 03
6. `03c_Processing_Page.md` – processing workflows hub. Requires: 02 + 07
7. `03d_Sample_Browser_Page.md` – sample browser UI. Requires: 03
8. `07_Python_IPC_Integration.md` – C++ client for separation/export (used by Processing Page). Requires: PYTHON_ML_INTEGRATION.md
9. `04_Audio_Engine_and_Graph.md` – audio device, graph, parameters (can start in parallel; needed for playback later). Requires: 01
10. `05_Wave_Editor_Port.md` – end-to-end wave editor mapping (reuse first). Requires: 03 + 08
   - `05a_WaveEditor_UI.md`, `05b_WaveEditor_Audio.md`, `05c_WaveEditor_Effects_Port_Map.md`
10. `06_Plugins_Mapping.md` – every SM4 plugin mapped to C++ modules. Requires: 02 + 03
11. `08_Licensing_Security.md` – license + security UI/services. Requires: 02 + 03
12. `09_Presets_Sessions.md` – presets, session save/restore. Requires: 01 + 02
13. `10_Packaging_CMake_Tooling.md` – build, deps, optional features. Requires: 01
14. `11_Testing_QA.md` – unit, audio golden, UI smoke. Requires: all above
15. `12_Migration_Checklists.md` – actionable step lists per module. Requires: all above
16. `13_Audit_and_Gap_Analysis.md` – coverage review and corrections. Requires: 06 + 12

Usage:
- Work top-to-bottom; do not start a module without its “Requires”.
- Each module states its deliverables and acceptance criteria.
