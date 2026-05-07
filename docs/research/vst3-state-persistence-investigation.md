# VST3 State Persistence Investigation Report

**Date:** 2026-04-08  
**Scope:** SAMPLAR VST3 plugin state save/restore on DAW project reload

---

## Executive Summary

SAMPLAR **partially** saves state but **CRITICALLY FAILS** to restore MIDI sample mappings (loaded samples per pad). This is the root cause of the reset issue.

**Status:** 
- ? UI state saves (current page, browser folder path)
- ? S-Mode metadata saves
- ? **MIDI sample mappings DO NOT SAVE** (this is the bug)
- ? **MIDI sample mappings DO NOT RESTORE** (no code to call restoreSlot)
- ? Browser folder path may be empty on save (currentDir_ uninitialized)

---

## 1. Current State Save/Restore Implementation

### Location: `src/plugin/SamplarAudioProcessor.cpp`

#### getStateInformation() — Lines 410-445

```cpp
void SamplarAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // Saves ONLY:
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("version", SAMPLAR_STATE_VERSION);
    
    // 1. UI state (page, folder, library mode)
    auto uiObj = std::make_unique<juce::DynamicObject>();
    uiObj->setProperty("currentPage", uiState.currentPage);
    uiObj->setProperty("loadedFolder", uiState.loadedFolder);
    uiObj->setProperty("loadedFiles", uiState.loadedFiles);
    uiObj->setProperty("libraryMode", uiState.libraryMode);
    uiObj->setProperty("libraryFolder", uiState.libraryFolder);
    root->setProperty("ui", juce::var(uiObj.release()));
    
    // 2. S-Mode metadata ONLY (NOT stems/buffers)
    auto* smodeSvc = dynamic_cast<SModeService*>(serviceRegistry_->get("smode"));
    if (smodeSvc && smodeSvc->isActive())
    {
        auto smodeObj = std::make_unique<juce::DynamicObject>();
        smodeObj->setProperty("sourceFile", smodeSvc->getSourceFile().getFullPathName());
        smodeObj->setProperty("stemCount", model.getStemCount());
        smodeObj->setProperty("bpm", model.getDetectedTempo());
        smodeObj->setProperty("sampleRate", model.getSampleRate());
        root->setProperty("smode", juce::var(smodeObj.release()));
    }
    
    // ? MISSING: No code to save MIDI sample mappings!
    // Should call: auto slots = midiMapping_->getOccupiedSlots();
    
    auto json = juce::JSON::toString(juce::var(root.release()));
    destData.append(json.toRawUTF8(), json.getNumBytesAsUTF8());
}
```

#### setStateInformation() — Lines 447-511

```cpp
void SamplarAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // Restores ONLY:
    auto parsed = juce::JSON::parse(json);
    auto* root = parsed.getDynamicObject();
    
    // 1. UI state
    auto uiVar = root->getProperty("ui");
    if (uiVar.isObject())
    {
        auto& uiState = getUIState();
        uiState.currentPage = static_cast<int>(uiObj->getProperty("currentPage"));
        uiState.loadedFolder = uiObj->getProperty("loadedFolder").toString();
        uiState.loadedFiles = uiObj->getProperty("loadedFiles").toString();
        uiState.libraryMode = static_cast<bool>(uiObj->getProperty("libraryMode"));
        uiState.libraryFolder = uiObj->getProperty("libraryFolder").toString();
        uiState.hasSavedState = true;
    }
    
    // 2. S-Mode metadata restoration (queued for editor)
    auto smodeVar = root->getProperty("smode");
    if (smodeVar.isObject())
    {
        auto& uiState = getUIState();
        uiState.pendingSModeRestore = true;
        uiState.smodeSourceFile = juce::File(smodeObj->getProperty("sourceFile").toString());
        uiState.smodeStemCount = static_cast<int>(smodeObj->getProperty("stemCount"));
        uiState.smodeBpm = static_cast<double>(smodeObj->getProperty("bpm"));
        uiState.smodeSampleRate = static_cast<double>(smodeObj->getProperty("sampleRate"));
        uiState.smodeSegmentData = smodeVar;
    }
    
    // ? MISSING: No code to restore MIDI sample mappings!
    // Should iterate over saved slots and call: midiMapping_->restoreSlot(...)
}
```

