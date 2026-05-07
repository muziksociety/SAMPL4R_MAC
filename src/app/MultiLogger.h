#pragma once

#include <juce_core/juce_core.h>
#include "../ui/Debug/DebugConsole.h"

namespace samplar {

// Logger that mirrors output to both DebugConsole and a file (overwritten each run)
class MultiLogger : public juce::Logger {
public:
    MultiLogger(DebugConsole& console, const juce::File& logFile)
        : console_(console)
    {
        // Ensure logs directory exists
        auto logDir = logFile.getParentDirectory();
        if (! logDir.exists()) (void) logDir.createDirectory();
        // CRITICAL: Use -1 to preserve ALL existing log content from earlyLogger
        // Using 0 would truncate the file and lose early startup logs (including classification loading)
        fileLogger_.reset(new juce::FileLogger(logFile, "", -1));
    }

    void logMessage(const juce::String& message) override
    {
        // Mirror to file and on-screen console; safe from any thread
        if (fileLogger_) fileLogger_->logMessage(message);
        console_.appendLine(message);
    }

private:
    DebugConsole& console_;
    std::unique_ptr<juce::FileLogger> fileLogger_;
};

} // namespace samplar

