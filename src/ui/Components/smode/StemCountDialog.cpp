#include "StemCountDialog.h"

namespace samplar {

StemCountDialog::StemCountDialog()
{
    setSize(460, 320);

    // Title
    addAndMakeVisible(titleLabel_);
    titleLabel_.setFont(juce::FontOptions(24.0f, juce::Font::bold));
    titleLabel_.setJustificationType(juce::Justification::centred);
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);

    // 4-stem button
    addAndMakeVisible(fourStemBtn_);
    fourStemBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d2d3a));
    fourStemBtn_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    fourStemBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    fourStemBtn_.onClick = [this]()
    {
        juce::Logger::writeToLog("🎵 [StemCountDialog] User selected 4 stems");
        if (onStemCountSelected) onStemCountSelected(4);
    };

    // 6-stem button
    addAndMakeVisible(sixStemBtn_);
    sixStemBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d2d3a));
    sixStemBtn_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    sixStemBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    sixStemBtn_.onClick = [this]()
    {
        juce::Logger::writeToLog("🎵 [StemCountDialog] User selected 6 stems");
        if (onStemCountSelected) onStemCountSelected(6);
    };

    // Descriptions
    addAndMakeVisible(fourDesc_);
    fourDesc_.setFont(juce::FontOptions(13.0f));
    fourDesc_.setJustificationType(juce::Justification::centred);
    fourDesc_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);

    addAndMakeVisible(sixDesc_);
    sixDesc_.setFont(juce::FontOptions(13.0f));
    sixDesc_.setJustificationType(juce::Justification::centred);
    sixDesc_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
}

void StemCountDialog::paint(juce::Graphics& g)
{
    // Dark background with rounded corners
    g.fillAll(juce::Colour(0xff1a1a2e));
    g.setColour(juce::Colour(0xff3a3a5a));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 12.0f, 2.0f);
}

void StemCountDialog::resized()
{
    auto area = getLocalBounds().reduced(24);

    titleLabel_.setBounds(area.removeFromTop(40));
    area.removeFromTop(16);

    // Two columns for the buttons
    auto buttonArea = area.removeFromTop(180);
    int gap = 16;
    int btnW = (buttonArea.getWidth() - gap) / 2;

    auto leftCol = buttonArea.removeFromLeft(btnW);
    buttonArea.removeFromLeft(gap);
    auto rightCol = buttonArea;

    fourStemBtn_.setBounds(leftCol.removeFromTop(60));
    leftCol.removeFromTop(8);
    fourDesc_.setBounds(leftCol);

    sixStemBtn_.setBounds(rightCol.removeFromTop(60));
    rightCol.removeFromTop(8);
    sixDesc_.setBounds(rightCol);
}

void StemCountDialog::show(juce::Component* parent, std::function<void(int stemCount)> callback)
{
    // 6-stem flow deprecated 2026-05-17: native C++/ONNX separation
    // (htdemucs.onnx) is 4-stem only. The dialog and its UI components are
    // kept compiled so call sites stay unchanged, but show() just dispatches
    // the 4-stem choice immediately instead of presenting a dialog.
    juce::ignoreUnused(parent);
    juce::Logger::writeToLog("[StemCountDialog] show() bypassed - auto-selecting 4 stems");
    if (callback)
        callback(4);
}

} // namespace samplar
