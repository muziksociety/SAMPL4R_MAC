#include "SamplarLookAndFeel.h"
#include <cmath>

namespace samplar {

//==============================================================================
// Color definitions
//==============================================================================
namespace SamplarColors {
    const juce::Colour bgDeep        = juce::Colour::fromRGB(5,   9,  14);
    const juce::Colour bgPanel       = juce::Colour::fromRGB(12,  18,  28);
    const juce::Colour bgPanelAlt    = juce::Colour::fromRGB(16,  24,  36);
    const juce::Colour borderSoft    = juce::Colour::fromRGB(38,  54,  72);
    const juce::Colour cyan          = juce::Colour::fromRGB(0,  229, 255);
    const juce::Colour green         = juce::Colour::fromRGB(0,  220, 118);
    const juce::Colour purple        = juce::Colour::fromRGB(116, 82, 255);
    const juce::Colour red           = juce::Colour::fromRGB(255,  45,  45);
    const juce::Colour orange        = juce::Colour::fromRGB(255, 132,   0);
    const juce::Colour yellow        = juce::Colour::fromRGB(255, 224,   0);
    const juce::Colour textMain      = juce::Colour::fromRGB(235, 242, 250);
    const juce::Colour textMuted     = juce::Colour::fromRGB(145, 158, 174);
} // namespace SamplarColors

using namespace SamplarColors;

//==============================================================================
SamplarLookAndFeel::SamplarLookAndFeel()
{
    // Build colour scheme
    auto scheme = getLightColourScheme(); // start from a known base
    scheme.setUIColour(juce::LookAndFeel_V4::ColourScheme::UIColour::windowBackground,      bgDeep);
    scheme.setUIColour(juce::LookAndFeel_V4::ColourScheme::UIColour::widgetBackground,       bgPanel);
    scheme.setUIColour(juce::LookAndFeel_V4::ColourScheme::UIColour::menuBackground,         bgPanel);
    scheme.setUIColour(juce::LookAndFeel_V4::ColourScheme::UIColour::outline,                borderSoft);
    scheme.setUIColour(juce::LookAndFeel_V4::ColourScheme::UIColour::defaultText,            textMain);
    scheme.setUIColour(juce::LookAndFeel_V4::ColourScheme::UIColour::defaultFill,            bgPanelAlt);
    scheme.setUIColour(juce::LookAndFeel_V4::ColourScheme::UIColour::highlightedText,        textMain);
    scheme.setUIColour(juce::LookAndFeel_V4::ColourScheme::UIColour::highlightedFill,        cyan.withAlpha(0.25f));
    scheme.setUIColour(juce::LookAndFeel_V4::ColourScheme::UIColour::menuText,               textMain);
    setColourScheme(scheme);

    // Buttons
    setColour(juce::TextButton::buttonColourId,   bgPanelAlt);
    setColour(juce::TextButton::buttonOnColourId,  cyan.withAlpha(0.85f));
    setColour(juce::TextButton::textColourOffId,   textMain);
    setColour(juce::TextButton::textColourOnId,    bgDeep);

    // ComboBox
    setColour(juce::ComboBox::backgroundColourId,  bgPanel);
    setColour(juce::ComboBox::outlineColourId,      borderSoft);
    setColour(juce::ComboBox::textColourId,         textMain);
    setColour(juce::ComboBox::arrowColourId,        cyan);
    setColour(juce::ComboBox::buttonColourId,       bgPanelAlt);

    // Label
    setColour(juce::Label::textColourId,            textMain);
    setColour(juce::Label::backgroundColourId,      juce::Colours::transparentBlack);
    setColour(juce::Label::outlineColourId,         juce::Colours::transparentBlack);

    // Slider
    setColour(juce::Slider::thumbColourId,                  cyan);
    setColour(juce::Slider::trackColourId,                  borderSoft);
    setColour(juce::Slider::backgroundColourId,             bgPanel);
    setColour(juce::Slider::rotarySliderFillColourId,       cyan);
    setColour(juce::Slider::rotarySliderOutlineColourId,    borderSoft);
    setColour(juce::Slider::textBoxTextColourId,            textMain);
    setColour(juce::Slider::textBoxBackgroundColourId,      bgPanel);
    setColour(juce::Slider::textBoxOutlineColourId,         borderSoft);
    setColour(juce::Slider::textBoxHighlightColourId,       cyan.withAlpha(0.4f));

    // PopupMenu
    setColour(juce::PopupMenu::backgroundColourId,              bgPanel);
    setColour(juce::PopupMenu::textColourId,                    textMain);
    setColour(juce::PopupMenu::highlightedBackgroundColourId,   cyan.withAlpha(0.25f));
    setColour(juce::PopupMenu::highlightedTextColourId,         textMain);
    setColour(juce::PopupMenu::headerTextColourId,              textMuted);

    // ScrollBar
    setColour(juce::ScrollBar::thumbColourId,   borderSoft);
    setColour(juce::ScrollBar::trackColourId,   bgPanel);

    // TreeView
    setColour(juce::TreeView::backgroundColourId,       bgPanel);
    setColour(juce::TreeView::evenItemsColourId,        bgPanel);
    setColour(juce::TreeView::oddItemsColourId,         bgPanelAlt);
    setColour(juce::TreeView::selectedItemBackgroundColourId, cyan.withAlpha(0.2f));

    // ListBox
    setColour(juce::ListBox::backgroundColourId,        bgPanel);
    setColour(juce::ListBox::outlineColourId,           borderSoft);
    setColour(juce::ListBox::textColourId,              textMain);

    // ResizableWindow / DocumentWindow
    setColour(juce::ResizableWindow::backgroundColourId, bgDeep);
    setColour(juce::DocumentWindow::textColourId,        textMain);

    // AlertWindow
    setColour(juce::AlertWindow::backgroundColourId,    bgPanel);
    setColour(juce::AlertWindow::textColourId,          textMain);
    setColour(juce::AlertWindow::outlineColourId,       borderSoft);

    // TextEditor
    setColour(juce::TextEditor::backgroundColourId,     bgPanel);
    setColour(juce::TextEditor::textColourId,           textMain);
    setColour(juce::TextEditor::highlightColourId,      cyan.withAlpha(0.35f));
    setColour(juce::TextEditor::highlightedTextColourId, textMain);
    setColour(juce::TextEditor::outlineColourId,        borderSoft);
    setColour(juce::TextEditor::focusedOutlineColourId, cyan);
}

//==============================================================================
// Buttons
//==============================================================================
void SamplarLookAndFeel::drawButtonBackground(
    juce::Graphics& g, juce::Button& button,
    const juce::Colour& backgroundColour,
    bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    const float radius = 6.0f;

    // Fill
    auto fill = backgroundColour;
    if (shouldDrawButtonAsDown)
        fill = fill.darker(0.15f);

    g.setColour(fill);
    g.fillRoundedRectangle(bounds, radius);

    // Border
    g.setColour(borderSoft);
    g.drawRoundedRectangle(bounds, radius, 1.0f);

    // Cyan glow on hover
    if (shouldDrawButtonAsHighlighted && !shouldDrawButtonAsDown)
    {
        g.setColour(cyan.withAlpha(0.55f));
        g.drawRoundedRectangle(bounds, radius, 1.2f);
    }
}

void SamplarLookAndFeel::drawButtonText(
    juce::Graphics& g, juce::TextButton& button,
    bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    juce::ignoreUnused(shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

    const auto font = getTextButtonFont(button, button.getHeight());
    g.setFont(font);

    const juce::Colour textCol = button.findColour(
        button.getToggleState() ? juce::TextButton::textColourOnId
                                : juce::TextButton::textColourOffId);
    g.setColour(textCol);

    const int yIndent    = juce::jmin(4, button.proportionOfHeight(0.3f));
    const int leftIndent  = 4;
    const int rightIndent = 4;

    g.drawFittedText(button.getButtonText(),
                     leftIndent, yIndent,
                     button.getWidth() - leftIndent - rightIndent,
                     button.getHeight() - 2 * yIndent,
                     juce::Justification::centred, 2);
}

juce::Font SamplarLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight)
{
    // Scale down only for small buttons (S/M/L at 20 px); 22 px and taller keep 13.5 bold.
    const float h = juce::jlimit(10.0f, 13.5f, (float) buttonHeight * 0.62f);
    return juce::Font(juce::FontOptions().withHeight(h).withStyle("Bold"));
}

//==============================================================================
// ComboBox
//==============================================================================
void SamplarLookAndFeel::drawComboBox(
    juce::Graphics& g, int width, int height, bool /*isButtonDown*/,
    int /*buttonX*/, int /*buttonY*/, int /*buttonW*/, int /*buttonH*/,
    juce::ComboBox& box)
{
    const float radius = 6.0f;
    const auto bounds = juce::Rectangle<float>(0.0f, 0.0f, (float)width, (float)height)
                            .reduced(0.5f);

    // Background
    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(bounds, radius);

    // Border
    g.setColour(box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(bounds, radius, 1.0f);

    // Cyan arrow triangle on the right side
    const float arrowSize = height * 0.28f;
    const float arrowX = width - arrowSize * 2.2f;
    const float arrowY = (height - arrowSize) * 0.5f;

    juce::Path arrow;
    arrow.addTriangle(arrowX,                arrowY,
                      arrowX + arrowSize,    arrowY,
                      arrowX + arrowSize * 0.5f, arrowY + arrowSize);

    g.setColour(box.findColour(juce::ComboBox::arrowColourId));
    g.fillPath(arrow);
}

void SamplarLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(6, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
}

juce::Font SamplarLookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return juce::Font(juce::FontOptions().withHeight(13.0f));
}

//==============================================================================
// Rotary Slider
//==============================================================================
void SamplarLookAndFeel::drawRotarySlider(
    juce::Graphics& g, int x, int y, int width, int height,
    float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
    juce::Slider& slider)
{
    juce::ignoreUnused(slider);

    // Inset scales with the knob so small (20-40 px) knobs still draw an arc.
    const float half   = (float)juce::jmin(width / 2, height / 2);
    const float radius = juce::jmax(3.0f, half - juce::jmin(6.0f, half * 0.2f));
    const float centreX = (float)x + (float)width  * 0.5f;
    const float centreY = (float)y + (float)height * 0.5f;
    const float rx = centreX - radius;
    const float ry = centreY - radius;
    const float rw = radius * 2.0f;
    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // Track arc (background)
    {
        juce::Path trackArc;
        trackArc.addArc(rx, ry, rw, rw, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(borderSoft);
        g.strokePath(trackArc, juce::PathStrokeType(3.0f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Fill arc (cyan, from start to current angle)
    if (sliderPos > 0.0f)
    {
        juce::Path fillArc;
        fillArc.addArc(rx, ry, rw, rw, rotaryStartAngle, angle, true);
        g.setColour(cyan);
        g.strokePath(fillArc, juce::PathStrokeType(3.0f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Pointer line from centre to edge
    {
        const float pointerLength = radius * 0.72f;
        const float pointerThickness = 2.0f;
        juce::Path pointer;
        pointer.addRectangle(-pointerThickness * 0.5f, -radius, pointerThickness, pointerLength);
        pointer.applyTransform(
            juce::AffineTransform::rotation(angle).translated(centreX, centreY));
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.fillPath(pointer);
    }

    // Centre dot
    {
        const float dotSize = radius * 0.18f;
        g.setColour(bgPanel);
        g.fillEllipse(centreX - dotSize, centreY - dotSize, dotSize * 2.0f, dotSize * 2.0f);
    }
}

//==============================================================================
// Linear Slider
//==============================================================================
void SamplarLookAndFeel::drawLinearSlider(
    juce::Graphics& g, int x, int y, int width, int height,
    float sliderPos, float minSliderPos, float maxSliderPos,
    const juce::Slider::SliderStyle style, juce::Slider& slider)
{
    juce::ignoreUnused(maxSliderPos, minSliderPos);

    if (style == juce::Slider::LinearBar)
    {
        // Compact value bar (chop inspector): panel background, fill from the
        // zero point for bipolar ranges (pitch / pan / gain) or from the left,
        // value text is the slider's own label drawn on top.
        auto r = juce::Rectangle<float>((float) x, (float) y, (float) width, (float) height);
        const bool on = slider.isEnabled();
        g.setColour(bgPanelAlt.withAlpha(on ? 1.0f : 0.5f));
        g.fillRoundedRectangle(r, 3.0f);
        const double lo = slider.getMinimum(), hi = slider.getMaximum();
        const float zeroX = (lo < 0.0 && hi > 0.0)
            ? (float) slider.getPositionOfValue(0.0) : (float) x;
        const float x0 = juce::jmin(zeroX, sliderPos), x1 = juce::jmax(zeroX, sliderPos);
        if (x1 - x0 >= 0.5f)
        {
            g.setColour(cyan.withAlpha(on ? 0.40f : 0.15f));
            g.fillRoundedRectangle(juce::Rectangle<float>(x0, (float) y, x1 - x0, (float) height).reduced(0.0f, 1.0f), 2.0f);
        }
        g.setColour(borderSoft.withAlpha(on ? 1.0f : 0.5f));
        g.drawRoundedRectangle(r.reduced(0.5f), 3.0f, 1.0f);
        return;
    }

    if (style == juce::Slider::LinearVertical)
    {
        const float trackW = 4.0f;
        const float cx = (float)x + (float)width * 0.5f;
        const float trackTop    = (float)y + 7.0f;
        const float trackBottom = (float)y + (float)height - 7.0f;

        // Track background
        g.setColour(borderSoft);
        g.fillRoundedRectangle(cx - trackW * 0.5f, trackTop,
                               trackW, trackBottom - trackTop, trackW * 0.5f);

        // Fill from bottom to thumb
        if (sliderPos < trackBottom)
        {
            g.setColour(cyan);
            g.fillRoundedRectangle(cx - trackW * 0.5f, sliderPos,
                                   trackW, trackBottom - sliderPos, trackW * 0.5f);
        }

        // Thumb
        const float thumbR = 7.0f;
        g.setColour(cyan);
        g.fillEllipse(cx - thumbR, sliderPos - thumbR, thumbR * 2.0f, thumbR * 2.0f);
        g.setColour(juce::Colours::white.withAlpha(0.7f));
        g.drawEllipse(cx - thumbR + 0.5f, sliderPos - thumbR + 0.5f,
                      thumbR * 2.0f - 1.0f, thumbR * 2.0f - 1.0f, 1.0f);
    }
    else // horizontal (default)
    {
        const float trackH = 4.0f;
        const float cy = (float)y + (float)height * 0.5f;
        const float trackLeft  = (float)x + 7.0f;
        const float trackRight = (float)x + (float)width - 7.0f;

        // Track background
        g.setColour(borderSoft);
        g.fillRoundedRectangle(trackLeft, cy - trackH * 0.5f,
                               trackRight - trackLeft, trackH, trackH * 0.5f);

        // Fill from left to thumb
        if (sliderPos > trackLeft)
        {
            g.setColour(cyan);
            g.fillRoundedRectangle(trackLeft, cy - trackH * 0.5f,
                                   sliderPos - trackLeft, trackH, trackH * 0.5f);
        }

        // Thumb
        const float thumbR = 7.0f;
        g.setColour(cyan);
        g.fillEllipse(sliderPos - thumbR, cy - thumbR, thumbR * 2.0f, thumbR * 2.0f);
        g.setColour(juce::Colours::white.withAlpha(0.7f));
        g.drawEllipse(sliderPos - thumbR + 0.5f, cy - thumbR + 0.5f,
                      thumbR * 2.0f - 1.0f, thumbR * 2.0f - 1.0f, 1.0f);
    }
}

//==============================================================================
// Label
//==============================================================================
void SamplarLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label)
{
    // Background (usually transparent)
    g.fillAll(label.findColour(juce::Label::backgroundColourId));

    // Optional outline
    if (!label.isBeingEdited())
    {
        const auto outlineCol = label.findColour(juce::Label::outlineColourId);
        if (outlineCol.getAlpha() > 0)
        {
            g.setColour(outlineCol);
            g.drawRect(label.getLocalBounds());
        }

        const float alpha = label.isEnabled() ? 1.0f : 0.5f;
        g.setColour(label.findColour(juce::Label::textColourId).withMultipliedAlpha(alpha));
        g.setFont(label.getFont());

        auto textArea = getLabelBorderSize(label).subtractedFrom(label.getLocalBounds());
        g.drawFittedText(label.getText(), textArea, label.getJustificationType(),
                         juce::jmax(1, (int)((float)textArea.getHeight() / label.getFont().getHeight())),
                         label.getMinimumHorizontalScale());
    }
    else if (label.isEnabled())
    {
        g.setColour(label.findColour(juce::Label::outlineWhenEditingColourId));
        g.drawRect(label.getLocalBounds());
    }
}

//==============================================================================
// PopupMenu
//==============================================================================
void SamplarLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    const auto bounds = juce::Rectangle<float>(0.0f, 0.0f, (float)width, (float)height)
                            .reduced(0.5f);
    g.setColour(bgPanel);
    g.fillRoundedRectangle(bounds, 8.0f);
    g.setColour(borderSoft);
    g.drawRoundedRectangle(bounds, 8.0f, 1.0f);
}

void SamplarLookAndFeel::drawPopupMenuItem(
    juce::Graphics& g, const juce::Rectangle<int>& area,
    bool isSeparator, bool isActive, bool isHighlighted,
    bool isTicked, bool hasSubMenu,
    const juce::String& text,
    const juce::String& shortcutKeyText,
    const juce::Drawable* icon,
    const juce::Colour* textColour)
{
    juce::ignoreUnused(icon, hasSubMenu);

    if (isSeparator)
    {
        auto r = area.reduced(4, 0).toFloat();
        r.setY(r.getCentreY() - 0.5f);
        r.setHeight(1.0f);
        g.setColour(borderSoft);
        g.fillRect(r);
        return;
    }

    if (isHighlighted && isActive)
    {
        g.setColour(cyan.withAlpha(0.25f));
        g.fillRect(area);
    }

    const juce::Colour col = textColour != nullptr
                             ? *textColour
                             : (isActive ? textMain : textMuted);

    g.setColour(col);
    g.setFont(juce::Font(juce::FontOptions().withHeight(13.0f)));

    const int leftPad  = isTicked ? 28 : 12;
    const int rightPad = shortcutKeyText.isNotEmpty() ? 80 : 12;
    g.drawFittedText(text,
                     area.getX() + leftPad, area.getY(),
                     area.getWidth() - leftPad - rightPad, area.getHeight(),
                     juce::Justification::centredLeft, 1);

    if (isTicked)
    {
        g.setColour(cyan);
        g.setFont(juce::Font(juce::FontOptions().withHeight(13.0f)));
        g.drawText(juce::CharPointer_UTF8("\xe2\x9c\x93"),
                   area.getX() + 4, area.getY(),
                   20, area.getHeight(), juce::Justification::centredLeft);
    }

    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour(textMuted);
        g.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
        g.drawText(shortcutKeyText,
                   area.getX(), area.getY(),
                   area.getWidth() - 8, area.getHeight(),
                   juce::Justification::centredRight);
    }
}

//==============================================================================
// TreeView
//==============================================================================
void SamplarLookAndFeel::drawTreeviewPlusMinusBox(
    juce::Graphics& g, const juce::Rectangle<float>& area,
    juce::Colour /*backgroundColour*/, bool isOpen, bool isMouseOver)
{
    const float size = juce::jmin(area.getWidth(), area.getHeight()) * 0.6f;
    const float cx   = area.getCentreX();
    const float cy   = area.getCentreY();

    g.setColour(isMouseOver ? cyan : textMuted);

    juce::Path triangle;
    if (isOpen)
    {
        triangle.addTriangle(cx - size * 0.5f, cy - size * 0.3f,
                             cx + size * 0.5f, cy - size * 0.3f,
                             cx,               cy + size * 0.5f);
    }
    else
    {
        triangle.addTriangle(cx - size * 0.3f, cy - size * 0.5f,
                             cx + size * 0.5f, cy,
                             cx - size * 0.3f, cy + size * 0.5f);
    }
    g.fillPath(triangle);
}

} // namespace samplar
