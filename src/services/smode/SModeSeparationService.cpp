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
    cancelSeparation();
    if (separationThread_ && separationThread_->joinable())
        separationThread_->join();
}

juce::StringArray SModeSeparationService::getStemNames() const
{
    // 6-stem flow deprecated 2026-05-18 along with the UVR/Python backend.
    // The native ONNX provider (htdemucs.onnx) is 4-stem only.
    // The StemCount enum is retained for ABI/UI compatibility but ::Six is
    // now a no-op alias for ::Four.
    juce::ignoreUnused(stemCount_);
    return { "Vocals", "Bass", "Drums", "Melody" };
}

juce::String SModeSeparationService::getModelName() const
{
    // Log-only identifier; the actual model file (htdemucs.onnx) is resolved
    // by FullSongService::locateOnnxModel via DataPaths.
    return "htdemucs.onnx (4-stem ONNX)";
}

void SModeSeparationService::startSeparation(const juce::File& sourceFile)
{
    if (separating_.load())
    {
        juce::Logger::writeToLog("❌ [SModeSeparation] Already separating, ignoring request");
        return;
    }

    sourceFile_ = sourceFile;
    separating_ = true;
    cancelRequested_ = false;
    stemFiles_.clear();

    juce::Logger::writeToLog("🎵 [SModeSeparation] === startSeparation ===");
    juce::Logger::writeToLog("📁 [SModeSeparation] Source: " + sourceFile.getFullPathName());
    juce::Logger::writeToLog("🎵 [SModeSeparation] Model: " + getModelName()
        + " (" + juce::String(static_cast<int>(stemCount_)) + " stems)");

    bus_.publish("smode.separation.progress", juce::var(0));

    separationThread_ = std::make_unique<std::thread>([this]()
    {
        juce::Logger::writeToLog("🎵 [SModeSeparation] Background thread started");

        // Create FullSongService instance for this separation
        FullSongService fullSongSvc(bus_);
        fullSongSvc.initialise();

        // Configure for ALL stems (not drums only). ONNX-only path -
        // the legacy UVR/Python backend was removed 2026-05-18.
        FullSongService::Config cfg;
        cfg.drumsOnly = false;

        // Progress callback → publish S-Mode progress events
        cfg.onProgress = [this](int pct, const juce::String& msg) {
            juce::MessageManager::callAsync([this, pct, msg]() {
                if (!separating_.load()) return;
                bus_.publish("smode.separation.progress", juce::var(pct));
                juce::Logger::writeToLog("🎵 [SModeSeparation] Progress: " + juce::String(pct) + "% - " + msg);
            });
        };

        juce::Logger::writeToLog("🎵 [SModeSeparation] Calling FullSongService::separateDrums (all stems mode)...");
        auto result = fullSongSvc.separateDrums(sourceFile_, cfg, "smode_");

        // Check for cancellation
        if (cancelRequested_.load())
        {
            juce::Logger::writeToLog("🎵 [SModeSeparation] Cancelled during separation");
            juce::MessageManager::callAsync([this]() {
                separating_ = false;
                bus_.publish("smode.separation.error", juce::var("Cancelled"));
            });
            fullSongSvc.shutdown();
            return;
        }

        // Check for errors
        if (result.error.isNotEmpty())
        {
            juce::Logger::writeToLog("❌ [SModeSeparation] Separation failed: " + result.error);
            juce::MessageManager::callAsync([this, err = result.error]() {
                separating_ = false;
                bus_.publish("smode.separation.error", juce::var(err));
            });
            fullSongSvc.shutdown();
            return;
        }

        // Find all stem output files in the output directory
        juce::Logger::writeToLog("🎵 [SModeSeparation] Separation done, scanning for stem files...");
        juce::Logger::writeToLog("📁 [SModeSeparation] Output dir: " + result.outputDir.getFullPathName());

        auto foundStems = findStemOutputFiles(result.outputDir);

        if (foundStems.empty())
        {
            // Fallback: if only drums stem was returned (single file),
            // check if it's the drum stem file
            if (result.drumStemFile.existsAsFile())
            {
                juce::Logger::writeToLog("⚠️ [SModeSeparation] Only drums stem found, checking output dir more broadly...");

                // Scan output dir recursively for any audio stem files (WAV or FLAC)
                juce::Array<juce::File> allWavs;
                result.outputDir.findChildFiles(allWavs, juce::File::findFiles, true, "*.wav");
                result.outputDir.findChildFiles(allWavs, juce::File::findFiles, true, "*.flac");

                juce::Logger::writeToLog("🎵 [SModeSeparation] Found " + juce::String(allWavs.size()) + " audio stem files total:");
                for (const auto& f : allWavs)
                    juce::Logger::writeToLog("  📁 " + f.getFileName());

                if (allWavs.size() > 1)
                {
                    // Multiple files found — use them
                    for (const auto& f : allWavs)
                        foundStems.push_back(f);
                }
            }
        }

        if (foundStems.empty())
        {
            juce::Logger::writeToLog("❌ [SModeSeparation] No stem files found in output");
            juce::MessageManager::callAsync([this]() {
                separating_ = false;
                bus_.publish("smode.separation.error", juce::var("No stems produced"));
            });
            fullSongSvc.shutdown();
            return;
        }

        stemFiles_ = foundStems;

        juce::Logger::writeToLog("✅ [SModeSeparation] Found "
            + juce::String(static_cast<int>(stemFiles_.size())) + " stems:");
        for (const auto& f : stemFiles_)
            juce::Logger::writeToLog("  📁 " + f.getFileName());

        // Publish completion
        juce::MessageManager::callAsync([this]() {
            separating_ = false;
            bus_.publish("smode.separation.progress", juce::var(100));

            juce::var stemPaths;
            for (const auto& f : stemFiles_)
                stemPaths.append(juce::var(f.getFullPathName()));

            bus_.publish("smode.separation.complete", stemPaths);
        });

        fullSongSvc.shutdown();
    });

    separationThread_->detach();
}

