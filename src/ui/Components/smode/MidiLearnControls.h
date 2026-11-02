#pragma once

// MidiLearnControls - slider / button variants that hand a right-click (the
// platform popup-menu click) to `onContextMenu` instead of dragging or
// clicking. SModePage uses this to show the "MIDI Learn / Clear MIDI" menu on
// every control that can be bound to a MIDI CC (stem pitch knob, chop
// inspector). Without onContextMenu they behave exactly like their base class.
//
// setMidiInfo() appends the learned CC ("CC 74 ch 1") to the control's own
// tooltip; an empty string restores the plain tooltip.

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace samplar {

template <class Base>
class MidiLearnable : public Base
{
public:
    using Base::Base;

    std::function<void()> onContextMenu;

    void setBaseTooltip(const juce::String& tip)
    {
        baseTip_ = tip;
        applyTooltip();
    }

    void setMidiInfo(const juce::String& info)
    {
        if (info == midiInfo_) return;
        midiInfo_ = info;
        applyTooltip();
    }

    const juce::String& getMidiInfo() const { return midiInfo_; }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onContextMenu)
        {
            swallow_ = true;          // the matching drag / up belong to the menu click
            onContextMenu();
            return;
        }
        swallow_ = false;
        Base::mouseDown(e);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (! swallow_) Base::mouseDrag(e);
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (swallow_) { swallow_ = false; return; }
        Base::mouseUp(e);
    }

private:
    void applyTooltip()
    {
        Base::setTooltip(midiInfo_.isEmpty() ? baseTip_
                                             : baseTip_ + (baseTip_.isEmpty() ? "" : "\n") + "MIDI: " + midiInfo_
                                                   + " (right-click to change)");
    }

    juce::String baseTip_, midiInfo_;
    bool swallow_ { false };
};

using MidiLearnSlider = MidiLearnable<juce::Slider>;
using MidiLearnButton = MidiLearnable<juce::TextButton>;

} // namespace samplar
