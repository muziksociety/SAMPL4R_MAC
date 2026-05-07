#include "TransparentHoverButtonLookAndFeel.h"

using namespace juce;

namespace samplar {

void TransparentHoverButtonLookAndFeel::drawButtonBackground(Graphics& g, Button& b, const Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat();
    auto rad = 6.0f;
    if (over || down) {
        auto bg = Colours::white.withAlpha(down ? 0.14f : 0.08f);
        g.setColour(bg);
        g.fillRoundedRectangle(r, rad);
        g.setColour(Colours::white.withAlpha(0.15f));
        g.drawRoundedRectangle(r, rad, 1.0f);
    } else {
        // fully transparent when not hovered
    }
}

void TransparentHoverButtonLookAndFeel::drawButtonText(Graphics& g, TextButton& b, bool highlighted, bool down)
{
    auto alpha = (highlighted || down) ? 0.95f : 0.5f; // transparent until hover
    g.setColour(Colours::whitesmoke.withAlpha(alpha));
    g.setFont(Font(13.0f));
    g.drawFittedText(b.getButtonText(), b.getLocalBounds().reduced(8), Justification::centred, 1);
}

} // namespace samplar
