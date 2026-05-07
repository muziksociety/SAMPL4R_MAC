#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <functional>
#include <vector>
#include "Service.h"

namespace samplar {

class AudioDeviceService : public IService, private juce::ChangeListener {
public:
    // Device change notification callback type
    using DeviceChangeCallback = std::function<void(const juce::String& deviceName, bool isAvailable)>;

    AudioDeviceService() = default;
    ~AudioDeviceService() override = default;

    void initialise() override;
    void shutdown() override;

    // Accessors
    juce::AudioDeviceManager& deviceManager() { return deviceManager_; }

    // Convenience passthroughs
    void addAudioCallback(juce::AudioIODeviceCallback* cb);
    void removeAudioCallback(juce::AudioIODeviceCallback* cb);

    // Device change notifications (SM4-style event system)
    void addDeviceChangeListener(const DeviceChangeCallback& callback);
    void removeDeviceChangeListener(const DeviceChangeCallback& callback);
    
    // Enhanced device management
    void refreshAvailableDevices();
    bool trySetPreferredDevice(const juce::String& deviceName);
    juce::StringArray getAvailableOutputDevices();
    juce::String getCurrentDeviceName() const;
    
    // Device availability checking (prevents app restart requirement)
    bool isDeviceAvailable(const juce::String& deviceName);
    void handleDeviceDisconnection();
    void handleDeviceReconnection();

    // Buffer size control (for sequencer timing stability)
    int getCurrentBufferSize() const;
    bool setBufferSize(int samples);
    juce::Array<int> getAvailableBufferSizes() const;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void refreshToDefaultDevice();
    void notifyDeviceChangeListeners(const juce::String& deviceName, bool isAvailable);
    void handleDeviceListChange();
    void gracefullySwitchDevice(const juce::String& newDeviceName);

    juce::AudioDeviceManager deviceManager_;
    bool refreshing_ { false };
    
    // SM4-style device change tracking
    std::vector<DeviceChangeCallback> deviceChangeCallbacks_;
    juce::StringArray lastKnownDevices_;
    juce::String lastActiveDevice_;
    
    // Enhanced device management state
    bool deviceSwitchInProgress_ { false };
    int reconnectionAttempts_ { 0 };
    static constexpr int MAX_RECONNECTION_ATTEMPTS = 3;
};

} // namespace samplar

