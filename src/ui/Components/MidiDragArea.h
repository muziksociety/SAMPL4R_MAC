#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace samplar {

/**
 * MidiDragArea - Draggable area for external MIDI file drag-out
 *
 * Displays a MIDI icon with label. User can click and drag to export
 * the current MIDI file to external applications (DAW, file explorer).
 */
class MidiDragArea : public juce::Component
{
public:
    MidiDragArea();
    ~MidiDragArea() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Mouse interaction for external drag
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;

    /**
     * Set the MIDI file path to drag
     * Call this whenever the pattern changes
     */
    void setMidiFilePath(const juce::String& path);

    /**
     * Check if we have a valid MIDI file to drag
     */
    bool hasMidiFile() const;

private:
    juce::String midiFilePath_;
    bool isDragging_ = false;
    bool hasValidFile_ = false;

    // Visual state
    bool isHovered_ = false;

    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    // Draw MIDI icon
    void drawMidiIcon(juce::Graphics& g, juce::Rectangle<float> bounds);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiDragArea)
};

} // namespace samplar
