# 03 – UI Shell and Navigation (JUCE)

Requires: `Sample_monsta5/docs/refactor_plan/02_EventBus_ServiceRegistry.md`
Provides: JUCE analogue of SM4 `ui_shell` (main window, sidebar, page host).

Goals
- Create `MainWindow` with a sidebar (TreeView or ListBox) and a central `StackedComponent`.
- Event contracts:
  - `ui.register_page { name, component, displayName, order }`
  - `ui.navigate "name"`
  - `ui.register_widget_factory { name, factory }` (for on-demand views)
- Navigation, focus, and keyboard routing.
- Debug mode (F1x10): built-in console panel toggled by pressing F1 ten times within 4s.
  - Captures `juce::Logger` output; supports clear; resizes as a bottom dock.

Deliverables
- `src/ui/MainWindow.h/.cpp` with `registerPage(...)`, `navigate(name)`.
- `src/ui/Pages/` folder for built-in pages (e.g., Wave Editor View placeholder).
- Event handlers wired to EventBus.

Acceptance
- Plugins can register pages; navigating switches visible component.
- Works on HiDPI; persists last page in Config.
- Debug console toggles with F1x10 and shows logs.

Links
- Consumed by `05a_WaveEditor_UI.md`, `06_Plugins_Mapping.md`.
