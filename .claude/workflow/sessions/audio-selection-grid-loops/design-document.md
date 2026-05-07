# Design Document: Audio Selection Page Beat Grid & Loop Boundaries

## Session ID: audio-selection-grid-loops
## Status: AWAITING USER APPROVAL

---

## Executive Summary

This design adds BPM detection, beat grid visualization, and draggable loop boundary markers to the SampleBrowserPage, allowing users to verify and adjust loop boundaries before processing.

---

## New Files to Create

| File | Purpose |
|------|---------|
| `src/services/BeatAnalysisService.h` | BPM detection and loop analysis service |
| `src/services/BeatAnalysisService.cpp` | Implementation |
| `src/services/BeatAnalysisTypes.h` | Data structures (BPMResult, BeatGridInfo, LoopBoundary) |
| `src/ui/Components/BeatGridOverlay.h` | Transparent beat grid overlay |
| `src/ui/Components/BeatGridOverlay.cpp` | Implementation |
| `src/ui/Components/DraggableMarker.h` | Draggable marker component |
| `src/ui/Components/DraggableMarker.cpp` | Implementation |

## Files to Modify

| File | Changes |
|------|---------|
| `src/ui/Pages/SampleBrowserPage.h` | Add beat grid components, BPM UI controls |
| `src/ui/Pages/SampleBrowserPage.cpp` | Integrate BPM detection, grid overlay, markers |
| `CMakeLists.txt` | Add new source files to build |

---

## Key Data Structures

```cpp
struct BPMResult {
    float bpm = 120.0f;           // Detected BPM
    float confidence = 0.0f;       // 0.0 - 1.0 confidence
    bool isManualOverride = false; // True if user manually set
};

struct BeatGridInfo {
    BPMResult bpm;
    double downbeatTimeSeconds = 0.0;     // First beat position
    int beatsPerBar = 4;                   // Time signature
    double audioLengthSeconds = 0.0;
    std::vector<BeatPosition> beats;       // All beat positions
    std::vector<LoopBoundary> detectedLoops;
};

struct LoopBoundary {
    double startTimeSeconds = 0.0;
    double endTimeSeconds = 0.0;
    int lengthBars = 4;
    float confidence = 0.0f;
};
```

---

## UI Components

### 1. BPM Controls (Top Bar)
- **BPM Label**: Shows detected BPM (e.g., "BPM: 94.0")
- **BPM Slider**: Manual adjustment (30-300 range)
- **Tap Tempo Button**: For fallback when detection fails
- **Detect Loops Button**: Triggers loop boundary detection
- **Show Grid Toggle**: Enable/disable grid visibility

### 2. Beat Grid Overlay
- Transparent overlay positioned over waveform
- **Downbeat lines**: WHITE, 2px thick, brighter
- **Regular beat lines**: WHITE, 1px, dimmer (25% opacity)
- Adaptive density based on zoom level

### 3. Draggable Markers
- **Downbeat Marker**: RED triangle pointing down + vertical line
- **Loop Boundary Markers**: BLUE rectangles + vertical lines
- Grid snap ALWAYS ON when dragging
- Minimum 20x40 pixel hit area for easy grabbing

---

## Algorithm: BPM Detection

**Approach**: Energy-based autocorrelation (~50-200ms processing time)

1. Convert to mono, downsample to ~8kHz
2. Compute onset detection function (energy flux in 10ms windows)
3. Autocorrelate to find periodicity
4. Find peaks in 60-200 BPM range
5. Return highest confidence peak

**Edge Case Handling**:
- Files < 2 seconds: Show "Too short" message, default to 120 BPM
- Low confidence: Show warning, enable manual entry
- Unusual BPM (<60 or >200): Normalize by doubling/halving

---

## Algorithm: Loop Detection

1. Segment audio into 4-bar chunks based on beat grid
2. Compute spectral fingerprint for each chunk
3. Find similar adjacent chunks (correlation > 0.7 threshold)
4. Create loop boundary markers at transition points

---

## Risk Mitigations

### Critical Risks Addressed:

| Risk | Mitigation |
|------|------------|
| **BPM detection blocks UI** | Run on background thread with AsyncUpdater callback |
| **Empty buffer crash** | Guard: `if (buffer.getNumSamples() == 0) return;` |
| **Component destroyed during async** | Use WeakReference pattern |
| **Division by zero** | Guard: `if (sampleRate <= 0) return;` |
| **Boundaries stored as pixels** | Store as sample positions, convert to pixels only for rendering |

### UX Risks Addressed:

| Risk | Mitigation |
|------|------------|
| **Grid too dense at high zoom** | Adaptive density: 16ths at high zoom, bars at low zoom |
| **Markers too small** | 20x40 pixel minimum hit area, cursor change on hover |
| **Downbeat vs loop confusion** | RED triangle for downbeat, BLUE rectangles for loops |

---

## Implementation Order

1. **BeatAnalysisService** - Thread-safe BPM detection (can test in isolation)
2. **BeatAnalysisTypes** - Data structures
3. **BeatGridOverlay** - Visual grid (no interaction)
4. **DraggableMarker** - Interaction layer
5. **SampleBrowserPage integration** - Wire everything together

---

## Data Flow to ProcessingPage

When user clicks "Process":
```cpp
eventBus_.publish("processing.startWithBeatGrid", {
    "bpm": 94.0,
    "downbeat": 0.125,  // seconds
    "loops": [
        { "start": 0.0, "end": 8.0, "bars": 4 },
        { "start": 8.0, "end": 16.0, "bars": 4 }
    ]
});
```

---

## User Requirements Checklist

- [x] Move tempo detection to audio load time
- [x] Display detected BPM with manual adjustment
- [x] Tap-to-tempo fallback
- [x] Overlay beat grid on waveform
- [x] "Detect Loops" button for live detection
- [x] Loop boundaries as draggable BLUE lines
- [x] Downbeat as draggable RED triangle
- [x] Grid snap ALWAYS ON
- [x] No limit on loop boundary count

---

## Approval Required

Please review this design and reply:
- **"yes"** or **"approved"** to proceed with implementation
- Or describe any modifications needed

