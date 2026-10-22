#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "../../../services/smode/SModeSegmentModel.h"
#include "../../../services/smode/PatternGenerator.h"

namespace samplar {

class SequencerSlotComponent : public juce::Component, public juce::TooltipClient
{
public:
    SequencerSlotComponent();
    ~SequencerSlotComponent() override = default;

    void paint(juce::Graphics& g) override;

    void setSlotData(const PatternSlot& slot, SModeSegmentModel* model, juce::Colour stemColour);
    void setTempoInfo(double bpm, double sampleRate) { bpm_ = bpm; sampleRate_ = sampleRate; }
    void setCellWidth(float cw) { cellWidth_ = cw; }
    void setMaxCellSpan(int max) { maxCellSpan_ = max; }
    void setTotalCells(int tc) { totalCells_ = tc; }
    /** Registry of the owning plugin instance (audition), never the global locator. */
    void setServiceRegistry(class ServiceRegistry* r) { registry_ = r; }
    void clearSlot();

    const PatternSlot& getSlotData() const { return slot_; }
    bool isEmpty() const { return slot_.empty; }
    void setRoleLabel(char role) { slot_.role = role; repaint(); }

    /** Sync grid position from the lane (single source of truth). */
    void setGridPosition(int cell, int span) { slot_.gridCell = cell; slot_.cellSpan = span; }

    // Callbacks
    std::function<void(int stemIndex, int segIndex)> onClicked;          // double-click play
    std::function<void(int slotIndex)> onDragStarted;
    std::function<void(int slotIndex, const PatternSlot& updatedSlot)> onSlotModified;
    std::function<void(int slotIndex, int targetCell)> onSlotMoved;       // click-drag reposition
    std::function<void(int slotIndex, int newGridCell, int newCellSpan)> onSlotResized; // edge drag resize
    std::function<void(int slotIndex, float speed, int baseCellSpan)> onSpeedChanged;  // speed change (removes overlaps)
    std::function<void(int slotIndex, const PatternSlot& copy)> onSlotCopied; // shift-click-drag copy

    juce::String getTooltip() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& k) override;
    void modifierKeysChanged(const juce::ModifierKeys&) override;

    int slotIndex { -1 };

    void setPlaying(bool playing) { playing_ = playing; progress_ = 0.0f; repaint(); }
    bool isPlaying() const { return playing_; }
    void setProgress(float p) { progress_ = p; repaint(); }

    /** Enable/disable tooltips. */
    void setTooltipsEnabled(bool e) { tooltipsEnabled_ = e; }

    // ── Copy-drag ghost query (used by the parent lane to paint a ghost
    //    rectangle at the destination cell while the user is shift/alt
    //    dragging from this slot). Returns -1 when not in copy preview. */
    int  getCopyPreviewCell()    const { return (previewActive_ && previewMode_ == DragMode::Copy) ? previewGridCell_ : -1; }
    int  getCopyPreviewCellSpan()const { return slot_.cellSpan; }

private:
    bool playing_ { false };
    float progress_ { 0.0f };
    PatternSlot slot_;
    SModeSegmentModel* model_ { nullptr };
    juce::Colour stemColour_ { juce::Colours::grey };

    juce::Path waveformPath_;
    bool pathValid_ { false };
    double bpm_ { 120.0 };
    double sampleRate_ { 44100.0 };

    // Drag interaction mode — 4-corner zones like wave editor
    // Top-left/right = trim, Bottom-left/right = speed-stretch
    enum class DragMode { None, TrimLeft, TrimRight, SpeedLeft, SpeedRight, Move, Pitch, Copy };
    DragMode dragMode_ { DragMode::None };
    int dragStartX_ { 0 };
    int dragStartY_ { 0 };
    int dragStartGridCell_ { 0 };
    int dragStartCellSpan_ { 1 };
    float dragStartPitch_ { 0.0f };
    float dragStartTrimStart_ { 0.0f };
    float dragStartTrimEnd_ { 1.0f };
    int lastAppliedCellSteps_ { 0 };
    bool dragging_ { false };
    float cellWidth_ { 20.0f };
    int maxCellSpan_ { 4 };
    int totalCells_ { 16 };
    bool tooltipsEnabled_ { true };

    // Tooltip
    bool tooltipVisible_ { false };
    juce::Point<int> tooltipPos_;

    // ── Live-preview state (during drag — WYSIWYG) ──
    // While dragging these reflect the PREVIEW state (not yet committed to slot_).
    // On mouseUp, they're applied via callbacks; on cancel, discarded.
    bool      previewActive_     { false };
    DragMode  previewMode_       { DragMode::None };
    int       previewGridCell_   { 0 };       // Move/Copy target cell
    int       previewCellSpan_   { 1 };       // Trim/Speed new span
    int       previewGridCellL_  { 0 };       // Trim/Speed new gridCell (left-side resize)
    float     previewTrimStart_  { 0.0f };
    float     previewTrimEnd_    { 1.0f };
    float     previewSpeed_      { 1.0f };
    float     previewPitch_      { 0.0f };
    int       slotBaseX_         { 0 };       // X recorded at mouseDown for restoring on cancel
    int       slotBaseY_         { 0 };
    int       slotBaseW_         { 0 };
    int       slotBaseH_         { 0 };

    static constexpr int kEdgeZone = 8;

    void notifyModified()
    {
        pathValid_ = false;
        repaint();
        if (onSlotModified) onSlotModified(slotIndex, slot_);
    }

    void rebuildWaveformPath();

    class ServiceRegistry* registry_ { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SequencerSlotComponent)
};

} // namespace samplar
