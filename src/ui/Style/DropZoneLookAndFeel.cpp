#include "DropZoneLookAndFeel.h"

using namespace juce;

namespace samplar {

void DropZoneLookAndFeel::drawButtonBackground(Graphics& g, Button& b, const Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat();
    auto rad = 20.0f; // rounder corners
    auto panel = Colour::fromString("ff21262D"); // SM4 tertiary panel grey
    auto bg = panel.withAlpha(0.22f);
    if (down) bg = panel.withAlpha(0.30f);
    if (over && !down) bg = panel.withAlpha(0.26f);
    g.setColour(bg);
    g.fillRoundedRectangle(r, rad);

    // Dashed border
    Path rect; rect.addRoundedRectangle(r, rad);
    Path dashed;
    float dashes[] = { 12.0f, 8.0f };
    PathStrokeType pst(2.4f);
    pst.createDashedStroke(dashed, rect, dashes, 2);
    g.setColour(Colour::fromString("ff6E7681").withAlpha(0.9f)); // muted grey stroke
    g.strokePath(dashed, PathStrokeType(2.4f));

    // subtle outer glow
    g.setColour(Colours::white.withAlpha(0.08f));
    g.drawRoundedRectangle(r.expanded(1.5f), rad + 1.5f, 3.0f);
}

void DropZoneLookAndFeel::drawButtonText(Graphics& g, TextButton& b, bool, bool)
{
    g.setColour(Colours::whitesmoke);
    g.setFont(Font(18.0f, Font::bold));
    auto text = b.getButtonText();
    g.drawFittedText(text, b.getLocalBounds().reduced(16), Justification::centred, 2);
}

} // namespace samplar
