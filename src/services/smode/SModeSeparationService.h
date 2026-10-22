#pragma once

#include "../../app/Service.h"
#include "../../app/EventBus.h"
#include "../FullSongService.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <functional>
#include <atomic>
#include <memory>
#include <thread>

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
    enum class StemCount { Four = 4 };   // htdemucs.onnx is 4-stem only

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

    /** Last published "smode.separation.progress" value (0-100) of the current
     *  or last run. Lets a reopened editor replay the progress bars (B10). */
    int getProgressPercent() const { return progressPercent_.load(); }

    /** Get the separated stem audio files (valid after separation.complete). */
    const std::vector<juce::File>& getStemFiles() const { return stemFiles_; }

    /** Get the source file that was separated. */
    const juce::File& getSourceFile() const { return sourceFile_; }

private:
    EventBus& bus_;
    StemCount stemCount_ { StemCount::Four };
    std::atomic<bool> separating_ { false };
    std::atomic<bool> cancelRequested_ { false };
    std::atomic<int>  progressPercent_ { 0 };

    juce::File sourceFile_;
    std::vector<juce::File> stemFiles_;

    // Worker is JOINABLE (never detached): shutdown() cancels and joins it so
    // the plugin can be removed mid-separation without a use-after-free.
    std::unique_ptr<std::thread> separationThread_;

    // Liveness token for MessageManager::callAsync lambdas. Flipped to false in
    // shutdown(); queued callbacks check it before touching `this`.
    std::shared_ptr<std::atomic<bool>> alive_ { std::make_shared<std::atomic<bool>>(true) };

    /** Join a finished/cancelled worker before starting a new one. */
    void joinWorker();

    /** Get the demucs model name for the current stem count. */
    juce::String getModelName() const;

    /** Exact stem paths in getStemNames() order; empty if any stem file is missing. */
    std::vector<juce::File> orderedStemFiles(const FullSongService::StemFiles& stems) const;
};

} // namespace samplar
