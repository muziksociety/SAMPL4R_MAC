#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "../../../services/smode/SModeSegmentModel.h"
#include "MidiLearnControls.h"
#include <atomic>
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
    /** Stem kind — drives the icon and name label in the control panel. */
    enum class StemKind { Vocals, Bass, Drums, Melody, Other };

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
    /** Runs the spectral analysis via `runBackground` (the processor-owned
     *  SModeService pool), repaints when done. Falls back to the synchronous
     *  path if no runner is available (e.g. after shutdown). */
    /** `cancel` (optional, must outlive the job: SModeService::getJobCancelFlag())
     *  stops the analysis early when the plugin unloads (review A4). */
    void computeSpectralColorsAsync(const std::function<bool(std::function<void()>)>& runBackground,
                                    const std::atomic<bool>* cancel = nullptr);

    // Callbacks
    std::function<void(int stemIndex, int segIndex)> onSegmentSelected;
    std::function<void(int stemIndex, int segIndex)> onSegmentClicked;      // Single-click play
    std::function<void(int stemIndex, int segIndex)> onSegmentAltClicked;   // Alt+click loop
    std::function<void(int stemIndex, int segIndex)> onSegmentDragOut;      // Drag out as WAV (single)
    std::function<void(int stemIndex, int segIndex)> onMultiSegmentDragOut; // Ctrl+Shift+drag: drag every selected segment on any stem as multiple WAVs
    std::function<void()> onSegmentsChanged;
    std::function<void(int segIndex, int64_t start, int64_t end)> onSegmentDeleted; // Fired specifically when a single segment is deleted — lets the page mirror the surgical erase on linked stems instead of the nuclear clear+rebuild path used for bulk changes.
    std::function<void(int stemIndex, int segIndex)> onMidiAssignRequested;
    std::function<void(double startTime, double endTime)> onViewRangeChanged;
    std::function<void(int stemIndex, double beatsPerSegment)> onGridQuickAssign;
    std::function<void(int stemIndex, double gridOffset)> onGridOffsetChanged;
    std::function<void(int stemIndex)> onGridDragStarted;     // Ctrl+drag began (page pushes ONE undo entry)
    std::function<void(int stemIndex)> onFocusRequested;      // V button: expand this stem
    std::function<void(int stemIndex)> onRemoveRequested;     // Right-click: remove channel
    std::function<void(int stemIndex, bool exclude)> onQuickAssignExcludeChanged;
    std::function<void(int stemIndex)> onLinkToggled;         // L button: parent decides new link state
    std::function<void(int stemIndex)> onStemHovered;         // -1 when hover ends; mirrors SequencerLane dot pattern
    std::function<void(int stemIndex, float semi)> onPitchChanged;  // pitch knob -12..+12 semitones
    std::function<void(int stemIndex)> onPitchMidiMenu;             // right-click on the pitch knob (MIDI Learn menu)

    /** Push a value into the pitch knob without firing onPitchChanged (used
     *  by AppState restore + EventBus mirror). */
    void setPitchSemitones(float semi);
    float getPitchSemitones() const;
    /** Learned CC shown in the pitch knob tooltip ("" = none). */
    void setPitchMidiInfo(const juce::String& info) { pitchKnob_.setMidiInfo(info); }
    juce::String getPitchMidiInfo() const { return pitchKnob_.getMidiInfo(); }
    juce::Component& getPitchKnob() { return pitchKnob_; }

    /** Sync the L button visual with the current model.isLinked() state.
        Call this after the parent updates link state for any stem. */
    void refreshLinkButton();
    /** Sync the S / M button colours with the model (after undo / reopen, B24). */
    void refreshStemButtons();

    bool excludedFromQuickAssign_ { false };

    /** Loading state — shown during stem separation. Animates a sweep bar across the lane. */
    void setLoadingState(bool loading)
    {
        if (loadingState_ == loading) return;
        loadingState_ = loading;
        if (loading)
        {
            loadingPhase_ = 0.0f;
            // Use the existing timer; if not already running, start at 30 Hz
            if (!isTimerRunning())
                startTimerHz(30);
        }
        repaint();
    }
    bool isLoading() const { return loadingState_; }

    /** Update real separation progress (0.0 = 0%, 1.0 = 100%).
     *  Only has visual effect while loadingState_ is true. */
    void setLoadingProgress(float p01)
    {
        loadingProgress_ = juce::jlimit(0.0f, 1.0f, p01);
        if (loadingState_)
            repaint();
    }

    /** Set transport playhead position (negative = hidden). Repaints only the
     *  old and new playhead columns (B32), never the whole lane. */
    void setPlayheadTime(double timeSeconds);
    /** Waveform rect in local coords (SModePage maps its playhead strip onto it). */
    juce::Rectangle<int> getWaveArea() const { return waveArea_; }
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

    /** Debug perf counters (UI self-test only reads them): total time spent in
     *  paint() and number of paint() calls across all lanes. */
    static inline double debugPaintMs_ { 0.0 };
    static inline int    debugPaintCount_ { 0 };
    static inline int    debugWaveCacheBuilds_ { 0 };

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
    MidiLearnSlider  pitchKnob_;  // per-stem pitch shift (-12..+12 semitones, time-preserving)
    juce::DrawableButton focusBtn_ { "Focus", juce::DrawableButton::ImageFitted };
    std::unique_ptr<juce::Drawable> eyeIcon_, eyeIconOver_;

    // Control panel redesign members
    juce::TextButton chevronBtn_ { juce::String::charToString(0x25BE) };  // ▾
    juce::DrawableButton stemIconBtn_ { "stemIcon", juce::DrawableButton::ImageFitted };
    std::unique_ptr<juce::Drawable> stemIconDrawable_;
    juce::Label stemNameLabel_;
    StemKind stemKind_ { StemKind::Other };

    // Mouse interaction state
    enum class DragMode { None, Creating, ResizingLeft, ResizingRight, DraggingGrid };
    DragMode dragMode_ { DragMode::None };
    int dragStartX_ { 0 };
    int activeSegIdx_ { -1 };
    int clickedSegIdx_ { -1 };   // Segment clicked — tracks drag-out candidate
    bool dragOutStarted_ { false };
    bool multiDragIntent_ { false };   // Armed by Ctrl+Shift+click on a segment
    bool resizeUndoPushed_ { false };  // B24: "Resize segment" pushed on the first real move only
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

    /** Set the diagonal background gradient colours for this lane. */
    void setBackgroundGradient(juce::Colour a, juce::Colour b) { bgA_ = a; bgB_ = b; repaint(); }

    void setStemKind(StemKind kind);

