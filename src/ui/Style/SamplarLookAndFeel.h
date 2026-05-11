#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace samplar {

namespace SamplarColors {
    extern const juce::Colour bgDeep;
    extern const juce::Colour bgPanel;
    extern const juce::Colour bgPanelAlt;
    extern const juce::Colour borderSoft;
    extern const juce::Colour cyan;
    extern const juce::Colour green;
    extern const juce::Colour purple;
    extern const juce::Colour red;
    extern const juce::Colour orange;
    extern const juce::Colour yellow;
    extern const juce::Colour textMain;
    extern const juce::Colour textMuted;
} // namespace SamplarColors

class SamplarLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SamplarLookAndFeel();
    ~SamplarLookAndFeel() override = default;

    // Buttons
    void drawButtonBackground(juce::Graphics&, juce::Button&,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&,
                        bool shouldDrawButtonAsHighlighted,
                        bool shouldDrawButtonAsDown) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;

    // ComboBox
    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;

    // Sliders
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          const juce::Slider::SliderStyle, juce::Slider&) override;

    // Label
    void drawLabel(juce::Graphics&, juce::Label&) override;

    // PopupMenu
    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>& area,
                           bool isSeparator, bool isActive, bool isHighlighted,
                           bool isTicked, bool hasSubMenu,
                           const juce::String& text,
                           const juce::String& shortcutKeyText,
                           const juce::Drawable* icon,
                           const juce::Colour* textColour) override;

    // TreeView disclosure triangles - keep them visible (inherited from Sm5LookAndFeel)
    void drawTreeviewPlusMinusBox(juce::Graphics& g, const juce::Rectangle<float>& area,
                                  juce::Colour backgroundColour, bool isOpen,
                                  bool isMouseOver) override;
};

} // namespace samplar
