# 03f – Wave Preview Page

Requires: `Sample_monsta5/docs/refactor_plan/03_UI_Shell_and_Navigation.md`
Provides: immediate audio preview UI after selection, before entering full processing/editor.

Goals
- Display a waveform preview of the selected audio (using JUCE `AudioThumbnail`).
- Allow click+drag selection (start/end) and Spacebar to drop a marker at the current selection edge.
- Centralized layout with minimal chrome to keep focus on the audio region.

Deliverables
- `src/ui/Pages/WavePreviewPage.h/.cpp`
  - `loadFile(File)`: loads audio and renders thumbnail
  - Mouse selection + Spacebar marker
- Shell wiring:
  - From Browser: open preview on context “Edit Sample” or double‑click
  - From Initial: after folder selection, navigate to Browser; preview opens when a sample is chosen

Acceptance
- Loading a sample shows a waveform; drag creates a visible selection; Spacebar places a yellow marker.
- Navigation back/forth does not crash; page resizes with the window.

Notes
- Visual/UX should mirror SM4 conventions; refine colours and spacing after parity review against SAMPLAR 4.

