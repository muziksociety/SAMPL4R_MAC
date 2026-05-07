# SAMPLAR - Changelog 2026-03-14

## Summary of Changes

This document covers all changes made in the March 14, 2026 session. Six source files were modified across five features plus two optimizations and one UI tweak.

---

## Files Modified

| File | Lines Changed | Feature |
|------|--------------|---------|
| `src/audio/SequencerAudioSource.cpp` | ~60 | Sampleless sequencer playback |
| `src/ui/Components/StepSequencerComponent.cpp` | ~40 | Transport buttons + pattern label |
| `src/ui/Pages/SampleBrowserPage.h` | ~30 | Trim handles (data model) |
| `src/ui/Pages/SampleBrowserPage.cpp` | ~500 | Trim handles + playback optimization |
| `src/ui/Pages/WavePreviewPage.h` | ~4 | Grid tempo buttons (data model) |
| `src/ui/Pages/WavePreviewPage.cpp` | ~40 | Grid tempo buttons (Half/Double) |

---

## Feature 1: Sequencer Plays Without Samples

**Problem:** The step sequencer required at least one sample loaded to play. Without samples, clicking play did nothing and the playhead didn't move.

**Root Cause:** Three gates in `SequencerAudioSource.cpp` blocked operation when no samples were present:
1. `schedule_.empty()` early return prevented audio processing
2. `isValid()` check in `rebuildSchedule()` excluded sampleless rows
3. Playhead position only updated on trigger events (no triggers = no movement)

**Fix (SequencerAudioSource.cpp):**

```cpp
// CHANGE 1: Remove empty schedule gate (line 73)
// BEFORE:
if (schedule_.empty() || patternLengthSamples_ <= 0)
    return;
// AFTER:
if (patternLengthSamples_ <= 0)
    return;

// CHANGE 2: Position-based playhead (replaces trigger-only update)
// Added continuous playhead tracking independent of triggers:
const int totalSteps = totalSteps_.load(std::memory_order_acquire);
const int samplesPerStep = (totalSteps > 0) ? (patternLengthSamples_ / totalSteps) : patternLengthSamples_;
// ... inside sample loop:
if (totalSteps > 0 && samplesPerStep > 0)
{
    int currentStep = static_cast<int>(currentPos / samplesPerStep) % totalSteps;
    playheadStep_.store(currentStep, std::memory_order_release);
    int posInStep = static_cast<int>(currentPos) % samplesPerStep;
    float phase = static_cast<float>(posInStep) / static_cast<float>(samplesPerStep);
    playheadPhase_.store(phase, std::memory_order_release);
}

// CHANGE 3: Schedule triggers without samples (rebuildSchedule)
// Removed isValid() gate - steps now scheduled even without loaded samples
// triggerVoice() handles null buffers gracefully
```

---

## Feature 2: Transport Buttons (Play/Stop) on Step Sequencer

**Problem:** No visible play/stop buttons on the step sequencer UI.

**Fix (StepSequencerComponent.cpp):**

Added `playBtn_` and `stopBtn_` as `juce::TextButton` children positioned above the channel rows, aligned with the row name text area (after lock+solo icons at X=36). Buttons auto-hide when the sequencer is collapsed.

```cpp
// In constructor:
addAndMakeVisible(playBtn_);   // Unicode ▶
addAndMakeVisible(stopBtn_);   // Unicode ■

// In resized():
const int transportBtnH = 22;
const int transportBtnW = 28;
const int transportGap = 4;
const int transportX = b.getX() + 36;  // After lock+solo icons
const int transportY = rowContentTopY() - transportBtnH - 2;
const bool showTransport = b.getHeight() >= 100;
playBtn_.setVisible(showTransport);
stopBtn_.setVisible(showTransport);
playBtn_.setBounds(transportX, transportY, transportBtnW, transportBtnH);
stopBtn_.setBounds(transportX + transportBtnW + transportGap, transportY, transportBtnW, transportBtnH);

// rowContentTopY() updated to reserve 46px header for handle + transport buttons
```

---

## Feature 3: Hover-Reveal Trim Handles on Sample Browser Tiles