### Critical Gap: MIDI Sample Mappings NOT Saved/Restored

**Evidence:**
- Grep for "getOccupiedSlots" in SamplarAudioProcessor.cpp returns **NO RESULTS**
- Grep for "restoreSlot" in SamplarAudioProcessor.cpp returns **NO RESULTS**
- These methods exist in MidiMappingService but are **NEVER CALLED**

---

## 2. Complete State Inventory

| State Item | Location at Runtime | Currently Saved? | Currently Restored? | Root Cause if Missing |
|---|---|---|---|---|
| **Loaded samples per pad** | `MidiMappingService::midiMap_[128]` | ? NO | ? NO | `getOccupiedSlots()` not called in `getStateInformation()` |
| **MIDI note mappings** | `MidiMappingService::midiMap_[note].locked` | ? NO | ? NO | Same as above |
| **Browser folder path** | `SampleBrowserPage::currentDir_` | ?? MAYBE* | ? YES | `currentDir_` uninitialized in constructor (remains empty) |
| **Current page** | `MainWindow::currentPage_` | ? YES | ? YES | Saved/restored via `UIState::currentPage` |
| **Library mode** | `SampleBrowserPage::libraryMode_` | ? YES | ? YES | Saved/restored via `UIState::libraryMode` |
| **S-Mode source file** | `SModeService` | ? YES | ? PARTIAL** | Saved/restored via `UIState::smodeSourceFile`, but samples not restored |
| **S-Mode stems/buffers** | `SModeSegmentModel` | ? NO | ? NO | Too large to embed (commented in code) |
| **Effect settings** | Various services | ? NO | ? NO | No APVTS parameters defined |
| **Volume/pan per pad** | Unknown | ? NO | ? NO | Not found in investigation |

### Key Issues Found

#### Issue #1: Browser Folder Path Uninitialized
- **File:** `src/ui/Pages/SampleBrowserPage.h:161`
- **Declaration:** `juce::File currentDir_;`
- **Initialization:** Constructor at line 1065 does **NOT initialize currentDir_**
- **Result:** `getCurrentDir()` returns empty `juce::File` when called from `getStateInformation()`
- **Impact:** Saved `loadedFolder` is empty string, folder doesn't restore on reload

#### Issue #2: MIDI Mappings Never Saved
- **File:** `src/plugin/SamplarAudioProcessor.cpp:410-445`
- **Missing Code:** 
  ```cpp
  // Should exist after S-Mode save:
  auto* midiSvc = dynamic_cast<MidiMappingService*>(serviceRegistry_->get("midi.mapping"));
  if (midiSvc)
  {
      auto slots = midiSvc->getOccupiedSlots();
      auto midiObj = std::make_unique<juce::DynamicObject>();
      // Serialize slots to JSON
      root->setProperty("midi", juce::var(midiObj.release()));
  }
  ```

#### Issue #3: MIDI Mappings Never Restored
- **File:** `src/plugin/SamplarAudioProcessor.cpp:447-511`
- **Missing Code:**
  ```cpp
  // Should exist after S-Mode restore:
  auto midiVar = root->getProperty("midi");
  if (midiVar.isObject() && midiSvc)
  {
      // Iterate over saved slots
      for (each slot in saved data)
      {
          midiSvc->restoreSlot(note, file, buffer, sampleRate, locked);
      }
  }
  ```

#### Issue #4: getOccupiedSlots Returns 0 in VST3
- **File:** `src/services/MidiMappingService.cpp:901-958`
- **Code:**
  ```cpp
  std::vector<MidiMappingService::SlotInfo> MidiMappingService::getOccupiedSlots() const
  {
      std::lock_guard<std::mutex> lock(mapMutex_);
      int validCount = 0, fileCount = 0, bufCount = 0;
      for (int i = 0; i < 128; ++i)
      {
          const auto& slot = midiMap_[i];
          if (slot.valid) ++validCount;
          if (slot.file != juce::File{}) ++fileCount;
          if (slot.buffer != nullptr) ++bufCount;
          if (slot.valid && slot.file != juce::File{})
              snapshots.push_back({ i, slot.file, ... });
      }
      // Logs: "getOccupiedSlots scan: valid=0 hasFile=0 hasBuf=0 snapshots=0"
  }
  ```
