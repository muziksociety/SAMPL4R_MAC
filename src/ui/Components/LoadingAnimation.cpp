#include "LoadingAnimation.h"
#include "BinaryData.h"

namespace samplar {

LoadingAnimation::LoadingAnimation()
{
    loadSpriteSheet();
    juce::Logger::writeToLog("[LoadingAnimation] Created, sprite sheet loaded: " +
                             juce::String(spriteSheetLoaded_ ? "yes" : "no"));
}

LoadingAnimation::~LoadingAnimation()
{
    stopTimer();
}

void LoadingAnimation::loadSpriteSheet()
{
    // Load sprite sheet from BinaryData
    if (BinaryData::loading_spritesheet_pngSize > 0)
    {
        spriteSheet_ = juce::ImageFileFormat::loadFrom(
            BinaryData::loading_spritesheet_png,
            BinaryData::loading_spritesheet_pngSize);
        spriteSheetLoaded_ = spriteSheet_.isValid();

        if (spriteSheetLoaded_)
        {
            juce::Logger::writeToLog("[LoadingAnimation] Sprite sheet loaded: " +
                                     juce::String(spriteSheet_.getWidth()) + "x" +
                                     juce::String(spriteSheet_.getHeight()));
        }
        else
        {
            juce::Logger::writeToLog("[LoadingAnimation] ERROR: Failed to decode sprite sheet");
        }
    }
    else
    {
        juce::Logger::writeToLog("[LoadingAnimation] ERROR: Sprite sheet not found in BinaryData");
    }
}

void LoadingAnimation::paint(juce::Graphics& g)
{
    // Transparent background - parent handles background color

    if (!spriteSheetLoaded_)
    {
        // Fallback: draw loading text
        g.setColour(juce::Colours::white);
        g.setFont(24.0f);
        g.drawText("Loading...", getLocalBounds(), juce::Justification::centred);
        return;
    }

    // Calculate current frame position in sprite sheet grid
    int col = currentFrame_ % GRID_COLS;
    int row = currentFrame_ / GRID_COLS;
    int srcX = col * FRAME_WIDTH;
    int srcY = row * FRAME_HEIGHT;

    // Scale to fit component bounds while maintaining aspect ratio
    auto bounds = getLocalBounds();
    float frameAspect = static_cast<float>(FRAME_WIDTH) / FRAME_HEIGHT;
    float boundsAspect = static_cast<float>(bounds.getWidth()) / bounds.getHeight();

    int destW, destH;
    if (boundsAspect > frameAspect)
    {
        // Bounds are wider than frame - fit to height
        destH = bounds.getHeight();
        destW = static_cast<int>(destH * frameAspect);
    }
    else
    {
        // Bounds are taller than frame - fit to width
        destW = bounds.getWidth();
        destH = static_cast<int>(destW / frameAspect);
    }

    int destX = (bounds.getWidth() - destW) / 2;
    int destY = (bounds.getHeight() - destH) / 2;

    // Draw current frame scaled to fit
    g.drawImage(spriteSheet_,
                destX, destY, destW, destH,               // destination (scaled)
                srcX, srcY, FRAME_WIDTH, FRAME_HEIGHT);   // source (original)
}

void LoadingAnimation::resized()
{
    // Nothing to resize - animation is centered in paint()
}

void LoadingAnimation::startAnimation()
{
    if (!isTimerRunning())
    {
        currentFrame_ = 0;
        startTimerHz(FPS);
        juce::Logger::writeToLog("[LoadingAnimation] Animation started at " + juce::String(FPS) + " fps");
    }
}

void LoadingAnimation::stopAnimation()
{
    if (isTimerRunning())
    {
        stopTimer();
        juce::Logger::writeToLog("[LoadingAnimation] Animation stopped");
    }
}

void LoadingAnimation::timerCallback()
{
    // Advance to next frame
    currentFrame_ = (currentFrame_ + 1) % TOTAL_FRAMES;
    repaint();
}

} // namespace samplar
