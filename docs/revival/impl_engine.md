# SAMPLAR engine rework - implementation report (2026-10-21)

Build dir: `build_engine/` (Release + Debug EngineTests). Nothing committed.

## Architecture (new files)
- `src/audio/engine/EngineTypes.h` - EngineParams (atomics: stem pitch, mod routing, velocity curve, mono/gate/loop/learn/seqBpm), HostInfo, POD Command / Notification / RawMidi.
- `src/audio/engine/RtFifo.h` - SpscFifo (juce::AbstractFifo, fixed storage) and MpscFifo (non-audio producers serialise on a SpinLock; the audio consumer never locks).
- `src/audio/engine/EngineSnapshot.h` - immutable Snapshot: shared stem buffers (+ raw pointers), segment table, note map (128 lists), per-stem volume/audible, trimmed sequencer lanes (cellStart / cellCover), transport length.
- `src/audio/engine/PlaybackEngine.{h,cpp}` - THE renderer for MIDI, QWERTY, audition, sequencer, stem transport and export. SnapshotManager (RCU owner) lives here too.
- `src/audio/engine/OfflineRender.{h,cpp}` - renderSequence / renderSegment (same engine), resampleStem (Kaiser-windowed sinc).
- `src/audio/engine/tests/EngineTests.cpp` + `cmake/extra_engine.cmake` (EngineTests is EXCLUDE_FROM_ALL).

RCU protocol: the message thread publishes a raw `const Snapshot*` with an atomic store. process() sets inProcess, loads the pointer and stores ackSeq. Each voice records its snapshot seq, and the engine publishes the oldest seq still in use. SnapshotManager frees retired snapshots on the message thread only, below min(ack if busy, oldest voice seq). The audio thread never owns, copies or drops a shared_ptr. Commands carry the seq they were issued against; when a command is newer than the loaded snapshot, the engine reloads it.

## Finding -> fix -> evidence
| ID | Fix | Evidence |
|---|---|---|
| A1 | Voices read a [start,end) window of the shared stem buffer. `StemData::audioStorage` (shared) + `SModeSegmentModel::setStemAudio()`; `audioBuffer` refers to it (existing API kept). Note lookup uses the snapshot noteMap: no vector, no copy, no callAsync. | EngineTests "zero heap allocations": 10,004 events / 3,150 blocks / 47 snapshot swaps, allocs=0 frees=0. In Debug the CRT malloc hook is on too (covers JUCE HeapBlock malloc) and also gives 0. |
| A2 | Every UI action is a Command in an MPSC FIFO (stop, play segment/slot, note, transport, sequencer, all-sound-off). Only the audio thread touches voices and stretchers. Notifications go out through an SPSC FIFO, drained by a 30 Hz MidiMappingService timer. | `playBufferDirect` is gone. `grep` finds no Logger/callAsync in PlaybackEngine.cpp; the only locks are in SnapshotManager (message side). |
| A3 | The model is never read by the audio thread. The timer rebuilds the snapshot when `computePlaybackHash()` changes (segments, notes, loop, mute/solo/volume, buffers, rate). UI play actions call `syncSnapshotNow()` first. | RCU test: an old snapshot is kept while a voice plays it and freed afterwards (retired=0). |
| A9 | The host BPM is stored in an atomic; the timer publishes `host.tempo` (incl. re-request and the 120 BPM fallback). processBlock no longer logs or posts. | `SamplarAudioProcessor::processBlock` |
| A10 | The block is split at each MIDI event's samplePosition (raw bytes, sysex skipped). The host isPlaying / ppq / bpm is read each block. Pitch bend, CC1, aftertouch and channel pressure from the VST3 buffer reach the existing per-stem mod routing. Other CCs are forwarded to the existing CC-learn / binding code on the message thread. | "Onset unpitched" expected=1673 got=1673 (+-0) |
| B36/B26 | The sequencer clock runs on the audio thread: host-synced from PPQ while the DAW plays, free-running (integer sample clock) otherwise. Each lane loops on its own length and chokes only its own voice. SModePage/SequencerSlot now send commands and show the playhead / playing slot from engine atomics; the callAfterDelay chain is deleted. Lane drag-WAV, Play-All drag-WAV, segment drag-out and multi drag-out all render through the engine. | "steps land on host PPQ" 4/4, worst error 0 samples. "offline == realtime bit for bit (incl. pitched stem)" maxDiff=0. "unpitched render identical for any block partition" maxDiff=0. |
| A11 | GATE releases one-shots on note-off. Looped MIDI chops always release on note-off. CC123 releases all MIDI voices; CC120 also releases sequencer voices and stops the transport. When the host stops a host-driven sequencer, everything stops. | Four tests PASS (gate, loop in 1SH, CC123, CC120, host stop) |
| A12 | Pitched voices pre-roll with signalsmith `outputSeek()`, so their output is aligned with unpitched voices. Latency reported = 0 (setLatencySamples). Tails are flushed for seekLen samples, then a 3 ms fade. A direct-to-pitched switch mid-voice uses a 5 ms crossfade. | "pitched voice aligned" delta=0 samples (was 30-80 ms late). The log shows "latency reported: 0". |
| A13 | 2 ms edge fades (existing MicroFade flag). 3 ms release fades on stop, steal, gate, cell end and bypass. 5 ms loop crossfade using pre-roll audio, with a dip fallback. Transport start uses an attack ramp. | Max step on full-scale DC = 0.0113 (hard cut = 1.0). Loop wrap max step = 0.0313, equal to the sine's own max step. |
| A14 | Transport length = longest stem (muted stems included); position is clamped and wrapped. | Pos 1.966 s finite while all stems are muted; unmuting resumes |
| A15 | MIDI/audition pool: 32 voices + 8 fade reserve. Sequencer pool: 24. Transport pool: 16. Steals the oldest voice in its own pool, with a fade. MONO is scoped to the MIDI pool. | Steal test: max step 0.0029, 32 voices active |
| A22 | Rendering is chunked to the prepared size; the random test uses blocks up to 4096 with maxBlock 512. | Zero allocations, no NaN |
| A23 | Stems are resampled once to the host rate on a background pool (Kaiser sinc, anti-aliased when downsampling). Until the copy is ready, 4-point Hermite on the fly. | Resample SNR 120.7 dB (JUCE WindowedSinc measured 40 dB) |
| A25 | `*v = Voice{}` on every start | code |
| A26 | Tail = longest chop + 0.25 s (cap 60 s). processBlockBypassed fades once, then outputs silence (host input never passes through). | "Bypass" PASS |
| A27 | Standalone MIDI goes through an MPSC raw-MIDI FIFO into the audio thread. CC-learn callbacks run on the message thread. | code |
| A28 | Stretchers are only used by pitched voices; there is no per-block reset. | code |
| B6 | Deleted `src/audio/SamplePreviewCache.*`, midiMap_, the mapSampleToNote/lock/registerSampleAtNextFreeNote family, the browser.page.changed subscription, midi.pad.* and the slot save/restore API. Zero callers were verified with grep. Removed the processor "midi" binding; AppState keeps the unknown "midi" subtree, so old projects still load. | Build green |

