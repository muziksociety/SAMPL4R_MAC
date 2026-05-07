# 03d – Sample Browser Page

Requires: `Sample_monsta5/docs/refactor_plan/03_UI_Shell_and_Navigation.md`
Provides: parity with SM4 `sample_browser_ui` / `sample_browser_modular`.

Goals
- File system browser with:
  - Sidebar: folder tree / favorites / recent
  - Main: grid/list of samples with waveform thumbnails (later)
  - Preview controls (later: requires audio engine)
  - Context menu actions: edit sample, send to processing queue, tag, reveal in explorer
  - Search, filters, sorting

Deliverables
- `src/ui/Pages/SampleBrowserPage.h/.cpp`
- Event hooks:
  - `browser.open_folder { path }`
  - `context_menu.action.sample_edit_requested { widget, sample_index, sample_path }`
  - `processing.enqueue { op, inputs, options }`

Acceptance
- Navigation to folders works; context menu fires expected events.
- Preview placeholder present; wired to Audio Engine when available.

