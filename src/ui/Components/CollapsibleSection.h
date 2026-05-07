#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace samplar {

/**
 * AccordionPanelHeader - Custom header for ConcertinaPanel sections
 *
 * Features:
 * - Clickable to expand/collapse panel (inherits from Button for reliable clicks)
 * - Shows section title and preview value
 * - Chevron indicator for state
 * - Cyan accent color scheme
 */
class AccordionPanelHeader : public juce::Button {
public:
    AccordionPanelHeader(const juce::String& title, juce::ConcertinaPanel& owner, juce::Component* panel);
    ~AccordionPanelHeader() override = default;

    void setPreviewText(const juce::String& text);
    void paintButton(juce::Graphics& g, bool isMouseOver, bool isMouseDown) override;
    void clicked() override;

private:
    juce::String title_;
    juce::String previewText_;
    juce::ConcertinaPanel& owner_;
    juce::Component* panel_;

    // Colors
    juce::Colour headerBgColor_{ 0xff1a1a2e };
    juce::Colour headerHoverColor_{ 0xff252545 };
    juce::Colour textColor_{ 0xffe0e0e0 };
    juce::Colour accentColor_{ 0xff00ffff };
    juce::Colour previewColor_{ 0xff808090 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AccordionPanelHeader)
};

/**
 * AccordionLookAndFeel - Custom look for ConcertinaPanel
 * Provides dark theme styling for the accordion panels
 */
class AccordionLookAndFeel : public juce::LookAndFeel_V4 {
public:
    AccordionLookAndFeel() = default;

    void drawConcertinaPanelHeader(juce::Graphics& g, const juce::Rectangle<int>& area,
                                    bool isMouseOver, bool isMouseDown,
                                    juce::ConcertinaPanel& panel, juce::Component& component) override;
};

} // namespace samplar
