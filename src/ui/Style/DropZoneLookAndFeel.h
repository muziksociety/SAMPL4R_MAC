#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace samplar {

class DropZoneLookAndFeel : public juce::LookAndFeel_V4 {
public:
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override;
    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override;
};

} // namespace samplar

