#pragma once

#include "../../app/Service.h"
#include "../../app/EventBus.h"
#include "../FullSongService.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <functional>

namespace samplar {

/**
 * SModeSeparationService — wraps FullSongService to produce ALL stems
 * instead of just drums. Supports 4-stem and 6-stem models.
 */
class SModeSeparationService : public IService
{
public:
    explicit SModeSeparationService(EventBus& bus);

    void initialise() override;
    void shutdown() override;

    // ── Configuration ────────────────────────────────────────────────
    enum class StemCount { Four = 4, Six = 6 };

    void setStemCount(StemCount count) { stemCount_ = count; }
    StemCount getStemCount() const { return stemCount_; }

    /** Get stem names for the current model configuration. */
    juce::StringArray getStemNames() const;

    // ── Separation ───────────────────────────────────────────────────

    /**
     * Start async separation. Publishes events:
     *   "smode.separation.progress" (juce::var with 0-100)
     *   "smode.separation.complete" (juce::var with stem file paths)
     *   "smode.separation.error"    (juce::var with error message)
     */
    void startSeparation(const juce::File& sourceFile);

    /** Cancel in-progress separation. */
    void cancelSeparation();

    /** Are we currently separating? */
    bool isSeparating() const { return separating_.load(); }

    /** Get the separated stem audio files (valid after separation.complete). */
    const std::vector<juce::File>& getStemFiles() const { return stemFiles_; }

    /** Get the source file that was separated. */
    const juce::File& getSourceFile() const { return sourceFile_; }

private:
    EventBus& bus_;
    StemCount stemCount_ { StemCount::Four };
    std::atomic<bool> separating_ { false };
    std::atomic<bool> cancelRequested_ { false };

    juce::File sourceFile_;
    std::vector<juce::File> stemFiles_;

    std::unique_ptr<std::thread> separationThread_;

    /** Get the demucs model name for the current stem count. */
    juce::String getModelName() const;

    /** Find stem output files after separation completes. */
    std::vector<juce::File> findStemOutputFiles(const juce::File& outputDir) const;
};

} // namespace samplar
