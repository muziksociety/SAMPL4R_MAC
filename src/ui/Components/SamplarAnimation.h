#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <cmath>

namespace samplar {

/**
 * Epic SAMPLAR Animation Component
 * 
 * Ported from Python splash screen animation with C++ enhancements:
 * - Smooth 60fps animation with JUCE Timer
 * - Reddish-orange monster with green hair and evil smile
 * - Horizontal bouncing movement with mouth/eye animation
 * - Audio waveform trail following the monster
 * - GPU-accelerated rendering with JUCE Graphics
 * - Customizable size and animation speed
 */
class SamplarAnimation : public juce::Component, private juce::Timer
{
public:
    SamplarAnimation();
    ~SamplarAnimation() override;

    // Component overrides
    void paint(juce::Graphics& g) override;
    void resized() override;

    // Animation control
    void startAnimation();
    void stopAnimation();
    bool isAnimating() const { return isTimerRunning(); }
    
    // Customization
    void setAnimationSpeed(float speed) { animationSpeed_ = speed; }
    void setMonsterSize(int size) { monsterSize_ = size; }
    void setShowAudioTrail(bool show) { showAudioTrail_ = show; }
    void setBackgroundColor(juce::Colour color) { backgroundColor_ = color; }

    // Processing status integration
    void setProcessingActive(bool active) { processingActive_ = active; }
    void setProgress(float progress) { processingProgress_ = juce::jlimit(0.0f, 1.0f, progress); }
    void resetToBeginning() { 
        processingProgress_ = 0.0f; 
        monstaX_ = 80.0f;
    }
    void startDrumStemProcess() {
        resetToBeginning();
        processingActive_ = true;
    }
    
    // Background management for new full songs
    void selectRandomBackground();
    void startNewFullSong() {
        selectRandomBackground();
        resetToBeginning();
    }

private:
    // Timer callback for animation
    void timerCallback() override;

    // Animation state variables (ported from Python)
    float monstaX_ = 80.0f;          // Horizontal position
    int monstaDirection_ = 1;        // 1 for right, -1 for left
    float monstaMouthAngle_ = 0.0f;  // Mouth opening/closing animation
    int monstaEyeBlink_ = 0;         // Eye blinking counter
    
    // Animation parameters
    float animationSpeed_ = 1.0f;    // Speed multiplier
    int monsterSize_ = 35;           // Monster size in pixels
    bool showAudioTrail_ = true;     // Show waveform trail
    bool processingActive_ = false;  // Processing status
    float processingProgress_ = 0.0f; // 0.0 to 1.0
    
    // Visual customization
    juce::Colour backgroundColor_ = juce::Colours::black;
    juce::Colour monsterColor_ = juce::Colour(220, 80, 40);      // Reddish orange
    juce::Colour monsterOutline_ = juce::Colour(180, 60, 30);    // Darker orange
    juce::Colour hairColor_ = juce::Colours::green;
    juce::Colour trailColor_ = juce::Colours::cyan;
    
    // Background image system
    juce::Image currentBackground_;
    std::vector<juce::String> backgroundFiles_;

    // Drawing methods (ported from Python)
    void drawBackground(juce::Graphics& g);
    void drawMonstaAnimation(juce::Graphics& g);
    void drawMonstaHair(juce::Graphics& g, const juce::Rectangle<float>& bodyRect);
    void drawMonstaFeatures(juce::Graphics& g, const juce::Rectangle<float>& bodyRect);
    void drawEvilSmile(juce::Graphics& g, float centerX, float centerY, float mouthAngle);
    void drawAudioTrail(juce::Graphics& g, float yPos);
    void drawProcessingStatus(juce::Graphics& g);
    
    // Background system methods
    void loadBackgroundFiles();
    // Spooky message system moved to ProcessingPage center

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplarAnimation)
};

} // namespace samplar