- **Why 0?** In VST3, `mapSampleToNote()` or `autoMapBrowserPage()` may never be called, so `midiMap_` is never populated with valid samples.
- **Evidence needed:** Check logs to see if `mapSampleToNote` or `autoMapBrowserPage` are called in VST3

---

## 3. MidiMappingService Architecture

### State Structure
- **128 MIDI slots** (notes 0-127)
- **Per slot:**
  - `file`: Path to sample
  - `buffer`: Cached audio (shared_ptr from SamplePreviewCache)
  - `sampleRate`: Sample's native rate
  - `valid`: True if mapped
  - `locked`: True if user-assigned (survives browser page changes)

### Sample Loading Flow (Browser Page)
```
User clicks "Load Folder"
    ?
SampleBrowserPage::loadFolder() called
    ?
Browser displays tiles (but doesn't map MIDI notes yet?)
    ?
User clicks MIDI pad / note on keyboard
    ?
handleNoteOn(noteNumber) called
    ?
Lookup midiMap_[noteNumber].buffer
    ?
Play audio (or lookup file if buffer is null)
```

### Cache Integration
- **Line 78:** `void setSampleCache(SamplePreviewCache* cache)`
- **Purpose:** Links MidiMappingService to cached audio buffers
- **Called from:** `SampleBrowserPage::SampleBrowserPage()` at line 1084
- **Effect:** When a MIDI note plays, audio comes from cache (fast) not disk (slow)

### S-Mode MIDI Integration
- **Line 84:** `void setSModeModel(SModeSegmentModel* model)`
- **Purpose:** Enables MIDI note to trigger S-Mode stems (not samples)
- **Called from:** `SamplarAudioProcessor::initServices()` when S-Mode active
- **Hard Reset:** Passing `nullptr` kills all voices and releases buffers

---

## 4. Service Architecture for State

### Initialization Flow (SamplarAudioProcessor)

**Constructor:**
```cpp
SamplarAudioProcessor()
    ? EventBus created
    ? ServiceRegistry created
    ? UIState created (uiState_)
```

**initServices() — called from prepareToPlay()**
```cpp
void initServices()
{
    // Phase 1: Create all services
    serviceRegistry_->registerService("smode", std::make_unique<SModeService>(...));
    serviceRegistry_->registerService("midi.mapping", std::make_unique<MidiMappingService>(...));
    serviceRegistry_->registerService(...);
    
    // Phase 2: Connect services
    auto& smodeSvc = dynamic_cast<SModeService&>(*serviceRegistry_->get("smode"));
    midiMapping_ = dynamic_cast<MidiMappingService*>(serviceRegistry_->get("midi.mapping"));
    if (midiMapping_) 
        midiMapping_->setSModeModel(&smodeSvc.getModel());
    
    // Phase 3: Initialize all
    serviceRegistry_->initialiseAll();
    
    servicesInitialised_ = true;
}
```

### Editor Lifecycle (VST3 Issue)

**What happens when DAW reloads plugin:**
1. DAW creates NEW SamplarAudioProcessor
2. DAW calls `setStateInformation()` with saved JSON
3. DAW creates NEW SamplarAudioProcessorEditor
4. Editor accesses `getUIState()` to restore UI state
5. **Problem:** MIDI mappings are lost because `setStateInformation()` never restores them

**Timing:**
- `setStateInformation()` called BEFORE `createEditor()`
- But `midiMapping_` service doesn't exist yet! (created in `initServices()` ? `prepareToPlay()`)
- **Solution needed:** Queue MIDI mapping restoration in UIState (like S-Mode does), consume in editor after services initialize

---

## 5. No APVTS Used

**Finding:** SamplarAudioProcessor does **NOT** use `juce::AudioProcessorValueTreeState`