**Problem:** No way to trim sample start/end points directly from the sample browser.

**Solution:** Green (start) and red (end) bracket handles appear when hovering over any sample tile waveform. Dragging sets trim points. A scissors button (✂) appears to confirm the trim, which destructively rewrites the WAV file. Original is backed up for undo via right-click context menu.

### Header Changes (SampleBrowserPage.h)

```cpp
// Added to Tile class:
float trimStart_ { 0.0f };
float trimEnd_ { 1.0f };
enum class TrimHandle { None, Start, End };
TrimHandle activeTrimHandle_ { TrimHandle::None };
bool trimDragging_ { false };
bool trimHovered_ { false };
bool hasPendingTrim() const { return trimStart_ > 0.001f || trimEnd_ < 0.999f; }
juce::TextButton trimConfirmBtn_;

// Public accessors:
float getTrimStart() const;
float getTrimEnd() const;
void setTrimPoints(float start, float end);

// Added mouse overrides:
void mouseMove(const juce::MouseEvent& e) override;
void mouseEnter(const juce::MouseEvent& e) override;
void mouseExit(const juce::MouseEvent& e) override;

// Added to SampleBrowserPage class:
void applyTrim(int tileIndex);
void undoTrim(int tileIndex);
bool hasTrimBackup(int tileIndex) const;
std::map<juce::String, juce::File> trimBackups_;  // session-only
void cleanupTrimBackups();
```

### Implementation (SampleBrowserPage.cpp)

#### Tile Constructor - Trim confirm button setup
```cpp
addAndMakeVisible(trimConfirmBtn_);
trimConfirmBtn_.setButtonText("\xe2\x9c\x82");  // ✂ scissors
trimConfirmBtn_.setTooltip("Apply trim to sample");
trimConfirmBtn_.setVisible(false);
trimConfirmBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff00cc44));
trimConfirmBtn_.onClick = [this]{
    if (auto* p = findParentComponentOfClass<SampleBrowserPage>())
        p->applyTrim(index_);
};
```

#### Tile::resized() - Button positioned center of waveform
```cpp
const int trimBtnW = 28;
const int trimBtnH = 20;
trimConfirmBtn_.setBounds(centerX - trimBtnW / 2, getHeight() / 2 - trimBtnH / 2, trimBtnW, trimBtnH);
```

#### Tile::paint() - Trim visualization
```cpp
// On hover: always show green start bracket + red end bracket
// Dim regions outside trim range when trim is moved from defaults
// When NOT hovered: subtle dim on trimmed regions only

// Playback animation maps to trim range:
float trimRange = trimEnd_ - trimStart_;
float trimStartPx = waveformArea.getWidth() * trimStart_;
float trimWidthPx = waveformArea.getWidth() * trimRange;
float highlightWidth = trimWidthPx * juce::jmax(progress, 0.01f);
juce::Rectangle<float> highlight = waveformArea
    .withX(waveformArea.getX() + trimStartPx)
    .withWidth(highlightWidth);
```

#### Tile::mouseDown() - Detect trim handle clicks
```cpp
float startX = wX + wW * trimStart_;
float endX = wX + wW * trimEnd_;
const float hitZone = 8.0f;
if (std::abs(mx - startX) < hitZone) {
    trimDragging_ = true;
    activeTrimHandle_ = TrimHandle::Start;
    return;
}
// Same for End handle
```

#### Tile::mouseDrag() - Update trim positions
```cpp
if (trimDragging_) {
    float normalized = juce::jlimit(0.0f, 1.0f, ((float)e.getPosition().x - wX) / wW);
    if (activeTrimHandle_ == TrimHandle::Start)
        trimStart_ = juce::jmin(normalized, trimEnd_ - 0.02f);  // Min 2% gap
    else if (activeTrimHandle_ == TrimHandle::End)
        trimEnd_ = juce::jmax(normalized, trimStart_ + 0.02f);
    repaint();
    return;
}
// Block DnD when trim is pending:
if (hasPendingTrim()) return;
```

