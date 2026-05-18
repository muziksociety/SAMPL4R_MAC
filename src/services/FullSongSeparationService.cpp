#include "FullSongSeparationService.h"

namespace samplar {

FullSongSeparationService::FullSongSeparationService(EventBus& bus)
    : bus_(bus)
{
}

FullSongSeparationService::~FullSongSeparationService()
{
    juce::Logger::writeToLog("[FullSongSep] === dtor START ===");
    cancelRequested_ = true;
    joinWorker();
    juce::Logger::writeToLog("[FullSongSep] === dtor END ===");
}

void FullSongSeparationService::initialise()
{
    juce::Logger::writeToLog("[FullSongSep] === initialise ===");
}

void FullSongSeparationService::shutdown()
{
    juce::Logger::writeToLog("[FullSongSep] === shutdown START ===");
    cancelRequested_ = true;
    joinWorker();
    juce::Logger::writeToLog("[FullSongSep] === shutdown END ===");
}

void FullSongSeparationService::joinWorker()
{
    if (separationThread_ && separationThread_->joinable())
    {
        juce::Logger::writeToLog("[FullSongSep] joinWorker: waiting for worker to exit...");
        separationThread_->join();
        juce::Logger::writeToLog("[FullSongSep] joinWorker: worker joined");
    }
    separationThread_.reset();
}

bool FullSongSeparationService::startSeparation(const juce::File& sourceFile)
{
    juce::Logger::writeToLog("[FullSongSep] === startSeparation ===");
    juce::Logger::writeToLog("[FullSongSep]   source: " + sourceFile.getFullPathName());

    if (state_.load() == State::Running)
    {
        juce::Logger::writeToLog("[FullSongSep]   ALREADY RUNNING - request ignored");
        return false;
    }

    // Reap any prior worker (Done/Failed/Cancelled) before starting a new one.
    joinWorker();

    {
        const juce::ScopedLock sl(lock_);
        sourceFile_ = sourceFile;
        currentMessage_ = "Starting separation...";
        lastResult_ = {};
    }

    cancelRequested_ = false;
    resultConsumed_  = false;
    currentProgress_ = 0;
    state_.store(State::Running);

    // Publish initial 0% progress so any subscriber that just attached gets a tick.
    juce::MessageManager::callAsync([this]() {
        bus_.publish("fullSong.separation.progress", juce::var(0));
        bus_.publish("fullSong.separation.message",  juce::var(juce::String("Starting separation...")));
    });

    separationThread_ = std::make_unique<std::thread>(
        [this, sourceFile]() { runWorker(sourceFile); });

    juce::Logger::writeToLog("[FullSongSep]   worker thread launched");
    return true;
}

void FullSongSeparationService::cancelSeparation()
{
    if (state_.load() == State::Running)
    {
        juce::Logger::writeToLog("[FullSongSep] cancel requested");
        cancelRequested_ = true;
    }
}

void FullSongSeparationService::runWorker(juce::File sourceFile)
{
    juce::Logger::writeToLog("[FullSongSep] [worker] thread started");

    // Local FullSongService instance for this run (mirrors SModeSeparationService).
    FullSongService fullSongSvc(bus_);
    fullSongSvc.initialise();

    FullSongService::Config cfg;
    cfg.drumsOnly = true;
    cfg.onProgress = [this](int pct, const juce::String& msg) {
        if (cancelRequested_.load()) return;

        currentProgress_.store(pct);
        {
            const juce::ScopedLock sl(lock_);
            currentMessage_ = msg;
        }

        juce::MessageManager::callAsync([this, pct, msg]() {
            // Don't fire UI events if the run was cancelled mid-flight.
            if (state_.load() != State::Running) return;
            bus_.publish("fullSong.separation.progress", juce::var(pct));
            bus_.publish("fullSong.separation.message",  juce::var(msg));
        });
    };

    juce::Logger::writeToLog("[FullSongSep] [worker] calling FullSongService::separateDrums...");
    auto result = fullSongSvc.separateDrums(sourceFile, cfg, "fullSong_");
    juce::Logger::writeToLog("[FullSongSep] [worker] separateDrums returned");

    if (cancelRequested_.load())
    {
        juce::Logger::writeToLog("[FullSongSep] [worker] cancelled");
        state_.store(State::Cancelled);
        resultConsumed_ = true;  // nothing to consume
        juce::MessageManager::callAsync([this]() {
            bus_.publish("fullSong.separation.cancelled", juce::var{});
        });
        fullSongSvc.shutdown();
        return;
    }

    if (result.error.isNotEmpty() || ! result.drumStemFile.existsAsFile())
    {
        const juce::String err = result.error.isNotEmpty()
            ? result.error
            : juce::String("Drum stem file missing after separation");

        juce::Logger::writeToLog("[FullSongSep] [worker] FAILED: " + err);
        {
            const juce::ScopedLock sl(lock_);
            lastResult_.sourceFile   = sourceFile;
            lastResult_.error        = err;
        }
        state_.store(State::Failed);
        resultConsumed_ = true;  // failure is not a consumable Done

        juce::MessageManager::callAsync([this, err]() {
            bus_.publish("fullSong.separation.error", juce::var(err));
        });
        fullSongSvc.shutdown();
        return;
    }

    juce::Logger::writeToLog("[FullSongSep] [worker] SUCCESS - drum stem: "
        + result.drumStemFile.getFullPathName());

    {
        const juce::ScopedLock sl(lock_);
        lastResult_.sourceFile   = sourceFile;
        lastResult_.drumStemFile = result.drumStemFile;
        lastResult_.error        = {};
    }
    currentProgress_.store(100);
    state_.store(State::Done);
    resultConsumed_ = false;

    const juce::String stemPath = result.drumStemFile.getFullPathName();
    juce::MessageManager::callAsync([this, stemPath]() {
        bus_.publish("fullSong.separation.progress", juce::var(100));
        bus_.publish("fullSong.separation.complete", juce::var(stemPath));
    });

    fullSongSvc.shutdown();
}

bool FullSongSeparationService::startBatchSeparation(const std::vector<juce::File>& sources)
{
    juce::Logger::writeToLog("[FullSongSep] === startBatchSeparation === (" + juce::String(static_cast<int>(sources.size())) + " files)");

    if (sources.empty())
    {
        juce::Logger::writeToLog("[FullSongSep]   empty source list - ignored");
        return false;
    }
    if (state_.load() == State::Running)
    {
        juce::Logger::writeToLog("[FullSongSep]   ALREADY RUNNING - request ignored");
        return false;
    }

    joinWorker();

    {
        const juce::ScopedLock sl(lock_);
        sourceFile_ = sources.front();
        currentMessage_ = "Starting batch separation...";
        lastResult_ = {};
        batchSources_  = sources;
        batchDrumStems_.clear();
    }

    cancelRequested_ = false;
    resultConsumed_  = true;          // single-file slot is unused in batch
    batchResultConsumed_ = false;
    isBatch_         = true;
    batchIndex_      = 0;
    batchCount_      = static_cast<int>(sources.size());
    currentProgress_ = 0;
    state_.store(State::Running);

    juce::MessageManager::callAsync([this]() {
        bus_.publish("fullSong.separation.progress", juce::var(0));
        bus_.publish("fullSong.separation.message",  juce::var(juce::String("Starting batch separation...")));
    });

    separationThread_ = std::make_unique<std::thread>(
        [this, sources]() { runBatchWorker(sources); });

    juce::Logger::writeToLog("[FullSongSep]   batch worker thread launched");
    return true;
}

void FullSongSeparationService::runBatchWorker(std::vector<juce::File> sources)
{
    juce::Logger::writeToLog("[FullSongSep] [batch worker] thread started, " + juce::String(static_cast<int>(sources.size())) + " files");

    FullSongService fullSongSvc(bus_);
    fullSongSvc.initialise();

    std::vector<juce::File> stems;
    stems.reserve(sources.size());

    for (size_t i = 0; i < sources.size() && ! cancelRequested_.load(); ++i)
    {
        batchIndex_.store(static_cast<int>(i));
        const auto& current = sources[i];

        {
            const juce::ScopedLock sl(lock_);
            sourceFile_ = current;
        }

        const int total = static_cast<int>(sources.size());
        const int idx   = static_cast<int>(i);
        juce::MessageManager::callAsync([this, idx, total, current]() {
            if (state_.load() != State::Running) return;
            const auto msg = "File " + juce::String(idx + 1) + " of " + juce::String(total) + ": " + current.getFileName();
            bus_.publish("fullSong.separation.message", juce::var(msg));
        });

        FullSongService::Config cfg;
        cfg.drumsOnly = true;
        cfg.onProgress = [this, idx, total](int pct, const juce::String& msg) {
            if (cancelRequested_.load()) return;

            // Total batch progress: each file is (100/total)% of the run.
            const int totalProgress = (idx * 100 + pct) / juce::jmax(1, total);
            currentProgress_.store(totalProgress);
            {
                const juce::ScopedLock sl(lock_);
                currentMessage_ = msg;
            }

            juce::MessageManager::callAsync([this, totalProgress, msg]() {
                if (state_.load() != State::Running) return;
                bus_.publish("fullSong.separation.progress", juce::var(totalProgress));
                bus_.publish("fullSong.separation.message",  juce::var(msg));
            });
        };

        juce::Logger::writeToLog("[FullSongSep] [batch worker] file " + juce::String(idx + 1) + "/" + juce::String(total) + ": " + current.getFileName());
        auto result = fullSongSvc.separateDrums(current, cfg, "batch_" + juce::String(idx) + "_");

        if (cancelRequested_.load()) break;

        if (result.error.isNotEmpty() || ! result.drumStemFile.existsAsFile())
        {
            juce::Logger::writeToLog("[FullSongSep] [batch worker] file " + juce::String(idx + 1) + " FAILED: " + result.error);
            // Continue with next file (batch is best-effort, mirrors prior behaviour).
            continue;
        }

        stems.push_back(result.drumStemFile);
        juce::Logger::writeToLog("[FullSongSep] [batch worker] file " + juce::String(idx + 1) + " ok: " + result.drumStemFile.getFileName());
    }

    fullSongSvc.shutdown();

    if (cancelRequested_.load())
    {
        juce::Logger::writeToLog("[FullSongSep] [batch worker] cancelled");
        state_.store(State::Cancelled);
        batchResultConsumed_ = true;
        juce::MessageManager::callAsync([this]() {
            bus_.publish("fullSong.separation.cancelled", juce::var{});
        });
        return;
    }

    {
        const juce::ScopedLock sl(lock_);
        batchDrumStems_ = stems;
    }
    currentProgress_.store(100);
    state_.store(State::Done);
    batchResultConsumed_ = false;

    if (stems.empty())
    {
        juce::Logger::writeToLog("[FullSongSep] [batch worker] no stems produced");
        state_.store(State::Failed);
        batchResultConsumed_ = true;
        const juce::String err = "No drum stems were extracted from the batch.";
        juce::MessageManager::callAsync([this, err]() {
            bus_.publish("fullSong.separation.error", juce::var(err));
        });
        return;
    }

    juce::Logger::writeToLog("[FullSongSep] [batch worker] DONE - " + juce::String(static_cast<int>(stems.size())) + " stems extracted");

    juce::var stemPaths;
    for (const auto& f : stems)
        stemPaths.append(juce::var(f.getFullPathName()));

    juce::MessageManager::callAsync([this, stemPaths]() {
        bus_.publish("fullSong.separation.progress", juce::var(100));
        bus_.publish("fullSong.separation.batch.complete", stemPaths);
    });
}

bool FullSongSeparationService::hasUnconsumedBatchResult() const noexcept
{
    return isBatch_.load() && state_.load() == State::Done && ! batchResultConsumed_.load();
}

std::vector<juce::File> FullSongSeparationService::consumeBatchResult()
{
    std::vector<juce::File> out;
    {
        const juce::ScopedLock sl(lock_);
        out = batchDrumStems_;
    }
    batchResultConsumed_ = true;
    if (state_.load() == State::Done)
        state_.store(State::Idle);
    isBatch_ = false;
    return out;
}

std::vector<juce::File> FullSongSeparationService::getCurrentBatchSources() const
{
    const juce::ScopedLock sl(lock_);
    return batchSources_;
}

juce::String FullSongSeparationService::getCurrentMessage() const
{
    const juce::ScopedLock sl(lock_);
    return currentMessage_;
}

bool FullSongSeparationService::hasUnconsumedResult() const noexcept
{
    return state_.load() == State::Done && ! resultConsumed_.load();
}

FullSongSeparationService::Result FullSongSeparationService::consumeResult()
{
    Result out;
    {
        const juce::ScopedLock sl(lock_);
        out = lastResult_;
    }
    resultConsumed_ = true;
    if (state_.load() == State::Done)
        state_.store(State::Idle);
    return out;
}

juce::File FullSongSeparationService::getCurrentSourceFile() const
{
    const juce::ScopedLock sl(lock_);
    return sourceFile_;
}

} // namespace samplar
