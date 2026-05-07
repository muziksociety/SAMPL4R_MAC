# Pattern Generation System Redesign - Architecture

## Overview

This document outlines the architecture for enhancing the MIDI pattern generation system with:
1. Learn Pattern Feature
2. Variable Step Count (12/16/24/32/48)
3. Triplet Grid Support
4. User Pattern Database (JSON storage)
5. Per-Category Pattern Library (kick/snare/hihat/perc per genre)

---

## Current State Analysis

### Existing Data Structures
```cpp
// Current: Fixed 16-step array
struct DrumTrack {
    std::array<uint8_t, 16> steps = {0};  // PROBLEM: Fixed size
};

// Current: Genre has 4 fixed patterns
struct GenrePatterns {
    CorePattern kick;
    CorePattern snare;
    CorePattern hihat;
    CorePattern perc;
};
```

### Current Flow
```
User selects Genre -> genreCombo_
User clicks Generate -> generatePattern()
    -> MidiPatternDatabase::generatePattern()
    -> PatternLibrary::generateFlexiblePattern()
    -> Apply variation
    -> Update grid_
```

---

## Proposed Architecture

### 1. New Data Structures

```cpp
// ============================================================
// ENHANCED PATTERN STRUCTURES (Variable length)
// ============================================================

/**
 * Step count options for different grid types
 */
enum class GridType {
    Grid16,     // Standard 16th notes (16 steps per bar)
    Grid12,     // Triplets (12 steps per bar - 8th note triplets)
    Grid24,     // 16ths + triplets (24 steps per bar)
    Grid32,     // 32nd notes (32 steps per bar)
    Grid48      // 32nds + triplets (48 steps per bar)
};

/**
 * Enhanced drum track with variable step count
 */
struct FlexibleDrumTrack {
    int midiNote = 36;
    juce::String name = "Kick";
    DrumCategory category = DrumCategory::Kick;
    std::vector<uint8_t> steps;           // Variable length!
    GridType gridType = GridType::Grid16;

    // Velocity per step (optional, 0 = use default)
    std::vector<uint8_t> velocities;

    // Constructor with step count
    FlexibleDrumTrack(int stepCount = 16) {
        steps.resize(stepCount, 0);
        velocities.resize(stepCount, 100);
    }

    int getStepCount() const { return (int)steps.size(); }
    void setStepCount(int count);  // Resize with intelligent mapping
};

/**
 * Per-category pattern library entry
 * Each genre has multiple pattern variations PER instrument
 */
struct CategoryPatterns {
    DrumCategory category;
    std::vector<std::vector<uint8_t>> patterns;  // Multiple base patterns
    juce::StringArray patternNames;              // e.g., "Basic", "Busy", "Sparse"

    // Get random pattern for this category
    std::vector<uint8_t> getRandomPattern(juce::Random& rng) const;

    // Get pattern by index
    std::vector<uint8_t> getPattern(int index) const;
};

/**
 * Enhanced genre definition with per-category pattern banks
 */
struct EnhancedGenrePatterns {
    juce::String name;
    juce::String category;  // "Electronic", "Hip-Hop", etc.
    double defaultTempo = 120.0;
    GridType preferredGrid = GridType::Grid16;

    // Per-category pattern banks (multiple patterns each)
    std::map<DrumCategory, CategoryPatterns> categoryBanks;

    // Get patterns for a category
    const CategoryPatterns* getBank(DrumCategory cat) const;

    // Generate a complete pattern set
    FlexiblePatternSet generate(float variation, GridType grid) const;
};

/**
 * User-learned pattern entry
 */
struct UserPattern {
    juce::String id;              // UUID
    juce::String name;            // User-given name
    juce::String genre;           // Associated genre (optional)
    DrumCategory category;        // What instrument this is for
    GridType gridType;
    std::vector<uint8_t> steps;
    std::vector<uint8_t> velocities;
    juce::Time createdAt;
    juce::Time lastUsed;
    int useCount = 0;

    // Serialize to JSON
    juce::var toJson() const;
    static UserPattern fromJson(const juce::var& json);
};
```

