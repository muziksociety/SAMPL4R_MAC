---
name: ascii-mockup
description: >
  Create precise ASCII art UI mockups for JUCE Component prototyping.
  Use when designing UI layouts, creating BEFORE/AFTER comparisons,
  documenting component hierarchies, or planning user interaction flows.
  Specifically optimized for SAMPLAR JUCE development.
---

# ASCII Mockup Skill

Create terminal-based UI prototypes for JUCE Components with embedded design thinking.

## Quick Reference: Box-Drawing Characters

### Basic ASCII (Recommended - Wide Compatibility)

```
Borders:    + - - - - +     Corners: +
            |         |     Lines:   - |
            + - - - - +
```

### Standard Characters

| Purpose | Character | Example |
|---------|-----------|---------|
| Horizontal line | `-` | `+----+` |
| Vertical line | `\|` | `\| content \|` |
| Corner/junction | `+` | `+--+` |
| Header separator | `=` | `======` |
| Nested border | `:` | `:----:` |

### Special Symbols

| Symbol | Meaning |
|--------|---------|
| `[X]` | Button with label X |
| `( )` | Radio button (unselected) |
| `(*)` | Radio button (selected) |
| `[ ]` | Checkbox (unchecked) |
| `[x]` | Checkbox (checked) |
| `[===---]` | Slider at ~50% |
| `[v]` or `[V]` | Dropdown/ComboBox |
| `~~~` | Waveform representation |
| `...` | Truncated/more content |
| `<->` | Resizable/draggable |
| `[?]` | Help button |
| `[X]` | Close button |
| `*` | Emphasis/new element |

---

## JUCE Component Templates

### Page Layout (Full Window)

```
+------------------------------------------------------------------+
|  [Logo]  Application Title                              [?] [X]  |
+------------------------------------------------------------------+
|                                                                   |
|  +------------------+  +--------------------------------------+   |
|  |                  |  |                                      |   |
|  |  Side Panel      |  |  Main Content Area                   |   |
|  |  (Navigation)    |  |                                      |   |
|  |                  |  |                                      |   |
|  |  [Step 1]        |  |                                      |   |
|  |  [Step 2] *      |  |                                      |   |
|  |  [Step 3]        |  |                                      |   |
|  |                  |  |                                      |   |
|  +------------------+  +--------------------------------------+   |
|                                                                   |
|  [Status Bar]                                    [Progress: 45%]  |
+------------------------------------------------------------------+
```

### Panel with Controls

```
+-------------------------------+
|  Panel Title                  |
+-------------------------------+
|                               |
|  Label:     [TextField    ]   |
|                               |
|  Option:    [Dropdown   v]    |
|                               |
|  Amount:    [=====------]     |
|             0          100    |
|                               |
|  [Cancel]         [Apply]     |
+-------------------------------+
```

### Waveform Display with Overlays

```
+--------------------------------------------------+
|  Sample: kick_808.wav                    [Zoom+] |
+--------------------------------------------------+
|                                                   |
|  ~~~~~~~~____~~~~____~~~~~~~~____~~~~____~~~~~   |
|  |       |       |       |       |       |       |
|  1       2       3       4       5       6       |
|  ^                                               |
|  Downbeat marker (red)                           |
+--------------------------------------------------+
|  BPM: [120]  Beats: [16]  Grid: [16 v]          |
+--------------------------------------------------+
```

### Dialog Box

```
        +-----------------------------------+
        |  Dialog Title                 [X] |
        +-----------------------------------+
        |                                   |
        |  Message or content goes here.   |
        |  Can span multiple lines.        |
        |                                   |
        |  +---------------------------+   |
        |  |  Input field              |   |
        |  +---------------------------+   |
        |                                   |
        |     [Cancel]    [OK]             |
        +-----------------------------------+
```

### Step Sequencer Grid

```
+-----------------------------------------------+
|  Genre: [Trap v]                              |
+-----------------------------------------------+
| [S] ~~~ [G] | * . . . | * . . . | * . . . |  KICK
| [S] ~~~ [G] | . . . . | * . . . | . . . . |  SNARE
| [S] ~~~ [G] | * * * * | * * * * | * * * * |  HIHAT
| [S] ~~~ [G] | . . * . | . . * . | . . * . |  PERC
+-----------------------------------------------+
|              1         2         3           |
+-----------------------------------------------+

Legend: [S]=Solo  ~~~=Waveform  [G]=Generate  *=Hit  .=Empty
```

### Tab Component

```
+--[ Tab 1 ]--+--[ Tab 2 ]--+--[ Tab 3 ]--+------+
|                                                 |
|  Content of selected tab                        |
|                                                 |
+-------------------------------------------------+
```

---

## Design Principles for ASCII Mockups

### 1. Visual Hierarchy

Draw the eye to what matters most:

```
STRONG:  +=======+  Double lines = primary focus
         |       |
         +=======+

NORMAL:  +-------+  Single lines = secondary
         |       |
         +-------+

SUBTLE:  :-------:  Dotted/colon = tertiary
         :       :
         :-------:
```

### 2. Spatial Composition

**Balance whitespace intentionally:**

```
GOOD (Breathing room):          BAD (Cramped):
+---------------------------+   +---------------------------+
|                           |   |Label:[Value][Btn][Btn2]   |
|  Label:  [Value      ]    |   |Another:[X][=====][Cancel] |
|                           |   +---------------------------+
|  [Cancel]       [Apply]   |
|                           |
+---------------------------+
```

### 3. Proportions

