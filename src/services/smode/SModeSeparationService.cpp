#include "SModeSeparationService.h"
#include "../../utils/DataPaths.h"
#include <thread>

namespace samplar {

SModeSeparationService::SModeSeparationService(EventBus& bus)
    : bus_(bus)
{
}

void SModeSeparationService::initialise()
{
    juce::Logger::writeToLog("🎵 [SModeSeparation] === initialise ===");
}

void SModeSeparationService::shutdown()
{
    juce::Logger::writeToLog("🎵 [SModeSeparation] === shutdown ===");
    // Order matters: kill queued callbacks first, then stop + join the worker.
    alive_->store(false);
    cancelRequested_ = true;
    joinWorker();
    separating_ = false;
    juce::Logger::writeToLog("🎵 [SModeSeparation] shutdown complete (worker joined)");
}

void SModeSeparationService::joinWorker()
{
    if (separationThread_ && separationThread_->joinable())
    {
        juce::Logger::writeToLog("🎵 [SModeSeparation] joinWorker: waiting for worker thread");
        separationThread_->join();
        juce::Logger::writeToLog("🎵 [SModeSeparation] joinWorker: worker joined");
    }
    separationThread_.reset();
}

juce::StringArray SModeSeparationService::getStemNames() const
{
    // The native ONNX provider (htdemucs) is 4-stem only.
    juce::ignoreUnused(stemCount_);
    return { "Vocals", "Bass", "Drums", "Melody" };
}

juce::String SModeSeparationService::getModelName() const
{
    // Log-only identifier; the actual model file is resolved by
    // FullSongService::resolveModelFile (body model first, full export as fallback).
    return "htdemucs_body.onnx + host iSTFT (4-stem ONNX, GPU/CPU)";
}

void SModeSeparationService::startSeparation(const juce::File& sourceFile)
{
    if (separating_.load())
    {
        juce::Logger::writeToLog("❌ [SModeSeparation] Already separating, ignoring request");
        return;
    }

    if (!alive_->load())
    {
        juce::Logger::writeToLog("❌ [SModeSeparation] startSeparation after shutdown, ignoring");
        return;
    }

    // A previous worker may still be finishing its teardown after posting
    // "complete"; assigning over a joinable std::thread would std::terminate.
    joinWorker();

    sourceFile_ = sourceFile;
    separating_ = true;
    cancelRequested_ = false;
    stemFiles_.clear();

    juce::Logger::writeToLog("🎵 [SModeSeparation] === startSeparation ===");
    juce::Logger::writeToLog("📁 [SModeSeparation] Source: " + sourceFile.getFullPathName());
    juce::Logger::writeToLog("🎵 [SModeSeparation] Model: " + getModelName()
        + " (" + juce::String(static_cast<int>(stemCount_)) + " stems)");

    progressPercent_ = 0;
    bus_.publish("smode.separation.progress", juce::var(0));

    // `this` is safe inside the worker body itself: shutdown() joins the
    // worker before the service dies. Only the callAsync lambdas (which can
    // run AFTER shutdown) need the `alive` token.
    separationThread_ = std::make_unique<std::thread>([this, alive = alive_]()
    {
        juce::Logger::writeToLog("🎵 [SModeSeparation] Background thread started");

        // Create FullSongService instance for this separation
        FullSongService fullSongSvc(bus_);
        fullSongSvc.initialise();

        // Configure for ALL stems (not drums only). ONNX-only path -
        // the legacy UVR/Python backend was removed 2026-05-18.
        FullSongService::Config cfg;
        cfg.drumsOnly = false;

        // Wire cancellation: polled while waiting for the separation gate, between chunks,
        // and by the provider's watcher thread, which aborts the running ONNX chunk.
        fullSongSvc.shouldCancel = [this]() { return cancelRequested_.load(); };

        // Progress callback → publish S-Mode progress events
        cfg.onProgress = [this, alive](int pct, const juce::String& msg) {
            juce::MessageManager::callAsync([this, alive, pct, msg]() {
                if (!alive->load() || !separating_.load()) return;
                progressPercent_ = juce::jlimit(0, 100, pct);
                bus_.publish("smode.separation.progress", juce::var(pct));
                juce::Logger::writeToLog("🎵 [SModeSeparation] Progress: " + juce::String(pct) + "% - " + msg);
            });
        };

        juce::Logger::writeToLog("[SModeSeparation] Calling FullSongService::separateDrums (all stems mode)...");
        FullSongService::Result result;
        try
        {
            result = fullSongSvc.separateDrums(sourceFile_, cfg, "smode_");
        }
        catch (...)
        {
            // Nothing may escape a std::thread (std::terminate would take the DAW down).
            result = {};
            result.error = "Stem separation failed (unexpected error)";
            juce::Logger::writeToLog("[SModeSeparation] Decision: unexpected exception from separateDrums -> error");
        }

        // Check for cancellation
        if (cancelRequested_.load() || result.cancelled)
        {
            juce::Logger::writeToLog("[SModeSeparation] Cancelled during separation");
            juce::MessageManager::callAsync([this, alive]() {
                if (!alive->load()) return;
                separating_ = false;
                bus_.publish("smode.separation.error", juce::var("Cancelled"));
            });
            fullSongSvc.shutdown();
            return;
        }

        // Check for errors
        if (result.error.isNotEmpty())
        {
            juce::Logger::writeToLog("[SModeSeparation] Separation failed: " + result.error);
            juce::MessageManager::callAsync([this, alive, err = result.error]() {
                if (!alive->load()) return;
                separating_ = false;
                bus_.publish("smode.separation.error", juce::var(err));
            });
            fullSongSvc.shutdown();
            return;
        }

        // Exact stem paths from the separator (audit C14: no filename matching).
        juce::Logger::writeToLog("[SModeSeparation] Separation done: backend=" + result.backend
            + " fromCache=" + juce::String((int) result.fromCache)
            + " dir=" + result.outputDir.getFullPathName());
        auto foundStems = orderedStemFiles(result.stems);

        if (foundStems.empty())
        {
            juce::Logger::writeToLog("[SModeSeparation] Stem files missing after separation");
            juce::MessageManager::callAsync([this, alive]() {
                if (!alive->load()) return;
                separating_ = false;
                bus_.publish("smode.separation.error", juce::var("No stems produced"));
            });
            fullSongSvc.shutdown();
            return;
        }

        // Publish completion. stemFiles_ is assigned HERE, on the message
        // thread, never on this worker (review S-M2: it is read by the state
        // save and the UI on the message thread).
        juce::MessageManager::callAsync([this, alive, foundStems]() {
            if (!alive->load()) return;
            stemFiles_ = foundStems;
            separating_ = false;
            progressPercent_ = 100;
            juce::Logger::writeToLog("[SModeSeparation] complete: " + juce::String(static_cast<int>(stemFiles_.size())) + " stems");
            for (const auto& f : stemFiles_)
                juce::Logger::writeToLog("  [SModeSeparation]   " + f.getFullPathName());
            bus_.publish("smode.separation.progress", juce::var(100));

            juce::var stemPaths;
            for (const auto& f : stemFiles_)
                stemPaths.append(juce::var(f.getFullPathName()));

            bus_.publish("smode.separation.complete", stemPaths);
        });

        fullSongSvc.shutdown();
        juce::Logger::writeToLog("🎵 [SModeSeparation] Worker thread exiting");
    });
    // NOT detached - joined in joinWorker() (next start) or shutdown().
}

void SModeSeparationService::cancelSeparation()
{
    if (separating_.load())
    {
        juce::Logger::writeToLog("🎵 [SModeSeparation] Cancel requested");
        cancelRequested_ = true;
    }
}

std::vector<juce::File> SModeSeparationService::orderedStemFiles(const FullSongService::StemFiles& stems) const
{
    // Same order as getStemNames(): Vocals, Bass, Drums, Melody (= Demucs "other").
    // All four must exist - a missing stem must not shift the later lanes.
    juce::Logger::writeToLog("[SModeSeparation] orderedStemFiles: mapping exact stem paths");
    const std::vector<juce::File> ordered { stems.vocals, stems.bass, stems.drums, stems.other };
    for (const auto& f : ordered)
    {
        if (!f.existsAsFile())
        {
            juce::Logger::writeToLog("[SModeSeparation] orderedStemFiles: missing " + f.getFullPathName() + " -> none");
            return {};
        }
    }
    juce::Logger::writeToLog("[SModeSeparation] orderedStemFiles: 4 stems OK");
    return ordered;
}

} // namespace samplar
