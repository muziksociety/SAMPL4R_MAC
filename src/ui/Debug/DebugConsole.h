#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <mutex>
#include "../../security/LicenseCleanerUtility.h"

namespace samplar {

// Lightweight console panel that captures juce::Logger output
class DebugConsole : public juce::Component,
                     private juce::AsyncUpdater
{
public:
    DebugConsole();
    ~DebugConsole() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& e) override;

    // Append a line from any thread
    void appendLine(juce::String text);

    // Clear contents
    void clear();

    void setWordWrap(bool shouldWrap);
    void setAutoScroll(bool enabled);
    bool getWordWrap() const;
    bool getAutoScroll() const;

private:
    void handleAsyncUpdate() override;

    juce::CodeDocument document_;
    std::unique_ptr<juce::CodeEditorComponent> output_;
    juce::TextButton clearButton_{ "Clear" };

    std::mutex queueMutex_;
    juce::StringArray pending_;
    double lastUpdateTime_ { 0.0 };

    bool autoscroll_ { false }; // Disabled by default for performance
    bool wrap_ { true };
};

// Logger that forwards messages to a DebugConsole instance
class ConsoleLogger : public juce::Logger
{
public:
    explicit ConsoleLogger(DebugConsole& console) : console_(console) {}
    void logMessage(const juce::String& message) override { console_.appendLine(message); }
private:
    DebugConsole& console_;
};

} // namespace samplar
