#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <memory>
#include "EventBus.h"

namespace samplar {

class ServiceRegistry; // fwd

class App : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "SAMPLAR"; }
    const juce::String getApplicationVersion() override;
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String& commandLine) override;
    void shutdown() override;
    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const juce::String& /*cmd*/) override {}

    samplar::ServiceRegistry& services() { return *serviceRegistry; }
    samplar::EventBus& bus() { return eventBus_; }

private:
    class MainWindow;
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<samplar::ServiceRegistry> serviceRegistry;
    samplar::EventBus eventBus_;
    std::unique_ptr<juce::FileLogger> earlyLogger_;
};

} // namespace samplar
