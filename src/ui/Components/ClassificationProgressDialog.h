#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "LoadingAnimation.h"

namespace samplar {

/**
 * ClassificationProgressDialog - Modal dialog showing classification progress
 *
 * Displays the monster loading animation with a progress bar while
 * folder classification runs in a background thread.
 */
class ClassificationProgressDialog : public juce::Component
{
public:
    ClassificationProgressDialog()
    {
        setSize(300, 280);

        addAndMakeVisible(animation_);
        animation_.startAnimation();

        addAndMakeVisible(progressBar_);
        progressBar_.setPercentageDisplay(true);

        addAndMakeVisible(statusLabel_);
        statusLabel_.setJustificationType(juce::Justification::centred);
        statusLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
        statusLabel_.setText("Classifying samples...", juce::dontSendNotification);

        addAndMakeVisible(countLabel_);
        countLabel_.setJustificationType(juce::Justification::centred);
        countLabel_.setColour(juce::Label::textColourId, juce::Colours::grey);
        countLabel_.setFont(juce::Font(12.0f));
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xFF1a1a2e));

        // Border
        g.setColour(juce::Colour(0xFFFFD700));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1), 8.0f, 2.0f);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(20);

        animation_.setBounds(bounds.removeFromTop(150));
        bounds.removeFromTop(10);
        statusLabel_.setBounds(bounds.removeFromTop(24));
        bounds.removeFromTop(5);
        progressBar_.setBounds(bounds.removeFromTop(20));
        bounds.removeFromTop(5);
        countLabel_.setBounds(bounds.removeFromTop(20));
    }

    void setProgress(float progress, int current, int total)
    {
        progressBar_.setValue(progress);
        animation_.setProgress(progress);
        countLabel_.setText(juce::String(current) + " / " + juce::String(total) + " files",
                           juce::dontSendNotification);
        repaint();
    }

    void setStatus(const juce::String& status)
    {
        statusLabel_.setText(status, juce::dontSendNotification);
    }

private:
    LoadingAnimation animation_;
    juce::ProgressBar progressBar_ { progress_ };
    juce::Label statusLabel_;
    juce::Label countLabel_;
    double progress_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClassificationProgressDialog)
};

} // namespace samplar
