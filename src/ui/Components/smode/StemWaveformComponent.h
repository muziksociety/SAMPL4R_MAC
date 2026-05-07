#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "../../../services/smode/SModeSegmentModel.h"
#include <vector>
#include <functional>
#include <map>

namespace samplar {

/**
 * StemWaveformComponent — renders one stem's waveform with spectral coloring,
 * interactive segments (create/resize/select/delete via mouse), draggable grid,
 * right-click MIDI assignment, and Solo/Mute/Link buttons.
 */
class StemWaveformComponent : public juce::Component,
                              private juce::Timer
{
public:
    StemWaveformComponent(SModeSegmentModel& model, int stemIndex, juce::Colour stemColour);
    ~StemWaveformComponent() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;

    // Mouse interaction for segment creation/editing
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    // Configuration
    void setStemIndex(int index) { stemIndex_ = index; }
    void setSnapEnabled(bool snap) { snapEnabled_ = snap; }
    void setFreeformEnabled(bool freeform) { freeformEnabled_ = freeform; }
    void setGridBPM(double bpm) { gridBpm_ = bpm; }
    void setGridOffsetSeconds(double offset) { gridOffsetSeconds_ = offset; repaint(); }
    double getGridOffsetSeconds() const { return gridOffsetSeconds_; }
    /** Set grid subdivision: 1=whole, 2=half, 4=quarter, 8=eighth, 16=sixteenth.
        Drives both the rendered grid lines and the snap-to-grid math. */
    void setGridDivision(int division) { divisionValue_ = division; repaint(); }
    int  getGridDivision() const { return divisionValue_; }
    void setViewRange(double startTime, double endTime);
    void computeSpectralColors();        // sync (internal use only)
    void computeSpectralColorsAsync();   // launches background thread, repaints when done

    // Callbacks
    std::function<void(int stemIndex, int segIndex)> onSegmentSelected;
    std::function<void(int stemIndex, int segIndex)> onSegmentClicked;      // Single-click play
    std::function<void(int stemIndex, int segIndex)> onSegmentAltClicked;   // Alt+click loop
    std::function<void(int stemIndex, int segIndex)> onSegmentDragOut;      // Drag out as WAV (single)
    std::function<void(int stemIndex, int segIndex)> onMultiSegmentDragOut; // Ctrl+Shift+drag: drag every selected segment on any stem as multiple WAVs
    std::function<void()> onSegmentsChanged;
    std::function<void(int segIndex)> onSegmentDeleted; // Fired specifically when a single segment is deleted — lets the page mirror the surgical erase on linked stems instead of the nuclear clear+rebuild path used for bulk changes.
    std::function<void(int stemIndex, int segIndex)> onMidiAssignRequested;
    std::function<void(double startTime, double endTime)> onViewRangeChanged;
    std::function<void(int stemIndex, double beatsPerSegment)> onGridQuickAssign;
    std::function<void(int stemIndex, double gridOffset)> onGridOffsetChanged;
    std::function<void(int stemIndex)> onFocusRequested;      // V button: expand this stem
    std::function<void(int stemIndex)> onRemoveRequested;     // Right-click: remove channel
    std::function<void(int stemIndex, bool exclude)> onQuickAssignExcludeChanged;
    std::function<void(int stemIndex)> onLinkToggled;         // L button: parent decides new link state

    /** Sync the L button visual with the current model.isLinked() state.
        Call this after the parent updates link state for any stem. */
    void refreshLinkButton();

    bool excludedFromQuickAssign_ { false };

    /** Set transport playhead position (negative = hidden). */
    void setPlayheadTime(double timeSeconds) { playheadTime_ = timeSeconds; repaint(); }
    double getViewStartTime() const { return viewStartTime_; }
    double getViewEndTime() const { return viewEndTime_; }

    /** Start playback animation on a segment (progress sweep + glow, same as browser tiles). */
    void flashSegment(int segIndex);
    /** Start playback animation with known duration. If looping=true, animation wraps instead of ending. */
    void startSegmentAnimation(int segIndex, double durationSec, bool looping = false);
    /** Stop playback animation on a segment (note-off / gate). */
    void stopSegmentAnimation(int segIndex);

    // MIDI note name helper
    static juce::String midiNoteToName(int note);

private:
    // Timer callback for animation
    void timerCallback() override;

    // Per-segment playback animation (matches SampleBrowserPage tile animation)
    struct SegmentAnim {
        double startTime { 0.0 };   // milliseconds (hi-res)
        double durationSec { 0.0 };
        float  progress { 0.0f };   // 0..1
        bool   active { false };
        bool   looping { false };   // If true, animation wraps instead of ending
    };
    std::map<int, SegmentAnim> segmentAnims_;  // segIndex → animation state
    SModeSegmentModel& model_;
    int stemIndex_;
    juce::Colour stemColour_;

    // Spectral colors (one per pixel-width slice)
    std::vector<juce::Colour> spectralColors_;
    double spectralDuration_ { 0.0 };
    bool spectralReady_ { false };

    // View range
    double viewStartTime_ { 0.0 };
    double viewEndTime_ { 0.0 };

    // Layout areas
    juce::Rectangle<int> controlArea_;   // Left: S/M/L buttons
    juce::Rectangle<int> waveArea_;      // Main waveform + segments

    // Solo / Mute / Link / Focus buttons
    juce::TextButton soloBtn_ { "S" };
    juce::TextButton muteBtn_ { "M" };
    juce::TextButton linkBtn_ { "L" };
    juce::DrawableButton focusBtn_ { "Focus", juce::DrawableButton::ImageFitted };
    std::unique_ptr<juce::Drawable> eyeIcon_, eyeIconOver_;

    // Mouse interaction state
    enum class DragMode { None, Creating, ResizingLeft, ResizingRight, DraggingGrid };
    DragMode dragMode_ { DragMode::None };
    int dragStartX_ { 0 };
    int activeSegIdx_ { -1 };
    int clickedSegIdx_ { -1 };   // Segment clicked — tracks drag-out candidate
    bool dragOutStarted_ { false };
    bool multiDragIntent_ { false };   // Armed by Ctrl+Shift+click on a segment
    double gridDragStartOffset_ { 0.0 };  // Grid offset when drag began

    // Snap / grid
    bool snapEnabled_ { true };
    bool freeformEnabled_ { false };
    double gridBpm_ { 0.0 };
    double gridOffsetSeconds_ { 0.0 };  // User-draggable grid offset
    int    divisionValue_ { 4 };        // Grid subdivision: 1,2,4,8,16 (quarter = default)

    // Middle-button panning
    bool isPanning_ { false };
    int panStartX_ { 0 };
    double panStartViewStart_ { 0.0 };
    double panStartViewEnd_ { 0.0 };

    // Hover tooltips
    bool gridTooltipVisible_ { false };
    juce::Point<int> gridTooltipPos_;
    void drawGridTooltip(juce::Graphics& g);

    bool segTooltipVisible_ { false };
    juce::Point<int> segTooltipPos_;
    juce::String segTooltipText_;
    void drawSegTooltip(juce::Graphics& g);

    bool tooltipsEnabled_ { true };
public:
    void setTooltipsEnabled(bool enabled) { tooltipsEnabled_ = enabled; repaint(); }
    bool getTooltipsEnabled() const { return tooltipsEnabled_; }
private:

    // Transport playhead
    double playheadTime_ { -1.0 };  // negative = hidden

    // Drawing helpers
    void drawWaveform(juce::Graphics& g, juce::Rectangle<int> area);
    void drawSegments(juce::Graphics& g, juce::Rectangle<int> area);
    void drawGrid(juce::Graphics& g, juce::Rectangle<int> area);
    void drawPlayhead(juce::Graphics& g, juce::Rectangle<int> area);

    // Coordinate conversion
    int64_t pixelToSample(int x) const;
    int sampleToPixel(int64_t sample) const;
    double pixelToTime(int x) const;
    int timeToPixel(double time) const;
    int64_t snapToGrid(int64_t sample) const;

    // Hit testing
    int findSegmentAtX(int x) const;
    enum class EdgeHit { None, Left, Right };
    EdgeHit findSegmentEdge(int x, int& outSegIdx) const;
    bool isNearGridLine(int x) const;

    // Context menu
    void showSegmentContextMenu(int segIdx, juce::Point<int> pos);
    void showEmptyAreaContextMenu(juce::Point<int> pos);
    void showMidiNotePickerMenu(int segIdx);

    static constexpr int kControlWidth = 32;
    static constexpr int kEdgeHitZone = 6;
    static constexpr int kGridHitZone = 5;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StemWaveformComponent)
};

} // namespace samplar
