# Implementation Plan v2: Bar-by-Bar Context-Aware Pattern Generation

**Session ID**: bar-pattern-gen-redesign
**Date**: 2025-12-14
**Status**: APPROVED - EXECUTING

---

## OVERVIEW

Transform the pattern generation system to:
1. **Genre as GUIDE** - User selects style to filter pattern suggestions
2. **Per-Channel [G] Button** - Generate full pattern for one instrument
3. **Per-Bar gen Buttons** - Vary/Fill/Break single bars
4. **Hierarchical Pattern DB** - Genre → Instrument → Rhythmic Type

---

## UI LAYOUT (Final Design)

```
┌─────────────────────────────────────────────────────────────────────┐
│                    Genre Guide: [Trap ▼]                            │
├─────────────────────────────────────────────────────────────────────┤
│ [S](waveform)[G] │●○○○│●○○○│●○○○│●○○○│  KICK                       │
│ [S](waveform)[G] │○○○○│●○○○│○○○○│●○○○│  SNARE                      │
│ [S](waveform)[G] │●●●●│●●●●│●●●●│●●●●│  HIHAT                      │
│ [S](waveform)[G] │○○●○│○○●○│○○●○│○○●○│  PERC                       │
├──────────────────┼────┼────┼────┼────┤                             │
│                  │ ·  │ ·  │ ·  │ ·  │  ← Bar gen (hover-reveal)   │
└─────────────────────────────────────────────────────────────────────┘

Legend:
[S] = Solo button (existing)
(waveform) = Sample waveform preview (existing)
[G] = Generate FULL channel pattern (NEW)
 ·  = Bar generation button, hidden until hover (NEW)
```

---

## PATTERN DATABASE STRUCTURE

### Current (Genre-only)
```
patterns_enhanced.json
├── "trap": { kick: [...], snare: [...], ... }
├── "house": { kick: [...], snare: [...], ... }
└── ...
```

### New (Genre → Instrument → Rhythmic Type)
```
patterns_rhythmic.json
├── "trap"
│   ├── "kick"
│   │   ├── "foundation": [{steps, grid, confidence}, ...]
│   │   ├── "syncopated": [...]
│   │   ├── "sparse": [...]
│   │   └── "dense": [...]
│   ├── "snare"
│   │   ├── "backbeat": [...]
│   │   ├── "syncopated": [...]
│   │   ├── "sparse": [...]
│   │   └── "rolling": [...]
│   ├── "hihat"
│   │   ├── "straight_8th": [...]
│   │   ├── "straight_16th": [...]
│   │   ├── "syncopated": [...]
│   │   └── "sparse": [...]
│   └── "perc"
│       ├── "foundation": [...]
│       ├── "syncopated": [...]
│       └── "accent": [...]
├── "house"
│   └── ... (same structure)
├── "hip_hop"
│   └── ...
└── ... (16 genres total)
```

---

## RHYTHMIC CLASSIFICATION CRITERIA

| Category | Instrument | Criteria |
|----------|------------|----------|
| **Foundation** | Kick | `steps[0]==1 && density < 0.25` |
| **Syncopated** | All | `off_beat_ratio > 0.5` |
| **Sparse** | All | `hit_count <= 2` |
| **Dense** | All | `density > 0.4` |
| **Backbeat** | Snare | `hits on beats 2 & 4` |
| **Rolling** | Snare/Perc | `consecutive_hits >= 3` |
| **Straight_8th** | HiHat | `hits on every 2nd step` |
| **Straight_16th** | HiHat | `hits on every step` |
| **Accent** | Perc | `sparse + off-beat emphasis` |

---

## PHASE 1: Re-Classify Pattern Database

### Task 1.1: Create Pattern Classifier Script
**File**: `tools/classify_patterns_v2.py`

```python
# Input: patterns_enhanced.json (92k patterns, genre-organized)
# Output: patterns_rhythmic.json (genre → instrument → rhythmic_type)

def classify_pattern(steps, grid, instrument):
    density = sum(steps) / len(steps)
    hit_count = sum(steps)

    # Calculate off-beat ratio
    if grid == 16:
        on_beats = [0, 4, 8, 12]
    elif grid == 12:
        on_beats = [0, 3, 6, 9]
    # ... etc for 24, 32, 48

    off_beat_hits = sum(steps[i] for i in range(len(steps)) if i not in on_beats)
    off_beat_ratio = off_beat_hits / max(hit_count, 1)

    # Classify based on instrument + metrics
    if instrument == "kick":
        if steps[0] == 1 and density < 0.25:
            return "foundation"
        elif off_beat_ratio > 0.5:
            return "syncopated"
        elif hit_count <= 2:
            return "sparse"
        else:
            return "dense"
    # ... similar for snare, hihat, perc
```

