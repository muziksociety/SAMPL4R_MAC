# Implementation Plan: Bar-by-Bar Context-Aware Pattern Generation

**Session ID**: bar-pattern-gen-redesign
**Date**: 2025-12-14
**Status**: READY FOR USER APPROVAL

---

## OVERVIEW

Transform the pattern generation system from genre-based to:
1. **Instrument-based rhythmic classification** (Foundation, Syncopated, Sparse, Dense)
2. **Bar-by-bar context-aware generation** with hover-reveal UI
3. **Variation/Fill modes** that analyze existing pattern data

---

## PHASE 1: Re-Classify Pattern Database

### Task 1.1: Create Pattern Classifier Script
**File**: `tools/classify_patterns.py`

```python
# Analyze existing patterns_enhanced.json (92k patterns)
# Output: patterns_rhythmic.json organized by:
#   └── KICK/SNARE/HIHAT/PERC
#       └── Foundation/Syncopated/Sparse/Dense/Backbeat/etc.

Classification metrics:
- density = sum(steps) / len(steps)
- has_downbeat = steps[0] == 1
- syncopation_ratio = off_beat_hits / total_hits
- backbeat_score = hits_on_2_and_4 (for snare)
```

### Task 1.2: Define Rhythmic Categories

| Category | Criteria | Use Case |
|----------|----------|----------|
| **Foundation** | Downbeat + density < 25% | Solid backbone |
| **Syncopated** | syncopation_ratio > 0.5 | Groove/swing |
| **Sparse** | 1-2 hits per bar | Minimal/breakdown |
| **Dense** | density > 40% | Busy/fills |
| **Backbeat** | Hits on 2 & 4 (snare only) | Traditional feel |
| **Rolling** | Consecutive hits (3+) | Fills/rolls |

### Task 1.3: Generate New C++ Header
**File**: `src/utils/RhythmicPatterns.h`

```cpp
namespace samplar {
namespace RhythmicPatterns {
    // KICK patterns by type
    namespace Kick {
        extern const std::vector<std::vector<uint8_t>> Foundation;
        extern const std::vector<std::vector<uint8_t>> Syncopated;
        extern const std::vector<std::vector<uint8_t>> Sparse;
        extern const std::vector<std::vector<uint8_t>> Dense;
    }
    // SNARE, HIHAT, PERC similarly...
}}
```

---

## PHASE 2: Backend - Context-Aware Generation Service

### Task 2.1: Create PatternAnalyzer
**File**: `src/utils/PatternAnalyzer.h/.cpp`

```cpp
class PatternAnalyzer {
public:
    struct BarAnalysis {
        float density;           // 0.0 - 1.0
        bool hasDownbeat;
        float syncopationRatio;
        int hitCount;
        RhythmicType classification;
    };

    // Analyze a single bar (4 steps for 16-step, 3 steps for 12-step triplet)
    static BarAnalysis analyzeBar(const std::vector<uint8_t>& steps, int barIndex, int stepsPerBar);

    // Analyze full pattern
    static std::vector<BarAnalysis> analyzePattern(const std::vector<uint8_t>& steps, int gridSize);
};
```

### Task 2.2: Create BarPatternGenerator
**File**: `src/utils/BarPatternGenerator.h/.cpp`

```cpp
class BarPatternGenerator {
public:
    enum class GenerationMode {
        Variation,  // Similar to existing bar
        Fill,       // Busier than existing
        Break       // Sparser than existing
    };

    // Generate pattern for a single bar based on context
    static std::vector<uint8_t> generateBar(
        DrumCategory category,           // KICK, SNARE, HIHAT, PERC
        const std::vector<uint8_t>& existingPattern,  // Current full pattern
        int barIndex,                    // Which bar (0-3)
        GenerationMode mode,             // Variation/Fill/Break
        int gridSize                     // 12, 16, 24, 32, 48
    );

private:
    // Find patterns with similar characteristics
    static std::vector<uint8_t> findSimilarPattern(
        DrumCategory category,
        const PatternAnalyzer::BarAnalysis& targetAnalysis
    );

    // Find patterns that are busier
    static std::vector<uint8_t> findBusierPattern(
        DrumCategory category,
        const PatternAnalyzer::BarAnalysis& currentAnalysis
    );

    // Find patterns that are sparser
    static std::vector<uint8_t> findSparserPattern(
        DrumCategory category,
        const PatternAnalyzer::BarAnalysis& currentAnalysis
    );
};
```

---

## PHASE 3: Frontend - Hover-Reveal Bar Generation UI

### Task 3.1: Add BarGeneratorButton Component
**File**: `src/ui/Components/BarGeneratorButton.h/.cpp`

```cpp
class BarGeneratorButton : public juce::Component {
public:
    BarGeneratorButton(int barIndex, std::function<void(int, GenerationMode)> onGenerate);

    void paint(juce::Graphics& g) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    int barIndex_;
    bool isHovered_ = false;
    bool isShowingMenu_ = false;
    std::function<void(int, GenerationMode)> onGenerate_;

    void showContextMenu();
};
```

### Task 3.2: Visual Design (ASCII Mockup)

