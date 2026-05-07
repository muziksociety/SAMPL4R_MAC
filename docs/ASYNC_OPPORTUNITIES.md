# UI Thread Blocking Operations - Async Opportunities

This document identifies synchronous operations in the UI code that could be moved to background threads to improve responsiveness.

## CRITICAL (Fix Immediately)

| # | File | Line | Operation | Impact |
|---|------|------|-----------|--------|
| **1** | `src/ui/Components/FolderTreeItem.cpp` | 205 | `findChildFiles` **IN PAINT()** | Frame drops when scrolling tree |
| **2** | `src/ui/Pages/SampleBrowserPage.cpp` | 1803 | `findChildFiles` in `loadFolder()` | 2-10s freeze on large directories |

### Issue #1: File I/O in paint() - FolderTreeItem.cpp:205
**This is the worst offender.** The `countAudioFiles()` function is called from `paintItem()` and performs file system I/O on every repaint. This directly violates real-time painting constraints.

```cpp
void FolderTreeItem::paintItem(...) {
    // ... painting code ...
    int audioCount = countAudioFiles();  // Calls FILE I/O in paint!
}

int FolderTreeItem::countAudioFiles() const {
    juce::Array<juce::File> audioFiles;
    folder_.findChildFiles(audioFiles, ...);  // BLOCKING!
    return audioFiles.size();
}
```

**Fix**: Cache the count, update asynchronously in background, show "..." while computing.

---

## HIGH SEVERITY

| # | File | Line | Operation | Impact |
|---|------|------|-----------|--------|
| 3 | `src/ui/Pages/SampleBrowserPage.cpp` | 2701-2707 | Recursive scan + fallback before thread | 5-15s freeze |
| 4 | `src/ui/Pages/SampleBrowserPage.cpp` | 2906-2913 | Same pattern (custom categories) | 5-15s freeze |

These perform recursive file collection BEFORE spawning the background thread, causing the UI to freeze during the collection phase.

---

## MEDIUM SEVERITY

| # | File | Line | Operation | Impact |
|---|------|------|-----------|--------|
| 5 | `src/ui/Pages/InitialPage.cpp` | 221 | JSON parse from file | 50-200ms block |
| 6 | `src/ui/WaveEditor/sm4port/PresetManager.cpp` | 46,113 | Preset file reads | 50-150ms |
| 7 | `src/ui/WaveEditor/sm4port/PresetManager.cpp` | 67,86,237 | File writes on save | 100-300ms |
| 8 | `src/ui/WaveEditor/sm4port/State.cpp` | 73,237 | Session load/save | 200-500ms |
| 9 | `src/ui/Components/SettingsDialog.cpp` | 1409,1697,1442,1742 | JSON settings I/O | 50-100ms |
| 10 | `src/ui/Components/FolderTreeItem.cpp` | 19 | `mightContainSubItems()` file scan | 50-200ms |
| 11 | `src/ui/Pages/SampleBrowserPage.cpp` | 880,2056,1135,2165 | Large collection loops | 200-800ms with 5000+ samples |

---

## LOW SEVERITY

| # | File | Line | Operation | Impact |
|---|------|------|-----------|--------|
| 12 | `src/ui/Pages/InitialPage.cpp` | 793-805 | Logo search loop | Startup only |
| 13 | `src/ui/Components/CategoryEditorDialog.cpp` | 298-312 | Preset loading | Dialog init |
| 14 | `src/ui/WaveEditor/WaveEditorView.cpp` | 718 | Debug export | Rarely used |

---

## Implementation Priority

### Phase 1 - Critical UX Impact:
1. FolderTreeItem paint I/O - Never do file I/O in paint!
2. loadFolder() file scan - Main folder loading freeze

### Phase 2 - High Frequency:
3. Pre-thread file collection in folder classification
4. Settings/Preset file I/O

### Phase 3 - Polish:
5. Large collection loop chunking
6. Remaining file I/O operations

---

## Recommended Patterns

### For File Discovery:
```cpp
// BAD - blocks UI
juce::Array<juce::File> files;
folder.findChildFiles(files, ...);  // Freezes UI

// GOOD - async discovery
std::thread([this, folder]() {
    juce::Array<juce::File> files;
    folder.findChildFiles(files, ...);
    juce::MessageManager::callAsync([this, files = std::move(files)]() {
        // Update UI with results
    });
}).detach();
```

### For Cached Values:
```cpp
// BAD - computed in paint
void paintItem(...) {
    int count = countFiles();  // File I/O!
}

// GOOD - cached with async update
int cachedCount_ = -1;  // -1 = not computed

void paintItem(...) {
    if (cachedCount_ < 0) {
        g.drawText("...", ...);  // Show loading
        startAsyncCount();
    } else {
        g.drawText(String(cachedCount_), ...);
    }
}
```

---

*Document created: 2025-12-26*
