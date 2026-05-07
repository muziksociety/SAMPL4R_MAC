#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <functional>
#include <atomic>

namespace samplar {

/**
 * WaveDragArea - Draggable area for external WAV file drag-out
 *
 * Displays a wave icon with label and integrated progress bar.
 * User clicks to render the pattern to audio, then can drag to export
 * to external applications (DAW, file explorer) or right-click to save.
 *
 * States:
 * - Idle: No pattern/samples loaded, shows "Drag Wave" grayed out
 * - Ready: Rendered file available, shows "Drag Wave" active (green)
 * - Preparing: Click triggered render, shows progress bar filling (cyan)
 */
class WaveDragArea : public juce::Component,
                     private juce::Timer
{
public:
    WaveDragArea();
    ~WaveDragArea() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Mouse interaction for external drag
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    /**
     * Callback to check if pattern can be rendered (has samples + pattern data)
     * Return true if there's something to render
     */
    std::function<bool()> onCanRender;

    /**
     * Callback to render the pattern to audio
     * Called on background thread - should return owned buffer + sample rate
     * Progress callback can be called to update the progress bar (0.0 to 1.0)
     */
    std::function<std::unique_ptr<juce::AudioBuffer<float>>(double& outSampleRate, std::function<void(float)> progressCallback)> onRenderPattern;

    /**
     * Check if we can render (has pattern + samples)
     */
    bool canRender() const;

    /**
     * Check if file is prepared and ready for drag
     */
    bool isReadyToDrag() const;

    /**
     * Force refresh/re-prepare the wave file
     * Call this when pattern changes to invalidate cached render
     */
    void invalidate();

    /**
     * Mark as having renderable content (call when samples are loaded)
     */
    void setHasContent(bool hasContent);

private:
    enum class State { Idle, Ready, Preparing };
    State state_ = State::Idle;

    // Rendered audio (owned by this component after render)
    std::unique_ptr<juce::AudioBuffer<float>> renderedBuffer_;
    double sampleRate_ = 44100.0;

    // Prepared file path (temp directory)
    juce::String wavFilePath_;
    bool hasValidFile_ = false;
    bool hasContent_ = false;  // True if there's something to render

    // Progress tracking
    std::atomic<float> progress_{ 0.0f };
    std::unique_ptr<std::thread> prepareThread_;

    // Visual state
    bool isHovered_ = false;
    bool isDragging_ = false;

    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    // Timer for progress updates
    void timerCallback() override;

    // Prepare the WAV file (called on click)
    void startPreparing();
    void renderAndPrepareWavFile();
    void onPrepareComplete(bool success, const juce::String& filePath);

    // Context menu
    void showContextMenu();
    void saveAs();

    // Draw wave icon
    void drawWaveIcon(juce::Graphics& g, juce::Rectangle<float> bounds);

    // Draw progress bar overlay
    void drawProgressBar(juce::Graphics& g, juce::Rectangle<float> bounds);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveDragArea)
};

} // namespace samplar
