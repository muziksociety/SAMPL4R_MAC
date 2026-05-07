#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

namespace samplar {

class MainWindow : public juce::DocumentWindow, public juce::FileDragAndDropTarget {
public:
    explicit MainWindow(const juce::String& name);
    void closeButtonPressed() override;
    juce::BorderSize<int> getBorderThickness() const { return juce::BorderSize<int>(0); }
    
    // FileDragAndDropTarget - forward to content component
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
};

/// Factory: create the root UI component (used by both MainWindow and SamplarAudioProcessorEditor)
std::unique_ptr<juce::Component> createRootComponent();

} // namespace samplar
