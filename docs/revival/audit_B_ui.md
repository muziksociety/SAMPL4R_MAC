# Audit B - SAMPLAR UI layer and user flow

Scope: src/ui/Pages/SModePage.{h,cpp} (read in full), src/ui/Components/smode/*, src/ui/MainWindow.cpp (RootComponent), ProcessingPage, WaveDragArea, MidiDragArea, SettingsDialog, Style/Theme, and docs/S-MODE_PROPOSAL.md. Working tree = v1.0.0 plus the uncommitted resize change (1060x724 default, limits 850x500 to 1810x1130).
Method: static read only. Nothing was built or run. "Verified" means the code path was traced line by line. "PLAUSIBLE" means it depends on timing or host behaviour I could not confirm.

Severity scale: CRITICAL = crash or data loss on a common path. HIGH = crash on a realistic path, or a core feature broken. MEDIUM = wrong behaviour or bad UX. LOW = cosmetic or cleanup.

---

## 1. Lifetime and crash bugs

### B1 - CRITICAL - The model keeps calling into a destroyed SModePage
- **Where:** SModePage.cpp:1216-1221 and the destructor at 1104-1120.
- **Evidence:**
  - `model.setExtraStateHooks([this]() { return captureUiExtraState(); }, [this](const juce::var& v) { restoreUiExtraState(v); });`
  - `model.onUndoStackChanged = [this]() { updateUndoUi(); };`
  - `~SModePage()` never clears these hooks. The model belongs to SModeService and lives as long as the processor.
  - `SModeSegmentModel::clear()` calls `notifyUndoStackChanged()` (SModeSegmentModel.cpp:15).
  - `clear()` runs from `SModeService::shutdown()`, `reset()` and `startWorkflow()` (SModeService.cpp:37, 44, 57).
  - The processor teardown calls `shutdownServices()`, which calls `serviceRegistry_->shutdownAll()` (SamplarAudioProcessor.cpp:407).
- **Trigger:** Load a song, close the plugin window, then remove the plugin or close the DAW project. `updateUndoUi()` then runs on freed memory: it touches `undoBtn_` and `redoBtn_` of a dead page.
- **User impact:** The host crashes or the heap is corrupted on project close or plugin removal. This happens on every session where a song was loaded and the window was closed before the plugin was removed.
- **Fix:** In `~SModePage`, if `smodeService_` is set, set `onUndoStackChanged = nullptr` and call `setExtraStateHooks({}, {})` on the model. Better still, have the UI register a token-based listener that the service can drop.

### B2 - HIGH - Sequencer playback keeps running after the page is destroyed
- **Where:** SModePage.cpp:3464-3557 (single lane) and 3717-3757 (Play All).
- **Evidence:**
  - Playback is scheduled with chains of `juce::Timer::callAfterDelay(...)`.
  - The lambdas `playSlot`, `progressTimer2` and `playheadTick` capture `this` and index `sequencerLanes_`.
  - The destructor never calls `seqPlayingFlag_->store(false)`.
  - The closures also hold a shared_ptr to themselves (3468-3469, 3506, 3719), so every play leaks one closure cycle.
- **Trigger:** Close the editor while a lane or Play All is playing. The next tick (at most 30 ms later) touches a freed page.
- **User impact:** Crash.
- **Fix:**
  - Set the cancel flag in the destructor.
  - Capture `SafePointer<SModePage>` in the lambdas.
  - Break the self-reference, for example with a weak_ptr.
  - Long term, move sequencing to the audio thread (see B36).

### B3 - HIGH - Detached worker threads post raw `this` back to the message thread
- **Where:**

  | Feature | Thread starts | Callback |
  |---|---|---|
  | Quick Assign | SModePage.cpp:596 | 624 |
  | Auto: Transient | 2331 | 2357 |
  | BPM detection | 3010 | 3042 |
  | Single segment drag-out | 1577 | 1594 |
  | Multi-segment drag-out | 1675 | 1704 |
  | Sequencer WAV export (dead code) | 3973 | 3991 |
  | Spectral colours | StemWaveformComponent.cpp:400 | 478 |
  | WaveDragArea (destructor joins the thread, but the posted lambdas still run) | WaveDragArea.cpp:354, 390, 402 | 420 |

- **Evidence:** Every completion uses `juce::MessageManager::callAsync([this, ...]` with no SafePointer and no session check. The BPM and spectral threads also read raw stem pointers (`getArrayOfReadPointers()`, lines 3005 and StemWaveformComponent.cpp:391). Their comments say "model is processor-lifetime, buffers won't be deleted", but `unloadCurrentAudio()` calls `smodeService_->reset()`, which frees those buffers (1897-1901). Loading a new song also frees them, through `startWorkflow` and `model_.clear()`.
- **Trigger:**
  - The spectral pass takes about 1-2 s per stem. The Beat This BPM detection takes several seconds.
  - Closing the editor, ejecting, or loading another song inside that window leads to a use-after-free, either in the callback or (in a narrower window) in the thread's copy step.
  - `smodeService_` points to the same service object after a reload. So a stale Quick Assign, transient or BPM result is applied to the next song, with wrong segment indices and the old song's BPM.
- **Fix:**
  - Capture `SafePointer` plus a per-load `sessionId` and drop the result when either no longer matches.
  - Hand workers immutable `shared_ptr<const AudioBuffer>` snapshots instead of raw pointers.

### B4 - HIGH - The DAW tempo subscription is never removed
- **Where:** SModePage.cpp:366-373, destructor at 1104-1120.
- **Evidence:** `hostTempoSub_ = bus->subscribe("host.tempo", [this](const juce::var& v) { ... applyBpm(hostBpm); });`. The destructor unsubscribes four other topics but not `hostTempoSub_`. The processor publishes `host.tempo` whenever the host tempo changes by more than 0.01 BPM (SamplarAudioProcessor.cpp:508-513).
- **Trigger:** Turn on DAW sync, close the editor, then change or automate the tempo in the DAW.
- **User impact:** Crash. Each reopen also adds another live handler.
- **Fix:** Unsubscribe in the destructor.

### B5 - HIGH - RootComponent leaves the update and licence subscriptions behind
- **Where:** MainWindow.cpp:447-454.
- **Evidence:** `bus->subscribe("update.available", [this](...) { handleUpdateAvailable(payload); });` and the same for `license.status.reminder`. The handler IDs are thrown away, there is no `alive` guard, and the destructor (633-677) does not unsubscribe them. `UpdateCheckService` publishes once, 5 s after startup plus network time (UpdateCheckService.h:54, .cpp:67-80 and 130).
- **Trigger:** Insert the plugin (most DAWs open the editor automatically), then close the window within about 5 s. If a newer release exists, the handler runs on a freed RootComponent. v1.0.0 is stale, so any new release will trigger this.
- **Fix:** Store the IDs, add the `alive` guard the other handlers already use, and unsubscribe in the destructor.

### B6 - HIGH - Sequencer slot audition fires a timer on a deleted slot
- **Where:** SequencerSlotComponent.cpp:786-789.
- **Evidence:** `juce::Timer::callAfterDelay((int)(1000.0 * outLen / sampleRate_), [this]() { setPlaying(false); });`. Every Shuffle, Mutate, Slice, Clear, undo or restore destroys the slot components (SequencerLaneComponent.cpp:317-321).
- **Trigger:** Double-click a slot to audition it, then press SHUFFLE before it finishes. This is the normal audition-then-shuffle loop.
- **User impact:** Crash.
- **Fix:** Capture a `SafePointer`.

### B7 - MEDIUM (PLAUSIBLE) - Popup menu callbacks with raw `this` and stale indices
- **Where:** StemWaveformComponent.cpp:1038, 1462 and 1650; SModePage.cpp:509 and 542; tap-tempo flash at SModePage.cpp:834.
- **Evidence:** The lane menus use `withTargetScreenArea` and no target component, so they are not dismissed when the lane is destroyed. Lanes are rebuilt by `onStemsReady`, by `beginSeparationVisuals`, and by a second `onStemsReady` on reopen (B11). `segIdx` is captured when the menu opens.
- **User impact:** Possible crash, or the wrong segment is deleted.
- **Fix:** Use `withTargetComponent(this)` and re-check the index against the model when the menu returns.

---

## 2. Session persistence and the load/restore flow

### B8 - CRITICAL - Chops and MIDI mappings are never saved in the DAW project
- **Where:** SamplarAudioProcessor.cpp:670-753 (the "smode" binding).
- **Evidence:**
  - The binding saves only `sourceFile`, `stemCount`, `bpm`, `sampleRate` and the stem file paths.
  - Searching src for segment serialization finds nothing. `ui.smodeSegmentData` is filled at line 731 and never read.
  - The SModePage lane binding (SModePage.cpp:50-142) saves sequencer slots that point at `stemIndex`/`segmentIndex` values that will not exist after a reload.
- **User impact:** Reopen a project and every chop, note assignment, link/solo/mute setting and grid offset is gone. MIDI clips in the DAW then play nothing. For an instrument plugin this is data loss.
- **Fix:** Serialize segments (start, end, midiNote, effects) and per-stem flags in the smode binding, and restore them in the service after `loadStemAudio`.

### B9 - HIGH - S-Mode only restores if the editor is open while the project loads
- **Where:**
  - MainWindow.cpp:1057-1143: the only code that reads `pendingSModeRestore`, inside the "state.restored" handler.
  - SamplarAudioProcessor.cpp:623-625: `setStateInformation` publishes "state.restored" via `callAsync`. If no editor exists, nobody is subscribed.
  - MainWindow.cpp:388-410 and 605-619: the RootComponent constructor checks only `smodeSvc->isActive()`, which is false because the stems were never loaded.
- **User impact:** In the normal case (project loads with the plugin window closed), opening the plugin later shows the empty LOAD AUDIO screen, and MIDI stays silent until the user opens the window.
- **Fix:** Reload the stems in the processor or service when `setStateInformation` runs, with no UI involved. The UI should only reconnect.

### B10 - MEDIUM - Closing the window mid-separation loses the progress UI
- **Where:** MainWindow.cpp:305-363 resumes FullSong separation but has no equivalent for S-Mode. The constructor never checks `smodeService_->getSeparation().isSeparating()` and never subscribes to `smode.workflow.ready`.
- **User impact:**
  - The reopened window shows the empty overlay with no progress bars.
  - When separation finishes, nothing appears until the window is closed and reopened again.
  - Loading again in the meantime only shows "Separation in progress".
- **Fix:** In the constructor, check `isSeparating()`, then call `beginSeparationVisuals`, subscribe to progress and ready, and replay the current progress.

### B11 - MEDIUM - Reopening the window wipes undo history
- **Where:** MainWindow.cpp:406 calls `smodePage_.onStemsReady(*smodeService_)` synchronously in the constructor, and 616 calls it again through `callAfterDelay(100)`.
- **Evidence:**
  - Each call runs `model.clearUndoHistory()` (SModePage.cpp:1224), rebuilds every lane, recomputes the spectral colour state and re-subscribes.
  - The comment at MainWindow.cpp:602-604 says a synchronous call in the constructor "crashes", yet line 406 does exactly that.
- **User impact:** Closing and reopening the plugin window erases undo history.
- **Fix:** Reconnect once (deferred). Clear undo history only when a new song is loaded.

### B12 - LOW - Bus subscriptions leak on every song load
- **Where:** MainWindow.cpp:557 discards the ID of the `smode.separation.progress` handler. The ready and error handlers (528-555) each unsubscribe only themselves.
- **Evidence:** After a success, the error handler is still subscribed; after a failure, the ready handler is. `smodeWorkflowReadySub_` is also overwritten by the state-restore path (1096 and 1122).
- **User impact:** After N loads, progress is logged and repainted N times, and one later failure shows a stack of "S-Mode Failed" alerts.

---

## 3. Layout at the new default size (1060x724) and the new minimum (850x500)

### B13 - HIGH - Solo/Mute/Link buttons have zero height at the default size
- **Where:** MainWindow.cpp:721, SModePage.cpp:2030-2195, StemWaveformComponent.cpp:519-559.
- **Arithmetic at 1060x724:**
  - The page is 1052x716.
  - The page height left for the waveform and sequencer area is 576 px.
  - `onStemsReady` auto-creates one sequencer lane. It is clamped to 140 px plus a 28 px button row.
  - That leaves 400 px for waveforms, so each of the 4 stems gets 97 px.
  - Inside a lane: panel 77 px, minus knob row 44, minus name row 28, minus gap 6, leaves 0 px for the S/M/L row.
  - `reduced(0,3)` clamps that row to 0, so `soloBtn_`, `muteBtn_` and `linkBtn_` are invisible.
  - Full 22 px buttons need lanes of at least 126 px.
- **Before the resize change:** At 1500x1024 the same collapse already happened once there were 3 or more sequencer lanes, and Smart Fill creates up to 4.
- **At the 850x500 minimum:** Lanes are 41 px. The stem name and icon also vanish, and the pitch knob shrinks to 21 px.
- **Fix:** Use a compact one-row lane header (icon, name, S/M/L, small knob) when the lane is under about 120 px. Put stems in a `Viewport` with a minimum lane height, and make the sequencer area collapsible.

### B14 - HIGH - The top bar overflows below about 1200 px wide
- **Where:** SModePage.cpp:2038-2099. Resize limits at SamplarAudioProcessorEditor.cpp:52-54 (uncommitted change).
- **Evidence:**
  - The fixed zones add up to 1022 px (logo 130, transport 152, utility 230, grid 100, BPM 150, tempo modes 260), plus 32 px eject plus the LOAD SONG button.
  - At 1060 px the top bar is 1008 px wide. The tempo-mode zone is cut to 246 px and LOAD SONG gets 0 px width, so it disappears once a song is loaded.
  - At 850 px the BPM zone gets 36 px and the tempo-mode zone 0 px. `withSizeKeepingCentre` then stacks Dbl, LOCK and TAP on top of Half, and the eject button overlaps them.
  - The old minimum width (1200) matched these widths. The new 850 minimum does not.
- **User impact:** At the default size there is no visible way to load a different song except Eject then Load, or drag-and-drop.
- **Fix:** Lay the top bar out with `juce::FlexBox` and an overflow ("...") menu for Half, Dbl, LOCK and TAP. Alternatively keep the minimum at 1200, or scale the whole UI with a transform.

### B15 - LOW - Minor overlaps
- The undo toast (SModePage.cpp:2104-2107) covers the Smart Fill button (2112-2115).
- RootComponent's window buttons (Settings, plus Pin/Drag/Exit in standalone) sit 6 px into SModePage's top bar, over the DAW toggle (MainWindow.cpp:726-737).
- The page-level playhead line (SModePage.cpp:2017-2023) is painted in the parent, underneath lanes that fill their own background, so it only shows in the 4 px gaps between lanes.

---

## 4. Functional bugs

### B16 - HIGH - LINK ALL defaults to on and turns stem chops into full-mix chops
This is the opposite of the product vision.
- **Evidence:**
  - Default: `bool linkAllActive_ { true };` (SModePage.h:555), re-applied to every stem in `onStemsReady` (SModePage.cpp:1811-1812).
  - Creating or resizing a segment on one stem runs `clearSegments(j)` and copies the segments onto every other stem (1366-1394). This wipes the other stems' own chops and MIDI notes.
  - Clicking a vocal segment that has no note plays the same segment index on every stem (1474-1484). The user hears the full mix, not the vocal.
  - MIDI Learn assigns the same note to the same segment index on all stems (874-884). Segment index is not the same as time position once stems diverge (transient auto-chop is per stem), so one key can fire unrelated regions.
- **Fix:**
  - Default LINK ALL to off.
  - Link by time range, not by index.
  - Keep "link" for syncing view and grid only, unless the user explicitly chooses "layer".

### B17 - HIGH - The grid LOCK "transpose" does nothing audible
- **Where:** SModePage.cpp:2457-2471 writes `fx.pitchSemitones` into every segment's effects.
- **Evidence:** Nothing in playback reads `SegmentEffects::pitchSemitones`, `reverse`, `filterCutoff`, `volume` or `pan`. Only `effects.loop` is used (MidiMappingService.cpp:765). The only visible result is the orange dot at StemWaveformComponent.cpp:924.
- **User impact:** LOCK plus a BPM change appears to work but changes nothing.
- **Fix:** Route it to the per-stem pitch (`setStemPitchSemitones`), or remove LOCK.

### B18 - HIGH - Sequencer subdivisions x8 and x16 overflow the grid
- **Evidence:**
  - `PatternGenerator::generate` returns `numBars * slotsPerBar` slots (PatternGenerator.cpp:99).
  - The lane grid is always `numBars * 4` cells, and `cellsPerSlot = max(1, totalCells / slots.size())` (SequencerLaneComponent.cpp:250-256).
  - With x16 and 4 bars that is 64 slots on a 16-cell grid. Slots 16 to 63 sit off the lane.
  - Play All and both WAV exports skip them (`writePos + len > total`, SModePage.cpp:3679 and 1015).
  - Single-lane play schedules all 64 at one beat each (3322-3435), so it plays 64 beats while the lane shows 16.
- **Fix:** Use `numBars * slotsPerBar` cells with a cell length of `4/slotsPerBar` beats, consistently in all three renderers and the playhead.

### B19 - MEDIUM - Ctrl+Shift multi-segment drag-out can never happen
- **Where:** StemWaveformComponent.cpp:1082 tests `if (gridBpm_ > 0.0 && e.mods.isCtrlDown())` (grid drag) before the segment hit test and multi-select branch (1103-1133).
- **Evidence:** BPM is always set after detection, so Ctrl+Shift+drag moves the grid instead.
- **User impact:** The grid drag pushes no undo state (1084-1088, 1269-1274) and only changes that lane's offset, not the model's `gridOffsetSeconds_`. The user's beat grid is silently and irreversibly misaligned.
- **Fix:** Hit-test segments first, or move grid drag to Alt or a handle. Push an undo state.

### B20 - MEDIUM - Auto: Transient reports progress on a hidden button
- **Where:** `autoSegmentByTransient` disables and relabels `autoTransientBtn_` (SModePage.cpp:2328-2329, 2365-2366), which is hidden (519-521). The visible `autoBeatBtn_` stays enabled.
- **User impact:**
  - The user gets no "Analyzing..." feedback.
  - Clicking twice runs two analyses that both append segments, giving duplicate overlapping segments.
  - Pressing Ctrl+Z during analysis restores the old segments, then the results are appended on top.

### B21 - MEDIUM - Quick Assign deletes segments and maps to the wrong range
- **Evidence:**
  - Any segment quieter than -40 dB is deleted (603-648), although the button says "assign".
  - Segment indices are captured before the async RMS pass (571-591) and applied afterwards (624-656). Any edit in between assigns or deletes the wrong segments.
  - Notes run from 36 upward across all stems and stop at 127 (633-638). After Auto: Beat on a 3-minute song, only the first ~92 chops get notes, which covers roughly the first 20-45 s of the vocal.
  - The computer keyboard only reaches notes 48-71, with no octave shift (MidiMappingService.cpp:1372-1410). Without a hardware controller, the first 12 assigned chops (notes 36-47) can't be played.
- **Fix:** Use fixed per-stem banks and add an octave shift (see the UX section).

### B22 - MEDIUM - Three different octave conventions for note names
- Lane badges: `octave = note/12 - 1`, so 36 shows as "C2" (StemWaveformComponent.cpp:1681).
- `MidiMappingService::noteNumberToName`: `note/12 - 2`, so 36 is "C1" (.cpp:1198).
- Keyboard-map comments: 48 is "C3".
- Quick Assign tooltip says "C1+" (SModePage.cpp:1037).
- **User impact:** The badge can't be matched to the DAW piano roll.

### B23 - MEDIUM - "Remove Channel" can't be undone
- **Where:** SModePage.cpp:1775-1792 hides and mutes the stem.
- **Evidence:** `removedStems_` is not in the undo snapshot (`captureUiExtraState`, 4045-4060), and no UI restores a removed stem. Undo un-mutes the stem but it stays hidden. Unfocus also skips removed stems (1755-1761).
- **User impact:** The stem is gone for the session.

### B24 - MEDIUM - Undo and redo leave the UI out of sync, and the undo stack fills with junk
- **UI not refreshed after undo** (`restoreUiExtraState`, 4062-4096):
  - It never calls `linkAllBtn_.setToggleState(...)`. The button toggles on click, so after an undo the next click appears to do nothing.
  - It recolours `gridLockBtn_` but never resets its text, and uses different colours from the click handler (4074-4075 vs 791-792).
  - It updates the division combo but does not push the division to the lanes.
  - It does not re-apply the loop state to the service (`setSModeTransportLooping`).
  - Solo and Mute colours are set only in `onClick` (StemWaveformComponent.cpp:21-22 and 37-38), so they are wrong after undo or reopen.
- **Junk undo entries:**
  - `bpmInput_.onFocusLost` re-applies the BPM on every focus change (749-753), and `applyBpm` always pushes "BPM change".
  - Every host tempo change also pushes one entry.
  - A click on empty lane space pushes "Create segment" even if no segment is created (1167).
  - A mousedown on a segment edge pushes "Resize segment" even with no movement (1098).
  - The stack holds 50 entries (SModeSegmentModel.h:215), so real edits fall off quickly.

### B25 - MEDIUM - The MONO/POLY mode lives in a function-local static
- **Where:** SModePage.cpp:680, `static int midiMode = 0;`.
- **User impact:** The mode is shared across every plugin instance in the DAW. On reopen the label resets to "POLY 1SH" while the service keeps the old mode.

### B26 - MEDIUM - Each playback and export path sounds different
| Path | Stem pitch knob | Slot pitch | Reverse | Speed |
|---|---|---|---|---|
| Waveform click / loop (1513-1516, 2553-2556) | yes | n/a | n/a | n/a |
| Single sequencer lane (3364-3426) | no | yes | yes | yes |
| Play All (3682-3706) | no | no | no | yes |
| Slot double-click (SequencerSlotComponent.cpp:727-766) | no | no | yes | yes |
| Lane drag-WAV / Play-All drag-WAV (3143-3176, 957-1027) | no | no | no | no (no micro-fade either) |
| Segment drag-out (1563-1569) | no | n/a | n/a | n/a |

What the user auditions is not what lands in the DAW.

### B27 - MEDIUM - The plugin grabs keyboard focus on mouse-over
- **Evidence:**
  - `StemWaveformComponent::mouseEnter` calls `grabKeyboardFocus()` (1302-1310).
  - Computer-keyboard MIDI is on by default (MidiMappingService.h:511).
  - SModePage consumes Space (2720-2737).
- **User impact:** In a DAW, moving the mouse over the plugin takes over the DAW's Space and QWERTY shortcuts and triggers samples.
- **Fix:** Take focus on click only, turn computer-keyboard MIDI off by default in the plugin, and let Space pass through to the host.

### B28 - MEDIUM - Dropping a file over a loaded session discards it without asking
- **Where:** `SModePage::filesDropped` (4124-4138) calls `onRequestSeparation`, then `startWorkflow`, then `model_.clear()`. Recents behave the same way.
- **Evidence:**
  - In the empty state, only the button accepts drops (LoadAudioOverlay 299-329). Once loaded, the whole page does, and it destroys the work.
  - Sequencer lanes from the old song stay, pointing at stems and segments that no longer exist.
- **Fix:** Ask "Replace current session?" and reset the lanes on a new song.

---

## 5. Drag-and-drop and temp files

### B29 - HIGH - Exported WAV files collide, leak and change bit depth
- **Name collisions:** Segment drag-outs write `%TEMP%\<Stem>_<Note>_seg<N>.wav` (SModePage.cpp:1579-1580, 1664). The same stem, note and index produce the same file name across songs. Ableton and FL reference dropped files in place by default, so a later drag overwrites audio an older project still uses, and that project silently plays the new audio.
- **Fragile location:** Windows Storage Sense purges %TEMP%, so projects later report missing samples.
- **Leak:** Pattern drags create a new `SAMPLAR_pattern_<ms>.wav` on every click (WaveDragArea.cpp:368-370). These are never deleted.
- **Bit depth:** Chops are 24-bit (1586); patterns are 16-bit (WaveDragArea.cpp:381).
- **Fix:** Export to `Documents/SAMPLAR/Exports/<song>/` with unique names (song hash plus timestamp) and 24-bit throughout.

### B30 - MEDIUM (PLAUSIBLE) - Drag-out can start after the mouse is already released
- **Where:** SModePage.cpp:1577-1603. The WAV is written on a thread, then `performExternalDragDropOfFiles` is called later through `callAsync`.
- **User impact:** With a long segment, if the mouse is released before the write finishes, the drag silently fails. Separately, pattern export needs a click to render and then a second drag gesture (WaveDragArea.cpp:216-257), which is not discoverable.
- **Fix:** Pre-render on hover or selection, or write synchronously for short segments.

### B31 - MEDIUM - WaveDragArea renders on a background thread from UI-owned data
- **Where:** WaveDragArea.cpp:333-349 calls `onRenderPattern` on a `std::thread`. The SModePage callbacks (957-1027, 3112-3179) read `sequencerLanes_`, lane slots, `detectedBpm_` and model segments without any lock.
- **User impact:** Shuffle, Clear or Remove Lane during a render is a data race. Removing a lane destroys its WaveDragArea, which joins the very thread that is iterating the vector being erased.
- **Fix:** Snapshot the lanes and slots on the message thread, then render the snapshot.

---

## 6. Performance

### B32 - HIGH - Every lane redraws its whole waveform 30 times a second during playback
- **Evidence:**
  - `SModePage::timerCallback` runs at 30 Hz (2594, 2622-2644). It calls `setPlayheadTime()` on every lane, and each call does a full `repaint()` (StemWaveformComponent.h:113). It also repaints the whole page, which forces every child to repaint.
  - `drawWaveform` recomputes min/max from raw samples for every pixel column on every frame, with no cached image (StemWaveformComponent.cpp:813-845).
  - Fully zoomed out, a 4-minute stereo stem is about 21M samples scanned per lane per frame. Across 4 lanes that is about 85M samples per frame, 30 times a second. This is an estimate, not a measurement; it is enough to saturate the message thread.
  - Each paint also clips to a rounded path (591-594).
  - Segment-trigger animations repaint whole lanes at 30 Hz as well (1706-1765).
- **Fix:**
  - Cache each lane's waveform and spectral render as an `Image` keyed on view range and size.
  - Build a min/max peak pyramid once per stem.
  - Draw the playhead as a thin overlay component that repaints only a 3 px rect.

### B33 - MEDIUM - Large memory spike when stems become ready
- **Evidence:** Several workers copy whole stems at once:
  - 4 spectral threads, each copying a full stem (StemWaveformComponent.cpp:400-406).
  - The BPM thread copies the drum stem (3015-3017).
  - Auto: Transient copies a mono mix of every stem (2305-2324).
  - Quick Assign copies all segment audio (566-591).
- **User impact:** About 85 MB per 4-minute stereo stem, so roughly 400 MB of transient allocation (estimate).
- **Fix:** Share immutable buffers and compute a peak cache once in the service.

### B34 - LOW - Hidden ProcessingPage and log noise
- ProcessingPage runs a 60 Hz timer for the editor's whole lifetime (ProcessingPage.cpp:125, 588-604).
- It preloads a 6-12 MB GIF and a tileset (MainWindow.cpp:591-596) for an overlay that S-Mode never shows (the show call is commented out at 573).
- `RootComponent::resized` writes a log line on every call (742), so window resizing hammers the log file.
- The "TIMER-DEBUG" spam does not exist in the current src/. It is in `logs/sm5_console.log`, dated Feb 2026, from the Sample Monsta 5 binary before the fork (variable `playbackAnimating_`). It is not a SAMPLAR issue.

### B36 - MEDIUM - The sequencer clock runs on the message thread
- **Where:** SModePage.cpp:3542-3556 and 3737-3757.
- **Evidence:** Each step is scheduled with `callAfterDelay` relative to the previous one, so timing jitters (10-30 ms) and drifts. It stalls whenever the UI is busy (made worse by B32) and is not synced to the host transport. Every step calls `stopAllVoices()` (3522, 3531), which cuts tails and any MIDI-triggered chops.
- **Fix:** Move scheduling into MidiMappingService or the processor, as sample-accurate events against the host PPQ.

---

## 7. Dead or half-built UI

### Proposal features not implemented
- **Export WAV:** `exportBtn_` has no `onClick` and is hidden (SModePage.h:544, SModePage.cpp:1039, 2117). There is no `SModeExporter` (src/services/smode/ has only PatternGenerator, SegmentModel, Separation, Service and SpectralFlux).
- **6 stems:** `StemCountDialog::show` skips the dialog and always picks 4 (StemCountDialog.cpp:83-92). The dialog UI is compiled but dead.
- **Not implemented at all:**
  - Session save/load (.sm5s)
  - Per-segment effect strip (proposal section 9). `SegmentEffects` exists in the model but has no UI and no playback.
  - Per-stem volume (`StemData::volume` exists, no control)
  - Delete key for segments
  - G / S / 1-6 shortcuts
  - Context-menu "Solo stem" and "Export this segment"
  - Overview minibar
  - Double-click to loop
- **What the segment menu actually offers:** Assign/Clear MIDI, Delete, Select (StemWaveformComponent.cpp:1443-1458).
- `onSegmentSelected` only writes a log line (SModePage.cpp:1434-1437). It was meant to open the effect strip.

### Visible controls that do nothing
- The chevron and stem-icon buttons in every lane header have no `onClick` (StemWaveformComponent.cpp:133-138).

### Hidden or unused members
- Still constructed but hidden: `autoBarBtn_`, `auto2BarBtn_`, `autoTransientBtn_`, `trigModeBtn_`, `playChainBtn_`, `stopAllSeqBtn_`, `transportBtn_`, `titleLabel_`, `trackNameLabel_`, `bpmLabel_` (still updated), `controllerAreaLabel_`.
- Defined but unused: the `StemStatusIndicator` class (SModePage.h:130).
- Defined but never called: `playLanesChained()` and `dragSequencerAsWav()`, about 240 lines (3764-4001).
- Declared but never defined: `showMidiNotePickerMenu` (StemWaveformComponent.h:253).
- Unreachable: `SequencerSlotComponent::keyPressed`, because the component sets `setWantsKeyboardFocus(false)` (.cpp:12).

### Leftovers in MainWindow
- The debug sidebar (press F1 ten times, 746-760) still lists "Initial", "Community", "Mentor" and "Processing" (1328). `switchTo` is a no-op stub (1150-1171).
- The `processing.viz`, `processing.progress` and `processing.result` handlers (779-1018) listen for events nothing publishes, and contain `&& false` branches.
- The batch separation code (1421-1700) is never called.
- The FullSong resume path (305-363) is for a mode SAMPLAR no longer exposes.

### Compiled but never referenced
- MidiDragArea, DropZoneLookAndFeel, TransparentHoverButtonLookAndFeel, EngineColorManager, SamplarAnimation, CollapsibleSection, ClassificationProgressDialog.
- `SettingsDialog.h.bak` and `SettingsDialog.h.new` are sitting in the source tree.

### Checked and found fine
- Event publishers marshal to the message thread with `destroyed_` guards (MidiMappingService.cpp:794, 914, 653; SModeService.cpp:195).
- SModePage unsubscribes its own 4 topics. SModeMidiLearnOverlay unsubscribes.
- `flashUndoStatus` and the BPM dots timer use SafePointer.
- RootComponent clears the default LookAndFeel.
- SettingsDialog is a separate DialogWindow, so it does not clip at 850x500.

---

## 8. UX gaps vs the vision ("sample a song in stems, intuitive and creative")

### Click counts today

**Flow 1: Load song, then play a vocal chop from a MIDI keyboard.** Best case:

| Step | Clicks |
|---|---|
| LOAD AUDIO | 1 |
| Pick file, Open | 2 |
| Stem count dialog (auto-skipped) | 0 |
| Wait for separation | - |
| Turn LINK ALL off (otherwise chops are the full mix, B16; nothing tells the user this) | 1 |
| Auto: Beat | 1 |
| Quick Assign, then "Vocals" | 2 |
| **Total** | **7** |

Then the user hits a key, but which one?
- After Auto: Beat, a 3:30 song gives about 420 chops per stem across ~858 px, about 2 px each. Note badges are hidden below 18 px (StemWaveformComponent.cpp:908), so no labels show.
- There is no keyboard or pad map on screen.
- Without hardware, the first 12 chops can't be reached (B21).

Doing it by hand instead costs 3 actions per chop (drag, right-click, "Assign MIDI Key...", press key), and with LINK ALL on, each chop is copied to all stems.

**Flow 2: Drag a bass chop into the DAW.** About 5 actions: load (3), drag on the bass lane to make a segment (1), drag the segment out (1). Problems:
- The mousedown that starts the drag also plays the chop.
- Nothing says segments can be dragged. The hover tip reads "Click = Play | Alt+Click = Loop | Right-Click = Menu" (1417).
- There is no way to drag a whole stem.
- The file ignores the bass pitch knob (B26) and lands in %TEMP% (B29).

### Proposals, ranked by impact versus effort

1. **Make stems independent by default.** (High impact / Low effort.) LINK ALL off; link by time range; "Layer" as an explicit option. Fixes B16 and makes clicking a vocal play only the vocal.
2. **Make chops survive the DAW.** (High / Medium.) Persist segments, notes, effects and flags in the project state, and restore in the service with no UI dependency. Fixes B8, B9 and B10. Without this the instrument does not work across sessions.
3. **Chop banks with a visible keyboard.** (High / Medium.)
   - Fixed 16-note banks per stem: Vocals C1-D#2, Bass E2-G3, Drums G#3-B4, Other C5-D#6. Pick one note convention and use it everywhere (B22).
   - A pad or piano strip under the lanes showing which chop sits on which key, lit when triggered, with drag-to-reassign.
   - Computer-keyboard octave shift on Z/X, or remap the 24 keys onto the current bank.
4. **Auto-chop per stem with sensible defaults.** (High / Low-Medium.)
   - Scope "Chop" to the focused or hovered stem.
   - Defaults: transient for drums and vocals (vocal phrase onset), bar for bass and melody.
   - Cap at 16 chops per stem (the bank), picked by energy or novelty rather than "first N".
   - Zoom to fit chops so labels are readable.
5. **Per-chop inspector.** (High / High.) Wire up the existing `SegmentEffects`: pitch (semitones, time-preserving), reverse, gain, pan, fade in/out (ADSR-lite: attack/release), LP/HP filter, start trim, one-shot/gate/loop. Apply it in voices and exports alike, which also fixes B26.
6. **Drag everything.** (Medium-High / Low-Medium.)
   - A drag handle on each lane header for the full stem (WAV).
   - Drag a chop from the pad strip.
   - Drag a sequencer lane as a MIDI clip that fires the chop notes, so the DAW pattern stays editable.
   - Use a persistent export folder (B29) and one-gesture drag (B30).
7. **Kit save and recall.** (Medium / Medium.) Save chops, mapping and effects as a kit: a folder of WAVs plus JSON, optionally SFZ or an Ableton Drum Rack, loadable without the source song.
8. **Key and tempo awareness.** (Medium / Medium.) Detect key on the "Other" or vocal stem and show it next to BPM. Add "Match key" to drive the per-stem pitch knobs. Replace the broken LOCK (B17).
9. **Responsive layout.** (Medium / Low.) Compact lane header, a stem Viewport, a collapsible sequencer, a FlexBox top bar (B13, B14).
10. **Teach in place.** (Low-Medium / Low.)
    - Empty-lane hint: "Drag to chop - Right-click: Auto-chop - Drag a chop into your DAW".
    - A status line with the selected chop, its note and its length.
    - Confirm before replacing a session (B28).
11. **Real sequencer clock.** (Medium / High.) Schedule on the audio thread, synced to host transport (B36), so patterns can be performed and recorded in time.
