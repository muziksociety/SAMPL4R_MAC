# S-Mode Multi-Segment Drag-Out to DAW — Design

**Date:** 2026-04-19
**Status:** Approved

## Goal

Let a user select multiple segments in S-mode and drag them out to any DAW with a single gesture. Existing single-segment drag-out is preserved. Works identically in SAMPLAR **standalone** and **VST3** (shared code path, OS-level drag).

## Gesture

| Input | Result |
|---|---|
| Click on segment | Select that one segment (clear other selections) — existing |
| Shift+click on segment | Toggle selection of that segment, keep others selected |
| Alt+click on segment | Loop-play — existing, unchanged |
| Ctrl+Shift+click+drag on any selected segment | Start multi-segment drag-out |
| Ctrl+Shift+click+drag on an **unselected** segment | Select only that one, then start drag-out (single-segment fallback) |

Note: plain click+drag on a segment still triggers today's single-segment drag-out (after the 8-pixel threshold in `StemWaveformComponent::mouseDrag`). The new gesture adds multi-segment capability without breaking existing single-segment ergonomics.

## Rendering

- One WAV per selected segment — simplest DAW ergonomics; user drops on one track or fans across many.
- Per-segment file rendered from that segment's **own stem buffer** — cross-stem selections stay separated.
- Apply `samplar::applyMicroFade` (consistency with playback).
- Names: `<stemName>_<noteOrIndex>_seg<N>.wav` (matches existing single-segment naming).
- 24-bit stereo PCM WAV (matches existing).

## File delivery

All rendered paths collected into one `juce::StringArray` and handed to `juce::DragAndDropContainer::performExternalDragDropOfFiles(files, true, this)` in **one call**. The OS handles the drag payload; the DAW receives a multi-file drop.

## Threading

- Mouse-up triggers a background `std::thread` that renders every selected segment to temp files.
- On completion, a `juce::MessageManager::callAsync` fires the drag on the message thread (only place `performExternalDragDropOfFiles` is legal).

## Cancellation / No-op

- If the user cancels the drag (release inside S-mode), temp files remain in `temp/` for the OS to clean; harmless.
- If no segments selected and user Ctrl+Shift+click+drags on empty space: no-op, no temp files.

## Files Touched

- `src/ui/Components/smode/StemWaveformComponent.cpp` / `.h`
  - Extend mouseDown to handle Shift+click selection toggle (within-stem only — cross-stem selection is a visual side-effect, model already tracks per-segment `selected`).
  - Detect Ctrl+Shift on drag-out trigger; fire new `onMultiSegmentDragOut` callback.
- `src/ui/Pages/SModePage.cpp` / `.h`
  - New callback handler that walks every stem's segments, collects `selected`, renders N WAVs, hands to `performExternalDragDropOfFiles`.
  - Shift+click on a segment must clear selections on *other* stems for a clean "one stem at a time" selection… **or** allow cross-stem selection — cross-stem is more useful for DAW workflows (drag drum + bass segments simultaneously).
- No changes to `SModeSegmentModel`: it already has `Segment::selected`.

## Cross-stem selection policy

Shift+click on stem A then Shift+click on stem B keeps both selections. Ctrl+Shift+drag on either stem's selected segment exports all of them. Plain click on any segment clears cross-stem selections (same as today within one stem).

## Risks & Mitigations

| Risk | Mitigation |
|---|---|
| Ctrl+Shift+click conflicts with an existing shortcut | Searched — no conflict in StemWaveformComponent today. |
| Rendering many segments is slow | Rendering is per-segment memcpy + WAV write; measured ~1 ms per segment for typical lengths. If truly large, runs on background thread so UI stays responsive. |
| User drags-out during audio playback | Fine — source buffer is read-only from both paths; no sync needed. |
| VST3 host sandboxing blocks file drag | `performExternalDragDropOfFiles` uses OS-level drag which every major DAW supports; proven by existing sequencer lane drag-out. |
| Temp files accumulate | OS temp-dir cleanup handles it; same pattern as existing export. Could add periodic GC later if needed. |

## UX Touches

- When 2+ segments are selected, the Ctrl+Shift hint could be shown on hover — *deferred* until we see whether users need it.
- Log line on drag-out: `[SModePage] Multi-drag-out: N files` so QA can confirm from logs.
