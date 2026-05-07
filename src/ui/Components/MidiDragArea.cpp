#include "MidiDragArea.h"

namespace samplar {

MidiDragArea::MidiDragArea()
{
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
}

void MidiDragArea::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(2.0f);

    // Background
    juce::Colour bgColour = hasValidFile_
        ? juce::Colour::fromString("ff1a1a2e")  // Dark blue-ish when valid
        : juce::Colour::fromString("ff1a1a1a"); // Dark gray when no file

    if (isHovered_ && hasValidFile_)
        bgColour = bgColour.brighter(0.15f);

    g.setColour(bgColour);
    g.fillRoundedRectangle(bounds, 4.0f);

    // Border
    juce::Colour borderColour = hasValidFile_
        ? juce::Colour::fromString("ff00FFFF").withAlpha(0.6f)  // Cyan accent
        : juce::Colours::grey.withAlpha(0.3f);

    g.setColour(borderColour);
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

    // Layout: icon on left, text on right
    auto iconBounds = bounds.removeFromLeft(bounds.getHeight()).reduced(6.0f);
    auto textBounds = bounds.reduced(4.0f, 0.0f);

    // Draw MIDI icon
    drawMidiIcon(g, iconBounds);

    // Draw label
    g.setColour(hasValidFile_
        ? juce::Colours::white.withAlpha(0.9f)
        : juce::Colours::grey.withAlpha(0.6f));
    g.setFont(juce::Font(11.0f));
    g.drawText("Drag MIDI", textBounds, juce::Justification::centredLeft, true);
}

void MidiDragArea::drawMidiIcon(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    // Simple MIDI connector icon (5-pin DIN style)
    auto centre = bounds.getCentre();
    float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.4f;

    // Outer circle
    g.setColour(hasValidFile_
        ? juce::Colour::fromString("ff00FFFF")  // Cyan
        : juce::Colours::grey.withAlpha(0.5f));
    g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.5f);

    // 5 pins in semi-circle (like MIDI DIN connector)
    float pinRadius = radius * 0.15f;
    float pinDistance = radius * 0.6f;

    // Top row: 3 pins
    for (int i = 0; i < 3; ++i)
    {
        float angle = juce::MathConstants<float>::pi * (0.25f + 0.25f * i);
        float px = centre.x + pinDistance * std::cos(angle);
        float py = centre.y - pinDistance * std::sin(angle);
        g.fillEllipse(px - pinRadius, py - pinRadius, pinRadius * 2.0f, pinRadius * 2.0f);
    }

    // Bottom row: 2 pins
    for (int i = 0; i < 2; ++i)
    {
        float angle = juce::MathConstants<float>::pi * (0.35f + 0.3f * i);
        float px = centre.x + pinDistance * std::cos(angle);
        float py = centre.y + pinDistance * std::sin(angle) * 0.5f;
        g.fillEllipse(px - pinRadius, py - pinRadius, pinRadius * 2.0f, pinRadius * 2.0f);
    }
}

void MidiDragArea::resized()
{
    // No child components to layout
}

void MidiDragArea::mouseEnter(const juce::MouseEvent&)
{
    isHovered_ = true;
    repaint();
}

void MidiDragArea::mouseExit(const juce::MouseEvent&)
{
    isHovered_ = false;
    repaint();
}

void MidiDragArea::mouseDown(const juce::MouseEvent&)
{
    isDragging_ = false;
}

void MidiDragArea::mouseDrag(const juce::MouseEvent& e)
{
    if (!hasValidFile_ || midiFilePath_.isEmpty())
    {
        juce::Logger::writeToLog("[MidiDragArea] Drag ignored: sequence is empty");
        return;
    }

    // Only start drag after some movement
    if (!isDragging_ && e.getDistanceFromDragStart() > 4)
    {
        isDragging_ = true;

        juce::File midiFile(midiFilePath_);
        if (midiFile.existsAsFile())
        {
            juce::Logger::writeToLog("[MidiDragArea] Starting external drag: " + midiFilePath_);

            // Use static method for external drag
            juce::StringArray files;
            files.add(midiFilePath_);

            // performExternalDragDropOfFiles returns true if drop was successful
            bool canMove = false;  // Copy, don't move
            juce::DragAndDropContainer::performExternalDragDropOfFiles(files, canMove);

            juce::Logger::writeToLog("[MidiDragArea] External drag completed");
        }
        else
        {
            juce::Logger::writeToLog("[MidiDragArea] MIDI file not found: " + midiFilePath_);
        }
    }
}

void MidiDragArea::setMidiFilePath(const juce::String& path)
{
    midiFilePath_ = path;
    hasValidFile_ = juce::File(path).existsAsFile();
    repaint();

    if (hasValidFile_)
        juce::Logger::writeToLog("[MidiDragArea] MIDI file updated: " + path);
}

bool MidiDragArea::hasMidiFile() const
{
    return hasValidFile_;
}

} // namespace samplar
