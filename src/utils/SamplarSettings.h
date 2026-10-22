#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <memory>
#include <mutex>

namespace samplar {

/**
 * SamplarSettings — Lightweight singleton wrapping a juce::PropertiesFile
 * for persistent user preferences that do not belong to any single service.
 *
 * Storage: %APPDATA%/Samplar/Samplar.user_prefs (Application Support on macOS).
 *
 * Threading: all accessors are guarded by a mutex; PropertiesFile itself
 * batches writes via its built-in timer (saveIfNeeded on destruction).
 *
 * Current keys:
 *   - "autoRoundBpm" (bool, default = true)
 *       When ON, auto-detected BPM is rounded to nearest integer in the
 *       SModePage tempo-detection callback. Manual / host / tap-tempo
 *       entry paths bypass this and always keep full precision.
 *   - "useGpuSeparation" (bool, default = true)
 *       When ON, stem separation runs on a discrete GPU via DirectML when one is
 *       available (falls back to the CPU automatically). OFF forces the CPU.
 *       Read by FullSongService at the start of every separation.
 */
class SamplarSettings
{
public:
    static SamplarSettings& getInstance()
    {
        static SamplarSettings instance;
        return instance;
    }

    // ── autoRoundBpm ───────────────────────────────────────────────
    bool getAutoRoundBpm() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return props_ ? props_->getBoolValue("autoRoundBpm", true) : true;
    }

    void setAutoRoundBpm(bool value)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (props_)
            {
                props_->setValue("autoRoundBpm", value);
                props_->saveIfNeeded();
            }
        }
        juce::Logger::writeToLog(juce::String("[Settings] autoRoundBpm = ")
                                 + (value ? "ON" : "OFF"));
    }

    // -- useGpuSeparation ----------------------------------------------
    bool getUseGpuSeparation() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return props_ ? props_->getBoolValue("useGpuSeparation", true) : true;
    }

    void setUseGpuSeparation(bool value)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (props_)
            {
                props_->setValue("useGpuSeparation", value);
                props_->saveIfNeeded();
            }
        }
        juce::Logger::writeToLog(juce::String("[Settings] useGpuSeparation = ")
                                 + (value ? "ON" : "OFF"));
    }

private:
    SamplarSettings()
    {
        juce::PropertiesFile::Options opts;
        opts.applicationName     = "Samplar";
        opts.folderName          = "Samplar";
        opts.filenameSuffix      = ".user_prefs";
        opts.osxLibrarySubFolder = "Application Support";

        props_ = std::make_unique<juce::PropertiesFile>(opts);
        juce::Logger::writeToLog("[Settings] PropertiesFile created at: "
                                 + props_->getFile().getFullPathName());
    }

    ~SamplarSettings()
    {
        if (props_)
            props_->saveIfNeeded();
    }

    SamplarSettings(const SamplarSettings&) = delete;
    SamplarSettings& operator=(const SamplarSettings&) = delete;

    mutable std::mutex mutex_;
    std::unique_ptr<juce::PropertiesFile> props_;
};

} // namespace samplar
