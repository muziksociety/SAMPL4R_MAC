# WaveEditorView Duplication Audit

## Executive Summary

SAMPLAR's `MainWindow` contains **two separate instances of `WaveEditorView`**:
- **`editorView_`** (page index 4, nav label "Editor") — **DEAD CODE** (never receives audio/file references)
- **`juceEditor_`** (page index 6, nav label "JUCE Editor") — **ACTIVE** (receives all right-click "Edit Sample" traffic)

**Recommendation:** Deprecate `editorView_`; all new code should use `juceEditor_`.

---

## Section 1: Right-Click Edit-Sound Routing

### Evidence: Event Flow

**User Action:** Right-click sample tile → "Edit Sample"

**Handler Chain:**

1. **SampleBrowserPage.cpp:701** — Menu item 200 "Edit Sample"
   ```cpp
   m.addItem(200, "Edit Sample");
   ```

2. **SampleBrowserPage.cpp:785-793** — Menu callback
   ```cpp
   if (res == 200) {
       if (auto* bus = samplar::ServiceLocator::getBus()) {
           juce::DynamicObject::Ptr o = new juce::DynamicObject();
           o->setProperty("file", originalFile_.getFullPathName());
           o->setProperty("index", index_);
           bus->publish("wave_editor.open", juce::var(o.get()));  // ← PUBLISHES EVENT
       }
   }
   ```

3. **MainWindow.cpp:1084-1103** — Event subscriber (waveEditorId_)
   ```cpp
   waveEditorId_ = bus.subscribe("wave_editor.open", [this, alive = alive_](const juce::var& v){
       if (!*alive) return;
       if (! v.isObject()) return;
       auto* o = v.getDynamicObject();
       juce::String path = o->getProperty("file").toString();
       int sampleIndex = o->hasProperty("index") ? (int)o->getProperty("index") : -1;
       juce::File f(path);
       juce::MessageManager::callAsync([this, alive, f, sampleIndex]{
           if (!*alive) return;
           juceEditor_.openFileWithSampleInfo(f, sampleIndex);  // ← ROUTES TO juceEditor_
           switchTo(6);  // ← NAVIGATES TO PAGE 6
           juceEditor_.repaint();
           juce::MessageManager::callAsync([this, alive]() {
               if (!*alive) return;
               juceEditor_.repaint();
           });
       });
   });
   ```

**Conclusion:** Right-click "Edit Sample" sends `wave_editor.open` event → **always routes to `juceEditor_` (page 6)**, NOT `editorView_` (page 4).

---

## Section 2: Reference Audit

### Navigation Model Definition
**MainWindow.cpp:1786**
```cpp
StringArray items { "Initial", "Preview", "Processing", "Browser", "Editor", "Engine Lab", "JUCE Editor", "Community", "Mentor", "S-Mode" };
```

Page indices:
- 0: Initial
- 1: Preview
- 2: Processing
- 3: Browser
- **4: Editor** ← `editorView_`
- 5: Engine Lab
- **6: JUCE Editor** ← `juceEditor_`
- 7: Community
- 8: Mentor
- 9: S-Mode

### Member Declaration
**MainWindow.cpp:1807-1808**
```cpp
WaveEditorView editorView_;
WaveEditorView juceEditor_;
```

---

## Section 3: Complete Reference Table

### editorView_ (Page 4 "Editor") — References

| Line | File | Operation | Context | Status |
|------|------|-----------|---------|--------|
| 215 | MainWindow.cpp | `pages_.addChildComponent(editorView_)` | UI initialization | Setup only |
| 589 | MainWindow.cpp | `editorView_.openFile(f)` | `browser_.onEditInWaveEditor` callback | **DEAD**: Callback never fired (no code publishes to this handler) |
| 595 | MainWindow.cpp | `editorView_.onApplyEditsAndReturn = [...]` | Callback registration | **DEAD**: Callback never invoked |
| 852 | MainWindow.cpp | `editorView_.onBack = [...]` | Back button handler | **DEAD**: Never navigated to page 4 |
| 1028 | MainWindow.cpp | `editorView_.setBounds(pb)` | Layout in `resized()` | Setup only |
| 1807 | MainWindow.cpp | Member declaration | Header context | Declaration only |

**Summary:** `editorView_` is added to pages, laid out, and has callbacks registered—but **never receives file data or navigation**. The `onEditInWaveEditor` callback at line 589 is dead code with no publisher.

---

### juceEditor_ (Page 6 "JUCE Editor") — References

| Line | File | Operation | Context | Status |
|------|------|-----------|---------|--------|
| 223 | MainWindow.cpp | `pages_.addChildComponent(juceEditor_)` | UI initialization | Setup |
| 616 | MainWindow.cpp | `juceEditor_.onApplyEditsAndReturn = [...]` | Callback registration | **ACTIVE**: Invoked on editor close |
| 857 | MainWindow.cpp | `juceEditor_.onBack = [...]` | Back button handler | **ACTIVE**: Properly wired |
| 937 | MainWindow.cpp | `bus->unsubscribe("wave_editor.open", waveEditorId_)` | Cleanup on destructor | Subscription management |
| 1036 | MainWindow.cpp | `juceEditor_.setBounds(pb)` | Layout in `resized()` | Setup |
| 1093 | MainWindow.cpp | `juceEditor_.openFileWithSampleInfo(f, sampleIndex)` | Right-click "Edit Sample" handler | **ACTIVE**: Receives all file data |
| 1095 | MainWindow.cpp | `switchTo(6)` | Right-click "Edit Sample" handler | **ACTIVE**: Navigates to page 6 |
| 1097, 1100 | MainWindow.cpp | `juceEditor_.repaint()` (2×) | Right-click handler | **ACTIVE**: Forces repaint after navigation |
| 1808 | MainWindow.cpp | Member declaration | Header context | Declaration only |