#### Tile::mouseUp() - End trim drag, show confirm button
```cpp
if (trimDragging_) {
    trimDragging_ = false;
    activeTrimHandle_ = TrimHandle::None;
    trimConfirmBtn_.setVisible(hasPendingTrim());
    repaint();
    return;
}
```

#### Tile::mouseMove() - Cursor changes near handles
```cpp
if (std::abs(mx - startX) < hitZone || std::abs(mx - endX) < hitZone)
    setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
else
    setMouseCursor(juce::MouseCursor::NormalCursor);
```

#### Tile::mouseEnter/mouseExit() - Hover state
```cpp
void mouseEnter(...) { trimHovered_ = true; repaint(); }
void mouseExit(...)  { trimHovered_ = false; setMouseCursor(NormalCursor); repaint(); }
```

#### applyTrim() - Destructive trim with backup
```cpp
// 1. Read original WAV via AudioFormatReader
// 2. Calculate start/end sample positions from normalized trim points
// 3. Read trimmed portion into buffer
// 4. Save backup of original to temp directory (SAMPLAR_trim_backup_*.wav)
// 5. Write trimmed audio back to original file path
// 6. Reset tile trim points to 0.0/1.0
// 7. Refresh waveform thumbnail
// 8. Invalidate + re-preload into sample cache
```

#### undoTrim() - Restore from backup
```cpp
// 1. Find backup file in trimBackups_ map
// 2. Copy backup back to original path
// 3. Delete backup, remove from map
// 4. Reset tile, refresh waveform
// 5. Invalidate + re-preload cache
```

#### Context Menu - "Undo Trim" option
```cpp
// Added menu item 800 "Undo Trim" (only visible when backup exists)
if (parentPage && parentPage->hasTrimBackup(index_)) {
    m.addSeparator();
    m.addItem(800, "Undo Trim");
}
// Handler:
else if (res == 800) {
    if (auto* parentPage = findParentComponentOfClass<SampleBrowserPage>())
        parentPage->undoTrim(index_);
}
```

#### Destructor - Cleanup temp backups
```cpp
// In ~SampleBrowserPage():
cleanupTrimBackups();  // Deletes all temp backup files
```

---

## Optimization 1: Faster Spam-Click Playback

**Problem:** Rapidly clicking different samples was unresponsive due to 70ms teardown sleep in `teardownAudioChainAsync`.

**Fix:**

1. **Reduced teardown sleep** from 70ms to 30ms total (35->15, 10->5, 25->10)
2. **Cache hits interrupt transitions** - when a cached sample is clicked during a teardown, it plays immediately instead of waiting

```cpp
// In playFileFromCache(), before cache hit handling:
if (isAudioTransitioning_.load()) {
    pendingPlayFile_ = juce::File();  // Cancel queued file
    // Cache playback proceeds immediately - no wait needed
}
```

3. **Post-trim cache preload** - after applying trim, the new file is immediately preloaded into cache for instant subsequent playback

---

## Optimization 2: Post-Trim Cache Reload

After applying or undoing a trim, the sample cache is invalidated AND the new file is immediately re-preloaded:

```cpp
if (sampleCache_) {
    sampleCache_->invalidateFile(srcFile);
    juce::Array<juce::File> filesToPreload;
    filesToPreload.add(srcFile);
    sampleCache_->requestPreload(filesToPreload, true);
}
```

---

## UI Tweak: Step Sequencer Pattern Button

**Change:** Removed "Genre" label text above the pattern selector button in the right panel. Changed button text from "Select Pattern..." to "Patterns".

```cpp
// BEFORE:
genreLabel_.setBounds(contentPad, cy, cw, h);  // "Genre" label
cy += h + 2;
genreButton_.setButtonText("Select Pattern...");

// AFTER:
genreLabel_.setVisible(false);  // Hidden
genreButton_.setButtonText("Patterns");
```

---

## Feature 4: Grid Tempo Half/Double Buttons (Processing Page Step 2)

**Problem:** Users had to manually calculate and type half or double tempo values to adjust the beat grid density. Common workflow: detected BPM is 140 but user wants a half-time feel (70 BPM grid) or double-time (280 BPM grid).

