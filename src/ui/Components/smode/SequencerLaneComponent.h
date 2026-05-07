#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "SequencerSlotComponent.h"
#include "../../../services/smode/SModeSegmentModel.h"
#include "../../../services/smode/PatternGenerator.h"
#include "../WaveDragArea.h"
#include <vector>
#include <memory>
#include <functional>

namespace samplar {

class SequencerLaneComponent : public juce::Component
{
public:
    SequencerLaneComponent(SModeSegmentModel& model, int laneIndex);
    ~SequencerLaneComponent() override = default;

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    std::function<void(int stemIndex)> onStemHovered;  // -1 = no hover

    void setStemInfo(int stemCount, std::function<juce::Colour(int)> colorFunc);
    void setStemDotActive(int stemIndex, bool active);
    void shuffle();

    const std::vector<PatternSlot>& getSlots() const { return slots_; }
    void setSlot(int slotIndex, int stemIndex, int segIndex);
    void fillWithSegments(const std::vector<std::pair<int,int>>& pairs);

    int getLaneIndex() const { return laneIndex_; }

    /** Highlight a specific slot with progress (set -1 to clear). */
    void setPlayingSlot(int slotIndex, float progress = 0.0f);
    int getSlotComponentCount() const { return static_cast<int>(slotComponents_.size()); }

    /** Set playhead position across the full grid (0.0 to 1.0). -1 = hidden. */
    void setPlayheadPosition(float pos) { playheadPos_ = pos; repaint(); }
    int getNumBars() const { return numBars_; }

    /** Snapshot / restore — used by SModeSegmentModel's undo stack. */
    juce::var captureState() const;
    void restoreState(const juce::var& v);

    void setBpm(double bpm)
    {
        if (std::abs(bpm - bpm_) < 0.001) return;
        bpm_ = bpm;
        // Propagate new tempo to existing slot components and refresh layout
        for (auto& sc : slotComponents_)
        {
            if (sc != nullptr)
            {
                double sr = model_.getSampleRate();
                if (sr <= 0.0) sr = 44100.0;
                sc->setTempoInfo(bpm_, sr);
            }
        }
        repaint();
        // Notify external playback engine so it can re-time using the new BPM
        if (onLaneChanged) onLaneChanged(laneIndex_);
    }
    double getBpm() const { return bpm_; }

    /** Clear all slots. */
    void clearAll();

    bool isMuted() const { return muted_; }
    WaveDragArea& getDragWavArea() { return dragWavArea_; }

    // Callbacks
    std::function<void(int laneIndex)> onLaneChanged;  // Fired when slots are modified (for live playback update)
    std::function<void(int laneIndex)> onRemoveRequested;
    std::function<void(int laneIndex)> onPlayRequested;
    std::function<void(int laneIndex)> onStopRequested;
    std::function<void(int laneIndex)> onDragWavRequested;
    std::function<void(int stemIndex, int segIndex)> onSlotClicked;
    std::function<juce::Colour(int)> stemColorFunc_;

private:
    SModeSegmentModel& model_;
    int laneIndex_;

    struct StemDot { bool active { true }; juce::Colour colour; };
    std::vector<StemDot> stemDots_;

    PatternGenerator::PatternType patternType_ { PatternGenerator::PatternType::AABA };
    int numBars_ { 4 };
    int slotsPerBar_ { 1 };
    double bpm_ { 120.0 };

    std::vector<PatternSlot> slots_;
    std::vector<std::unique_ptr<SequencerSlotComponent>> slotComponents_;

    // Controls
    juce::TextButton actionBtn_ { "SHUFFLE" };  // Combo: right-click to switch mode
    enum class ActionMode { Shuffle, Mutate, Slice };
    ActionMode actionMode_ { ActionMode::Shuffle };
    juce::TextButton clearBtn_ { "CLEAR" };
    juce::TextButton playBtn_ { juce::String::charToString(0x25B6) };
    juce::TextButton stopBtn_ { juce::String::charToString(0x25A0) };
    juce::TextButton muteBtn_ { "M" };
    WaveDragArea dragWavArea_;
    juce::TextButton removeBtn_ { "X" };
    bool muted_ { false };
    juce::ComboBox patternCombo_;
    juce::ComboBox barsCombo_;
    juce::ComboBox subdivCombo_;

    static constexpr int kDotSize = 14;
    static constexpr int kDotGap = 3;
    static constexpr int kControlRowHeight = 22;
    static constexpr int kSlotHeight = 36;

    void buildSlotComponents();
    void collectAvailableSegments(std::vector<PatternGenerator::SegmentRef>& out) const;
    void paintDot(juce::Graphics& g, int dotIndex, juce::Rectangle<int> area);

    // Cached slot area for grid line drawing
    juce::Rectangle<int> slotArea_;

    // Playhead position (0.0 to 1.0 across full grid, -1 = hidden)
    float playheadPos_ { -1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SequencerLaneComponent)
};

} // namespace samplar
