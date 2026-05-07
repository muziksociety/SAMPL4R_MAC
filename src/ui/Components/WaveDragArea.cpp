#include "WaveDragArea.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace samplar {

WaveDragArea::WaveDragArea()
{
    juce::Logger::writeToLog("[WaveDragArea] === CONSTRUCTOR ===");
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
}

WaveDragArea::~WaveDragArea()
{
    juce::Logger::writeToLog("[WaveDragArea] === DESTRUCTOR ===");
    stopTimer();

    // Wait for any background thread to finish
    if (prepareThread_ && prepareThread_->joinable())
    {
        prepareThread_->join();
    }
}

void WaveDragArea::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(2.0f);

    // Determine colors based on state
    juce::Colour bgColour;
    juce::Colour borderColour;
    juce::Colour textColour;

    bool canDoRender = canRender();

    switch (state_)
    {
        case State::Idle:
            if (canDoRender)
            {
                // Has content but not yet rendered - show as available
                bgColour = juce::Colour::fromString("ff1a1a2e");     // Dark blue-ish
                borderColour = juce::Colour::fromString("ff00FFFF").withAlpha(0.4f);  // Dim cyan
                textColour = juce::Colours::white.withAlpha(0.7f);
            }
            else
            {
                // No content - grayed out
                bgColour = juce::Colour::fromString("ff1a1a1a");     // Dark gray
                borderColour = juce::Colours::grey.withAlpha(0.3f);
                textColour = juce::Colours::grey.withAlpha(0.6f);
            }
            break;

        case State::Ready:
            bgColour = juce::Colour::fromString("ff1a2e1a");     // Dark green-ish when ready
            borderColour = juce::Colour::fromString("ff00FF88").withAlpha(0.6f);  // Green accent
            textColour = juce::Colours::white.withAlpha(0.9f);
            break;

        case State::Preparing:
            bgColour = juce::Colour::fromString("ff1a1a2e");     // Dark blue-ish during prep
            borderColour = juce::Colour::fromString("ff00FFFF").withAlpha(0.6f);  // Cyan during loading
            textColour = juce::Colours::white.withAlpha(0.9f);
            break;
    }

    // Hover brightening (only when can render or ready)
    if (isHovered_ && (state_ == State::Ready || canDoRender))
        bgColour = bgColour.brighter(0.15f);

    // Background
    g.setColour(bgColour);
    g.fillRoundedRectangle(bounds, 4.0f);

    // Progress bar overlay (when preparing)
    if (state_ == State::Preparing)
    {
        drawProgressBar(g, bounds);
    }

    // Border
    g.setColour(borderColour);
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

    // Layout: icon on left, text on right
    auto iconBounds = bounds.removeFromLeft(bounds.getHeight()).reduced(6.0f);
    auto textBounds = bounds.reduced(4.0f, 0.0f);

    // Draw wave icon
    drawWaveIcon(g, iconBounds);

    // Draw label
    g.setColour(textColour);
    g.setFont(juce::Font(11.0f));

    juce::String labelText;
    switch (state_)
    {
        case State::Idle:
            labelText = "Drag all waves";
            break;
        case State::Ready:
            labelText = "Drag all waves";
            break;
        case State::Preparing:
            labelText = "Rendering...";
            break;
    }

    g.drawText(labelText, textBounds, juce::Justification::centredLeft, true);
}