**Solution:** Added "Grid:" label with [Half] and [Double] buttons on Step 2 (SET TEMPO & BEAT GRID), positioned below the tempo detector labels. Buttons only appear on Step 2.

### Header Changes (WavePreviewPage.h)

```cpp
// Added to WavePreviewPage class:
juce::Label gridLabel_;
juce::TextButton halfTempoBtn_ { "Half" };
juce::TextButton doubleTempoBtn_ { "Double" };
```

### Implementation (WavePreviewPage.cpp)

#### Constructor - Button setup with onClick handlers
```cpp
// Grid tempo scaling buttons (Half / Double)
addAndMakeVisible(gridLabel_);
gridLabel_.setText("Grid:", juce::dontSendNotification);
gridLabel_.setFont(juce::Font(13.0f));
gridLabel_.setColour(juce::Label::textColourId, juce::Colour::fromString("ffAABBCC"));

addAndMakeVisible(halfTempoBtn_);
halfTempoBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour::fromString("ff21262D"));
halfTempoBtn_.setColour(juce::TextButton::textColourOnId, juce::Colour::fromString("ff00FFFF"));
halfTempoBtn_.setTooltip("Halve the tempo (grid becomes wider)");
halfTempoBtn_.onClick = [this] {
    double currentBpm = bpmSlider_.getValue();
    double halfBpm = currentBpm / 2.0;
    if (halfBpm >= 30.0)
    {
        juce::Logger::writeToLog("[Preview] Half tempo: " + juce::String(currentBpm, 3)
            + " -> " + juce::String(halfBpm, 3));
        bpmSlider_.setValue(halfBpm, juce::sendNotification);
    }
};

addAndMakeVisible(doubleTempoBtn_);
doubleTempoBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour::fromString("ff21262D"));
doubleTempoBtn_.setColour(juce::TextButton::textColourOnId, juce::Colour::fromString("ff00FFFF"));
doubleTempoBtn_.setTooltip("Double the tempo (grid becomes tighter)");
doubleTempoBtn_.onClick = [this] {
    double currentBpm = bpmSlider_.getValue();
    double doubleBpm = currentBpm * 2.0;
    if (doubleBpm <= 300.0)
    {
        juce::Logger::writeToLog("[Preview] Double tempo: " + juce::String(currentBpm, 3)
            + " -> " + juce::String(doubleBpm, 3));
        bpmSlider_.setValue(doubleBpm, juce::sendNotification);
    }
};
```

#### resized() - Positioned below detector labels
```cpp
// Grid tempo scaling buttons - below detector labels
int gridRowY = detectorY + 22;
gridLabel_.setBounds(leftX, gridRowY, 35, 26);
halfTempoBtn_.setBounds(leftX + 37, gridRowY, 50, 26);
doubleTempoBtn_.setBounds(leftX + 91, gridRowY, 60, 26);

// ZOOM controls - same row as grid buttons, right side
zoomLabel_.setBounds(boxInner.getRight() - 200, gridRowY, 50, 26);
zoomSlider_.setBounds(boxInner.getRight() - 145, gridRowY, 130, 26);
```

#### setStep2ControlsVisible() - Only visible on Step 2
```cpp
// Grid tempo scaling buttons (Half / Double) - Step 2 only
gridLabel_.setVisible(visible);
halfTempoBtn_.setVisible(visible);
doubleTempoBtn_.setVisible(visible);
```

**Notes:**
- BPM clamped to 30-300 range (Half disabled below 60, Double disabled above 150)
- Buttons reuse existing `bpmSlider_.setValue(sendNotification)` path, which auto-updates the BPM input field, beat grid overlay, and all downstream state
- Styled consistently with existing Step 2 controls (dark bg, cyan accent)

---

## Feature 5: Parameter Labels on Step 2.5 Processing Sliders

**Problem:** The Gate, Compressor, and Limiter slider bars on Step 2.5 (PROCESS) had no visible labels indicating what each slider controls. Users could only see numeric values with units (dB, ms, %) but not what parameter they were adjusting.

**Solution:** Added short text labels to the left of every parameter slider across all three effect columns. Labels use a subtle grey color (`ff889099`) at 10pt font, right-aligned to sit cleanly next to the slider bars.