## Verification
- Build: `SamplarPlugin_VST3`, `VST3SmokeHost`, `EngineTests` (Release, plus Debug for EngineTests): green.
- VST3SmokeHost `--show-ms 2500`: ALL PASS.
- `--two-instances` 5/5: ALL PASS (unload 29-97 ms).
  - An earlier run failed on timing (unload 3-7 s, reopen 835 ms) only while my Debug compile was running in parallel. It passed on rerun with no code change.
- `--ui-sweep`: ALL PASS (15 sweeps, 0 failures).
- EngineTests: Release 21/21 PASS; Debug 21/21 PASS (with the CRT malloc hook, debug STL checks and JUCE assertions logged). Output: `scratchpad/eng/tests_final.txt` and `tests_dbg_final.txt`.

## Behaviour changes to know
- The stem pitch knob now applies to sequencer playback, slot audition and exports (it used to be bypassed there; B26 asked for one path).
- Slot pitch is now time-preserving (pitch shifter) instead of tape-style resampling.
- Slot speed is the playback rate; the voice is cut at the cell end.
- Play-All loops each lane on its own length.
- Sequencer slots now honour stem mute.
- Slot `probability` is still ignored, as before.
- Exports are always stereo, at the model rate.
- Closing the editor still stops the sequencer (unchanged UX).
- Per-voice gain includes the stem volume.

## Not done / follow-ups
- Pitched voices: output depends on the host block partition (signalsmith processing granularity). Offline and realtime are bit-identical when both use the same block size.
- MidiMappingService's 30 Hz snapshot timer is the change detector. Model edits reach DAW MIDI within about 33 ms; UI-triggered plays sync immediately.

## Changes needed in files I do not own
- `CMakeLists.txt` line ~256: remove `src/audio/SamplePreviewCache.cpp`. It is currently filtered out from `cmake/extra_engine.cmake`, so the build already works.
- `src/plugin/SamplarAudioProcessor.{h,cpp}`:
  - I removed the "midi" binding block in `registerDefaultBindings` (required by B6, because it called deleted APIs).
  - `lastHostBpm_` is now unused.
  - The state agent should know about both.
- `SModePage` `static int midiMode` (A6) is still process-global. Not touched (not playback wiring).
