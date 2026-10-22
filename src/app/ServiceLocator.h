#pragma once

#include <juce_core/juce_core.h>

namespace samplar {

class ServiceRegistry;
class EventBus;
class AppState;

class ServiceLocator
{
public:
    // Opaque UI state pointer � used by RootComponent to persist state across editor recreations.
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

        // S-Mode sessions are NOT here: the processor's "smode" AppState
        // binding restores them straight into SModeService (B9).
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

    // Force teardown - standalone (single instance, always safe).
    static void shutdown()
    {
        juce::Logger::writeToLog("[ServiceLocator] Shutdown (force)");
        sRegistry = nullptr;
        sBus      = nullptr;
        sUIState  = nullptr;   // were left dangling before (A6)
        sAppState = nullptr;
    }

    // Ownership-guarded teardown - plugin. A host can run N instances that all
    // share these process-global statics; only clear them if they still point
    // at THIS instance, so an unloading instance can't null (or leave dangling
    // UIState / AppState pointers for) another live instance. (SM5 86e3698a)
    static void shutdown(ServiceRegistry* owner)
    {
        if (sRegistry != owner)
        {
            juce::Logger::writeToLog("[ServiceLocator] Shutdown SKIPPED (not owner)");
            return;
        }
        juce::Logger::writeToLog("[ServiceLocator] Shutdown (owner)");
        sRegistry = nullptr;
        sBus      = nullptr;
        sUIState  = nullptr;
        sAppState = nullptr;
    }

    /** Point every global at ONE instance (registry, bus, UI state, AppState). */
    static void claim(ServiceRegistry* registry, EventBus* bus, UIState* uiState, AppState* appState)
    {
        sRegistry = registry;
        sBus      = bus;
        sUIState  = uiState;
        sAppState = appState;
    }

    static ServiceRegistry* getRegistry()    { return sRegistry; }
    static EventBus*        getBus()         { return sBus; }

    static bool isAvailable() { return sRegistry != nullptr; }

    /** The services of ONE plugin instance, captured when the UI is built (the
        editor claims the locator for its instance before creating any UI).
        UI code reads through its captured Context, so a callback of instance A
        that runs while instance B holds the process-wide claim still reaches
        A's registry / bus / state (SM5 pattern, review round 1 A2). A slot that
        was empty at capture falls back to the live global (e.g. a component
        built before the locator was set up). */
    struct Context
    {
        ServiceRegistry* registry = nullptr;
        EventBus*        bus      = nullptr;
        UIState*         uiState  = nullptr;
        AppState*        appState = nullptr;

        ServiceRegistry* getRegistry() const { return registry != nullptr ? registry : sRegistry; }
        EventBus*        getBus()      const { return bus      != nullptr ? bus      : sBus; }
        UIState*         getUIState()  const { return uiState  != nullptr ? uiState  : sUIState; }
        AppState*        getAppState() const { return appState != nullptr ? appState : sAppState; }
    };

    /** Snapshot of the current claim (message thread). */
    static Context capture() { return Context { sRegistry, sBus, sUIState, sAppState }; }

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
