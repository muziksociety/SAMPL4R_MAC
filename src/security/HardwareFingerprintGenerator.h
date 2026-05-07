#pragma once

#include <mutex>
#include <optional>
#include <string>

namespace samplar {

class HardwareFingerprintGenerator {
public:
    struct HardwareComponents {
        std::string cpuInfo;
        std::string macAddress;
        std::string motherboardSerial;
        std::string systemUuid;
        std::string diskSerial;
    };

    std::string generateHardwareId();
    bool verifyHardwareIdConsistency();

    HardwareComponents gatherHardwareComponents() const;

    // Migration support: allow setting hardware ID from existing license cache
    bool migrateFromExistingHardwareId(const std::string& existingHardwareId);

private:
    std::string getCpuInfo() const;
    std::string getMacAddress() const;
    std::string getMotherboardSerial() const;
    std::string getSystemUuid() const;
    std::string getDiskSerial() const;
    std::string executeSystemCommand(const std::string& command) const;
    std::string computeSha256Hash(const std::string& input) const;
    std::string buildFingerprintPayload(const HardwareComponents& components) const;

    // Persistent cache methods
    std::string getCacheFilePath() const;
    std::optional<std::string> loadCachedHardwareIdFromDisk() const;
    bool saveCachedHardwareIdToDisk(const std::string& hardwareId) const;

    mutable std::mutex hardwareIdMutex_;
    mutable std::optional<std::string> cachedHardwareId_;
};

} // namespace samplar

