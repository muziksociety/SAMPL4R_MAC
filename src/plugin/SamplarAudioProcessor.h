#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../app/EventBus.h"
#include "../app/ServiceRegistry.h"
#include "../app/ServiceLocator.h"
#include "../app/AppState.h"

namespace samplar {

class MidiMappingService;
class AsyncFileLogger;
class SModeService;   // forward-declare � avoid heavy include in header

/**
 * SamplarAudioProcessor � Heart of the hybrid Standalone + VST3 build for SAMPLAR.
 *
 * Owns the ServiceRegistry, EventBus, and audio routing.
 * In standalone mode JUCE wraps this in StandaloneFilterWindow.
 * In VST3 mode the DAW host drives processBlock().
 */
class SamplarAudioProcessor : public juce::AudioProcessor
{
public:
    SamplarAudioProcessor();
    ~SamplarAudioProcessor() override;

    // === AudioProcessor interface ===
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    /** Host bypass: voices fade out once, then silence (A26). */
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // === Plugin metadata ===
    const juce::String getName() const override { return "SAMPLAR"; }
    bool acceptsMidi()  const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    /** Longest chop + pitch-shifter flush (A26). */
    double getTailLengthSeconds() const override;

    // === Programs (presets) ===
    int  getNumPrograms()    override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    // === State save / restore ===
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // === Editor ===
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    // === Bus layout ===
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    // === SAMPLAR accessors (used by Editor) ===
    ServiceRegistry& getServiceRegistry() { return *serviceRegistry_; }

    /** Point the process-global ServiceLocator (registry, bus, UI state,
     *  AppState) at THIS instance. Called on construction, by the editor
     *  before it builds / when it is shown, and on a survivor when the owning
     *  instance unloads (A6). Message thread. */
    void claimServiceLocator();
    EventBus&        getEventBus()        { return eventBus_; }

    // === UI state persistence (survives editor close/reopen) ===
    ServiceLocator::UIState& getUIState() { return uiState_; }

    // === Centralized plugin state (see docs/.../2026-04-21-appstate-full-plan.md) ===
    AppState& getAppState() { return *appState_; }

    // === Runtime mode ===
    bool isRunningAsPlugin() const;
    bool isRunningStandalone() const;

private:
    // Core framework � mirrors what App owned before
    EventBus eventBus_;
    std::unique_ptr<ServiceRegistry> serviceRegistry_;
    // Process-wide log file shared by every SAMPLAR instance in the host
    // (see acquireSharedLogger in the .cpp). The last instance to go away
    // clears the global juce::Logger.
    std::shared_ptr<AsyncFileLogger> earlyLogger_;   // shared by all instances (AsyncFileLogger.h)

    // Phase 2: MIDI input service (raw pointer � owned by serviceRegistry_)
    MidiMappingService*    midiMapping_    { nullptr };
    // LicenseSystemService audio gate (flag owned by serviceRegistry_).
    const std::atomic<bool>* licenceAudioAllowed_ { nullptr };

    bool servicesInitialised_ { false };
    double lastHostBpm_ { 0.0 };  // For DAW tempo sync (avoid flooding EventBus)
    std::atomic<bool> tempoReRequestPending_ { false };  // UI requests tempo re-publish
    ServiceLocator::UIState uiState_;

    // Centralized state store. Constructed in ctor, destroyed with processor.
    // Phase 2: get/setStateInformation delegate to appState_->serialize/deserialize.
    std::unique_ptr<AppState> appState_;

    // A8: liveness token for MessageManager::callAsync lambdas posted by the
    // processor (host tempo, state restore). Flipped false first thing in the
    // destructor; lambdas check it before touching `this`.
    std::shared_ptr<std::atomic<bool>> aliveToken_ { std::make_shared<std::atomic<bool>>(true) };

    // Built-in bindings registered by the processor for ui/smode/midi sections.
    // Held here so they unregister cleanly in the processor destructor (RAII).
    std::vector<std::unique_ptr<ScopedStateBinding>> defaultBindings_;

    void registerDefaultBindings();

    // -- Project persistence (A4 / A16 / A17 / A18) ------------------------
    SModeService* smodeService() const;
    /** Fingerprint of the persisted S-Mode session (song, stems, chops, page state). */
    uint64_t computeStateFingerprint();
    /** Fingerprint of the persisted play settings (engine atomics: modes, stem pitch/mod, CC). */
    uint64_t computePlayFingerprint();
    /** Message-thread watcher: when the session changes, refresh the cached
     *  "smode" tree (for saves that arrive off the message thread) and tell
     *  the host the project is dirty. A finished project restore re-baselines
     *  instead (loading a project must not dirty it). */
    void pollStateChanges();
    struct StateWatcher : juce::Timer
    {
        explicit StateWatcher(SamplarAudioProcessor& o) : owner(o) {}
        void timerCallback() override { owner.pollStateChanges(); }
        SamplarAudioProcessor& owner;
    };
    StateWatcher stateWatcher_ { *this };
    uint64_t lastStateFingerprint_ { 0 };   // session part (re-baselined while a restore runs)
    uint64_t lastPlayFingerprint_ { 0 };    // play settings (user edits dirty even while restoring)
    uint64_t seenRestoreCompletions_ { 0 };
    uint64_t seenRelinks_ { 0 };            // SModeService::getRelinkCount (user relink = edit)
    bool     rebaselinePending_ { true };

    // -- Debug / test hook (VST3SmokeHost --session-roundtrip) -------------
    // SAMPLAR_DEBUG_SESSION_SEED=<song>: load it and seed a known session once
    // the stems are ready. SAMPLAR_DEBUG_SESSION_ACK=<dir>: write status files
    // there (seeded / ready / missing). Inert unless the variables are set.
    void startDebugSessionHookIfRequested();
    void seedDebugSession();
    void writeDebugAck(const juce::String& name, const juce::String& text) const;
    juce::File debugAckDir_;
    bool debugSeedPending_ { false };
    int  debugAckSeq_ { 0 };


    void initServices();
    void shutdownServices();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplarAudioProcessor)
};

} // namespace samplar
