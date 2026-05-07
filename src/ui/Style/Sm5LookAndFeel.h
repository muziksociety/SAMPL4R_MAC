#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace samplar {

class Sm5LookAndFeel : public juce::LookAndFeel_V4 {
public:
    Sm5LookAndFeel();
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    // TreeView disclosure triangles - make them white/visible
    void drawTreeviewPlusMinusBox(juce::Graphics& g, const juce::Rectangle<float>& area,
                                   juce::Colour backgroundColour, bool isOpen, bool isMouseOver) override;
};

} // namespace samplar

