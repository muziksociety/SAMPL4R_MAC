#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../app/ServiceLocator.h"
#include "../app/ServiceRegistry.h"
#include "../app/AudioDeviceService.h"

namespace samplar {

/**
 * AudioEngine — Centralised audio device connection.
 *
 * Phase 0 (VST3 prep): Each UI page that plays audio creates an AudioEngine
 * instead of managing player_ + deviceManager_ directly.
 *
 * In standalone mode the engine connects an AudioSourcePlayer to the shared
 * AudioDeviceManager via AudioDeviceService.
 *
 * In a future VST3 build the engine can be driven by processBlock() instead,
 * without touching any UI code.
 */
class AudioEngine
{
public:
    explicit AudioEngine(const juce::String& ownerName)
        : ownerName_(ownerName)
    {
        juce::Logger::writeToLog("[AudioEngine:" + ownerName_ + "] Created");
    }

    ~AudioEngine()
    {
        disconnect();
        juce::Logger::writeToLog("[AudioEngine:" + ownerName_ + "] Destroyed");
    }

    // ── Device connection (standalone mode) ────────────────────

    /** Connect the player to AudioDeviceService. Returns true on success. */
    bool connect()
    {
        if (connected_)
            return true;

        auto* reg = ServiceLocator::getRegistry();
        if (!reg) return false;

        auto* audioSvc = dynamic_cast<AudioDeviceService*>(reg->get("audio"));
        if (!audioSvc) return false;

        deviceManager_ = &audioSvc->deviceManager();
        audioSvc->addAudioCallback(&player_);
        connected_ = true;

        juce::Logger::writeToLog("[AudioEngine:" + ownerName_ + "] Connected to AudioDeviceService");
        return true;
    }

    /** Disconnect player from device manager. Safe to call multiple times. */
    void disconnect()
    {
        if (!connected_)
            return;

        auto* reg = ServiceLocator::getRegistry();
        if (reg)
        {
            if (auto* audioSvc = dynamic_cast<AudioDeviceService*>(reg->get("audio")))
                audioSvc->removeAudioCallback(&player_);
        }

        // Fallback: remove directly if service still reachable
        if (deviceManager_)
            deviceManager_->removeAudioCallback(&player_);

        deviceManager_ = nullptr;
        connected_ = false;

        juce::Logger::writeToLog("[AudioEngine:" + ownerName_ + "] Disconnected");
    }

    // ── Source management ──────────────────────────────────────

    /** Set the audio source the player reads from. */
    void setSource(juce::AudioSource* source)
    {
        player_.setSource(source);
    }

    // ── Accessors ──────────────────────────────────────────────

    juce::AudioSourcePlayer&       getPlayer()        { return player_; }
    juce::AudioDeviceManager*      getDeviceManager() { return deviceManager_; }
    bool                           isConnected() const { return connected_; }
    const juce::String&            getOwnerName() const { return ownerName_; }

    /** Get current sample rate from device (0.0 if not connected). */
    double getSampleRate() const
    {
        if (deviceManager_)
            if (auto* device = deviceManager_->getCurrentAudioDevice())
                return device->getCurrentSampleRate();
        return 0.0;
    }

    /** Get current buffer size from device (0 if not connected). */
    int getBufferSize() const
    {
        if (deviceManager_)
            if (auto* device = deviceManager_->getCurrentAudioDevice())
                return device->getCurrentBufferSizeSamples();
        return 0;
    }

private:
    juce::String ownerName_;
    juce::AudioSourcePlayer player_;
    juce::AudioDeviceManager* deviceManager_ { nullptr };
    bool connected_ { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};

} // namespace samplar
