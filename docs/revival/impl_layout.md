# SAMPLAR responsive layout - implementation report

Date: 2026-10-21. Plan followed: `plan_layout.md` (decisions D-9 overlay below bar + disabled controls, D-10 chevron hidden, D-4 stems >= 60%).
Build: `build_layout` (VS 2022 x64 Release), targets SamplarPlugin_VST3 + VST3SmokeHost: GREEN.
Not touched: CMakeLists.txt, SettingsDialog, services/utils, licensing files, `build/`, `build_dml/`. No git operations.

## Files changed
| File | Change |
|---|---|
| src/ui/Pages/SModePage.h | LoadAudioOverlay::resized clamped (button 36-64 px, recents list fits or hides); BigPlayButton dims when disabled; public `setSystemZoneWidth()` / `getSystemZoneBounds()`; new members (TopBarMode, tempoMoreBtn_ "...", seqCollapseBtn_, seqContent_ + seqViewport_, system zone, helpers); `uiSelfTest_` (declared last). |
| src/ui/Pages/SModePage.cpp | Mode-table top bar (FULL/MEDIUM/COMPACT by measured width; bar 64/52/44 by height), tool row + seq header rows, sequencer in a Viewport (lanes 64..112 px, scrolls), collapse toggle, stems >= max(4x64, 60%); overlay starts below bar; Play/Loop/R/Half/Dbl/LOCK/TAP disabled until audio (D-9); tempo overflow PopupMenu (SafePointer); strip x-map from the lane's real wave area (removed 2x hardcoded 178); page-level playhead line removed (B15); playback tick repaints only the strip marker (B32); lanes reparented to seqContent_ in add/remove/unload; Smart Fill / +SEQ auto-expand; 3 never-laid-out buttons (Auto: Bar/2-Bar/Transient, 0x0 but "visible") hidden; layout log once per change. **Bug fix found by the sweep:** `unloadCurrentAudio()` now detaches the model hooks before nulling `smodeService_` (Eject then plugin unload crashed in `SModeService::shutdown -> model.clear -> onUndoStackChanged` on a dead page). Debug-only `debugLoadSyntheticStems()`. |
| src/ui/Components/smode/StemWaveformComponent.{h,cpp} | Header width 178 -> 160, 3 header modes Tall (>=140) / Compact (62-139) / Mini (<62), mode applied + logged only on change; chevron never shown; `getWaveArea()`; **B32**: waveform cached in a `juce::Image` keyed on size/scale/view range/audio generation/spectral state (rebuilt only then), `setPlayheadTime()` repaints only 5 px old/new columns; SEPARATING font capped 14..28; debug paint counters. |
| src/ui/Components/smode/SequencerLaneComponent.{h,cpp} | One control row (chrome 60 -> 34 px): "SEQ n" + dots + controls left, CLEAR/WAV/X right; `getDotArea()` single source for paint/mouseDown/mouseMove. |
| src/ui/Components/smode/SModeUiSelfTest.h (new, header-only) | Env-gated (SAMPLAR_UI_SWEEP_DIR) hit-test sweep + snapshot + perf probe; inert (nullptr) otherwise. Verified: plain smoke log has 0 "UiSelfTest" lines. |
| src/ui/Components/smode/SModeMidiLearnOverlay.cpp | Fixed-size centred panel (420x200 max) - text no longer clipped at 850x500. |
| src/ui/Components/WaveDragArea.cpp | Label falls back to "WAV" / "..." when "Drag all waves" does not fit. |
| src/ui/Style/SamplarLookAndFeel.cpp | TextButton font = clamp(10, 13.5, h*0.62); rotary inset scales so 20-40 px knobs draw. |
| src/ui/MainWindow.cpp | RootComponent::resized places window buttons in the page's system zone (2 plugin / 5 standalone, 20 px + 4 gap), re-centres UpdateDialog, logs once per change; `isPlugin_`; standalone MainWindow `setResizeLimits(850,500,1810,1130)`. |
| tools/vst3_smoke_host/VST3SmokeHost.cpp | `--ui-sweep <outdir>` (see below). |
| SamplarAudioProcessorEditor | No change needed (already 1060x724, 850x500..1810x1130). |

## How the sweep works (and why not in the host)
The host only holds JUCE's VST3 wrapper editor; the plugin's components live in another DLL behind their own HWND, so the host cannot walk them or snapshot them. The host therefore calls `ed->setSize(W,H)` (real VST3 resize path) and drives a plugin-side self-test through the process environment (`SAMPLAR_UI_CMD="<n>|verb|arg"`, ack file per command). Sweep root = RootComponent (whole UI without the editor-level licence panel). This machine is NOT licensed ("license server reply could not be verified"), so the root is disabled behind the sign-in panel; the sweep re-enables it only for the synchronous check+snapshot and restores it before returning (no event pumping in between - nothing is unlocked). Rule per component (Button/Slider/ComboBox/TextEditor/ListBox/WaveDragArea/SequencerSlot/StemWaveform): visible+enabled -> >= 16x16, inside parent, not covered by a later sibling, `getComponentAt` at a 3x3 grid of its visible area returns it or a descendant; parts clipped by a Viewport are INFO (scroll).

States: empty, loaded1 (4 synthetic sine stems + 1 seq lane), loaded4top / loaded4bottom (4 lanes, viewport scrolled), collapsed, ejected - each at 850x500, 1060x724, 1810x1130.

