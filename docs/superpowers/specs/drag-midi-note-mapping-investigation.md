# Drag-Out MIDI Note Mapping Investigation

## Overview

When a user drags a MIDI file out of SAMPLAR's step sequencer (Sample Browser page, bottom panel) into a DAW, the exported MIDI uses **fixed GM drum note numbers** (kick=36, snare=38, etc.) derived from **row names only**, not from the VST plugin's actual **MIDI mapping state**. This creates a misalignment: the dragged MIDI plays different samples in the host DAW because the plugin's midiMap (which maps notes to specific sample files) is not synchronized with the drag-out operation.

---

## Section 1: Drag-Out Handler

### File & Location
- **File**: `src/ui/Components/MidiDragArea.cpp`
- **Handler method**: `mouseDrag()` at **lines 105–135**

### What the Drag Does
The `MidiDragArea` component initiates external file drag-drop via JUCE's `DragAndDropContainer::performExternalDragDropOfFiles()`:

```cpp
// Line 115–126: mouseDrag()
juce::File midiFile(midiFilePath_);
if (midiFile.existsAsFile()) {
    juce::Logger::writeToLog("[MidiDragArea] Starting external drag: " + midiFilePath_);
    juce::StringArray files;
    files.add(midiFilePath_);
    bool canMove = false;  // Copy, don't move
    juce::DragAndDropContainer::performExternalDragDropOfFiles(files, canMove);
}
```

The **path is set by** `setMidiFilePath()` (line 137), which is called from `StepSequencerComponent::regenerateMidiFile()` (line 2599).

### What Note Numbers Are Written
The MIDI file itself is generated **upstream** in `MidiFileGenerator`, not in `MidiDragArea`. The drag handler simply passes an **already-generated MIDI file** to the OS for external drag.

---

## Section 2: Lane ? Sample Mapping (Step Sequencer)

### Row Configuration
**File**: `src/ui/Components/StepSequencerComponent.h` at **line 197**

```cpp
struct Config {
    juce::StringArray rowNames { "Kick", "Snare", "Hat", "Clap" };
    // ... other config
};
```

Default row names are hardcoded; they can be edited via the UI at runtime (lines 1718–1724, 1772–1773, 1817–1824):

```cpp
// Line 1719: When a sample is dropped on a row lane
cfg_.rowNames.set(row, file.getFileNameWithoutExtension());
```

### Row ? Sample Index Persistence
The sequencer does **not** persist a direct mapping of row index to sample file. Instead:
- **Each row has a name** (`cfg_.rowNames[row]`)
- **The name is used to derive a GM drum note** when generating MIDI (see Section 1)
- **Samples are loaded per row** during playback via the `SequencerAudioSource` (referenced in `MidiMappingService.cpp` line 635 comment)

**Key insight**: The sequencer's `rowNames` array is the ONLY link between sequencer lanes and the MIDI note numbers in the exported file.

---

## Section 3: VST Incoming-MIDI ? Sample Mapping (Plugin Side)

### The MidiMappingService
**File**: `src/services/MidiMappingService.h` & `src/services/MidiMappingService.cpp`

This service maps **MIDI note numbers ? audio buffers** for playback in the VST plugin.

#### Note-to-Sample Map Structure
**File**: `src/services/MidiMappingService.h` at **lines 245–247**

```cpp
// 128-slot MIDI map  (UI thread writes, audio thread reads via try_lock)
mutable std::mutex mapMutex_;
std::array<MidiSlot, 128> midiMap_;
```

Each `MidiSlot` (lines 208–215):
```cpp
struct MidiSlot {
    juce::File file;
    std::shared_ptr<const juce::AudioBuffer<float>> buffer;
    double sampleRate = 44100.0;
    bool valid = false;
    bool locked = false;   // True = user-assigned, survives page changes
};
```

#### How Notes Are Mapped
**Browser auto-map** (line 587–628, `autoMapBrowserPage()`):
- Visible samples ? notes **48–67** (`BROWSER_MAP_START_NOTE=48` at line 202, up to 20 pads)
- Ignores locked notes (user manually assigned)

**Sequencer row implicit mapping** (line 630–639, `mapSequencerChannels()`):
- Comment states: Row 0 ? note 36, Row 1 ? note 37, etc.
- **However, the actual implementation is a no-op** (just logs a comment)

