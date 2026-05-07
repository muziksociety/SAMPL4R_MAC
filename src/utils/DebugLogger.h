/*
  ==============================================================================
    DebugLogger.h

    LOCK-FREE debug logging system for ReVibe+
    Uses JUCE's AbstractFifo - safe to call from audio thread!

  ==============================================================================
*/

#pragma once

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <array>

//==============================================================================
/**
    Lock-free debug logger using AbstractFifo
    ✅ SAFE TO CALL FROM AUDIO THREAD!
    ✅ NO STRING ALLOCATION
    ✅ NO BLOCKING
*/
class DebugLogger
{
public:
    struct LogEntry
    {
        juce::int64 timestamp; // not set on audio thread
        char category[16];
        char message[256];

        LogEntry() : timestamp(0)
        {
            category[0] = '\0';
            message[0] = '\0';
        }
    };

    static DebugLogger& getInstance()
    {
        static DebugLogger instance;
        return instance;
    }

    // ✅ LOCK-FREE - Safe to call from audio thread!
    void log(const char* category, const char* message)
    {
        int start1, size1, start2, size2;
        fifo.prepareToWrite(1, start1, size1, start2, size2);

        if (size1 > 0)
        {
            auto& entry = buffer[start1];
            // Do NOT call system time on audio thread. Timestamp added on read.
            entry.timestamp = 0;

            // Safe string copy - no allocation
            std::strncpy(entry.category, category, sizeof(entry.category) - 1);
            entry.category[sizeof(entry.category) - 1] = '\0';

            std::strncpy(entry.message, message, sizeof(entry.message) - 1);
            entry.message[sizeof(entry.message) - 1] = '\0';

            fifo.finishedWrite(size1);
        }
        // If buffer is full, silently drop the message (no blocking!)
    }

    // ✅ LOCK-FREE - Safe to call from audio thread!
    void logValue(const char* category, const char* name, float value)
    {
        char msg[256];
        std::snprintf(msg, sizeof(msg), "%s = %.6f", name, value);
        log(category, msg);
    }

    // ✅ LOCK-FREE - Safe to call from audio thread!
    void logValue(const char* category, const char* name, int value)
    {
        char msg[256];
        std::snprintf(msg, sizeof(msg), "%s = %d", name, value);
        log(category, msg);
    }

    // ✅ LOCK-FREE - Safe to call from audio thread!
    void logValue(const char* category, const char* name, bool value)
    {
        char msg[256];
        std::snprintf(msg, sizeof(msg), "%s = %s", name, value ? "TRUE" : "FALSE");
        log(category, msg);
    }

    // Read up to maxCount logs (GUI thread only). Formats timestamp on read.
    juce::String getLogsBatch(int maxCount)
    {
        juce::String result;

        const int available = fifo.getNumReady();
        const int toRead = juce::jmin(maxCount, available);
        if (toRead <= 0) return result;

        int start1, size1, start2, size2;
        fifo.prepareToRead(toRead, start1, size1, start2, size2);

        auto processEntries = [&](int start, int size)
        {
            for (int i = 0; i < size; ++i)
            {
                const auto& entry = buffer[(start + i) % bufferSize];

                // Timestamp now (on GUI thread)
                auto now = juce::Time::getCurrentTime();
                auto timestamp = juce::String::formatted("[%02d:%02d:%02d.%03d]",
                                                         now.getHours(),
                                                         now.getMinutes(),
                                                         now.getSeconds(),
                                                         now.getMilliseconds());

                result += timestamp + " [" + juce::String(entry.category) + "] " +
                          juce::String(entry.message) + "\n";
            }
        };

        if (size1 > 0) processEntries(start1, size1);
        if (size2 > 0) processEntries(start2, size2);

        fifo.finishedRead(size1 + size2);
        return result;
    }