### 2. UserPatternDatabase Service

```cpp
/**
 * UserPatternDatabase - Stores user-learned patterns
 *
 * File: src/services/UserPatternDatabase.h/cpp
 */
class UserPatternDatabase {
public:
    static UserPatternDatabase& getInstance();

    // Learn a pattern from current sequencer state
    UserPattern learnPattern(
        const juce::String& name,
        DrumCategory category,
        const std::vector<uint8_t>& steps,
        const juce::String& genre = ""
    );

    // Get patterns by category
    std::vector<UserPattern> getPatternsForCategory(DrumCategory cat) const;

    // Get patterns by genre + category
    std::vector<UserPattern> getPatternsForGenreAndCategory(
        const juce::String& genre,
        DrumCategory cat
    ) const;

    // Get all user patterns
    const std::vector<UserPattern>& getAllPatterns() const;

    // Delete a pattern
    bool deletePattern(const juce::String& id);

    // Save/Load
    bool save();
    bool load();

    // Get database file path
    juce::File getDatabaseFile() const;

private:
    std::vector<UserPattern> patterns_;
    bool loaded_ = false;

    // JSON file: data/user_patterns.json
    static constexpr const char* DB_FILENAME = "user_patterns.json";
};
```

### 3. Enhanced PatternLibrary

```cpp
/**
 * EnhancedPatternLibrary - Replaces/extends PatternLibrary
 *
 * Key changes:
 * - Per-category pattern banks (multiple patterns per instrument per genre)
 * - Variable step count support
 * - Triplet grid support
 * - Integration with UserPatternDatabase
 */
class EnhancedPatternLibrary {
public:
    // Get all genres (built-in + user patterns considered)
    static juce::StringArray getGenreNames();

    // Get genre by name
    static const EnhancedGenrePatterns* getGenre(const juce::String& name);

    // Generate pattern for a specific category within a genre
    static std::vector<uint8_t> generateCategoryPattern(
        const juce::String& genre,
        DrumCategory category,
        GridType grid,
        float variation,
        bool includeUserPatterns = true  // Mix in learned patterns
    );

    // Generate complete pattern set
    static FlexiblePatternSet generateFullPattern(
        const juce::String& genre,
        GridType grid,
        float variation,
        bool includeUserPatterns = true
    );

    // Convert between grid types (e.g., 16 -> 24 for triplets)
    static std::vector<uint8_t> convertGrid(
        const std::vector<uint8_t>& source,
        GridType sourceGrid,
        GridType targetGrid
    );

private:
    static std::vector<EnhancedGenrePatterns> createLibrary();
};
```

### 4. UI Changes to StepSequencerComponent

```cpp
// New UI controls to add to right panel:

// Grid type selector
juce::Label gridTypeLabel_{ {}, "Grid" };
juce::ComboBox gridTypeCombo_;  // 16, 12 (triplet), 24, 32, 48

// Learn pattern button
juce::TextButton learnBtn_{ "Learn" };

// Per-category generate buttons (optional advanced mode)
juce::TextButton genKickBtn_{ "K" };
juce::TextButton genSnareBtn_{ "S" };
juce::TextButton genHihatBtn_{ "H" };
juce::TextButton genPercBtn_{ "P" };

// New methods:
void onLearnPattern();           // Save current row as user pattern
void onGridTypeChanged();        // Handle grid change (resize steps)
void generateForCategory(DrumCategory cat);  // Generate single category
```

---

## Per-Category Pattern Bank Structure

### Example: Trap Genre