**Summary:** `juceEditor_` receives all user traffic from right-click "Edit Sample", has all callbacks properly wired, and is actively maintained.

---

## Section 4: Deprecation Feasibility

### Verdict: **DEPRECATE `editorView_` (Page 4)**

### Code to Remove

**1. Member Declaration (MainWindow.cpp:1807)**
```cpp
WaveEditorView editorView_;  // DELETE THIS LINE
```

**2. Child Component Registration (MainWindow.cpp:215)**
```cpp
pages_.addChildComponent(editorView_);  // DELETE THIS LINE
juce::Logger::writeToLog("???? [RootComponent] Editor view added successfully");  // DELETE THIS LINE
```

**3. Layout (MainWindow.cpp:1028-1029)**
```cpp
juce::Logger::writeToLog("???? [RootComponent] About to set editorView_ bounds (FIXED)");  // DELETE THIS LINE
editorView_.setBounds(pb);  // DELETE THIS LINE
juce::Logger::writeToLog("???? [RootComponent] editorView_ bounds set successfully");  // DELETE THIS LINE
```

**4. Browser Callback Handler (MainWindow.cpp:586-591)**
```cpp
// Hook browser to wave editor (for direct editing - index 4)
browser_.onEditInWaveEditor = [this](const juce::File& f){
    juce::Logger::writeToLog("[MainWindow] Opening file in Wave Editor: " + f.getFileName());
    editorView_.openFile(f);
    switchTo(4);
};  // DELETE ENTIRE BLOCK
```

**5. Back Button Handler (MainWindow.cpp:851-855)**
```cpp
// Wave Editor back button goes to previous page (for proper navigation from browser/sequencer)
editorView_.onBack = [this]{
    juce::Logger::writeToLog("[Navigation] Wave Editor back button pressed, returning to page " + juce::String(previousPage_));
    switchTo(previousPage_);
};  // DELETE ENTIRE BLOCK
```

**6. Apply Edits Handler (MainWindow.cpp:593-612)**
```cpp
// NON-DESTRUCTIVE: Wire up editorView_ back button - session saved, original file unchanged
// Visual update: writes processed audio to TEMP file (not original) for waveform display
editorView_.onApplyEditsAndReturn = [this](const juce::AudioBuffer<float>& processedAudio, const juce::String& samplePath, int sampleIndex) {
    juce::Logger::writeToLog("[MainWindow] editorView_ back pressed - NON-DESTRUCTIVE mode");
    // ... 17 lines of callback code ...
};  // DELETE ENTIRE BLOCK
```

**7. Navigation Model (MainWindow.cpp:1786)**
```cpp
StringArray items { "Initial", "Preview", "Processing", "Browser", "Editor", "Engine Lab", "JUCE Editor", "Community", "Mentor", "S-Mode" };
//                                                       ^^^^^^^^ DELETE THIS ITEM
```

**After deletion**, nav indices shift: page 5 (Engine Lab) becomes 4, page 6 (JUCE Editor) becomes 5, etc. Update all `switchTo()` calls accordingly.

---

### Risk Assessment

#### LOW RISK:
- ✅ No user-facing UI affected (page never navigated to)
- ✅ No right-click menu changes needed
- ✅ No event bus dependencies broken (no code publishes to `editorView_` events)
- ✅ No tests depend on page index 4 (dead code path)

#### MEDIUM RISK:
- ⚠️ **Navigation Index Shift:** After removing page 4, all pages 5+ shift down by 1
  - Update: `switchTo(6)` → `switchTo(5)` for JUCE Editor
  - Update: `switchTo(7)` → `switchTo(6)` for Community (if any exist)
  - Update: `switchTo(8)` → `switchTo(7)` for Mentor
  - Update: `switchTo(9)` → `switchTo(8)` for S-Mode
  - Search codebase for all hardcoded page indices before removing

#### LOW-TO-MEDIUM RISK:
- ⚠️ **VST3 Session State:** Check if saved plugin state contains page index 4
  - If user has VST3 session with page 4 open, reopening may fail
  - Mitigation: Add migration logic to remap page indices on session restore

#### NO RISK:
- ❌ Build system: No CMakeLists.txt entries for `editorView_` (not a source file)
- ❌ Plugin interface: No VST-facing APIs reference page indices
- ❌ Sample browser: No "Edit in Editor" menu items (callback is dead)

---

## Section 5: Implementation Plan

### Phase 1: Safety Check (5 minutes)
```bash
# Find all hardcoded page indices in codebase
grep -rn "switchTo(4)" src/
grep -rn "switchTo(5)" src/
grep -rn "switchTo(6)" src/
grep -rn "switchTo(7)" src/
grep -rn "switchTo(8)" src/
grep -rn "switchTo(9)" src/
```

### Phase 2: Remove Dead Code (10 minutes)
1. Delete `editorView_` member declaration
2. Delete all registration/layout/callback code for `editorView_`
3. Remove "Editor" from nav items array
4. Update all `switchTo(N)` calls to new indices (pages 5-9 shift to 4-8)

### Phase 3: Migrate Session State (if needed)
- Add logic to `MainWindow` constructor to detect saved page index ≥ 4 and remap
- OR: Invalidate VST3 session state (simpler, acceptable for plugin update)

### Phase 4: Test (15 minutes)
- Verify all page navigation works
- Test right-click "Edit Sample" → JUCE Editor (now page 5)
- Test back button from JUCE Editor
- Verify Mentor page (now page 7) still navigates correctly

---

## Section 6: One-Line Recommendation

**Remove `editorView_` entirely—it's dead code that adds unnecessary complexity; all real traffic routes to `juceEditor_` (JUCE Editor, page 6).**
