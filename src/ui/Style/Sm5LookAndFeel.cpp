#include "Sm5LookAndFeel.h"

using namespace juce;

namespace samplar {

static Colour defaultBgButton()    { return Colour::fromString("ff1a1a2e"); }  // Dark blue-ish (matches WaveDragArea)
static Colour borderCol()          { return Colour::fromString("ff00FFFF"); }  // Cyan glow
static Colour defaultTextCol()     { return Colours::white; }

Sm5LookAndFeel::Sm5LookAndFeel()
{
    setColour(TextButton::buttonColourId, defaultBgButton());
    setColour(TextButton::textColourOffId, defaultTextCol());
}

void Sm5LookAndFeel::drawButtonBackground(Graphics& g, Button& b, const Colour& backgroundColour, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat();
    auto rad = 6.0f;

    // Use the button's configured color (from setColour), falling back to default
    Colour baseColour = b.findColour(TextButton::buttonColourId);

    // Apply hover/down effects by adjusting brightness
    Colour finalColour;
    if (down)
        finalColour = baseColour.darker(0.2f);
    else if (over)
        finalColour = baseColour.brighter(0.1f);
    else
        finalColour = baseColour;

    g.setColour(finalColour);
    g.fillRoundedRectangle(r, rad);
    float borderAlpha = over ? 0.7f : 0.4f;
    g.setColour(borderCol().withAlpha(borderAlpha));
    g.drawRoundedRectangle(r.reduced(0.5f), rad, 1.0f);
}

void Sm5LookAndFeel::drawButtonText(Graphics& g, TextButton& b, bool, bool)
{
    // Use the button's configured text color (respects textColourOffId/textColourOnId)
    Colour textColour = b.findColour(b.getToggleState() ? TextButton::textColourOnId : TextButton::textColourOffId);
    g.setColour(textColour);
    g.setFont(Font(11.0f));
    g.drawFittedText(b.getButtonText(), b.getLocalBounds(), Justification::centred, 1);
}

void Sm5LookAndFeel::drawTreeviewPlusMinusBox(Graphics& g, const Rectangle<float>& area,
                                               Colour /*backgroundColour*/, bool isOpen, bool isMouseOver)
{
    // Draw white disclosure triangle for visibility on dark backgrounds
    // Scale down to 60% of the original size for better proportions
    auto smallArea = area.reduced(area.getWidth() * 0.2f, area.getHeight() * 0.2f);

    Path p;

    // Triangle pointing right (closed) or down (open)
    if (isOpen)
    {
        // Down arrow when open
        p.addTriangle(smallArea.getX(), smallArea.getY(),
                      smallArea.getRight(), smallArea.getY(),
                      smallArea.getCentreX(), smallArea.getBottom());
    }
    else
    {
        // Right arrow when closed
        p.addTriangle(smallArea.getX(), smallArea.getY(),
                      smallArea.getRight(), smallArea.getCentreY(),
                      smallArea.getX(), smallArea.getBottom());
    }

    // White color with hover effect
    Colour arrowColour = isMouseOver ? Colours::white : Colour(0xffcccccc);
    g.setColour(arrowColour);
    g.fillPath(p);
}

} // namespace samplar

