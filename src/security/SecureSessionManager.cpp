#include "SecureSessionManager.h"
#include "LicenseCache.h"

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_cryptography/juce_cryptography.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <system_error>

namespace samplar {

namespace {
constexpr auto kSessionFilenamePrefix = "session_";
constexpr auto kSessionFilenameSuffix = ".json";
constexpr size_t kKeyBytes = 16;
const juce::String kSessionKeySalt { "SAMPLAR_SESSION_KEY|" };

juce::MemoryBlock deriveKeyMaterial(const juce::String& baseKey)
{
    juce::SHA256 hash((kSessionKeySalt + baseKey).toUTF8());
    auto raw = hash.getRawData();
    const auto bytes = static_cast<size_t>(juce::jmin<int>(static_cast<int>(raw.getSize()), static_cast<int>(kKeyBytes)));
    auto result = juce::MemoryBlock(raw.getData(), bytes);
    if (result.getSize() < kKeyBytes)
        result.setSize(kKeyBytes, true);
    return result;
}


juce::String toBase64(const juce::MemoryBlock& block)
{
    return juce::Base64::toBase64(block.getData(), static_cast<int>(block.getSize()));
}

juce::MemoryBlock fromBase64(const juce::String& text)
{
    juce::MemoryOutputStream stream;
    if (!juce::Base64::convertFromBase64(stream, text))
        return {};

    return stream.getMemoryBlock();
}

} // namespace

bool SecureSessionManager::storeSession(const SessionData& session)
{
    std::scoped_lock lock(sessionMutex_);

    SessionData sessionCopy = session;
    if (sessionCopy.expiresAt.time_since_epoch().count() == 0) {
        sessionCopy.expiresAt = std::chrono::system_clock::now() + std::chrono::hours(24 * SESSION_DURATION_DAYS);
    }

    auto* object = new juce::DynamicObject();
    object->setProperty("accessToken", encryptData(juce::String(sessionCopy.accessToken)));
    object->setProperty("userEmail", encryptData(juce::String(sessionCopy.userEmail)));
    object->setProperty("userId", sessionCopy.userId);
    object->setProperty("expiresAtMs", static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(sessionCopy.expiresAt.time_since_epoch()).count()));
    object->setProperty("hardwareId", juce::String(sessionCopy.hardwareId));

    const auto json = juce::JSON::toString(juce::var(object));
    return getSessionFile(sessionCopy.hardwareId).replaceWithText(json);
}

std::optional<SecureSessionManager::SessionData> SecureSessionManager::retrieveSession(const std::string& hardwareId) const
{
    std::scoped_lock lock(sessionMutex_);

    const auto file = getSessionFile(hardwareId);
    if (!file.existsAsFile()) {
        return std::nullopt;
    }

    const auto json = file.loadFileAsString();
    const auto parsed = juce::JSON::parse(json);
    if (!parsed.isObject()) {
        return std::nullopt;
    }

    const auto* object = parsed.getDynamicObject();
    if (object == nullptr) {
        return std::nullopt;
    }

    SessionData session{};
    session.accessToken = decryptData(object->getProperty("accessToken").toString()).toStdString();
    session.userEmail = decryptData(object->getProperty("userEmail").toString()).toStdString();
    session.userId = static_cast<int>(object->getProperty("userId"));

    const auto expiresMs = static_cast<int64_t>(object->getProperty("expiresAtMs"));
    session.expiresAt = std::chrono::system_clock::time_point(std::chrono::milliseconds(expiresMs));
    session.hardwareId = object->getProperty("hardwareId").toString().toStdString();

    return session;
}

bool SecureSessionManager::isSessionValid(const std::string& hardwareId) const
{
    const auto session = retrieveSession(hardwareId);
    if (!session.has_value()) {
        return false;
    }

    return session->expiresAt > std::chrono::system_clock::now();
}

void SecureSessionManager::clearSession(const std::string& hardwareId)
{
    std::scoped_lock lock(sessionMutex_);
    const auto file = getSessionFile(hardwareId);
    if (file.existsAsFile()) {
        file.deleteFile();
    }
}

void SecureSessionManager::clearExpiredSessions()
{
    std::scoped_lock lock(sessionMutex_);

    const auto sessionDir = getSessionFile("placeholder").getParentDirectory();
    if (!sessionDir.exists()) {
        return;
    }

    juce::Array<juce::File> files;
    sessionDir.findChildFiles(files, juce::File::TypesOfFileToFind::findFiles, false, "session_*.json");

    const auto now = std::chrono::system_clock::now();
    for (const auto& file : files) {
        auto parsed = juce::JSON::parse(file.loadFileAsString());
        if (!parsed.isObject()) {
            continue;
        }
        const auto* object = parsed.getDynamicObject();
        if (object == nullptr) {
            continue;
        }
        const auto expiresMs = static_cast<int64_t>(object->getProperty("expiresAtMs"));
        const auto expiresAt = std::chrono::system_clock::time_point(std::chrono::milliseconds(expiresMs));
        if (expiresAt <= now) {
            file.deleteFile();
        }
    }
}

bool SecureSessionManager::isEncryptionAvailable() const
{
    return ensureEncryptionKey().isNotEmpty();
}