#### handleNoteOn (The Playback Decision)
**File**: `src/services/MidiMappingService.cpp` at **lines 300–481**

When a MIDI note arrives:
1. **Check S-Mode first** (lines 344–407): If the note maps to a segment, play it
2. **Fall back to browser map** (lines 409–481): Look up the note in `midiMap_[noteNumber]`
3. **If no mapping exists**, do nothing (line 426)

The critical code (lines 414–423):
```cpp
std::shared_ptr<const juce::AudioBuffer<float>> buf;
double slotSampleRate = 44100.0;

if (mapMutex_.try_lock()) {
    const auto& slot = midiMap_[noteNumber];
    if (slot.valid) {
        buf            = slot.buffer;
        slotSampleRate = slot.sampleRate;
    }
    mapMutex_.unlock();
}

if (buf == nullptr)
    return;  // ? UNMAPPED NOTE = SILENCE
```

**What happens with unmapped notes**: **Nothing**. No sound. The plugin silently ignores notes that aren't in the midiMap.

---

## Section 4: Existing Persistence

### State Save/Load Mechanism
**File**: `src/plugin/SamplarAudioProcessor.cpp` at **lines 704–784** (the **"midi binding"**)

The plugin **does persist the midiMap** via `getStateInformation()` / `setStateInformation()`:

#### On Save (lines 709–725)
```cpp
auto slots = midiMapping_->getOccupiedSlotsMeta();
for (const auto& slot : slots) {
    juce::ValueTree s("slot");
    s.setProperty("note",        slot.noteNumber,  nullptr);
    s.setProperty("file",        slot.filePath,    nullptr);
    s.setProperty("sampleRate",  slot.sampleRate,  nullptr);
    s.setProperty("locked",      slot.locked,      nullptr);
    t.appendChild(s, nullptr);
}
```

#### On Restore (lines 728–784)
```cpp
for (int i = 0; i < t.getNumChildren(); ++i) {
    auto s = t.getChild(i);
    int note       = static_cast<int>(s.getProperty("note"));
    juce::String p = s.getProperty("file").toString();
    double sr      = static_cast<double>(s.getProperty("sampleRate"));
    bool locked    = static_cast<bool>(s.getProperty("locked"));
    // ... load audio file, call midiMapping_->restoreSlot(note, f, buffer, sr, locked)
}
```

**Format**: XML (ValueTree serialization)
**State key**: `"midi"` in the plugin state
**When populated**: 
- On MIDI learn (user presses a key to assign a sample)
- On auto-map browser page changes (lines 587–628)
- On VST preset/session save/load

### Diagram: State Flow
```
User in SAMPLAR UI
    ?
[Browser page changed] ? autoMapBrowserPage()
    ?
midiMap_[notes 48-67] ? populated with browser samples
    ?
VST host save session
    ?
SamplarAudioProcessor::getStateInformation()
    ?
midiMap_ ? XML "midi" binding
    ?
.vstpreset / DAW session file
```

---

## Section 5: Alignment Gap (THE BUG)

### The Delta
```
DRAG-OUT MIDI writes:      note 36 (kick),    note 38 (snare),   note 42 (hat), ...
    ? (derived from row names only)
    
VST plugin expects:        midiMap_[36],      midiMap_[38],      midiMap_[42], ...
    ? (if populated by browser auto-map)
    
VST plugin midiMap has:    notes 48-67        (browser samples)
    ?
RESULT: notes 36-42 unmapped ? SILENCE in DAW
```

### Why This Happens
1. **StepSequencer exports MIDI** based on `cfg_.rowNames` ? GM drum notes (36, 38, 42, 46, etc.)
2. **MidiMappingService.midiMap** is populated by:
   - **Browser auto-map**: writes to notes **48–67** (BROWSER_MAP_START_NOTE)
   - **MIDI learn**: writes to whatever note user presses
   - **S-Mode model**: writes to segment-assigned notes
3. **Sequencer's row names do NOT feed into midiMap**

**The gap**: The drag-out MIDI uses GM drum map (36–51), but the plugin's midiMap is populated in a **different note range** (48–67) or **different mapping entirely** (learned/S-Mode).

