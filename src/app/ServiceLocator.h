#pragma once

#include <juce_core/juce_core.h>

namespace samplar {

class ServiceRegistry;
class EventBus;
class AppState;

class ServiceLocator
{
public:
    // Opaque UI state pointer — used by RootComponent to persist state across editor recreations.
    // Points to SamplarAudioProcessor::UIState when running as plugin; nullptr in standalone.
    struct UIState {
        int currentPage { 0 };
        int previousPage { 0 };
        juce::String loadedFolder;
        juce::String loadedFiles;   // Semicolon-separated
        bool libraryMode { false };
        juce::String libraryFolder; // Active folder in drum library mode
        bool hasSavedState { false };

        // -- DAW state restore (set by setStateInformation, consumed by state.restored handler)
        // These are separate from the live fields above because switchTo() overwrites
        // the live fields. The state.restored handler reads ONLY from these pending fields.
        bool pendingDAWRestore { false };       // True = setStateInformation has data to consume
        int pendingPage { 0 };
        juce::String pendingFolder;
        bool pendingLibraryMode { false };
        juce::String pendingLibraryFolder;

        // S-Mode state restoration
        bool pendingSModeRestore { false };
        juce::File smodeSourceFile;
        int smodeStemCount { 4 };
        double smodeBpm { 0.0 };
        double smodeSampleRate { 44100.0 };
        juce::var smodeSegmentData;
        std::vector<juce::File> smodeStemFiles;  // Cached stem WAV paths for fast restore
    };

    static void initialise(ServiceRegistry* registry, EventBus* bus)
    {
        sRegistry = registry;
        sBus = bus;
        juce::Logger::writeToLog("[ServiceLocator] Initialised with registry=" +
            juce::String(registry != nullptr ? "valid" : "null") +
            " bus=" + juce::String(bus != nullptr ? "valid" : "null"));
    }

    static void setUIState(UIState* state) { sUIState = state; }
    static UIState* getUIState() { return sUIState; }

    static void      setAppState(AppState* s) { sAppState = s; }
    static AppState* getAppState()            { return sAppState; }

    static void shutdown()
    {
        juce::Logger::writeToLog("[ServiceLocator] Shutdown");
        sRegistry = nullptr;
        sBus = nullptr;
    }

    static ServiceRegistry* getRegistry()    { return sRegistry; }
    static EventBus*        getBus()         { return sBus; }

    static bool isAvailable() { return sRegistry != nullptr; }

    static void requestQuit()
    {
        if (auto* app = juce::JUCEApplication::getInstance())
            app->systemRequestedQuit();
    }

    static juce::String getAppVersion()
    {
        if (auto* app = juce::JUCEApplication::getInstance())
            return app->getApplicationVersion();
        return "unknown";
    }

private:
    static inline ServiceRegistry* sRegistry = nullptr;
    static inline EventBus*        sBus      = nullptr;
    static inline UIState*         sUIState  = nullptr;
    static inline AppState*        sAppState = nullptr;
};

} // namespace samplar