void WaveDragArea::drawWaveIcon(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    // Draw a simple waveform icon
    auto centre = bounds.getCentre();
    float w = bounds.getWidth() * 0.8f;
    float h = bounds.getHeight() * 0.6f;

    juce::Colour iconColour;
    bool canDoRender = canRender();

    switch (state_)
    {
        case State::Idle:
            iconColour = canDoRender
                ? juce::Colour::fromString("ff00FFFF").withAlpha(0.6f)  // Dim cyan when available
                : juce::Colours::grey.withAlpha(0.5f);
            break;
        case State::Ready:
            iconColour = juce::Colour::fromString("ff00FF88");  // Green
            break;
        case State::Preparing:
            iconColour = juce::Colour::fromString("ff00FFFF");  // Cyan
            break;
    }

    g.setColour(iconColour);

    // Draw simple sine-wave-like waveform
    juce::Path wavePath;
    float startX = centre.x - w / 2.0f;
    float startY = centre.y;

    wavePath.startNewSubPath(startX, startY);

    int segments = 12;
    for (int i = 1; i <= segments; ++i)
    {
        float t = (float)i / (float)segments;
        float x = startX + t * w;
        float amplitude = h / 2.0f * std::sin(t * juce::MathConstants<float>::pi);
        float y = centre.y + amplitude * std::sin(t * juce::MathConstants<float>::twoPi * 2.0f);
        wavePath.lineTo(x, y);
    }

    g.strokePath(wavePath, juce::PathStrokeType(1.5f));
}

void WaveDragArea::drawProgressBar(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    float currentProgress = progress_.load();

    // Progress bar fills from left to right inside the button
    auto progressBounds = bounds.reduced(1.0f);
    progressBounds.setWidth(progressBounds.getWidth() * currentProgress);

    // Semi-transparent overlay
    g.setColour(juce::Colour::fromString("ff00FFFF").withAlpha(0.25f));
    g.fillRoundedRectangle(progressBounds, 3.0f);

    // Progress bar edge glow
    if (currentProgress > 0.01f && currentProgress < 0.99f)
    {
        g.setColour(juce::Colour::fromString("ff00FFFF").withAlpha(0.7f));
        g.drawLine(progressBounds.getRight(), progressBounds.getY() + 2.0f,
                   progressBounds.getRight(), progressBounds.getBottom() - 2.0f, 2.0f);
    }
}

void WaveDragArea::resized()
{
    // No child components to layout
}

void WaveDragArea::mouseEnter(const juce::MouseEvent&)
{
    isHovered_ = true;
    repaint();
}

void WaveDragArea::mouseExit(const juce::MouseEvent&)
{
    isHovered_ = false;
    repaint();
}

void WaveDragArea::mouseDown(const juce::MouseEvent& e)
{
    juce::Logger::writeToLog("[WaveDragArea] mouseDown - state: " + juce::String((int)state_) +
                             ", canRender: " + juce::String(canRender() ? "true" : "false"));

    isDragging_ = false;

    // Right-click: show context menu
    if (e.mods.isPopupMenu())
    {
        if (state_ == State::Ready && hasValidFile_)
        {
            showContextMenu();
        }
        return;
    }

    // Left-click: start rendering if we can
    if (state_ == State::Idle && canRender())
    {
        juce::Logger::writeToLog("[WaveDragArea] Starting render...");
        startPreparing();
    }
}

void WaveDragArea::mouseUp(const juce::MouseEvent&)
{
    // Nothing special on mouse up
}

void WaveDragArea::mouseDrag(const juce::MouseEvent& e)
{
    // Only allow drag when ready with a valid file
    if (state_ != State::Ready || !hasValidFile_ || wavFilePath_.isEmpty())
        return;

    // Only start drag after some movement
    if (!isDragging_ && e.getDistanceFromDragStart() > 4)
    {
        isDragging_ = true;

        juce::File wavFile(wavFilePath_);
        if (wavFile.existsAsFile())
        {
            juce::Logger::writeToLog("[WaveDragArea] Starting external drag: " + wavFilePath_);

            juce::StringArray files;
            files.add(wavFilePath_);

            bool canMove = false;  // Copy, don't move
            juce::DragAndDropContainer::performExternalDragDropOfFiles(files, canMove);

            juce::Logger::writeToLog("[WaveDragArea] External drag completed");
        }
        else
        {
            juce::Logger::writeToLog("[WaveDragArea] WAV file not found: " + wavFilePath_);
        }
    }
}