juce::String SecureSessionManager::encryptData(const juce::String& data) const
{
    if (data.isEmpty())
        return {};

    // No key -> store nothing (never the token in clear text).
    const auto keyMaterial = ensureEncryptionKey();
    if (keyMaterial.isEmpty())
        return {};

    auto derived = deriveKeyMaterial(keyMaterial);
    juce::BlowFish cipher(derived.getData(), static_cast<int>(derived.getSize()));

    juce::MemoryBlock block(data.toRawUTF8(), static_cast<size_t>(data.getNumBytesAsUTF8()));
    cipher.encrypt(block);

    return juce::Base64::toBase64(block.getData(), block.getSize());
}

juce::String SecureSessionManager::decryptData(const juce::String& encryptedData) const
{
    // Ciphertext only: anything that does not decrypt (plain text, another key,
    // garbage) reads as "no value" - never passed through as-is.
    if (encryptedData.isEmpty())
        return {};

    const auto keyMaterial = ensureEncryptionKey();
    if (keyMaterial.isEmpty())
        return {};

    auto cipherBlock = fromBase64(encryptedData);
    if (cipherBlock.getSize() == 0 || (cipherBlock.getSize() % 8) != 0)
        return {};

    auto derived = deriveKeyMaterial(keyMaterial);
    juce::BlowFish cipher(derived.getData(), static_cast<int>(derived.getSize()));

    const int plainBytes = cipher.decrypt(cipherBlock.getData(), cipherBlock.getSize());   // -1 = bad padding
    if (plainBytes <= 0)
        return {};
    return juce::String::fromUTF8(static_cast<const char*>(cipherBlock.getData()), plainBytes);
}

bool SecureSessionManager::isUsableKey(const juce::String& key)
{
    // A key we wrote is a juce::Uuid string: 32 hex digits. Anything else
    // (empty, truncated, garbage) is replaced (review N-18).
    return key.length() >= kMinKeyChars && key.containsOnly("0123456789abcdefABCDEF-");
}

juce::String SecureSessionManager::ensureEncryptionKey() const
{
    const auto file = getEncryptionKeyFile();
    const bool existed = file.existsAsFile();
    if (existed) {
        const auto key = file.loadFileAsString().trim();
        if (isUsableKey(key))
            return key;
        juce::Logger::writeToLog("[Session] encryption.key unusable (" + juce::String(key.length()) +
                                 " chars) - regenerating (a stored sign-in token, if any, cannot be read)");
    }

    // Written to a unique temp file first, then moved into place, so no reader
    // (another plugin instance or process) ever sees a half-written key.
    //  - no key yet: hard link = create-if-absent; if another process won the
    //    race, ITS key is used (re-read below), so nobody encrypts with a key
    //    that is then overwritten.
    //  - unusable key: replaced with an atomic rename (MoveFileEx REPLACE_EXISTING).
    const auto key = juce::Uuid().toString();
    const auto temp = file.getSiblingFile("encryption.key.tmp-" + juce::Uuid().toString());
    if (!temp.replaceWithText(key)) {
        juce::Logger::writeToLog("[Session] ERROR: could not write " + temp.getFullPathName() + " - no encryption key");
        return {};
    }

    std::error_code ec;
    const std::filesystem::path tempPath(temp.getFullPathName().toWideCharPointer());
    const std::filesystem::path keyPath(file.getFullPathName().toWideCharPointer());
    if (!existed) {
        std::filesystem::create_hard_link(tempPath, keyPath, ec);
        if (ec && !file.existsAsFile()) {
            // No hard links on this volume (FAT / some network drives): plain atomic rename.
            juce::Logger::writeToLog("[Session] encryption.key hard link failed (" + juce::String(ec.message()) + ") - renaming instead");
            ec.clear();
            std::filesystem::rename(tempPath, keyPath, ec);
        } else if (ec) {
            juce::Logger::writeToLog("[Session] encryption.key already created by another instance - using it");
        }
        temp.deleteFile();
    } else {
        std::filesystem::rename(tempPath, keyPath, ec);
        if (ec) {
            juce::Logger::writeToLog("[Session] ERROR: encryption.key replace failed: " + juce::String(ec.message()));
            temp.deleteFile();
        }
    }

    // The key on disk is the one everybody uses.
    const auto onDisk = file.loadFileAsString().trim();
    if (!isUsableKey(onDisk)) {
        juce::Logger::writeToLog("[Session] ERROR: encryption.key still unusable after regeneration - no encryption key");
        return {};
    }
    juce::Logger::writeToLog(juce::String("[Session] encryption.key ") + (existed ? "regenerated" : "created") +
                             (onDisk == key ? "" : " (by another instance)"));
    return onDisk;
}

juce::File SecureSessionManager::getSessionFile(const std::string& hardwareId) const
{
    // Same root as license_cache.json (honours SAMPLAR_CACHE_DIR_OVERRIDE).
    auto dir = LicenseCache::cacheDirectory().getChildFile("sessions");
    if (!dir.exists()) {
        dir.createDirectory();
    }

    auto safeHardwareId = juce::String(hardwareId);
    safeHardwareId = safeHardwareId.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_");
    if (safeHardwareId.isEmpty()) {
        safeHardwareId = "default";
    }

    return dir.getChildFile(kSessionFilenamePrefix + safeHardwareId + kSessionFilenameSuffix);
}

juce::File SecureSessionManager::getEncryptionKeyFile() const
{
    auto dir = LicenseCache::cacheDirectory().getChildFile("sessions");
    if (!dir.exists()) {
        dir.createDirectory();
    }
    return dir.getChildFile("encryption.key");
}

} // namespace samplar



