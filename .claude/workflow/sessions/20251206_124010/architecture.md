# Pattern Generation System - Architecture Document

**Session ID:** 20251206_124010
**Status:** Approved
**Date:** 2025-12-06

---

## Overview

Automatic drum pattern generation system for the step sequencer with:
- 30+ genre templates
- Per-channel lock/unlock for iterative workflow
- 0-100% variation slider
- Core pattern library with algorithmic variations

---

## 1. UI Layout

### Left Panel - Lock Icons (Painted)

```
0px   18px  24px                              120px
 |     |     |                                  |
 |-----|-----|----------------------------------|
 | [o] |     | Kick                             |  <- Unlocked (hollow)
 |-----|-----|----------------------------------|
 | [*] |     | Snare                            |  <- Locked (filled)
 |-----|-----|----------------------------------|
 | [o] |     | HiHat                            |
 |-----|-----|----------------------------------|
 | [o] |     | Perc                             |
 |-----|-----|----------------------------------|
       ^
     Click to toggle
```

**Lock Graphics (No Unicode):**
- Unlocked: Hollow circle, grey
- Locked: Filled circle, cyan (accent color)

### Right Panel - Controls

```
+-------------------------------+
| [MIDI Drag Area]              |
| Tempo: [120___]               |
| Grid:  [16 v]                 |
| Swing: [----o----]            |
| [Export] [Import]             |
| ----------------------------- |
| Genre: [Trap v]               |
| Variation:                    |
| [0%--------o--------100%]     |
|                               |
| [Generate]  [Clear]           |
+-------------------------------+
```

---

## 2. Genre Library (30+ Styles)

### Electronic
- Techno (Berlin, Detroit, Minimal)
- House (Chicago, Deep, Tech, Afro)
- Trance (Uplifting, Psytrance)
- Drum & Bass (Liquid, Neurofunk, Jungle)
- Dubstep (Deep, Brostep)
- Breakbeat / Big Beat
- UK Garage / 2-Step
- Electro / Synthwave
- Hardstyle / Gabber

### Hip-Hop/Urban
- Boom Bap (90s NYC)
- Trap (Atlanta)
- Lo-Fi Hip Hop
- Drill (UK, Chicago)
- Phonk / Memphis
- G-Funk (West Coast)

### Latin/Caribbean
- Reggaeton
- Dembow
- Dancehall
- Afrobeats
- Soca
- Cumbia

### Other
- Disco / Nu-Disco
- Funk / Soul
- Rock (Standard)
- Jazz Swing

---

## 3. System Architecture

```
+--------------------------------------------------------------+
|                    PATTERN GENERATION SYSTEM                  |
+--------------------------------------------------------------+
|                                                              |
|  +-------------------+                                       |
|  | CORE PATTERN      |  <- Hardcoded base patterns per genre |
|  | LIBRARY           |                                       |
|  |                   |                                       |
|  | - Genre.Kick[]    |                                       |
|  | - Genre.Snare[]   |                                       |
|  | - Genre.HiHat[]   |                                       |
|  | - Genre.Perc[]    |                                       |
|  +---------+---------+                                       |
|            |                                                 |
|            v                                                 |
|  +-------------------+      +-------------------+            |
|  | GENRE ALGORITHM   | ---> | VARIATION         |            |
|  |                   |      | ENGINE            |            |
|  | Selects core      |      |                   |            |
|  | patterns          |      | 0-100% slider     |            |
|  +-------------------+      +---------+---------+            |
|                                       |                      |
|                                       v                      |
|  +-----------------------------------------------------+    |
|  |                   OUTPUT TO GRID                     |    |
|  |                                                      |    |
|  |   Row 0 (Kick)  -> [Only if NOT locked]             |    |
|  |   Row 1 (Snare) -> [Only if NOT locked]             |    |
|  |   Row 2 (HiHat) -> [Only if NOT locked]             |    |
|  |   Row 3 (Perc)  -> [Only if NOT locked]             |    |
|  +-----------------------------------------------------+    |
+--------------------------------------------------------------+
```