bool WaveDragArea::canRender() const
{
    bool result = false;
    bool hasCallback = (onCanRender != nullptr);

    if (onCanRender)
        result = onCanRender();
    else
        result = hasContent_;

    // Debug logging (throttled)
    static juce::int64 lastLogTime = 0;
    juce::int64 now = juce::Time::currentTimeMillis();
    if ((now - lastLogTime) > 1000)
    {
        lastLogTime = now;
        juce::Logger::writeToLog("[WaveDragArea] canRender: hasCallback=" +
            juce::String(hasCallback ? "YES" : "NO") +
            ", hasContent_=" + juce::String(hasContent_ ? "YES" : "NO") +
            ", result=" + juce::String(result ? "YES" : "NO"));
    }

    return result;
}

bool WaveDragArea::isReadyToDrag() const
{
    return state_ == State::Ready && hasValidFile_;
}

void WaveDragArea::invalidate()
{
    juce::Logger::writeToLog("[WaveDragArea] invalidate - clearing prepared file");

    // Don't invalidate while preparing
    if (state_ == State::Preparing)
        return;

    hasValidFile_ = false;
    wavFilePath_.clear();
    renderedBuffer_.reset();

    // Reset to idle
    state_ = State::Idle;
    repaint();
}

void WaveDragArea::setHasContent(bool hasContent)
{
    juce::Logger::writeToLog("[WaveDragArea] setHasContent: " + juce::String(hasContent ? "true" : "false"));
    hasContent_ = hasContent;
    repaint();
}

void WaveDragArea::startPreparing()
{
    if (state_ == State::Preparing)
    {
        juce::Logger::writeToLog("[WaveDragArea] Already preparing, ignoring");
        return;
    }

    if (!onRenderPattern)
    {
        juce::Logger::writeToLog("[WaveDragArea] No render callback set!");
        return;
    }

    state_ = State::Preparing;
    progress_ = 0.0f;
    startTimer(50);  // 20fps progress updates
    repaint();

    // Start rendering in background thread
    prepareThread_ = std::make_unique<std::thread>([this]() {
        renderAndPrepareWavFile();
    });
}

void WaveDragArea::renderAndPrepareWavFile()
{
    juce::Logger::writeToLog("[WaveDragArea] === renderAndPrepareWavFile START ===");

    // Progress callback for the renderer
    auto progressCallback = [this](float p) {
        progress_ = p * 0.8f;  // Rendering is 0-80%, file writing is 80-100%
    };

    // Call the render callback
    double outSampleRate = 44100.0;
    auto buffer = onRenderPattern(outSampleRate, progressCallback);

    if (!buffer || buffer->getNumSamples() == 0)
    {
        juce::Logger::writeToLog("[WaveDragArea] ERROR: Render returned empty buffer");
        juce::MessageManager::callAsync([this]() {
            onPrepareComplete(false, {});
        });
        return;
    }

    juce::Logger::writeToLog("[WaveDragArea] Render complete: " +
                             juce::String(buffer->getNumSamples()) + " samples, " +
                             juce::String(buffer->getNumChannels()) + " channels, " +
                             juce::String(outSampleRate) + " Hz");

    progress_ = 0.8f;

    // Create temp file
    juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    juce::File wavFile = tempDir.getChildFile("SAMPLAR_pattern_" +
        juce::String(juce::Time::currentTimeMillis()) + ".wav");

    juce::Logger::writeToLog("[WaveDragArea] Writing to: " + wavFile.getFullPathName());

    // Create WAV writer
    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wavFormat.createWriterFor(
            new juce::FileOutputStream(wavFile),
            outSampleRate,
            (unsigned int)buffer->getNumChannels(),
            16,  // bits per sample
            {},  // metadata
            0    // quality
        )
    );

    if (!writer)
    {
        juce::Logger::writeToLog("[WaveDragArea] ERROR: Failed to create WAV writer");
        juce::MessageManager::callAsync([this]() {
            onPrepareComplete(false, {});
        });
        return;
    }

    progress_ = 0.85f;

    // Write audio data
    if (!writer->writeFromAudioSampleBuffer(*buffer, 0, buffer->getNumSamples()))
    {
        juce::Logger::writeToLog("[WaveDragArea] ERROR: Write failed");
        juce::MessageManager::callAsync([this]() {
            onPrepareComplete(false, {});
        });
        return;
    }

    // Finalize
    writer.reset();  // Flush and close
    progress_ = 1.0f;

    juce::Logger::writeToLog("[WaveDragArea] === renderAndPrepareWavFile COMPLETE ===");
    juce::Logger::writeToLog("[WaveDragArea] File size: " +
        juce::String(wavFile.getSize() / 1024) + " KB");

    // Store the rendered buffer
    sampleRate_ = outSampleRate;

    juce::String filePath = wavFile.getFullPathName();
    juce::MessageManager::callAsync([this, filePath, buf = std::move(buffer)]() mutable {
        renderedBuffer_ = std::move(buf);
        onPrepareComplete(true, filePath);
    });
}