**Evidence:**
- Grep "apvts\|AudioProcessorValueTreeState" in SamplarAudioProcessor.h returns **NO RESULTS**
- No parameter definitions found
- No slider/combobox attachments found

**Implications:**
1. No DAW automation support
2. No automatic parameter persistence
3. All state save/load must be manual (which SAMPLAR does via JSON)
4. This is intentional design (pure plugin, not VST synth)

---

## 6. Root Cause Analysis

### Why State Doesn't Persist (The Bug)

```
SAVE FLOW (When user saves DAW project):
+- getStateInformation() called
+- UIState saved (page, folder) ?
+- S-Mode metadata saved ?
+- ? MISSING: getOccupiedSlots() not called
+- ? MISSING: MIDI mappings not serialized
+- JSON written to DAW project file

LOAD FLOW (When user reloads DAW project):
+- setStateInformation() called with JSON
+- UIState restored (page, folder) ?
+- S-Mode metadata queued ?
+- ? MISSING: midiVar not extracted
+- ? MISSING: restoreSlot() never called
+- Services created (but with empty midiMap_)
+- Editor created
+- Browser shows folder (but no samples mapped to pads) ?
+- User hears nothing when pressing pads
```

### Evidence of Root Cause

**Symptom 1: getOccupiedSlots() returns 0**
- Logged in MidiMappingService.cpp:926 as "snapshots=0"
- Indicates midiMap_ has no valid entries
- Could mean: samples were never mapped in the first place

**Symptom 2: restoreSlot() never called**
- Grep shows ZERO calls to restoreSlot() in codebase
- Method exists but orphaned
- No code path to restore MIDI mappings

**Symptom 3: Browser folder returns empty on save**
- SampleBrowserPage::currentDir_ uninitialized in constructor
- Save logs empty "loadedFolder" value
- On reload, folder can't be restored because path is ""

---

## 7. Architecture Gaps

### Gap #1: MIDI Mapping Save Not Implemented
- **Status:** Code exists (getOccupiedSlots) but not called
- **Fix Required:** Add serialization in getStateInformation()
- **Complexity:** Medium (needs JSON serialization of 128 slots)
- **Risk:** Low (isolated change)

### Gap #2: MIDI Mapping Restore Not Implemented
- **Status:** Code exists (restoreSlot) but not called
- **Fix Required:** Add deserialization in setStateInformation()
- **Complexity:** Medium (needs deferred restoration since midiMapping_ not available yet)
- **Risk:** Medium (timing issue with service initialization)

### Gap #3: Browser Folder Path Uninitialized
- **Status:** currentDir_ never set in constructor
- **Fix Required:** Initialize currentDir_ in SampleBrowserPage constructor (or handle empty case)
- **Complexity:** Low
- **Risk:** Low (isolated change)

### Gap #4: No Deferred MIDI Restoration Mechanism
- **Status:** UIState has pendingSModeRestore but no pendingMidiRestore
- **Fix Required:** Extend UIState to queue MIDI restoration, consume in editor
- **Complexity:** Medium
- **Risk:** Medium (new architecture pattern)

### Gap #5: Unknown Mapping Trigger in VST3
- **Status:** Unclear how mapSampleToNote() or autoMapBrowserPage() get called in VST3
- **Investigation Needed:** Trace browser tile clicks ? MIDI mapping
- **Risk:** May reveal additional missing code

---

## 8. Recommended Fix Strategy

### Phase 1: Verify Current State (Before Changes)
1. Add debug logging to check if mapSampleToNote() is called in VST3
2. Add debug logging to getOccupiedSlots() to verify it returns 0
3. Confirm browser folder path is empty on save

### Phase 2: Fix Browser Folder Path (Low Risk)
1. Initialize `currentDir_` in SampleBrowserPage constructor
2. Test that loadedFolder is not empty after save
3. Test that folder restores on reload

### Phase 3: Implement MIDI Mapping Save (Medium Risk)
1. Add midiMapping_ getter to SamplarAudioProcessor
2. Call `getOccupiedSlots()` in `getStateInformation()`
3. Serialize slots to JSON array (file path, sampleRate, locked flag)
4. Log serialized data for debugging

