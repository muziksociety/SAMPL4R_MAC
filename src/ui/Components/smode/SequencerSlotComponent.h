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

    int slotIndex { -1 };

    void setPlaying(bool playing) { playing_ = playing; progress_ = 0.0f; repaint(); }
    bool isPlaying() const { return playing_; }
    void setProgress(float p) { progress_ = p; repaint(); }

    /** Enable/disable tooltips. */
    void setTooltipsEnabled(bool e) { tooltipsEnabled_ = e; }

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

    static constexpr int kEdgeZone = 8;

    void notifyModified()
    {
        pathValid_ = false;
        repaint();
        if (onSlotModified) onSlotModified(slotIndex, slot_);
    }

    void rebuildWaveformPath();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SequencerSlotComponent)
};

} // namespace samplar