### Header Changes (WavePreviewPage.h)

```cpp
// Added to Gate controls:
juce::Label gateThreshLabel_, gateAttackLabel_, gateReleaseLabel_, gateHoldLabel_;

// Added to Comp controls:
juce::Label compAmountLabel_;

// Added to Limiter controls:
juce::Label limiterCeilingLabel_, limiterReleaseLabel_;
```

### Implementation (WavePreviewPage.cpp)

#### initStep25Controls() - Label setup with helper lambda
```cpp
auto setupSliderLabel = [this](juce::Label& label, const juce::String& text) {
    addAndMakeVisible(label);
    label.setText(text, juce::dontSendNotification);
    label.setFont(juce::Font(10.0f));
    label.setColour(juce::Label::textColourId, juce::Colour::fromString("ff889099"));
    label.setJustificationType(juce::Justification::centredRight);
    label.setVisible(false);
};
setupSliderLabel(gateThreshLabel_, "Thresh");
setupSliderLabel(gateAttackLabel_, "Attack");
setupSliderLabel(gateReleaseLabel_, "Rel");
setupSliderLabel(gateHoldLabel_, "Hold");
setupSliderLabel(compAmountLabel_, "Amount");
setupSliderLabel(limiterCeilingLabel_, "Ceil");
setupSliderLabel(limiterReleaseLabel_, "Rel");
```

#### resized() - Labels positioned left of each slider (35px wide)
```cpp
int labelW = 35;
int labeledSliderW = sliderW - labelW - 2;

// Example (Gate column - same pattern for Comp and Limiter):
gateThreshLabel_.setBounds(col1X, y, labelW, sliderHeight);
gateThresholdSlider_.setBounds(col1X + labelW + 2, y, labeledSliderW, sliderHeight);
```

#### setStep25ControlsVisible() - Labels shown/hidden with their sliders
```cpp
gateThreshLabel_.setVisible(visible);
gateAttackLabel_.setVisible(visible);
gateReleaseLabel_.setVisible(visible);
gateHoldLabel_.setVisible(visible);
compAmountLabel_.setVisible(visible);
limiterCeilingLabel_.setVisible(visible);
limiterReleaseLabel_.setVisible(visible);
```

**Labels by column:**
| Gate | Comp | Limiter |
|------|------|---------|
| Thresh | Amount | Ceil |
| Attack | | Rel |
| Rel | | |
| Hold | | |

---

## How to Apply

The complete unified diff is available at `docs/changelog_diffs.patch`. Apply with:

```bash
cd Sample_monsta5/
git apply docs/changelog_diffs.patch
```

Or apply changes manually file-by-file using the code blocks above.

---

## Testing Checklist

- [ ] Step sequencer plays with no samples loaded (playhead moves)
- [ ] Play/Stop buttons visible above sequencer channel rows
- [ ] Buttons auto-hide when sequencer is collapsed
- [ ] Hover over sample tile shows green/red trim brackets
- [ ] Drag trim handles, scissors button appears
- [ ] Click scissors - file is trimmed, waveform refreshes
- [ ] Right-click trimmed sample shows "Undo Trim"
- [ ] Undo Trim restores original file
- [ ] Drag-and-drop blocked while trim is pending
- [ ] Playback animation only sweeps within trim range
- [ ] Spam-clicking different samples responds instantly
- [ ] Trimmed samples play without delay (cached)
- [ ] "Patterns" button shows in sequencer (no "Genre" label)
- [ ] Closing app cleans up temp backup files
- [ ] Grid: [Half] [Double] buttons visible on Step 2 only
- [ ] Half button halves BPM and updates grid
- [ ] Double button doubles BPM and updates grid
- [ ] Buttons disappear when navigating to other steps
- [ ] Step 2.5: Gate sliders show Thresh/Attack/Rel/Hold labels
- [ ] Step 2.5: Comp slider shows Amount label
- [ ] Step 2.5: Limiter sliders show Ceil/Rel labels
- [ ] Labels only visible on Step 2.5, not other steps