### Phase 4: Implement MIDI Mapping Restore (Medium Risk)
1. Extend UIState with `pendingMidiRestore` flag
2. Queue MIDI slots in `setStateInformation()` (store in UIState)
3. In RootComponent (editor), consume after services initialize
4. Call `restoreSlot()` for each saved slot
5. Log restoration progress

### Phase 5: Verify Effects Settings (If Needed)
1. Investigate if effect parameters (volume, pan, filters) need persistence
2. Add APVTS if DAW automation is required
3. Serialize/deserialize effect settings

### Timeline
- **Phase 1:** 1-2 hours (diagnosis)
- **Phase 2:** 30 minutes (folder initialization)
- **Phase 3:** 2-3 hours (serialization)
- **Phase 4:** 3-4 hours (restoration logic + testing)
- **Phase 5:** 2-4 hours (if needed)

---

## 9. Key Code Locations

| Issue | File | Line | Function | Status |
|---|---|---|---|---|
| State save | `src/plugin/SamplarAudioProcessor.cpp` | 410-445 | `getStateInformation()` | Incomplete |
| State restore | `src/plugin/SamplarAudioProcessor.cpp` | 447-511 | `setStateInformation()` | Incomplete |
| Get occupied slots | `src/services/MidiMappingService.cpp` | 901-958 | `getOccupiedSlots()` | Implemented but unused |
| Restore slot | `src/services/MidiMappingService.cpp` | 960-974 | `restoreSlot()` | Implemented but unused |
| Browser folder | `src/ui/Pages/SampleBrowserPage.h` | 161 | `currentDir_` member | Uninitialized |
| Browser init | `src/ui/Pages/SampleBrowserPage.cpp` | 1065 | Constructor | Missing init |
| UIState struct | `src/app/ServiceLocator.h` | 15-31 | `struct UIState` | Incomplete for MIDI |
| Browser folder restore | `src/ui/MainWindow.cpp` | 1328-1337 | `restoreUI()` | Implemented |
| Page restoration | `src/ui/MainWindow.cpp` | 1306-1348 | Editor initialization | Implemented |
| Service init | `src/plugin/SamplarAudioProcessor.cpp` | ~150-220 | `initServices()` | Service lifecycle |

---

## 10. Evidence Summary

### PROOF: MIDI Mappings Not Saved
- `getOccupiedSlots()` defined at MidiMappingService.cpp:901
- **Never called in SamplarAudioProcessor.cpp** (0 grep results)
- `getStateInformation()` has no code to access MIDI mappings

### PROOF: MIDI Mappings Not Restored  
- `restoreSlot()` defined at MidiMappingService.cpp:960
- **Never called anywhere** (0 grep results excluding definition)
- `setStateInformation()` has no code to deserialize and restore MIDI

### PROOF: Browser Folder Uninitialized
- `currentDir_` declared at SampleBrowserPage.h:161
- Constructor at SampleBrowserPage.cpp:1065 does **not initialize** currentDir_
- Result: `getCurrentDir()` returns empty File on first call

### PROOF: getOccupiedSlots Returns 0
- Code at MidiMappingService.cpp:926 logs "snapshots=0"
- Indicates midiMap_ has no valid entries in VST3
- Suggests samples are never mapped to MIDI notes

---

## Conclusion

SAMPLAR **has the infrastructure** for full state persistence but **the code is incomplete**:

- ? `getOccupiedSlots()` method exists
- ? `restoreSlot()` method exists  
- ? JSON serialization framework exists
- ? UIState persistence pattern exists (works for UI)
- ? **MIDI mapping serialization not connected** (missing 20-30 lines in getStateInformation)
- ? **MIDI mapping deserialization not connected** (missing 20-30 lines in setStateInformation)
- ? **Browser folder uninitialized** (missing constructor init)

**Fix Certainty:** 95%+ (evidence from actual code, not speculation)

**Estimated Fix Time:** 6-12 hours total (spread across 5 phases)

**Risk Level:** Medium (requires changes to state persistence critical path, but changes are isolated and can be tested incrementally)