private:
    // Hover highlight state — true when mouse is over this lane or any of its buttons
    bool isHighlighted_ { false };

    // Per-stem themed background gradient
    juce::Colour bgA_ { juce::Colour::fromRGB(12, 18, 28) };
    juce::Colour bgB_ { juce::Colour::fromRGB(16, 24, 36) };

    // Loading state (shown during stem separation)
    bool  loadingState_ { false };
    float loadingPhase_ { 0.0f };   // 0..1, shimmer phase for leading-edge glow at ~30 Hz
    float loadingProgress_ { 0.0f }; // 0..1, real separation progress (0=start, 1=done)

    // Transport playhead
    double playheadTime_ { -1.0 };  // negative = hidden

    // Drawing helpers
    void drawWaveform(juce::Graphics& g, juce::Rectangle<int> area);
    /** B32: draws the waveform from a cached image; the image is rebuilt only
     *  when size, scale, view range, audio or spectral colours change. */
    void drawWaveformCached(juce::Graphics& g, juce::Rectangle<int> area);
    struct WaveCacheKey
    {
        int w { 0 }, h { 0 };
        float scale { 0.0f };
        double viewStart { 0.0 }, viewEnd { 0.0 };
        uint64_t generation { 0 };
        int numSamples { 0 };
        const float* data { nullptr };
        bool spectral { false };
        size_t spectralCount { 0 };
        bool operator==(const WaveCacheKey& o) const
        {
            return w == o.w && h == o.h && scale == o.scale && viewStart == o.viewStart
                && viewEnd == o.viewEnd && generation == o.generation && numSamples == o.numSamples
                && data == o.data && spectral == o.spectral && spectralCount == o.spectralCount;
        }
    };
    juce::Image  waveCache_;
    WaveCacheKey waveCacheKey_;
    bool         waveCacheValid_ { false };
    int playheadColumn(double timeSeconds) const;   // -1 = not drawn

    // Responsive header (lane height decides the layout; width is constant).
    enum class HeaderMode { Tall, Compact, Mini };
    HeaderMode headerMode_ { HeaderMode::Tall };
    bool headerModeApplied_ { false };
    void applyHeaderMode(HeaderMode mode);   // acts only on change
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
    void handleSegmentMenuResult(int result, int segIdxAtOpen, int64_t wantStart, int64_t wantEnd);
    void showEmptyAreaContextMenu(juce::Point<int> pos);
    void showMidiNotePickerMenu(int segIdx);

    static constexpr int kControlWidth = 160;   // was 178; one width at every size
    static constexpr int kTallLaneH    = 140;   // >= : name row, S/M/L row, big knob
    static constexpr int kCompactLaneH = 62;    // >= : 2 rows + 40 px knob; below: 1 row
    static constexpr int kPanelInset   = 6;     // card margin (paint + resized)
    static constexpr int kEdgeHitZone = 6;
    static constexpr int kGridHitZone = 5;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StemWaveformComponent)
};

} // namespace samplar
