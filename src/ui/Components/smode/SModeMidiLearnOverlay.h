#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../../../app/EventBus.h"
#include <functional>

namespace samplar {

/**
 * SModeMidiLearnOverlay — semi-transparent overlay that prompts the user
 * to press a MIDI key on their controller. Listens for the
 * "smode.midi.learn.assigned" event from MidiMappingService.
 * Click anywhere to cancel.
 */
class SModeMidiLearnOverlay : public juce::Component
{
public:
    /** `registry` = the owning plugin instance's registry (from the page's
     *  ServiceLocator::Context). Never re-read from the global locator. */
    SModeMidiLearnOverlay(EventBus& bus, class ServiceRegistry* registry = nullptr);
    ~SModeMidiLearnOverlay() override;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;

    /** Show the overlay for a specific stem/segment. */
    void show(int stemIndex, int segIndex, const juce::String& stemName);

    /** Hide and cancel learn mode. */
    void dismiss();

    /** Called when a MIDI note is assigned: (stemIndex, segIndex, noteNumber) */
    std::function<void(int, int, int)> onNoteAssigned;

private:
    EventBus& bus_;
    // Registry of the plugin instance that built this overlay (captured at
    // construction, when the editor has claimed the locator for THIS
    // instance) - never re-read from the global locator later (review A2).
    class ServiceRegistry* registry_ { nullptr };
    EventBus::HandlerId learnSub_ { 0 };

    int pendingStemIndex_ { -1 };
    int pendingSegIndex_ { -1 };
    juce::String stemName_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SModeMidiLearnOverlay)
};

} // namespace samplar
