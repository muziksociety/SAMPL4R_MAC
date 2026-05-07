# SAMPLAR — Changelog

## 1.3.0 — 2026-04-24

### New

- **Mentor — an interactive, built-in guide.** A new `?` button lives in the
  top-right window-control cluster on every screen. Click it to open the
  Mentor: context-aware lessons that match the page you are on. Click it
  again to go back to where you were.
  - Covers every workflow: *Getting Started*, *Full Song*, *Drum Stem*,
    *S-Mode*, *Drum Library*, *Sample Browser* and *Wave Editor*.
  - Full English + Spanish. First launch auto-detects your OS language.
    Switch any time with the `EN / ES` toggle.
  - Rendered with real UI mockups inside the steps so you can see what
    each button, dropdown or dot looks like in the real app.
  - A gentle nudge reminds new users where Mentor lives; it stops
    appearing after you open it the first time.

- **Workflow mode buttons on the start screen.** Replaced the old
  workflow dropdown with three clearly-labelled buttons — **DRUM STEM**,
  **FULL SONG** and **S-MODE** — placed above the drop zone. The
  currently-selected mode lights up with a cyan outline. Each button
  has a tooltip explaining what the mode does.

- **Drum Library: visible `+ Add Folder` button.** Folders can now be
  added with a button at the top of the folder pane. Right-click and
  drag-drop still work as alternatives.

### Improvements

- **Start screen redesign.** The BROWSE DRUM LIBRARY card now sits
  symmetrically below the drop zone with matching width, and the drop
  zone itself stretches to fill the available space between the mode
  buttons and the library button.

- **Sample Browser step sequencer → DAW.** Drag MIDI out of the
  sequencer into a DAW running the SAMPLAR VST and the pattern
  now plays the *same* samples your sequencer is using. MIDI notes are
  aligned with the plugin's live sample-to-note mapping, so the kit
  persists with your project.

- **`Show all lessons` toggle in Mentor** for when you want to browse
  every workflow's lessons at once rather than stay scoped to the
  current page.

- **Clear, scrollable step view** inside Mentor — paragraphs, progress
  bar ("Step N of M"), completion checkmarks in the lesson sidebar,
  and an attention-catching pulse on the **Next** button.

- **Tooltips everywhere they matter** — workflow mode buttons, add
  folder button, Mentor button, language toggle.

- **Top-of-window button cluster rearranged** so the Mentor `?` sits
  next to the traffic-light controls and is easy to reach.

### Fixes

- **Computer-keyboard triggering no longer machine-guns samples.** When
  using your laptop keyboard as a MIDI trigger, holding a key down used
  to spam the sample and bypass POLY / MONO / 1SH / Gate controls. A
  held key now fires exactly once on press and once on release, like a
  real MIDI keyboard. Affects both Sample Browser tiles and S-Mode
  segments.

- **Select All on the Sample Browser really means all.** Previously
  it only selected the visible page. It now selects every sample
  across every page. And your selection no longer clears when you
  flip to a different page.

- **S-Mode "Delete Segment" now deletes one segment.** With LINK ALL
  (or a per-lane link) on, deleting a segment used to wipe segments
  across every linked stem. Only the targeted segment is removed now;
  every other segment on every stem stays intact, with its MIDI
  assignments preserved.

- **Empty step sequencer no longer drags a blank MIDI file** into your
  DAW.

- **Removed the old page-specific `?` tooltip** on Wave Preview. The
  single global Mentor button handles help now.

- **Removed the CHAT button** from the Drum Library / Sample Browser
  top bar (replaced by Mentor + upcoming community integrations).

- **Settings button glyph** now displays correctly.

- **Language choice is remembered** across app launches.

### Under the hood

- Pre-release linter now covers source/CMake drift, variation engine
  wiring, installer manifest, event bus topics, `IService`
  registration, DLL portability, hardcoded dev paths, and staging-vs-
  build drift. Every check must pass before a release build ships.

- Performance: background BPM / tempo detection pipeline optimised
  with a content-hash cache and a hybrid detection strategy.

---

## Previous releases

See git history or earlier installer archives for the full list.
