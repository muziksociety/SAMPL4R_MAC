#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../app/EventBus.h"
#include "../app/ServiceRegistry.h"
#include "../app/ServiceLocator.h"
#include "../app/AppState.h"

namespace samplar {

class MidiMappingService;   // forward-declare — avoid heavy include in header
class PluginPreviewService; // forward-declare

/**
 * SamplarAudioProcessor — Heart of the hybrid Standalone + VST3 build for SAMPLAR.
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

    // === Plugin metadata ===
    const juce::String getName() const override { return "SAMPLAR"; }
    bool acceptsMidi()  const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

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
    EventBus&        getEventBus()        { return eventBus_; }

    // === UI state persistence (survives editor close/reopen) ===
    ServiceLocator::UIState& getUIState() { return uiState_; }

    // === Centralized plugin state (see docs/.../2026-04-21-appstate-full-plan.md) ===
    AppState& getAppState() { return *appState_; }

    // === Runtime mode ===
    bool isRunningAsPlugin() const;
    bool isRunningStandalone() const;

private:
    // Core framework — mirrors what App owned before
    EventBus eventBus_;
    std::unique_ptr<ServiceRegistry> serviceRegistry_;
    std::unique_ptr<juce::FileLogger> earlyLogger_;

    // Phase 2: MIDI input service (raw pointer — owned by serviceRegistry_)
    MidiMappingService*    midiMapping_    { nullptr };
    // Plugin preview audio (sample browser preview routed through processBlock)
    PluginPreviewService*  pluginPreview_  { nullptr };

    bool servicesInitialised_ { false };
    double lastHostBpm_ { 0.0 };  // For DAW tempo sync (avoid flooding EventBus)
    std::atomic<bool> tempoReRequestPending_ { false };  // UI requests tempo re-publish
    ServiceLocator::UIState uiState_;

    // Centralized state store. Constructed in ctor, destroyed with processor.
    // Phase 2: get/setStateInformation delegate to appState_->serialize/deserialize.
    std::unique_ptr<AppState> appState_;

    // Built-in bindings registered by the processor for ui/smode/midi sections.
    // Held here so they unregister cleanly in the processor destructor (RAII).
    std::vector<std::unique_ptr<ScopedStateBinding>> defaultBindings_;

    void registerDefaultBindings();

    // Pre-allocated buffer for mixing preview audio in processBlock (avoids heap alloc on audio thread)
    juce::AudioBuffer<float> previewBuffer_;

    void initServices();
    void shutdownServices();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplarAudioProcessor)
};

} // namespace samplar
