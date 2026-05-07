#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <string>

#include <juce_core/juce_core.h>

namespace samplar {

class SecureSessionManager {
public:
    struct SessionData {
        std::string accessToken;
        std::string userEmail;
        int userId{0};
        std::chrono::system_clock::time_point expiresAt{};
        std::string hardwareId;
    };

    bool storeSession(const SessionData& session);
    std::optional<SessionData> retrieveSession(const std::string& hardwareId) const;
    bool isSessionValid(const std::string& hardwareId) const;
    void clearSession(const std::string& hardwareId);
    void clearExpiredSessions();

    bool isEncryptionAvailable() const;

private:
    juce::String encryptData(const juce::String& data) const;
    juce::String decryptData(const juce::String& encryptedData) const;
    juce::String ensureEncryptionKey() const;

    juce::File getSessionFile(const std::string& hardwareId) const;
    juce::File getEncryptionKeyFile() const;

    static constexpr int SESSION_DURATION_DAYS = 7;

    mutable std::mutex sessionMutex_;
};

} // namespace samplar

