#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <vector>

namespace samplar {

/**
 * GifAnimatedBackground - Displays an animated GIF as a tiled/scaled background
 *
 * Decodes GIF frames and animates them at the correct frame rate.
 * Supports tiling or scaling to fill the component area.
 */
class GifAnimatedBackground : public juce::Component, private juce::Timer
{
public:
    enum class ScaleMode {
        Tile,           // Tile the GIF across the background
        ScaleToFill,    // Scale to fill, may crop
        ScaleToFit,     // Scale to fit, may have letterboxing
        Stretch         // Stretch to exact size
    };

    GifAnimatedBackground();
    ~GifAnimatedBackground() override;

    // Load GIF from binary data
    bool loadFromBinaryData(const char* data, int dataSize);

    // Load one of the preset backgrounds
    enum class BackgroundType { Campo, Lava };
    bool loadPreset(BackgroundType type);

    // Randomly select a background
    bool loadRandomBackground();

    // Component overrides
    void paint(juce::Graphics& g) override;
    void resized() override;

    // Animation control
    void startAnimation();
    void stopAnimation();
    bool isAnimating() const { return isTimerRunning(); }

    // Display settings
    void setScaleMode(ScaleMode mode) { scaleMode_ = mode; repaint(); }
    void setOpacity(float opacity) { opacity_ = juce::jlimit(0.0f, 1.0f, opacity); repaint(); }
    void setDarkOverlay(float amount) { darkOverlay_ = juce::jlimit(0.0f, 1.0f, amount); repaint(); }

private:
    void timerCallback() override;
    bool decodeGif(const char* data, int dataSize);
    static int getInterlacedY(int y, int height);

    // GIF frame data
    struct GifFrame {
        juce::Image image;
        int delayMs = 100;  // Frame delay in milliseconds
    };

    std::vector<GifFrame> frames_;
    int currentFrame_ = 0;
    int defaultFrameDelay_ = 100;  // Default 10 fps

    // Display settings
    ScaleMode scaleMode_ = ScaleMode::ScaleToFill;
    float opacity_ = 1.0f;
    float darkOverlay_ = 0.3f;  // Darken background for readability

    // Cached scaled image for performance
    juce::Image cachedScaled_;
    juce::Rectangle<int> lastBounds_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GifAnimatedBackground)
};

} // namespace samplar
