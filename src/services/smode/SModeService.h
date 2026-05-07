#pragma once

#include "../../app/Service.h"
#include "../../app/EventBus.h"
#include "SModeSegmentModel.h"
#include "SModeSeparationService.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace samplar {

/**
 * SModeService — top-level orchestrator for S-Mode.
 * Coordinates separation, segment model, and MIDI playback.
 * Registered as "smode" in ServiceRegistry.
 */
class SModeService : public IService
{
public:
    explicit SModeService(EventBus& bus);

    void initialise() override;
    void shutdown() override;

    // ── Access ───────────────────────────────────────────────────────
    SModeSegmentModel& getModel() { return model_; }
    const SModeSegmentModel& getModel() const { return model_; }

    SModeSeparationService& getSeparation() { return separation_; }
    const SModeSeparationService& getSeparation() const { return separation_; }

    // ── Workflow ─────────────────────────────────────────────────────
    /** Start the S-Mode workflow: separate stems, then load into model. */
    void startWorkflow(const juce::File& sourceFile,
                       SModeSeparationService::StemCount stemCount);

    /** Load separated stem audio files into the model. */
    void loadStemAudio(const std::vector<juce::File>& stemFiles);

    /** Is S-Mode active (stems loaded)? */
    bool isActive() const { return active_.load(); }

    /** Fully reset S-Mode state (model, active flag, source file). */
    void reset();

    /** Get/set the source file being processed. */
    const juce::File& getSourceFile() const { return sourceFile_; }
    void setSourceFile(const juce::File& f) { sourceFile_ = f; }

private:
    EventBus& bus_;
    SModeSegmentModel model_;
    SModeSeparationService separation_;
    juce::File sourceFile_;
    std::atomic<bool> active_ { false };

    // Event subscriptions
    EventBus::HandlerId separationCompleteSub_ { 0 };
    EventBus::HandlerId separationErrorSub_ { 0 };

    /** Called when separation completes — loads stems into model. */
    void onSeparationComplete(const juce::var& stemPaths);

    /** Called on separation error. */
    void onSeparationError(const juce::var& errorMsg);

    /** Load a single audio file into an AudioBuffer. */
    static juce::AudioBuffer<float> loadAudioFile(const juce::File& file, double& outSampleRate);
};

} // namespace samplar
