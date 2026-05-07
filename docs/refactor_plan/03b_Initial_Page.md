# 03b – Initial/Welcome Page

Requires: `Sample_monsta5/docs/refactor_plan/03_UI_Shell_and_Navigation.md`
Provides: parity with SM4 initial/loading pages (`initial_loading_page`, `loading_page`, `loading_animation`).

Goals
- Recreate the landing UI flow with:
  - Welcome panel (product name, version, quick actions)
  - Recent sessions list and “Open Project/Folder” actions
  - System checks: audio devices present, sample rate/latency hints
  - Optional tips/what’s new carousel
  - Loading animation for background boot tasks

Deliverables
- `src/ui/Pages/InitialPage.h/.cpp`: JUCE Component
- Event hooks:
  - `ui.navigate "processing"` / `ui.navigate "browser"`
  - `app.open_folder { path }`
  - `session.open_recent { id }`

Acceptance
- Page appears on first launch and via navigation.
- Background initialisation (services) does not block UI; loading indicator shows until ready.
- Recent items open the correct pages.

