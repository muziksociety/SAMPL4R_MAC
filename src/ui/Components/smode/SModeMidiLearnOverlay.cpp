#include "SModeMidiLearnOverlay.h"
#include "../../../app/ServiceLocator.h"
#include "../../../app/ServiceRegistry.h"
#include "../../../services/MidiMappingService.h"

namespace samplar {

SModeMidiLearnOverlay::SModeMidiLearnOverlay(EventBus& bus, ServiceRegistry* registry)
    : bus_(bus), registry_(registry)
{
    setOpaque(false);
    setAlwaysOnTop(true);
    setVisible(false);
    setInterceptsMouseClicks(true, false);

    // Subscribe to the S-Mode MIDI learn event
    learnSub_ = bus_.subscribe("smode.midi.learn.assigned", [this](const juce::var& v)
    {
        if (!isVisible()) return;

        int noteNumber = -1;
        if (auto* obj = v.getDynamicObject())
            noteNumber = static_cast<int>(obj->getProperty("noteNumber"));

        if (noteNumber >= 0 && noteNumber <= 127)
        {
            juce::Logger::writeToLog("✅ [SModeMidiLearn] Note received: " + juce::String(noteNumber)
                                     + " for stem=" + juce::String(pendingStemIndex_)
                                     + " seg=" + juce::String(pendingSegIndex_));

            if (onNoteAssigned)
                onNoteAssigned(pendingStemIndex_, pendingSegIndex_, noteNumber);

            dismiss();
        }
    });
}

SModeMidiLearnOverlay::~SModeMidiLearnOverlay()
{
    if (learnSub_ != 0)
        bus_.unsubscribe("smode.midi.learn.assigned", learnSub_);
}

void SModeMidiLearnOverlay::show(int stemIndex, int segIndex, const juce::String& stemName)
{
    pendingStemIndex_ = stemIndex;
    pendingSegIndex_ = segIndex;
    stemName_ = stemName;

    // Activate MIDI learn mode in the service
    if (auto* registry = registry_)
    {
        if (auto* midiSvc = dynamic_cast<MidiMappingService*>(registry->get("midi.mapping")))
            midiSvc->beginMidiLearnForSMode();
    }

    setVisible(true);
    toFront(true);
    repaint();

    juce::Logger::writeToLog("🎵 [SModeMidiLearn] Overlay shown: stem=" + juce::String(stemIndex)
                             + " seg=" + juce::String(segIndex) + " (" + stemName + ")");
}

void SModeMidiLearnOverlay::dismiss()
{
    // Cancel learn mode if still active
    if (auto* registry = registry_)
    {
        if (auto* midiSvc = dynamic_cast<MidiMappingService*>(registry->get("midi.mapping")))
        {
            if (midiSvc->isMidiLearning())
                midiSvc->endMidiLearn();
        }
    }

    setVisible(false);
    pendingStemIndex_ = -1;
    pendingSegIndex_ = -1;
}

void SModeMidiLearnOverlay::paint(juce::Graphics& g)
{
    // Semi-transparent dark background covering the entire page
    g.fillAll(juce::Colours::black.withAlpha(0.7f));

    // Centered dialog panel
    // Fixed-size panel (text needs ~176 px): reduced(w/4, h/3) clipped the last line at 850x500.
    auto panelBounds = getLocalBounds().withSizeKeepingCentre(juce::jmin(420, getWidth() - 40),
                                                              juce::jmin(200, getHeight() - 40));
    g.setColour(juce::Colour(0xff1e1e3a));
    g.fillRoundedRectangle(panelBounds.toFloat(), 12.0f);
    g.setColour(juce::Colour(0xff5555aa));
    g.drawRoundedRectangle(panelBounds.toFloat(), 12.0f, 2.0f);

    auto textArea = panelBounds.reduced(20);

    // Music icon
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(32.0f));
    g.drawText(juce::String::charToString(0x266B), textArea.removeFromTop(40),
               juce::Justification::centred);

    textArea.removeFromTop(8);

    // Main prompt
    g.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    g.drawText("Press a MIDI key (or A..; on the keyboard)", textArea.removeFromTop(24),
               juce::Justification::centred);

    textArea.removeFromTop(12);

    // Stem/segment info
    g.setFont(juce::FontOptions(13.0f));
    g.setColour(juce::Colours::lightgrey);
    juce::String info = stemName_ + " - Segment " + juce::String(pendingSegIndex_ + 1);
    g.drawText(info, textArea.removeFromTop(20), juce::Justification::centred);

    textArea.removeFromTop(16);

    // Cancel hint
    g.setFont(juce::FontOptions(11.0f));
    g.setColour(juce::Colours::grey);
    g.drawText("Click anywhere to cancel", textArea.removeFromTop(16),
               juce::Justification::centred);
}

void SModeMidiLearnOverlay::mouseDown(const juce::MouseEvent&)
{
    dismiss();
}

} // namespace samplar
