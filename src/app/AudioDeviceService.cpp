#include "AudioDeviceService.h"
#include <juce_core/juce_core.h>
#include <algorithm>

using namespace juce;

namespace samplar {

void AudioDeviceService::initialise()
{
    Logger::writeToLog("[AudioService] Initialising enhanced audio device service...");
    
    // Open default output device (0 inputs, 2 outputs)
    deviceManager_.initialise(0, 2, nullptr, true);
    deviceManager_.addChangeListener(this);
    
    // Initialize device tracking
    refreshAvailableDevices();
    lastKnownDevices_ = getAvailableOutputDevices();
    
    if (auto* dev = deviceManager_.getCurrentAudioDevice())
    {
        lastActiveDevice_ = dev->getName();
        Logger::writeToLog("[AudioService] Initialised device: " + lastActiveDevice_);
    }
    else
    {
        Logger::writeToLog("[AudioService] No audio device available on initialise.");
    }
}

void AudioDeviceService::shutdown()
{
    Logger::writeToLog("[AudioService] Shutting down audio device service...");
    deviceManager_.removeChangeListener(this);
    deviceChangeCallbacks_.clear();
    deviceManager_.closeAudioDevice();
}

void AudioDeviceService::changeListenerCallback(ChangeBroadcaster* source)
{
    if (source == &deviceManager_)
    {
        // PERFORMANCE FIX: Only handle actual device disconnections, not every audio activity
        // Check if current device is actually gone before doing expensive scanning
        if (auto* currentDevice = deviceManager_.getCurrentAudioDevice())
        {
            // Device is still available - this is just audio activity, not a device change
            // Don't do expensive device scanning on every sample playback
            Logger::writeToLog("[AudioService] [AUDIO] Audio activity detected (device still connected) - ignoring");
            return;
        }

        // Only if device is actually gone, handle the change
        Logger::writeToLog("[AudioService] [DEVICE] Device disconnection detected - handling device change");
        handleDeviceListChange();
        refreshToDefaultDevice();
    }
}

void AudioDeviceService::refreshToDefaultDevice()
{
    if (refreshing_) return;
    refreshing_ = true;

    Logger::writeToLog("[AudioService] Device change observed; refreshing to default...");

    // PERFORMANCE FIX: Skip expensive device scanning if we already have a working device
    if (auto* currentDevice = deviceManager_.getCurrentAudioDevice())
    {
        Logger::writeToLog("[AudioService] [OK] Current device still working: " + currentDevice->getName() + " - skipping scan");
        refreshing_ = false;
        return;
    }

    // Only scan if we actually need to find a new device
    Logger::writeToLog("[AudioService] [SCAN] No working device - performing device scan...");
    OwnedArray<AudioIODeviceType> types;
    deviceManager_.createAudioDeviceTypes(types);
    for (auto* t : types) t->scanForDevices();

    String newDeviceName;
    bool deviceRestored = false;

    if (deviceManager_.getCurrentAudioDevice() == nullptr)
    {
        Logger::writeToLog("[AudioService] No current device - initializing default...");
        deviceManager_.initialise(0, 2, nullptr, true);
        
        if (auto* dev = deviceManager_.getCurrentAudioDevice())
        {
            newDeviceName = dev->getName();
            Logger::writeToLog("[AudioService] Opened device: " + newDeviceName);
            deviceRestored = true;
        }
        else
        {
            Logger::writeToLog("[AudioService] No device after refresh.");
        }
    }
    else
    {
        auto* currentDev = deviceManager_.getCurrentAudioDevice();
        newDeviceName = currentDev->getName();
        
        auto typeName = deviceManager_.getCurrentAudioDeviceType();
        if (typeName.isNotEmpty())
        {
            deviceManager_.setCurrentAudioDeviceType(typeName, true);
            if (auto* dev = deviceManager_.getCurrentAudioDevice())
            {
                newDeviceName = dev->getName();
                Logger::writeToLog("[AudioService] Using type default: " + newDeviceName);
                deviceRestored = true;
            }
        }
    }

    // Notify listeners of device change (SM4-style event system)
    if (deviceRestored && newDeviceName != lastActiveDevice_)
    {
        Logger::writeToLog("[AudioService] Device changed: " + lastActiveDevice_ + " -> " + newDeviceName);
        notifyDeviceChangeListeners(newDeviceName, true);
        lastActiveDevice_ = newDeviceName;
        reconnectionAttempts_ = 0; // Reset reconnection attempts on successful switch
    }

    refreshing_ = false;
}

void AudioDeviceService::addDeviceChangeListener(const DeviceChangeCallback& callback)
{
    deviceChangeCallbacks_.push_back(callback);
    Logger::writeToLog("[AudioService] Added device change listener");
}

void AudioDeviceService::removeDeviceChangeListener(const DeviceChangeCallback& callback)
{
    // Note: This is a simplified implementation. In production, you'd need to store 
    // callback identifiers or use a more sophisticated removal mechanism
    Logger::writeToLog("[AudioService] Device change listener removal requested");
}

void AudioDeviceService::refreshAvailableDevices()
{
    Logger::writeToLog("[AudioService] Refreshing available devices...");

    // PERFORMANCE FIX: Only scan if we don't have a working device
    if (auto* currentDevice = deviceManager_.getCurrentAudioDevice())
    {
        Logger::writeToLog("[AudioService] [OK] Device working: " + currentDevice->getName() + " - skipping expensive scan");
        return;
    }

    Logger::writeToLog("[AudioService] [SCAN] No working device - performing device scan...");
    OwnedArray<AudioIODeviceType> types;
    deviceManager_.createAudioDeviceTypes(types);

    for (auto* type : types)
    {
        type->scanForDevices();
        Logger::writeToLog("[AudioService] Scanned device type: " + type->getTypeName());
    }
}

bool AudioDeviceService::trySetPreferredDevice(const String& deviceName)
{
    if (deviceSwitchInProgress_) return false;
    
    Logger::writeToLog("[AudioService] Attempting to set preferred device: " + deviceName);
    
    if (!isDeviceAvailable(deviceName))
    {
        Logger::writeToLog("[AudioService] Device not available: " + deviceName);
        return false;
    }
    
    gracefullySwitchDevice(deviceName);
    return true;
}

StringArray AudioDeviceService::getAvailableOutputDevices()
{
    StringArray devices;
    
    auto& types = deviceManager_.getAvailableDeviceTypes();
    for (auto* type : types)
    {
        auto deviceNames = type->getDeviceNames(false); // false = output devices
        devices.addArray(deviceNames);
    }
    
    return devices;
}

String AudioDeviceService::getCurrentDeviceName() const
{
    if (auto* dev = deviceManager_.getCurrentAudioDevice())
        return dev->getName();
    return {};
}

bool AudioDeviceService::isDeviceAvailable(const String& deviceName)
{
    auto availableDevices = getAvailableOutputDevices();
    return availableDevices.contains(deviceName);
}

void AudioDeviceService::handleDeviceDisconnection()
{
    Logger::writeToLog("[AudioService] Handling device disconnection...");
    
    auto currentName = getCurrentDeviceName();
    if (currentName.isNotEmpty())
    {
        notifyDeviceChangeListeners(currentName, false);
    }
    
    // Try to reconnect to a different available device
    handleDeviceReconnection();
}

void AudioDeviceService::handleDeviceReconnection()
{
    if (reconnectionAttempts_ >= MAX_RECONNECTION_ATTEMPTS)
    {
        Logger::writeToLog("[AudioService] Maximum reconnection attempts reached");
        return;
    }
    
    Logger::writeToLog("[AudioService] Attempting device reconnection... (attempt " + 
                      String(reconnectionAttempts_ + 1) + "/" + String(MAX_RECONNECTION_ATTEMPTS) + ")");
    
    reconnectionAttempts_++;
    
    // Try to find and connect to an available device
    auto availableDevices = getAvailableOutputDevices();
    
    for (const auto& deviceName : availableDevices)
    {
        if (trySetPreferredDevice(deviceName))
        {
            Logger::writeToLog("[AudioService] Successfully reconnected to: " + deviceName);
            reconnectionAttempts_ = 0;
            return;
        }
    }
    
    // If no specific device works, try default initialization
    refreshToDefaultDevice();
}

void AudioDeviceService::notifyDeviceChangeListeners(const String& deviceName, bool isAvailable)
{
    Logger::writeToLog("[AudioService] Notifying " + String(deviceChangeCallbacks_.size()) + 
                      " listeners of device change: " + deviceName + " (" + 
                      (isAvailable ? "available" : "unavailable") + ")");
    
    for (const auto& callback : deviceChangeCallbacks_)
    {
        try
        {
            callback(deviceName, isAvailable);
        }
        catch (const std::exception& e)
        {
            Logger::writeToLog("[AudioService] Error in device change callback: " + String(e.what()));
        }
    }
}

void AudioDeviceService::handleDeviceListChange()
{
    // PERFORMANCE FIX: Only check device list if we suspect actual device changes
    if (auto* currentDevice = deviceManager_.getCurrentAudioDevice())
    {
        // Current device is working - probably just audio activity, not device changes
        Logger::writeToLog("[AudioService] [AUDIO] Device list change ignored - current device working: " + currentDevice->getName());
        return;
    }

    Logger::writeToLog("[AudioService] [SCAN] Checking for actual device list changes...");
    auto currentDevices = getAvailableOutputDevices();

    // Check for newly added devices
    for (const auto& device : currentDevices)
    {
        if (!lastKnownDevices_.contains(device))
        {
            Logger::writeToLog("[AudioService] New device detected: " + device);
            notifyDeviceChangeListeners(device, true);
        }
    }

    // Check for removed devices
    for (const auto& device : lastKnownDevices_)
    {
        if (!currentDevices.contains(device))
        {
            Logger::writeToLog("[AudioService] Device removed: " + device);
            notifyDeviceChangeListeners(device, false);
        }
    }

    lastKnownDevices_ = currentDevices;
}

void AudioDeviceService::gracefullySwitchDevice(const String& newDeviceName)
{
    if (deviceSwitchInProgress_) return;
    
    deviceSwitchInProgress_ = true;
    Logger::writeToLog("[AudioService] Gracefully switching to device: " + newDeviceName);
    
    // Save current audio callbacks to restore them after switch
    // Note: JUCE handles most of this automatically, but we log the process
    
    auto currentDevice = getCurrentDeviceName();
    
    // Find the device type that contains our target device
    auto& types = deviceManager_.getAvailableDeviceTypes();
    for (auto* type : types)
    {
        auto deviceNames = type->getDeviceNames(false); // output devices
        if (deviceNames.contains(newDeviceName))
        {
            // Set the device type first, then the specific device
            deviceManager_.setCurrentAudioDeviceType(type->getTypeName(), false);
            
            AudioDeviceManager::AudioDeviceSetup setup;
            setup.outputDeviceName = newDeviceName;
            setup.useDefaultInputChannels = true;
            setup.useDefaultOutputChannels = true;
            
            auto result = deviceManager_.setAudioDeviceSetup(setup, true);
            if (result.isEmpty())
            {
                Logger::writeToLog("[AudioService] Successfully switched to: " + newDeviceName);
                lastActiveDevice_ = newDeviceName;
                notifyDeviceChangeListeners(newDeviceName, true);
            }
            else
            {
                Logger::writeToLog("[AudioService] Failed to switch device: " + result);
            }
            break;
        }
    }
    
    deviceSwitchInProgress_ = false;
}

void AudioDeviceService::addAudioCallback(juce::AudioIODeviceCallback* cb)
{
    juce::Logger::writeToLog("[TRACE] [TRACE] AudioDeviceService::addAudioCallback() called");
    juce::Logger::writeToLog("[TRACE] [TRACE] Audio Step 1: Getting current audio device");
    if (auto* device = deviceManager_.getCurrentAudioDevice())
    {
        juce::Logger::writeToLog("[TRACE] [TRACE] Audio Step 2: Current device: " + device->getName());
    }
    else
    {
        juce::Logger::writeToLog("[TRACE] [TRACE] Audio Step 2: No current audio device!");
    }
    juce::Logger::writeToLog("[TRACE] [TRACE] Audio Step 3: Adding audio callback to device manager");
    deviceManager_.addAudioCallback(cb);
    juce::Logger::writeToLog("[TRACE] [TRACE] AudioDeviceService::addAudioCallback() COMPLETED");
}

void AudioDeviceService::removeAudioCallback(juce::AudioIODeviceCallback* cb)
{
    juce::Logger::writeToLog("[TRACE] [TRACE] AudioDeviceService::removeAudioCallback() called");
    deviceManager_.removeAudioCallback(cb);
    juce::Logger::writeToLog("[TRACE] [TRACE] AudioDeviceService::removeAudioCallback() COMPLETED");
}

int AudioDeviceService::getCurrentBufferSize() const
{
    if (auto* device = deviceManager_.getCurrentAudioDevice())
        return device->getCurrentBufferSizeSamples();
    return 512;  // Default fallback
}

bool AudioDeviceService::setBufferSize(int samples)
{
    Logger::writeToLog("[AudioService] Setting buffer size to: " + String(samples) + " samples");

    auto setup = deviceManager_.getAudioDeviceSetup();
    setup.bufferSize = samples;

    auto result = deviceManager_.setAudioDeviceSetup(setup, true);
    if (result.isEmpty())
    {
        Logger::writeToLog("[AudioService] Buffer size changed successfully to " + String(samples));
        return true;
    }
    else
    {
        Logger::writeToLog("[AudioService] Failed to change buffer size: " + result);
        return false;
    }
}

juce::Array<int> AudioDeviceService::getAvailableBufferSizes() const
{
    juce::Array<int> sizes;

    if (auto* device = deviceManager_.getCurrentAudioDevice())
    {
        // Get available buffer sizes from device
        auto deviceSizes = device->getAvailableBufferSizes();
        for (int size : deviceSizes)
            sizes.add(size);
    }

    // If no device or empty list, return common defaults
    if (sizes.isEmpty())
    {
        sizes.add(128);
        sizes.add(256);
        sizes.add(512);
        sizes.add(1024);
        sizes.add(2048);
    }

    return sizes;
}

} // namespace samplar