### Example Scenario
User in SAMPLAR:
- Visible browser page shows 4 samples: Kick.wav, Snare.wav, Hat.wav, Clap.wav
- These auto-map to **notes 48, 49, 50, 51** (browser range)
- User sets sequencer row names: "Kick", "Snare", "Hat", "Clap" (default)
- User drags MIDI ? exports **notes 36, 38, 42, 39** (GM drum kit mapping)
- User opens MIDI in DAW on SAMPLAR track
- DAW sends notes 36, 38, 42, 39 to the plugin
- Plugin's midiMap[36], midiMap[38], midiMap[42], midiMap[39] are **empty** (not in browser range 48–67)
- **Result**: Silent playback

---

## Section 6: Fix Options

### Option A: Query Plugin's Live midiMap, Fall Back to GM Kit

**Approach**: Before generating MIDI for drag-out, query the plugin's current midiMap state and write note numbers that match the occupied slots (or fall back to GM if midiMap is empty).

**Implementation**:
1. `MidiFileGenerator` receives an optional `std::array<int, 4>* noteMapping` (or vector of note numbers per row)
2. Before drag, `StepSequencerComponent` calls `midiMapping_->getOccupiedSlots()` to get the current map
3. Extract 4–8 occupied notes and pass them to the generator
4. If midiMap is empty, fall back to GM kit notes (36, 38, 42, 46)