### Task 1.2: Run Classification
```bash
python tools/classify_patterns_v2.py \
    --input data/patterns_enhanced.json \
    --output data/patterns_rhythmic.json
```

### Task 1.3: Generate C++ Header
**File**: `src/utils/RhythmicPatterns.h`

```cpp
#pragma once
#include <vector>
#include <map>
#include <string>

namespace samplar {

enum class RhythmicType {
    Foundation,
    Syncopated,
    Sparse,
    Dense,
    Backbeat,
    Rolling,
    Straight8th,
    Straight16th,
    Accent
};

enum class DrumCategory {
    Kick,
    Snare,
    HiHat,
    Perc
};

struct RhythmicPattern {
    std::vector<uint8_t> steps;
    int gridSize;
    float confidence;
};

// Access patterns by genre → instrument → rhythmic type
const std::vector<RhythmicPattern>& getPatterns(
    const std::string& genre,
    DrumCategory category,
    RhythmicType type
);

// Get available genres
const std::vector<std::string>& getAvailableGenres();

} // namespace samplar
```

---

## PHASE 2: Backend Services

### Task 2.1: PatternAnalyzer
**File**: `src/utils/PatternAnalyzer.h/.cpp`

```cpp
class PatternAnalyzer {
public:
    struct Analysis {
        float density;
        int hitCount;
        bool hasDownbeat;
        float offBeatRatio;
        int consecutiveMax;
        RhythmicType suggestedType;
    };

    // Analyze full pattern
    static Analysis analyze(
        const std::vector<uint8_t>& steps,
        int gridSize,
        DrumCategory category
    );

    // Analyze single bar
    static Analysis analyzeBar(
        const std::vector<uint8_t>& steps,
        int barIndex,
        int gridSize,
        DrumCategory category
    );
};
```

### Task 2.2: ChannelPatternGenerator (NEW)
**File**: `src/utils/ChannelPatternGenerator.h/.cpp`

```cpp
class ChannelPatternGenerator {
public:
    // Generate FULL pattern for a channel (16/12/24/32/48 steps)
    // Called when user clicks [G] button
    static std::vector<uint8_t> generateFullPattern(
        const std::string& genre,      // From genre guide dropdown
        DrumCategory category,          // Which instrument
        int gridSize,                   // 12, 16, 24, 32, 48
        RhythmicType preferredType = RhythmicType::Foundation
    );

    // Generate with context (if pattern already exists)
    static std::vector<uint8_t> generateFullPatternWithContext(
        const std::string& genre,
        DrumCategory category,
        int gridSize,
        const std::vector<uint8_t>& existingPattern  // For variation
    );
};
```

### Task 2.3: BarPatternGenerator
**File**: `src/utils/BarPatternGenerator.h/.cpp`

```cpp
class BarPatternGenerator {
public:
    enum class Mode { Variation, Fill, Break };

    // Generate single bar (4 steps for 16-grid, 3 for 12-grid, etc.)
    static std::vector<uint8_t> generateBar(
        const std::string& genre,
        DrumCategory category,
        const std::vector<uint8_t>& existingPattern,
        int barIndex,
        int gridSize,
        Mode mode
    );

private:
    static int getStepsPerBar(int gridSize);
};
```

---

## PHASE 3: Frontend Components

### Task 3.1: Genre Guide Dropdown
**Location**: `StepSequencerComponent` header area

```cpp
// In StepSequencerComponent.h
juce::ComboBox genreGuideDropdown_;
std::string selectedGenre_ = "trap";  // Default

// Populate with available genres from RhythmicPatterns
void setupGenreDropdown() {
    for (const auto& genre : samplar::getAvailableGenres()) {
        genreGuideDropdown_.addItem(genre, id++);
    }
}
```

### Task 3.2: ChannelGenerateButton [G]
**File**: `src/ui/Components/ChannelGenerateButton.h/.cpp`

```cpp
class ChannelGenerateButton : public juce::Component {
public:
    ChannelGenerateButton(
        DrumCategory category,
        std::function<void(DrumCategory)> onGenerate
    );

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    DrumCategory category_;
    std::function<void(DrumCategory)> onGenerate_;
};
```

### Task 3.3: BarGeneratorButton (hover-reveal)
**File**: `src/ui/Components/BarGeneratorButton.h/.cpp`

```cpp
class BarGeneratorButton : public juce::Component {
public:
    BarGeneratorButton(
        int barIndex,
        std::function<void(int, BarPatternGenerator::Mode)> onGenerate
    );

    void paint(juce::Graphics& g) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    int barIndex_;
    bool isHovered_ = false;
    std::function<void(int, BarPatternGenerator::Mode)> onGenerate_;

    void showContextMenu();  // Variation / Fill / Break
};
```