void WaveDragArea::onPrepareComplete(bool success, const juce::String& filePath)
{
    juce::Logger::writeToLog("[WaveDragArea] onPrepareComplete - success: " +
        juce::String(success ? "true" : "false"));

    stopTimer();

    // Clean up thread
    if (prepareThread_ && prepareThread_->joinable())
    {
        prepareThread_->join();
    }
    prepareThread_.reset();

    if (success)
    {
        wavFilePath_ = filePath;
        hasValidFile_ = juce::File(filePath).existsAsFile();
        state_ = State::Ready;

        juce::Logger::writeToLog("[WaveDragArea] File ready for drag: " + wavFilePath_);
    }
    else
    {
        wavFilePath_.clear();
        hasValidFile_ = false;
        state_ = State::Idle;

        juce::Logger::writeToLog("[WaveDragArea] Render/preparation failed");
    }

    progress_ = 0.0f;
    repaint();
}

void WaveDragArea::timerCallback()
{
    // Repaint to update progress bar
    repaint();
}

void WaveDragArea::showContextMenu()
{
    juce::Logger::writeToLog("[WaveDragArea] showContextMenu");

    juce::PopupMenu menu;
    menu.addItem(1, "Save As...");
    menu.addSeparator();
    menu.addItem(2, "Re-render", canRender());

    menu.showMenuAsync(juce::PopupMenu::Options(),
        [this](int result)
        {
            if (result == 1)
            {
                saveAs();
            }
            else if (result == 2)
            {
                invalidate();
                if (canRender())
                    startPreparing();
            }
        });
}

void WaveDragArea::saveAs()
{
    juce::Logger::writeToLog("[WaveDragArea] saveAs");

    if (!hasValidFile_ || wavFilePath_.isEmpty())
    {
        juce::Logger::writeToLog("[WaveDragArea] No file to save");
        return;
    }

    juce::File sourceFile(wavFilePath_);
    if (!sourceFile.existsAsFile())
    {
        juce::Logger::writeToLog("[WaveDragArea] Source file not found: " + wavFilePath_);
        return;
    }

    auto chooser = std::make_shared<juce::FileChooser>(
        "Save Pattern as Wave",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile("pattern_render.wav"),
        "*.wav"
    );

    chooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser, sourceFile](const juce::FileChooser& fc)
        {
            auto result = fc.getResult();
            if (result != juce::File())
            {
                juce::Logger::writeToLog("[WaveDragArea] Saving to: " + result.getFullPathName());

                // Ensure .wav extension
                juce::File destFile = result;
                if (!destFile.hasFileExtension(".wav"))
                    destFile = destFile.withFileExtension(".wav");

                if (sourceFile.copyFileTo(destFile))
                {
                    juce::Logger::writeToLog("[WaveDragArea] Save successful");
                }
                else
                {
                    juce::Logger::writeToLog("[WaveDragArea] Save failed!");
                }
            }
        });
}

} // namespace samplar
