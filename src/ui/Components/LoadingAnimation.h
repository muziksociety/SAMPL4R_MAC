#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>

namespace samplar {

/**
 * LoadingAnimation - Animated sprite sheet component
 *
 * Displays a looping animation from a sprite sheet grid.
 * Used for the processing page loading indicator.
 *
 * Sprite sheet format: 11x11 grid of 400x172 frames (121 frames total)
 * Animation runs at 24fps with infinite loop.
 */
class LoadingAnimation : public juce::Component, private juce::Timer
{
public:
    LoadingAnimation();
    ~LoadingAnimation() override;

    // Component overrides
    void paint(juce::Graphics& g) override;
    void resized() override;

    // Animation control
    void startAnimation();
    void stopAnimation();
    bool isAnimating() const { return isTimerRunning(); }

    // Progress display (shown below animation)
    void setProgress(float progress) { progress_ = juce::jlimit(0.0f, 1.0f, progress); repaint(); }
    float getProgress() const { return progress_; }

private:
    void timerCallback() override;
    void loadSpriteSheet();

    // Sprite sheet
    juce::Image spriteSheet_;
    bool spriteSheetLoaded_ = false;

    // Frame dimensions
    static constexpr int FRAME_WIDTH = 400;
    static constexpr int FRAME_HEIGHT = 172;
    static constexpr int GRID_COLS = 9;
    static constexpr int GRID_ROWS = 6;
    static constexpr int TOTAL_FRAMES = 51;
    static constexpr int FPS = 10;

    // Animation state
    int currentFrame_ = 0;
    float progress_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoadingAnimation)
};

} // namespace samplar