```cpp
// Trap - per-category banks
EnhancedGenrePatterns trap;
trap.name = "Trap";
trap.category = "Hip-Hop";
trap.defaultTempo = 140.0;
trap.preferredGrid = GridType::Grid16;

// KICK BANK (multiple variations)
trap.categoryBanks[DrumCategory::Kick] = {
    DrumCategory::Kick,
    {
        {1,0,0,0, 0,0,0,1, 0,0,1,0, 0,0,0,0},  // Basic 808
        {1,0,0,0, 0,0,1,0, 0,0,0,0, 0,1,0,0},  // Syncopated
        {1,0,0,0, 0,0,0,0, 1,0,0,0, 0,0,1,0},  // Rolling
        {1,0,0,1, 0,0,0,0, 1,0,0,1, 0,0,0,0},  // Double hits
    },
    { "Basic 808", "Syncopated", "Rolling", "Double" }
};

// SNARE BANK
trap.categoryBanks[DrumCategory::Snare] = {
    DrumCategory::Snare,
    {
        {0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0},  // Standard 2&4
        {0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,1},  // With ghost
        {0,0,0,0, 1,0,0,1, 0,0,0,0, 1,0,1,0},  // Busy
    },
    { "Standard", "Ghost", "Busy" }
};

// HIHAT BANK (most variations for trap)
trap.categoryBanks[DrumCategory::HiHat] = {
    DrumCategory::HiHat,
    {
        {1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1},  // 16ths
        {1,0,1,1, 1,0,1,1, 1,0,1,1, 1,0,1,1},  // Trap roll pattern
        {1,1,0,1, 1,0,1,1, 1,1,0,1, 1,0,1,1},  // Variation
        {1,0,1,0, 1,1,1,0, 1,0,1,0, 1,1,1,1},  // Building
    },
    { "Straight 16ths", "Trap Roll", "Variation", "Building" }
};

// PERC BANK
trap.categoryBanks[DrumCategory::Percussion] = {
    DrumCategory::Percussion,
    {
        {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,1,0},  // Sparse
        {0,0,0,1, 0,0,0,0, 0,0,0,1, 0,0,0,0},  // 808 accent
        {0,1,0,0, 0,0,0,1, 0,1,0,0, 0,0,0,1},  // Cowbell pattern
    },
    { "Sparse", "808 Accent", "Cowbell" }
};
```

---

## User Flow

### Learn Pattern Flow
```
1. User creates/edits a pattern manually in sequencer
2. User right-clicks row OR clicks "Learn" button
3. Dialog: "Save pattern as..."
   - Name: [text input]
   - Category: [auto-detected or dropdown]
   - Genre: [optional dropdown]
4. Pattern saved to UserPatternDatabase
5. Future generates can include this pattern
```

### Generate Pattern Flow (Enhanced)
```
1. User selects Genre from dropdown
2. User selects Grid Type (16, 12-triplet, 24, 32, 48)
3. User adjusts Variation slider
4. User clicks "Generate"
5. System:
   a. For each category (kick, snare, hihat, perc):
      - Get built-in patterns for genre+category
      - Get user patterns for genre+category (if any)
      - Randomly select from combined pool
      - Apply variation algorithm
   b. Convert to selected grid type if needed
   c. Apply to sequencer rows (preserving locked/sample rows)
```

### Per-Category Generate Flow
```
1. User right-clicks a row
2. Context menu: "Generate as > Kick/Snare/HiHat/Perc"
3. System generates ONLY that category pattern
4. Only that row is updated (others unchanged)
```

---

## File Structure

```
src/
├── utils/
│   ├── PatternLibrary.h/cpp         # Keep existing, extend
│   ├── EnhancedPatternLibrary.h/cpp # New: per-category banks
│   ├── MidiPatternDatabase.h/cpp    # Keep existing
│   └── GridConverter.h/cpp          # New: grid type conversion
│
├── services/
│   └── UserPatternDatabase.h/cpp    # New: user learned patterns
│
└── ui/Components/
    └── StepSequencerComponent.h/cpp # Extend with new UI

data/
├── midi_patterns.json               # Existing
├── user_patterns.json               # New: user learned patterns
└── enhanced_patterns.json           # New: per-category pattern banks
```

