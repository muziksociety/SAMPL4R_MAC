#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <juce_core/juce_core.h>

namespace samplar {

class LicenseCache {
public:
    struct CacheEntry {
        std::string licenseKey;
        juce::var licenseData;
        std::string hardwareId;
        std::chrono::system_clock::time_point cachedAt{};
        std::chrono::system_clock::time_point lastValidated{};
        std::string integrityHash;
    };

    struct GraceStatus {
        bool hasGrace{false};
        std::chrono::minutes remainingTime{0};
        std::chrono::system_clock::time_point graceExpiresAt{};
    };

    bool storeLicense(const std::string& licenseKey,
                      const juce::var& licenseData,
                      const std::string& hardwareId);
    std::optional<CacheEntry> retrieveLicense(const std::string& hardwareId) const;
    bool isCacheValid(const std::string& hardwareId) const;
    void clearCache();

    bool needsPeriodicValidation(const std::string& hardwareId, int intervalHours = 24) const;
    void updateLastValidation(const std::string& hardwareId);

    GraceStatus getOfflineGraceStatus(const std::string& hardwareId, int gracePeriodHours = 48) const;

    void recordFailedAttempt(const std::string& username, const std::string& hardwareId);
    bool isUserLockedOut(const std::string& username, const std::string& hardwareId) const;

    // Migration support: get all hardware IDs stored in cache (for hardware ID migration)
    std::vector<std::string> getAllCachedHardwareIds() const;

private:
    juce::File getCacheDirectory() const;
    juce::File getLicenseCacheFile() const;
    juce::File getFailedAttemptsFile() const;

    std::string computeIntegrityHash(const CacheEntry& entry) const;
    std::string computeHMAC(const juce::MemoryBlock& key, const juce::String& message) const;
    juce::var encryptData(const juce::var& data) const;
    juce::var decryptData(const juce::var& encryptedData) const;

    std::optional<CacheEntry> loadCache() const;
    void persistCache(const CacheEntry& entry) const;
    void persistFailedAttempts(const juce::var& attempts) const;
    juce::var loadFailedAttempts() const;
    juce::MemoryBlock deriveKeyMaterial() const;

    constexpr static int MAX_FAILED_ATTEMPTS = 5;
    constexpr static int LOCKOUT_DURATION_MINUTES = 30;

    mutable std::once_flag keyInitFlag_;
    mutable juce::MemoryBlock keyMaterial_;
    mutable std::mutex cacheMutex_;
};

} // namespace samplar