---

## 4. Data Structures

### PatternLibrary.h

```cpp
namespace samplar {

enum class PatternRole { Kick, Snare, HiHat, Perc };

struct CorePattern {
    std::array<uint8_t, 16> steps;  // 16-step pattern
};

struct GenrePatterns {
    juce::String name;
    CorePattern kick;
    CorePattern snare;
    CorePattern hihat;
    CorePattern perc;
};

class PatternLibrary {
public:
    static const std::vector<GenrePatterns>& getAllGenres();
    static const GenrePatterns* getGenre(const juce::String& name);
    static juce::StringArray getGenreNames();

    // Apply variation (0.0 = exact, 1.0 = max variation)
    static CorePattern applyVariation(const CorePattern& base,
                                       PatternRole role,
                                       float variationAmount);
};

} // namespace samplar
```

### StepSequencerComponent additions

```cpp
// New member variables
std::vector<bool> rowLocked_;           // Lock state per row
std::vector<PatternRole> rowPatternRole_; // What pattern type each row generates
juce::ComboBox genreCombo_;
juce::Slider variationSlider_;
juce::TextButton generateBtn_{ "Generate" };
juce::TextButton clearBtn_{ "Clear" };

// New methods
void generatePattern();
void clearUnlockedRows();
void toggleRowLock(int row);
void setRowPatternRole(int row, PatternRole role);
```

---

## 5. Variation Algorithm

```cpp
CorePattern PatternLibrary::applyVariation(const CorePattern& base,
                                            PatternRole role,
                                            float variation) {
    CorePattern result = base;
    juce::Random rng;

    // Probability of modification per step based on variation
    float modProb = variation * 0.3f;  // Max 30% chance per step

    for (int i = 0; i < 16; ++i) {
        if (rng.nextFloat() < modProb) {
            if (base.steps[i] == 0) {
                // Maybe add a hit (ghost note / fill)
                float addProb = getAddProbability(role, i, variation);
                if (rng.nextFloat() < addProb)
                    result.steps[i] = 1;
            } else {
                // Maybe remove a hit (only at high variation)
                if (variation > 0.7f && rng.nextFloat() < 0.2f)
                    result.steps[i] = 0;
            }
        }
    }

    return result;
}
```

---

## 6. Right-Click Context Menu (Enhanced)

On row label (not lock icon):

```
+-------------------------+
| Generate as:            |
| ----------------------- |
| * Kick    <- current    |
| o Snare                 |
| o HiHat                 |
| o Perc                  |
| ----------------------- |
| Remove Sound            |
| Move Up / Move Down     |
+-------------------------+
```

---

## 7. Files to Create

| File | Purpose |
|------|---------|
| src/utils/PatternLibrary.h | Core pattern definitions |
| src/utils/PatternLibrary.cpp | Pattern data + variation algorithm |

## 8. Files to Modify

| File | Changes |
|------|---------|
| StepSequencerComponent.h | Add lock state, pattern role, UI controls |
| StepSequencerComponent.cpp | Lock painting, click detection, generate logic |
| CMakeLists.txt | Add PatternLibrary sources |

---

## 9. Implementation Order

1. Create PatternLibrary with 5-10 genres initially
2. Add lock state tracking + painting in StepSequencer
3. Add click detection for lock toggle
4. Add right panel controls (genre combo, variation slider, buttons)
5. Wire up generatePattern() to apply patterns
6. Add "Generate as" to right-click menu
7. Expand PatternLibrary to 30+ genres
8. Test and refine variation algorithm

---

## 10. User Workflow

1. Select genre from dropdown
2. Set variation slider (0% for exact, higher for variety)
3. Click "Generate" - all 4 rows fill with pattern
4. Like the kick? Click lock icon on Kick row
5. Click "Generate" again - only unlocked rows change
6. Repeat until satisfied
7. Drag MIDI to DAW or continue editing manually