### Task 3.4: Integrate into StepSequencerComponent

```cpp
// StepSequencerComponent.h additions
juce::ComboBox genreGuideDropdown_;
std::array<std::unique_ptr<ChannelGenerateButton>, 4> channelGenButtons_;
std::array<std::unique_ptr<BarGeneratorButton>, 4> barGenButtons_;  // 4 bars

// Callbacks
void onChannelGenerate(DrumCategory category);
void onBarGenerate(int barIndex, BarPatternGenerator::Mode mode);
```

---

## PHASE 4: Integration

### Task 4.1: Wire Channel Generation
```cpp
void StepSequencerComponent::onChannelGenerate(DrumCategory category) {
    auto pattern = ChannelPatternGenerator::generateFullPattern(
        selectedGenre_,
        category,
        currentGridSize_
    );

    // Update the track
    auto& track = tracks_[static_cast<int>(category)];
    track.setSteps(pattern);

    // Trigger repaint
    repaint();
}
```

### Task 4.2: Wire Bar Generation
```cpp
void StepSequencerComponent::onBarGenerate(int barIndex, BarPatternGenerator::Mode mode) {
    // Generate for ALL channels at this bar position
    for (int i = 0; i < 4; i++) {
        auto& track = tracks_[i];
        auto category = static_cast<DrumCategory>(i);

        auto newBarSteps = BarPatternGenerator::generateBar(
            selectedGenre_,
            category,
            track.getSteps(),
            barIndex,
            currentGridSize_,
            mode
        );

        // Replace just that bar in the track
        track.setBarSteps(barIndex, newBarSteps);
    }

    repaint();
}
```

---

## FILE LIST

### New Files (11)
| File | Purpose |
|------|---------|
| `tools/classify_patterns_v2.py` | Re-classify patterns |
| `data/patterns_rhythmic.json` | New pattern database |
| `src/utils/RhythmicPatterns.h` | Pattern access API |
| `src/utils/RhythmicPatterns.cpp` | Pattern data + accessors |
| `src/utils/PatternAnalyzer.h` | Pattern analysis |
| `src/utils/PatternAnalyzer.cpp` | Analysis implementation |
| `src/utils/ChannelPatternGenerator.h` | Full channel generation |
| `src/utils/ChannelPatternGenerator.cpp` | Channel gen implementation |
| `src/utils/BarPatternGenerator.h` | Bar-level generation |
| `src/utils/BarPatternGenerator.cpp` | Bar gen implementation |
| `src/ui/Components/ChannelGenerateButton.h` | [G] button |
| `src/ui/Components/ChannelGenerateButton.cpp` | [G] implementation |
| `src/ui/Components/BarGeneratorButton.h` | Bar gen button |
| `src/ui/Components/BarGeneratorButton.cpp` | Bar gen implementation |

### Modified Files (3)
| File | Changes |
|------|---------|
| `src/ui/Components/StepSequencerComponent.h` | Add genre dropdown, gen buttons |
| `src/ui/Components/StepSequencerComponent.cpp` | Wire generation callbacks |
| `CMakeLists.txt` | Add new source files |

---

## EXECUTION ORDER

```
PHASE 1: Pattern Database (Tasks 1.1-1.3)
├── 1.1 Write classify_patterns_v2.py
├── 1.2 Run on patterns_enhanced.json → patterns_rhythmic.json
└── 1.3 Generate RhythmicPatterns.h/.cpp

PHASE 2: Backend (Tasks 2.1-2.3)
├── 2.1 Implement PatternAnalyzer
├── 2.2 Implement ChannelPatternGenerator
└── 2.3 Implement BarPatternGenerator

PHASE 3: Frontend (Tasks 3.1-3.4)
├── 3.1 Add Genre Guide dropdown
├── 3.2 Create ChannelGenerateButton [G]
├── 3.3 Create BarGeneratorButton (hover)
└── 3.4 Integrate into StepSequencerComponent

PHASE 4: Wire & Test
├── 4.1 Wire channel generation
├── 4.2 Wire bar generation
└── 4.3 Build and verify
```

---

## SUCCESS CRITERIA

- [ ] patterns_rhythmic.json created with Genre → Instrument → Type hierarchy
- [ ] Genre Guide dropdown works and filters generation
- [ ] [G] button generates full channel pattern
- [ ] Bar gen buttons appear on hover
- [ ] Bar gen context menu shows Variation/Fill/Break
- [ ] Generation uses selected genre + rhythmic type
- [ ] All grid sizes work (12/16/24/32/48)
- [ ] Build passes, no regressions

---

**STATUS: APPROVED - NOW EXECUTING**