## Sweep results
BEFORE (current tree + self-test hook only, `sweep_before/`):
| state | 850x500 | 1060x724 | 1810x1130 |
|---|---|---|---|
| empty | 17 FAIL | 12 FAIL | 11 FAIL |
| loaded1 | 26 FAIL | 13 FAIL | 0 |
| loaded4 (top/bottom) | 34 FAIL | 21 FAIL | 12 FAIL |
| collapsed (n/a, no collapse) | 26 FAIL | 13 FAIL | 0 |
| ejected | 17 FAIL | 12 FAIL | 0 |
(0x0 "visible" buttons under the overlay, S/M/L 0 px high, stacked tempo buttons, LOAD SONG 0 px, etc. - B13/B14 confirmed.)

AFTER (`sweep_final/`, `sweep_final.log`): **18/18 sweeps fail=0**. checked counts: empty 10, loaded1 62-66, loaded4 93-97 (INFO 30 at 850 / 10 at 1060 = lanes scrolled out of the viewport, expected), collapsed 63-67, ejected 8.

PNG snapshots (looked at: no clipping/overlap): `C:/Users/Muzik/AppData/Local/Temp/claude/b--SANTI-VST-PROJECT-SAMPLAR/4dadef21-585f-41d5-bed7-0a9d8f807952/scratchpad/sweep_final/<state>_<WxH>.png` (e.g. loaded1_1060x724.png, loaded4bottom_850x500.png, loaded4top_1810x1130.png, collapsed_1060x724.png, empty_850x500.png, ejected_1060x724.png). Before: `.../scratchpad/sweep_before/*.png`.

Layout log (plugin console, matches plan section 4.1 arithmetic exactly):
```
[SModePage] layout: bar=COMPACT/44 lanes=4x64 seq=1 open seqH=85 sys=44     (850x500)
[SModePage] layout: bar=MEDIUM/52 lanes=4x111 seq=1 open seqH=112 sys=44    (1060x724)
[SModePage] layout: bar=FULL/64 lanes=4x209 seq=1 open seqH=112 sys=44      (1810x1130)
[SModePage] layout: bar=MEDIUM/52 lanes=4x82 seq=4 open seqH=226 sys=44     (4 lanes, viewport scrolls)
[SModePage] layout: bar=FULL/64 lanes=4x142 seq=4 open seqH=383 sys=44
[StemWaveform] stem=0 header mode -> Compact (h=111) / Tall (h=140 collapsed) / Compact (h=64)
[RootComponent] resized: SModePage=4 4 1052 716 sysZone=992 8 44 52
```

## Mockups (as built)
1060x724 (MEDIUM, 1 seq lane):
```
| [S] [>][x]@ [Undo][Redo] [grid v][120.00][Half][Dbl][LOCK][TAP][...]      [ LOAD SONG ][X] (*)(i) |  bar 52
| [=========== playhead strip (x-map = lane wave area) ===================================]        |
| |# [mic] Vocals (o)| ~~~~~ waveform ~~~~~                                              | 111 px x4  |
| |# [S][M][L][@]  (o)|                                                                 |           |
| [Auto: Beat][Quick Assign][POLY 1SH][LINK ALL] [x]Snap [ ]Free [x]Fade [x]Tips   [Clear All]        |
| [v SEQ][+ SEQ][Play All][Smart Fill]                                     [~ Drag all waves]         |
| | 1        2        3        4    slots 78 px                                          |            |
| | SEQ 1 (V)(B)(D)(M)[>][#][M][SHUFFLE][AABA v][4 bars v][x1 (bar) v]    [CLEAR][WAV][X] | 112        |
```
850x500: COMPACT bar (Half/Dbl/LOCK/R/DAW in "..." menu), stems 4x64 Compact headers, seq 85 px (scrolls with 4 lanes).
1810x1130: FULL bar (BPM: label, R, DAW, Half/Dbl/LOCK inline), stems 4x209 Tall headers (S/M/L row + 64 px knob with "+n st").
Before mockups: plan section 2 (confirmed by sweep_before PNGs).

## B32 performance
Self-test `perf` at 1060x724, 4 lanes, 20 s stereo stems, 90 frames with a moving playhead:
| metric | before | after |
|---|---|---|
| lane paint() time, 4 full lanes per frame | 27.97 ms | 9.9-11.1 ms (cache hit, 0 rebuilds) |
| full-lane render incl. snapshot overhead | 46.8 ms | 34.3-39.3 ms |
| area repainted per playback tick | whole page (every lane, seq lanes, bar) | 2 x 5 px columns per lane + 2 x 13 px strip marker |
On-screen per-frame time could not be measured here: the opaque licence panel covers the UI on this unlicensed machine, so the screen flush paints nothing (lane_paint_calls during the flush loop are 0 in both builds). Remaining full-lane cost is the gradient/rounded-clip background, now only paid on resize/zoom, not per tick.
Note: `drawImage` is modulated by the current colour opacity - fixed with `g.setOpacity(1)` (first attempt drew the cached waveform at 13% after drawGrid).

## Smoke
- `VST3SmokeHost --plugin build_layout/.../SAMPLAR.vst3 --show-ms 2500` -> RESULT: ALL PASS (exit 0)
- `... --two-instances` -> RESULT: ALL PASS (exit 0)
- `... --ui-sweep <dir>` -> RESULT: ALL PASS (exit 0) (before the Eject fix this run segfaulted on unload, in both old and new layout).

## Left undone / notes
- Standalone (pin/drag/exit, 116 px system zone) compiled but not swept (SamplarApp not built in this task).
- Hit-test sweep cannot verify behaviour behind the licence panel on a licensed machine's screen; layout/hit-test logic is identical.
- Pre-existing, not in scope: grid combo shows no text in snapshots; BPM field keeps the old value after Eject; segment-trigger animations still repaint whole lanes at 30 Hz; `seqCollapsed_` and editor size are not persisted.
- Suggest for other owners: none required in CMakeLists (new file is header-only).
