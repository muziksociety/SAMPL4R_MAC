#include "CollapsibleSection.h"

namespace samplar {

// ============================================================
// AccordionPanelHeader Implementation
// ============================================================

AccordionPanelHeader::AccordionPanelHeader(const juce::String& title,
                                           juce::ConcertinaPanel& owner,
                                           juce::Component* panel)
    : juce::Button(title), title_(title), owner_(owner), panel_(panel)
{
}

void AccordionPanelHeader::setPreviewText(const juce::String& text)
{
    previewText_ = text;
    repaint();
}

void AccordionPanelHeader::paintButton(juce::Graphics& g, bool isMouseOver, bool /*isMouseDown*/)
{
    auto bounds = getLocalBounds();
    const int w = bounds.getWidth();

    // Header background
    g.setColour(isMouseOver ? headerHoverColor_ : headerBgColor_);
    g.fillRoundedRectangle(bounds.toFloat(), 4.0f);

    // Header border (subtle cyan)
    g.setColour(accentColor_.withAlpha(0.3f));
    g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 4.0f, 1.0f);

    // Check if panel is expanded by looking at its current size
    bool isExpanded = false;
    if (panel_ != nullptr) {
        isExpanded = panel_->getHeight() > 10;
    }

    // Chevron icon (left side)
    const float chevronX = 14.0f;
    const float chevronY = (float)bounds.getCentreY();
    const float chevronSize = 4.0f;

    g.setColour(accentColor_);
    juce::Path chevron;
    if (isExpanded) {
        // Down chevron (v)
        chevron.startNewSubPath(chevronX - chevronSize, chevronY - chevronSize * 0.5f);
        chevron.lineTo(chevronX, chevronY + chevronSize * 0.5f);
        chevron.lineTo(chevronX + chevronSize, chevronY - chevronSize * 0.5f);
    } else {
        // Right chevron (>)
        chevron.startNewSubPath(chevronX - chevronSize * 0.5f, chevronY - chevronSize);
        chevron.lineTo(chevronX + chevronSize * 0.5f, chevronY);
        chevron.lineTo(chevronX - chevronSize * 0.5f, chevronY + chevronSize);
    }
    g.strokePath(chevron, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));

    // Calculate dynamic layout based on available width
    const int leftMargin = 24;  // After chevron
    const int rightMargin = 6;

    // Title text
    g.setColour(textColor_);
    g.setFont(juce::Font(11.0f, juce::Font::bold));

    // If collapsed and we have preview text, split space between title and preview
    if (!isExpanded && previewText_.isNotEmpty()) {
        // Title takes left half, preview takes right
        int titleW = juce::jmax(50, (w - leftMargin - rightMargin) / 2);
        auto titleBounds = bounds.withTrimmedLeft(leftMargin).withWidth(titleW);
        g.drawText(title_.toUpperCase(), titleBounds, juce::Justification::centredLeft);

        // Preview text on right
        g.setColour(previewColor_);
        g.setFont(juce::Font(10.0f));
        auto previewBounds = bounds.withTrimmedRight(rightMargin);
        previewBounds = previewBounds.removeFromRight(w - leftMargin - titleW - 8);
        g.drawText(previewText_, previewBounds, juce::Justification::centredRight);
    } else {
        // Full width for title
        auto titleBounds = bounds.withTrimmedLeft(leftMargin).withTrimmedRight(rightMargin);
        g.drawText(title_.toUpperCase(), titleBounds, juce::Justification::centredLeft);
    }
}

void AccordionPanelHeader::clicked()
{
    if (panel_ == nullptr) return;

    // Check current state
    bool isExpanded = panel_->getHeight() > 10;

    if (isExpanded) {
        // Collapse this panel to 0
        owner_.setPanelSize(panel_, 0, true);
    } else {
        // First collapse ALL panels
        for (int i = 0; i < owner_.getNumPanels(); ++i) {
            auto* p = owner_.getPanel(i);
            if (p != nullptr) {
                owner_.setPanelSize(p, 0, false);
            }
        }
        // Then expand only this one
        owner_.expandPanelFully(panel_, true);
    }

    // Repaint ALL headers since their states have changed
    owner_.repaint();
}

// ============================================================
// AccordionLookAndFeel Implementation
// ============================================================

void AccordionLookAndFeel::drawConcertinaPanelHeader(juce::Graphics& g,
                                                      const juce::Rectangle<int>& area,
                                                      bool isMouseOver, bool /*isMouseDown*/,
                                                      juce::ConcertinaPanel& /*panel*/,
                                                      juce::Component& /*component*/)
{
    // Dark themed header background
    juce::Colour bgColor = isMouseOver ? juce::Colour(0xff252545) : juce::Colour(0xff1a1a2e);
    g.setColour(bgColor);
    g.fillRoundedRectangle(area.toFloat(), 4.0f);

    // Cyan border
    g.setColour(juce::Colour(0xff00ffff).withAlpha(0.3f));
    g.drawRoundedRectangle(area.toFloat().reduced(0.5f), 4.0f, 1.0f);
}

} // namespace samplar