    void clear()
    {
        int start1, size1, start2, size2;
        fifo.prepareToRead(fifo.getNumReady(), start1, size1, start2, size2);
        fifo.finishedRead(size1 + size2);
    }

    int getLogCount() const { return fifo.getNumReady(); }

private:
    DebugLogger() : fifo(bufferSize) {}
    ~DebugLogger() = default;

    static constexpr int bufferSize = 8192;  // Power of 2 for AbstractFifo
    std::array<LogEntry, bufferSize> buffer;
    juce::AbstractFifo fifo;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DebugLogger)
};

//==============================================================================
/**
    Content component for debug log viewer
*/
class DebugLogContent : public juce::Component, public juce::Timer
{
public:
    DebugLogContent()
    {
        // Text editor for logs
        logDisplay.setMultiLine(true);
        logDisplay.setReadOnly(true);
        logDisplay.setScrollbarsShown(true);
        logDisplay.setCaretVisible(false);
        logDisplay.setPopupMenuEnabled(true);
        logDisplay.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
        logDisplay.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff1a1a1a));
        logDisplay.setColour(juce::TextEditor::textColourId, juce::Colours::lightgreen);
        addAndMakeVisible(logDisplay);

        // Clear button
        clearButton.setButtonText("Clear Logs");
        clearButton.onClick = [this]()
        {
            DebugLogger::getInstance().clear();
            updateLogs();
        };
        addAndMakeVisible(clearButton);

        // Auto-scroll checkbox
        autoScrollToggle.setButtonText("Auto-scroll");
        autoScrollToggle.setToggleState(true, juce::dontSendNotification);
        addAndMakeVisible(autoScrollToggle);

        // Start timer to update logs
        startTimer(100); // 10 Hz refresh
    }

    ~DebugLogContent() override
    {
        stopTimer();
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(10);

        auto buttonArea = bounds.removeFromTop(30);
        clearButton.setBounds(buttonArea.removeFromLeft(100));
        buttonArea.removeFromLeft(10);
        autoScrollToggle.setBounds(buttonArea.removeFromLeft(100));

        bounds.removeFromTop(10);
        logDisplay.setBounds(bounds);
    }

    void timerCallback() override
    {
        updateLogs();
    }
    
    void updateLogs()
    {
        // Drain logs in bounded batches to avoid UI spikes
        juce::String batch = DebugLogger::getInstance().getLogsBatch(300);
        if (batch.isNotEmpty())
        {
            // Append instead of replacing the entire text
            logDisplay.moveCaretToEnd();
            logDisplay.insertTextAtCaret(batch);

            // Trim if too large (keep last ~150k chars)
            constexpr int maxChars = 200000;
            constexpr int keepChars = 150000;
            const int len = logDisplay.getText().length();
            if (len > maxChars)
            {
                auto txt = logDisplay.getText();
                logDisplay.setText(txt.substring(len - keepChars), false);
                logDisplay.moveCaretToEnd();
            }

            // Auto-scroll to bottom if enabled
            if (autoScrollToggle.getToggleState())
                logDisplay.moveCaretToEnd();
        }
    }

private:
    juce::TextEditor logDisplay;
    juce::TextButton clearButton;
    juce::ToggleButton autoScrollToggle;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DebugLogContent)
};

//==============================================================================
/**
    Debug log viewer window
*/
class DebugLogViewer : public juce::DocumentWindow
{
public:
    DebugLogViewer()
        : DocumentWindow("ReVibe+ Debug Log",
                        juce::Colours::darkgrey,
                        DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setResizable(true, true);

        setContentOwned(new DebugLogContent(), true);

        centreWithSize(800, 600);
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DebugLogViewer)
};

//==============================================================================
// Convenience macros for logging
// ✅ SAFE TO USE FROM AUDIO THREAD!
#define LOG_DEBUG(category, message) DebugLogger::getInstance().log(category, message)
#define LOG_VALUE(category, name, value) DebugLogger::getInstance().logValue(category, name, value)