**Use realistic sizing** (think in grid units):

```
Typical JUCE sizing (in character units):
- Button:       [  OK  ] = ~8 chars
- Text field:   [            ] = ~14 chars
- Slider:       [========----] = ~12 chars
- Label:        "Label:" = ~6 chars
- Padding:      2 spaces between elements
```

### 4. Information Flow

**Guide the eye naturally:**

```
+-------------------------------------------+
|  1. First thing user sees (title)         |
+-------------------------------------------+
|  2. Primary content area (largest)        |
|     - Top-to-bottom reading               |
|     - Left-to-right scanning              |
+-------------------------------------------+
|  3. Actions (bottom-right = primary)      |
|                         [Secondary] [Primary]
+-------------------------------------------+
```

### 5. Grouping

**Related items stay together:**

```
+---------------------------+
|  File Settings            |  <- Group header
|  +---------------------+  |
|  | Path: [/path/to/  ] |  |  <- Related inputs
|  | Name: [filename   ] |  |
|  +---------------------+  |
|                           |
|  Export Options           |  <- New group
|  +---------------------+  |
|  | Format: [WAV v]     |  |
|  | Quality: [High v]   |  |
|  +---------------------+  |
+---------------------------+
```

---

## BEFORE/AFTER Pattern

**Always show both states with clear change markers:**

```
================================================================
                    PAGE NAME - CURRENT STATE
================================================================
+--------------------------------------------------+
|  Content as it exists today                       |
|                                                   |
|  [Existing Button]                                |
+--------------------------------------------------+

================================================================
                    PAGE NAME - PROPOSED STATE
================================================================
+--------------------------------------------------+
|  Content with changes                             |
|                                                   |
|  [Existing Button]  [New Button] *                |
|                                                   |
|  +----------------------------------+  *          |
|  |  New Component Added             |             |
|  +----------------------------------+             |
+--------------------------------------------------+

LEGEND:
  * = New/modified element
  ~ = Removed element (show in CURRENT only)
```

---

## Component Hierarchy Diagrams

**Document JUCE parent-child relationships:**

```
MainWindow
  |
  +-- HeaderComponent
  |     +-- LogoComponent
  |     +-- TitleLabel
  |     +-- CloseButton
  |
  +-- ContentPanel
  |     |
  |     +-- WaveViewerComponent
  |     |     +-- WaveformDisplay
  |     |     +-- BeatGridOverlay (NEW)
  |     |     +-- SelectionOverlay
  |     |
  |     +-- ControlsPanel
  |           +-- BpmLabel
  |           +-- BpmSlider (MODIFIED)
  |           +-- GridSelector
  |
  +-- FooterComponent
        +-- StatusLabel
        +-- ProgressBar
```

**Markers:**
- `(NEW)` = Component being added
- `(MODIFIED)` = Component being changed
- `(REMOVED)` = Show in hierarchy but mark for deletion

---

## User Flow Diagrams

**Document step-by-step interactions:**

```
USER FLOW: Loading and Processing a Sample
===========================================

1. User clicks [Load Sample]
   |
   v
2. File browser dialog opens
   |
   +---> User selects .wav file
   |
   v
3. Sample loads into WaveViewer
   |
   +---> Waveform displays
   +---> BPM auto-detected
   |
   v
4. User clicks [Process]
   |
   +---> [IF grid not set] --> Show grid selector dialog
   |                                |
   |                                v
   |                           User selects grid
   |                                |
   +<-------------------------------+
   |
   v
5. Processing begins
   |
   +---> Progress bar updates
   |
   v
6. Processing complete
   |
   +---> Results displayed
   +---> [Export] button enabled
```

**Flow markers:**
- `|` = Direct progression
- `+-->` = Action or branch
- `v` = Continue down
- `[IF condition]` = Decision point

---

## Common Mistakes to Avoid

### Mistake 1: Uneven Borders

```
BAD:                        GOOD:
+------------+              +-------------+
| Content    |              | Content     |
+----------+                +-------------+
  ^ missing corner            All corners aligned
```

### Mistake 2: Inconsistent Widths

```
BAD:                        GOOD:
+---------+                 +-------------+
| Short   |                 | Short       |
+-------------------+       +-------------+
| Wider content     |       | Wider cont. |
+-------------------+       +-------------+
```

### Mistake 3: Missing Whitespace

```
BAD:                        GOOD:
+----------+                +-------------+
|[Btn1][Btn2]|              |             |
+----------+                | [Btn1] [Btn2]|
                            |             |
                            +-------------+
```

### Mistake 4: No Change Markers

```
BAD (no indication of what changed):
+-------------+
| New content |
+-------------+

GOOD (clear markers):
+-------------+
| New content | *
+-------------+

LEGEND: * = Added in this change
```

---

## Integration with Frontend-Architect

When creating mockups for frontend-design.json:

1. **Always include BEFORE and AFTER**
2. **Mark all changes with `*`**
3. **Include component hierarchy**
4. **Document user flow if interaction changes**
5. **Specify JUCE component types**

Example output structure:
```
ascii_mockups: [
  {
    "name": "WavePreviewPage BEFORE",
    "diagram": "..."
  },
  {
    "name": "WavePreviewPage AFTER",
    "diagram": "..."
  }
]
```

---

## When to Use This Skill

- Planning new UI features
- Documenting existing UI for modification
- Creating frontend-design.json artifacts
- Communicating UI changes to users for approval
- Designing JUCE Component layouts
- Creating BEFORE/AFTER comparisons
