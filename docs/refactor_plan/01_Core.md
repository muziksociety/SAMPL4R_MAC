# 01 – Core App (JUCEApplication, Services, Config)

Requires: `Sample_monsta5/docs/BIBLE_CPP_CONVERSION.md`
Provides: base for all modules; see `02_EventBus_ServiceRegistry.md`.

Goals
- Create the JUCE `JUCEApplication` subclass and `MainWindow`.
- Implement `ServiceRegistry` (owning IService) and lifecycle hooks (`initialise`, `shutdown`).
- Implement `Config` (PropertiesFile + JSON helpers) and `Paths`.

Deliverables
- `src/app/App.h/.cpp`: `class App : public juce::JUCEApplication` with startup/shutdown.
- `src/app/ServiceRegistry.h`: register, get, initialiseAll, shutdownAll.
- `src/app/Config.h/.cpp`: app settings, recent files, feature flags.
- `src/ui/MainWindow.h/.cpp` minimal window hosting a placeholder `WaveEditorView`.

Acceptance
- App starts, shows a window, shuts down cleanly.
- Services can be registered and initialised in order; logs show lifecycle.

Notes
- Defer audio device init to `04_Audio_Engine_and_Graph.md`.

