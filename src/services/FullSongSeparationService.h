#pragma once

#include "../app/Service.h"
#include "../app/EventBus.h"
#include "FullSongService.h"
#include <juce_core/juce_core.h>
#include <atomic>
#include <memory>
#include <thread>

namespace samplar {

/**
 * FullSongSeparationService — long-lived async wrapper around FullSongService
 * for the FullSong (drums-only) drop flow. Lives on the AudioProcessor's
 * ServiceRegistry so that separation work survives editor open/close cycles
 * (VST3 wrapper window can be closed while separation continues in the
 * background; reopening shows live progress + final result).
 *
 * Events published on the EventBus:
 *   "fullSong.separation.progress" → juce::var int 0..100
 *   "fullSong.separation.message"  → juce::var String (status line)
 *   "fullSong.separation.complete" → juce::var String (drum stem path)
 *   "fullSong.separation.error"    → juce::var String (error message)
 *   "fullSong.separation.cancelled"
 *
 * Thread is JOINABLE (not detached). cancelSeparation() requests cancel via
 * an atomic flag; shutdown() and the destructor join. This avoids the
 * use-after-free that the previous detached-thread pattern in MainWindow
 * (and SModeSeparationService) is vulnerable to.
 */
class FullSongSeparationService : public IService
{
public:
    enum class State : int { Idle = 0, Running = 1, Done = 2, Failed = 3, Cancelled = 4 };

    struct Result
    {
        juce::File sourceFile;
        juce::File drumStemFile;
        juce::String error;
        bool isValid() const { return drumStemFile.existsAsFile() && error.isEmpty(); }
    };

    explicit FullSongSeparationService(EventBus& bus);
    ~FullSongSeparationService() override;

    void initialise() override;
    void shutdown() override;

    /** Start an async drums-only separation. Returns false if one is already running. */
    bool startSeparation(const juce::File& sourceFile);

    /** Start an async drums-only separation across N files in sequence.
     *  Each file's progress is published through "fullSong.separation.progress".
     *  When all files are done (or the run is cancelled), publishes
     *  "fullSong.separation.batch.complete" with a juce::var array of drum
     *  stem paths.
     *  Returns false if a single-file or batch separation is already running. */
    bool startBatchSeparation(const std::vector<juce::File>& sources);

    bool isBatch()                   const noexcept { return isBatch_.load(); }
    int  getBatchFileIndex()         const noexcept { return batchIndex_.load(); }
    int  getBatchFileCount()         const noexcept { return batchCount_.load(); }
    bool hasUnconsumedBatchResult()  const noexcept;
    std::vector<juce::File> consumeBatchResult();
    std::vector<juce::File> getCurrentBatchSources() const;

    /** Request cancellation (the audio-separator child still runs to completion;
     *  result is discarded and "cancelled" event fires when the worker exits). */
    void cancelSeparation();

    bool isSeparating() const noexcept       { return state_.load() == State::Running; }
    int  getCurrentProgress() const noexcept { return currentProgress_.load(); }
    State getState() const noexcept          { return state_.load(); }
    juce::String getCurrentMessage() const;

    /** True if a Done result is sitting unconsumed (e.g. user closed editor before
     *  it landed; reopened editor should pick this up and load the drum stem). */
    bool hasUnconsumedResult() const noexcept;

    /** Take ownership of the latest Done result; subsequent calls return Idle. */
    Result consumeResult();

    /** Source file of the in-flight (or last) separation. */
    juce::File getCurrentSourceFile() const;

private:
    EventBus& bus_;

    std::atomic<State> state_{ State::Idle };
    std::atomic<bool>  cancelRequested_{ false };
    std::atomic<bool>  resultConsumed_{ true };
    std::atomic<int>   currentProgress_{ 0 };

    mutable juce::CriticalSection lock_;
    juce::String currentMessage_;
    juce::File   sourceFile_;
    Result       lastResult_;

    std::unique_ptr<std::thread> separationThread_;

    // Batch state (zero/empty when not in batch mode)
    std::atomic<bool> isBatch_   { false };
    std::atomic<int>  batchIndex_{ 0 };
    std::atomic<int>  batchCount_{ 0 };
    std::atomic<bool> batchResultConsumed_{ true };
    std::vector<juce::File> batchSources_;
    std::vector<juce::File> batchDrumStems_;

    void joinWorker();   // join the worker thread if joinable; safe to call repeatedly
    void runWorker(juce::File sourceFile);  // body of the single-file background thread
    void runBatchWorker(std::vector<juce::File> sources);  // body of the batch background thread
};

} // namespace samplar