```
BEFORE HOVER:
┌────────────────────────────────────────────────────────┐
│  KICK   │ ●○○○ │ ●○○○ │ ●○○○ │ ●○○○ │                 │
│  SNARE  │ ○○○○ │ ●○○○ │ ○○○○ │ ●○○○ │                 │
│  HIHAT  │ ●●●● │ ●●●● │ ●●●● │ ●●●● │                 │
│  PERC   │ ○○●○ │ ○○●○ │ ○○●○ │ ○○●○ │                 │
├─────────┼──────┼──────┼──────┼──────┤                 │
│         │  ·   │  ·   │  ·   │  ·   │ ← Subtle dots   │
└─────────┴──────┴──────┴──────┴──────┘

AFTER HOVER (over bar 3):
┌────────────────────────────────────────────────────────┐
│  KICK   │ ●○○○ │ ●○○○ │ ●○○○ │ ●○○○ │                 │
│  SNARE  │ ○○○○ │ ●○○○ │ ○○○○ │ ●○○○ │                 │
│  HIHAT  │ ●●●● │ ●●●● │ ●●●● │ ●●●● │                 │
│  PERC   │ ○○●○ │ ○○●○ │ ○○●○ │ ○○●○ │                 │
├─────────┼──────┼──────┼──────┼──────┤                 │
│         │  ·   │  ·   │ [GEN]│  ·   │ ← Lit up        │
└─────────┴──────┴──────┴──────┴──────┘

AFTER CLICK (context menu):
                        ┌─────────────┐
                        │  Variation  │
                        ├─────────────┤
                        │    Fill     │
                        ├─────────────┤
                        │    Break    │
                        └─────────────┘
```

### Task 3.3: Integrate into StepSequencerComponent
**File**: `src/ui/Components/StepSequencerComponent.cpp`

- Add row of BarGeneratorButton below the grid
- Wire callbacks to BarPatternGenerator
- Update specific bar when generation completes
- Animate the change (brief highlight)

---

## PHASE 4: Wire It All Together

### Task 4.1: Update PatternLibrary
**File**: `src/utils/PatternLibrary.h/.cpp`

- Add methods to access rhythmic-classified patterns
- Keep backward compatibility with existing code
- Add `getPatternsByRhythmicType(category, type)` method

### Task 4.2: Integration Points

```
User hovers bar 3 → BarGeneratorButton lights up
User clicks → Context menu appears
User selects "Fill" →
    1. PatternAnalyzer.analyzeBar(currentPattern, bar=3)
    2. BarPatternGenerator.generateBar(category, pattern, bar=3, FILL)
    3. Generator finds busier pattern from RhythmicPatterns
    4. Returns 4-step pattern for that bar
    5. StepSequencerComponent updates bar 3 with new steps
    6. Brief animation shows the change
```

---

## FILE CHANGES SUMMARY

### New Files
| File | Purpose |
|------|---------|
| `tools/classify_patterns.py` | Re-classify 92k patterns |
| `src/utils/RhythmicPatterns.h` | New pattern organization |
| `src/utils/RhythmicPatterns.cpp` | Pattern data |
| `src/utils/PatternAnalyzer.h` | Analyze bar characteristics |
| `src/utils/PatternAnalyzer.cpp` | Analysis implementation |
| `src/utils/BarPatternGenerator.h` | Context-aware generation |
| `src/utils/BarPatternGenerator.cpp` | Generation implementation |
| `src/ui/Components/BarGeneratorButton.h` | Hover-reveal button |
| `src/ui/Components/BarGeneratorButton.cpp` | Button implementation |

### Modified Files
| File | Changes |
|------|---------|
| `src/ui/Components/StepSequencerComponent.h` | Add BarGeneratorButton row |
| `src/ui/Components/StepSequencerComponent.cpp` | Wire generation callbacks |
| `src/utils/PatternLibrary.h` | Add rhythmic access methods |
| `src/utils/PatternLibrary.cpp` | Implement rhythmic methods |
| `CMakeLists.txt` | Add new source files |

---

## IMPLEMENTATION ORDER

```
Week 1: Pattern Re-Classification
├── 1. Write classify_patterns.py
├── 2. Run on patterns_enhanced.json
├── 3. Generate RhythmicPatterns.h/.cpp
└── 4. Build and verify compiles

Week 2: Backend Services
├── 5. Implement PatternAnalyzer
├── 6. Implement BarPatternGenerator
└── 7. Unit test generation logic

Week 3: Frontend Integration
├── 8. Create BarGeneratorButton
├── 9. Integrate into StepSequencerComponent
├── 10. Add context menu
└── 11. Wire callbacks

Week 4: Polish & Test
├── 12. Add animation on generation
├── 13. Test all grid sizes (12/16/24/32/48)
├── 14. Test all categories
└── 15. Final verification
```

---

## SUCCESS CRITERIA

- [ ] 92k patterns re-classified by rhythmic type
- [ ] Hover-reveal UI working (hidden → visible on hover)
- [ ] Context menu shows Variation/Fill/Break options
- [ ] Generation respects existing pattern context
- [ ] Works with all grid sizes (triplet and straight)
- [ ] Build passes, no regressions
- [ ] User can still drag MIDI/WAV (existing functionality preserved)

---

## QUESTIONS RESOLVED

| Question | Answer |
|----------|--------|
| DAW complement vs supplant? | COMPLEMENT (sample audition tool) |
| Genre patterns? | Re-classify to rhythmic types |
| Export UI? | Already good, no changes needed |

---

**READY FOR USER APPROVAL TO PROCEED WITH IMPLEMENTATION**