---

## JSON Schema: user_patterns.json

```json
{
  "version": "1.0",
  "patterns": [
    {
      "id": "uuid-1234",
      "name": "My Trap Kick",
      "genre": "Trap",
      "category": "kick",
      "gridType": 16,
      "steps": [1,0,0,0,0,0,0,1,0,0,1,0,0,0,0,0],
      "velocities": [127,0,0,0,0,0,0,100,0,0,110,0,0,0,0,0],
      "createdAt": "2025-12-13T10:00:00Z",
      "lastUsed": "2025-12-13T12:00:00Z",
      "useCount": 5
    }
  ]
}
```

---

## JSON Schema: enhanced_patterns.json

```json
{
  "version": "1.0",
  "genres": [
    {
      "name": "Trap",
      "category": "Hip-Hop",
      "defaultTempo": 140,
      "preferredGrid": 16,
      "banks": {
        "kick": {
          "patterns": [
            {"name": "Basic 808", "steps": [1,0,0,0,0,0,0,1,0,0,1,0,0,0,0,0]},
            {"name": "Syncopated", "steps": [1,0,0,0,0,0,1,0,0,0,0,0,0,1,0,0]}
          ]
        },
        "snare": {
          "patterns": [
            {"name": "Standard", "steps": [0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0]},
            {"name": "Ghost", "steps": [0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,1]}
          ]
        },
        "hihat": {
          "patterns": [
            {"name": "16ths", "steps": [1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1]},
            {"name": "Trap Roll", "steps": [1,0,1,1,1,0,1,1,1,0,1,1,1,0,1,1]}
          ]
        },
        "perc": {
          "patterns": [
            {"name": "Sparse", "steps": [0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0]}
          ]
        }
      }
    }
  ]
}
```

---

## Implementation Priority

1. **Phase 1: Variable Step Count**
   - Change `std::array<uint8_t, 16>` to `std::vector<uint8_t>`
   - Add GridType enum and selector
   - Update all pattern generation/application code

2. **Phase 2: Per-Category Pattern Banks**
   - Create EnhancedPatternLibrary
   - Convert existing patterns to per-category format
   - Update generatePattern() to use banks

3. **Phase 3: User Pattern Database**
   - Create UserPatternDatabase service
   - Add "Learn" button and flow
   - Integrate with pattern generation

4. **Phase 4: Triplet Grid Support**
   - Implement GridConverter for 12/24 step patterns
   - Add triplet-specific patterns to library
   - Update UI to show triplet grid lines

---

## ASCII Mockup: Enhanced Right Panel

```
+---------------------------+
|  PATTERN GENERATION       |
+---------------------------+
|                           |
|  Genre: [Trap        v]   |
|                           |
|  Grid:  [16   v]          |
|         (16/12/24/32/48)  |
|                           |
|  Variation: [====----]    |
|             30%           |
|                           |
|  [  Generate All  ]       |
|                           |
+---------------------------+
|  Generate Individual:     |
|  [K] [S] [H] [P]          |
|  kick snare hat perc      |
+---------------------------+
|                           |
|  [  Learn Pattern  ]      |
|  (save current row)       |
|                           |
+---------------------------+
|  Tempo: [140] BPM         |
+---------------------------+
|  [Export MIDI] [WAV]      |
+---------------------------+
```

---

## Questions for Approval

1. **Grid types**: Should we support all 5 (16/12/24/32/48) or start with just 16/12/24?

2. **Learn UI**: Should "Learn" be a button or right-click context menu or both?

3. **Per-category buttons**: Show individual K/S/H/P buttons or keep in right-click menu only?

4. **Pattern mixing**: When generating, should user patterns have equal weight to built-in, or configurable?

5. **Backward compatibility**: Keep existing PatternLibrary API or migrate fully to Enhanced version?
