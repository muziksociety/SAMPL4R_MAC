# ProcessingPage GIF Loading Animation - Frontend Design

## Current State

```
+------------------------------------------------------------------+
|                     PROCESSING PAGE (Current)                     |
+------------------------------------------------------------------+
|  ~~~~~~~~~~~~~ Animated Backdrop Tiles ~~~~~~~~~~~~~~~~           |
|  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~          |
|                                                                    |
|              "Chewing on rhythm..."                               |
|              (spooky shaking text)                                |
|                                                                    |
|  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~          |
|  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~          |
|                                                                    |
+------------------------------------------------------------------+
|   +----------------------------------------------------------+   |
|   |           samplarAnimation (procedural)              |   |
|   |     Bouncing monster with waveform trail                  |   |
|   |              OR                                           |   |
|   |    ProcessingVisualizationGallery (waveform viz)          |   |
|   +----------------------------------------------------------+   |
+------------------------------------------------------------------+
|  [Back]                                                           |
+------------------------------------------------------------------+
```

## Proposed Design

```
+------------------------------------------------------------------+
|                     PROCESSING PAGE (New)                         |
+------------------------------------------------------------------+
|                                                                    |
|                         BLACK SCREEN                               |
|                                                                    |
|                                                                    |
|                                                                    |
|                    +------------------+                            |
|                    |                  |                            |
|                    |   [GIF ANIM]     |   <- loading_transparent.gif
|                    |   400 x 172      |      centered on screen    |
|                    |   24fps loop     |                            |
|                    |                  |                            |
|                    +------------------+                            |
|                                                                    |
|                                                                    |
|                         XX.X%                                      |
|                    (progress text)                                 |
|                                                                    |
+------------------------------------------------------------------+
|  [Back]                                                           |
+------------------------------------------------------------------+
```

## Visual Specifications

### Background
- **Color**: Pure black (`0x000000`)
- **No backdrop tiles**: Remove the animated backdrop
- **No spooky messages**: Remove the shaking text

### GIF Animation
- **File**: `loading_transparent.gif`
- **Size**: 400x172 pixels (native size, no scaling)
- **Position**: Centered horizontally and vertically (slightly above center to leave room for progress)
- **Framerate**: 24fps (native)
- **Loop**: Infinite loop while processing
- **Transparency**: GIF has transparent background, will show black behind

### Progress Indicator
- **Position**: Below the GIF, centered
- **Font**: 24px bold white text
- **Format**: "XX.X%" (e.g., "45.2%")
- **Color**: White (`0xFFFFFFFF`)

### Back Button
- **Position**: Bottom-left corner (unchanged from current)
- **Style**: Same as current

## Technical Approach

### Option A: Sprite Sheet (Recommended)
1. Convert GIF to PNG sprite sheet (121 frames in a strip)
2. Embed as BinaryData resource
3. Create `GifAnimationComponent` that:
   - Loads sprite sheet from BinaryData
   - Uses Timer at 24fps to cycle through frames
   - Draws current frame at center

### Option B: Frame-by-Frame PNGs
1. Extract all 121 frames as individual PNGs
2. Embed all as BinaryData (would be large)
3. Cycle through frames with Timer

### Option C: Runtime GIF Decoding
1. Embed GIF as BinaryData
2. Use juce::GIFImageFormat to decode frames at startup
3. Store frames in memory, cycle through with Timer

**Recommendation**: Option A (sprite sheet) - smallest file size, easy to implement

## Files to Modify

1. **ProcessingPage.h** - Remove samplarAnimation, add new GifAnimationComponent
2. **ProcessingPage.cpp** - Simplify paint() to black, update resized() layout
3. **CMakeLists.txt** - Add sprite sheet to BinaryData resources
4. **NEW: GifAnimationComponent.h/.cpp** - Simple animated sprite component

## Component Hierarchy (After)

```
ProcessingPage
  |
  +-- GifAnimationComponent (centered, 400x172)
  |     |
  |     +-- Timer (24fps frame cycling)
  |     +-- juce::Image (sprite sheet)
  |
  +-- juce::Label (progress %, centered below gif)
  |
  +-- juce::TextButton (Back, bottom-left)
```

## Removed Components
- samplarAnimation (procedural monster drawing)
- ProcessingVisualizationGallery (waveform viz)
- Spooky messages system
- Animated backdrop tiling