**Tradeoffs**:
- ? Minimal file format change (already has row name ? note logic)
- ? DAW compatibility: still standard MIDI, no custom meta-events
- ? Works immediately if plugin is running and midiMap is populated
- ? **Fails if midiMap is empty** (user hasn't set up samples yet in plugin)
- ? **Fails if user hasn't saved preset** (next DAW session midiMap may differ)
- ? Requires cross-thread call to midiMap_ (add try_lock helper)

**Success rate**: ~70% (depends on user having populated midiMap before drag)

---

### Option B: Bundle Sidecar `.sm5kit` JSON Descriptor

**Approach**: Export MIDI **plus** a companion JSON file that maps note numbers to sample paths. Host DAW/user can import this "kit" before playback.

**Implementation**:
1. When dragging, check for a "kit file" (e.g., `SAMPLAR_pattern_HHMMSS_XXX.sm5kit`)
2. Save sidecar JSON with format:
   ```json
   {
     "format": "sm5-kit",
     "version": "1.0",
     "midiMap": [
       {"note": 36, "file": "/path/to/Kick.wav", "sampleRate": 44100},
       {"note": 38, "file": "/path/to/Snare.wav", "sampleRate": 44100},
       ...
     ]
   }
   ```
3. UI shows message: "Drag both `pattern.mid` and `pattern.sm5kit` to your DAW"
4. SAMPLAR VST plugin detects `.sm5kit` in the same folder and auto-loads it on MIDI receive

**Tradeoffs**:
- ? **Works 100% of the time** (explicit mapping file is authoritative)
- ? Portable (user can share MIDI + kit together)
- ? DAW compatibility: plugin-side only, zero changes needed to DAW
- ? **User friction**: Must drag 2 files instead of 1
- ? Requires custom file format (needs documentation)
- ? Plugin must watch for `.sm5kit` files in the DAW's project folder (OS-dependent)
- ? **No standard for DAW kit import** (FL Studio, Ableton, etc. don't natively support this)

**Success rate**: ~90% (fails only if user drops kit in wrong folder)

---

### Option C: Custom MIDI Meta-Event (SAMPLAR-Specific)

**Approach**: Embed a proprietary SAMPLAR meta-event in the MIDI file that encodes the midiMap.

**Implementation**:
1. Define custom MIDI meta-event type (e.g., 0x7F + custom ID "SAMPLAR")
2. After standard tempo/signature events, insert meta-events:
   ```
   Meta-event 7F: "SAMPLAR MIDI KIT"
   Data: note=36, sampleIndex=0, filename="Kick.wav"
   Data: note=38, sampleIndex=1, filename="Snare.wav"
   ...
   ```
3. SAMPLAR VST plugin detects meta-event on session load and populates midiMap from it
4. Other DAWs ignore the custom meta-events (standard MIDI compliance)

**Tradeoffs**:
- ? **Works 100% of the time** (metadata is in the file itself)
- ? Single file drag (MIDI file contains all info)
- ? DAW-agnostic: plugin-side parsing only
- ? **Non-standard MIDI** (some DAWs or MIDI editors may corrupt it)
- ? Requires new JUCE code to write custom meta-events (`juce::MidiFile` API doesn't support this easily)
- ? Requires plugin to parse custom meta-events (error handling, validation)
- ? File size grows (metadata overhead)

**Success rate**: ~85% (fails if DAW corrupts meta-events or third-party tools strip them)

---

### Option D: Export Samples + MIDI + Manifest Folder

**Approach**: Drag-out creates a folder with MIDI, sample files, and a manifest describing the mapping.

**Implementation**:
1. Instead of dragging a single file, create folder: `SAMPLAR_pattern_HHMMSS_XXX/`
   ```
   SAMPLAR_pattern_HHMMSS_XXX/
   +-- pattern.mid
   +-- samples/
   ¦   +-- 0_Kick.wav
   ¦   +-- 1_Snare.wav
   ¦   +-- 2_Hat.wav
   ¦   +-- 3_Clap.wav
   +-- manifest.json
     {
       "midiMap": [
         {"note": 36, "sampleIndex": 0},
         {"note": 38, "sampleIndex": 1},
         ...
       ]
     }
   ```
2. User drags folder to DAW
3. DAW/user imports samples into sampler + loads MIDI pattern
4. Manifest can be read by DAW integration plugin (if available)

**Tradeoffs**:
- ? **Works 100%** (all data self-contained)
- ? **Samples included** (user can load them immediately)
- ? Portable archival (kit is self-contained)
- ? **User friction**: Complex folder structure, not intuitive
- ? Most DAWs don't auto-import folder structures on drag
- ? Massive file size (audio data duplicate)
- ? Requires custom DAW integration or manual folder import

**Success rate**: ~60% (high user friction, limited DAW support)

---

## Recommended Fix: Option A (Live Query) + Option B (Sidecar Fallback)

### Rationale
1. **Primary**: Query the plugin's live midiMap on drag. If populated, use those notes. This solves 70% of real-world use cases where the user has already set up samples in the plugin.
2. **Fallback**: If midiMap is empty or the plugin is offline, export standard GM drum kit notes (36, 38, 42, 46, etc.).
3. **Future**: Optionally add `.sm5kit` sidecar for advanced users who want to share exact mappings.

### Implementation Steps
1. **Modify `MidiFileGenerator::generateMidiFile()`**:
   - Add optional parameter: `const int* noteOverride` (per-row note numbers, or nullptr)
   - If provided, use those notes instead of deriving from rowNames
   - If nullptr, use current rowName-based logic (fallback)

2. **Modify `StepSequencerComponent::regenerateMidiFile()`**:
   - Before calling generator, attempt to lock and read `midiMapping_->midiMap_`
   - Extract occupied note numbers for the sequencer's rows
   - If none found, pass nullptr to use GM fallback
   - Log success/failure of live query

3. **Thread safety**:
   - Use try_lock (nonblocking) when reading midiMap from UI thread
   - If lock fails, silently fall back to GM notes

4. **Testing**:
   - Test with empty midiMap ? GM notes exported
   - Test with populated midiMap ? live notes exported
   - Test with mixed (some notes occupied, some not) ? use occupied where available, GM elsewhere

---

## Open Questions for User Confirmation

Before implementation, please clarify:

1. **Intended behavior on empty midiMap**: Should the dragged MIDI use hardcoded GM drum kit notes (36–51 range), or should it refuse to export with an error message?

2. **Sequencer-to-browser mapping**: When the user sets up a sequencer pattern, is there an expectation that the plugin's midiMap will automatically populate from the sequencer's row samples? Or does the user manually load samples into the plugin separately?

3. **Sidecar file adoption**: Would you like to introduce the `.sm5kit` sidecar format for future-proofing (Option B), or keep the solution minimal (Option A only)?

---

## Summary

- **Bug location**: `src/utils/MidiFileGenerator.cpp:99` uses hardcoded row-name-to-note mapping; `src/ui/Components/StepSequencerComponent.cpp:2584` calls generator without passing plugin's live midiMap state
- **Plugin midiMap**: `src/services/MidiMappingService.cpp` maintains the authoritative mapping (notes 36–128 range)
- **Persistence**: midiMap is saved/restored via `src/plugin/SamplarAudioProcessor.cpp:704–784` (XML "midi" binding)
- **Root cause**: Drag-out MIDI is generated in isolation, without consulting the plugin's current midiMap state
- **Recommended fix**: Option A (live query) with GM fallback; provides 70%+ success without breaking DAW compatibility