void SModeSeparationService::cancelSeparation()
{
    if (separating_.load())
    {
        juce::Logger::writeToLog("🎵 [SModeSeparation] Cancel requested");
        cancelRequested_ = true;
    }
}

std::vector<juce::File> SModeSeparationService::findStemOutputFiles(const juce::File& outputDir) const
{
    juce::Logger::writeToLog("🎵 [SModeSeparation] Scanning for stems in: " + outputDir.getFullPathName());

    std::vector<juce::File> result;
    auto stemNames = getStemNames();

    // audio-separator outputs files like:
    //   trackname_(Vocals)_modelname.wav
    //   trackname_(Bass)_modelname.wav
    //   trackname_(Drums)_modelname.wav
    //   trackname_(Other)_modelname.wav
    // Scan for files containing stem names

    juce::Array<juce::File> allWavs;
    outputDir.findChildFiles(allWavs, juce::File::findFiles, true, "*.wav");
    outputDir.findChildFiles(allWavs, juce::File::findFiles, true, "*.flac");

    juce::Logger::writeToLog("🎵 [SModeSeparation] Total audio stem files found: " + juce::String(allWavs.size()));

    for (const auto& stemName : stemNames)
    {
        // Demucs outputs "Other" on disk, but we display "Melody" — check both
        juce::StringArray searchNames;
        searchNames.add(stemName);
        if (stemName.equalsIgnoreCase("Melody"))
            searchNames.add("Other");

        bool found = false;
        for (const auto& searchName : searchNames)
        {
            if (found) break;
            for (const auto& wavFile : allWavs)
            {
                auto fileName = wavFile.getFileNameWithoutExtension();
                // audio-separator uses (StemName) format in filenames
                if (fileName.containsIgnoreCase("(" + searchName + ")") ||
                    fileName.containsIgnoreCase("_" + searchName + "_") ||
                    fileName.endsWithIgnoreCase("_" + searchName))
                {
                    result.push_back(wavFile);
                    juce::Logger::writeToLog("  ✅ " + stemName + " → " + wavFile.getFileName());
                    found = true;
                    break;
                }
            }
        }
        if (!found)
            juce::Logger::writeToLog("  ❌ " + stemName + " — not found");
    }

    // If we couldn't match by name but have the right count, use all files
    if (result.empty() && allWavs.size() >= stemNames.size())
    {
        juce::Logger::writeToLog("⚠️ [SModeSeparation] Name matching failed, using all "
            + juce::String(allWavs.size()) + " files in order");
        for (const auto& f : allWavs)
            result.push_back(f);
    }

    return result;
}

} // namespace samplar
