#include "LicenseCache.h"

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_cryptography/juce_cryptography.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>

namespace samplar {

namespace {

constexpr auto kCacheFilename = "license_cache.json";
constexpr auto kFailuresFilename = "license_failures.json";
constexpr size_t kKeyBytes = 16;
constexpr auto kEncryptionSalt = "SAMPLAR_CACHE_KEY|";

const juce::Identifier kEncryptedId("enc");
const juce::Identifier kDataId("data");
const juce::Identifier kCountId("count");
const juce::Identifier kLastAttemptId("lastAttemptMs");

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

juce::String buildLockoutKey(const std::string& username, const std::string& hardwareId)
{
    juce::String key(username);
    key << "|" << hardwareId;
    return key.toUpperCase();
}

std::int64_t currentTimeMs()
{
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

} // namespace

bool LicenseCache::storeLicense(const std::string& licenseKey,
                                const juce::var& licenseData,
                                const std::string& hardwareId)
{
    std::scoped_lock lock(cacheMutex_);

    CacheEntry entry;
    entry.licenseKey = licenseKey;
    entry.licenseData = licenseData;
    entry.hardwareId = hardwareId;
    entry.cachedAt = std::chrono::system_clock::now();
    entry.lastValidated = entry.cachedAt;
    entry.integrityHash = computeIntegrityHash(entry);

    persistCache(entry);
    return true;
}

std::optional<LicenseCache::CacheEntry> LicenseCache::retrieveLicense(const std::string& hardwareId) const
{
    std::scoped_lock lock(cacheMutex_);
    juce::Logger::writeToLog("[LicenseCache] retrieveLicense called with hardwareId: " +
        juce::String(hardwareId.substr(0, 16)) + "...");

    auto entry = loadCache();
    if (!entry.has_value())
    {
        juce::Logger::writeToLog("[LicenseCache] No cache file or failed to load");
        return std::nullopt;
    }

    juce::Logger::writeToLog("[LicenseCache] Cache loaded, stored hardwareId: " +
        juce::String(entry->hardwareId.substr(0, 16)) + "...");
    juce::Logger::writeToLog("[LicenseCache] Cache loaded, licenseKey: " +
        juce::String(entry->licenseKey.empty() ? "(empty)" : entry->licenseKey.substr(0, 12) + "..."));

    if (entry->hardwareId != hardwareId)
    {
        juce::Logger::writeToLog("[LicenseCache] Hardware ID mismatch - cache invalid");
        return std::nullopt;
    }

    auto computedHash = computeIntegrityHash(*entry);
    if (entry->integrityHash != computedHash)
    {
        juce::Logger::writeToLog("[LicenseCache] Integrity mismatch detected");
        juce::Logger::writeToLog("[LicenseCache]   stored hash: " + juce::String(entry->integrityHash.substr(0, 16)) + "...");
        juce::Logger::writeToLog("[LicenseCache]   computed hash: " + juce::String(computedHash.substr(0, 16)) + "...");
        return std::nullopt;
    }

    juce::Logger::writeToLog("[LicenseCache] Cache retrieved successfully");
    return entry;
}

bool LicenseCache::isCacheValid(const std::string& hardwareId) const
{
    return retrieveLicense(hardwareId).has_value();
}

void LicenseCache::clearCache()
{
    std::scoped_lock lock(cacheMutex_);
    auto file = getLicenseCacheFile();
    if (file.existsAsFile())
        file.deleteFile();
}

bool LicenseCache::needsPeriodicValidation(const std::string& hardwareId, int intervalHours) const
{
    const auto entry = retrieveLicense(hardwareId);
    if (!entry.has_value())
        return true;

    const auto now = std::chrono::system_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::hours>(now - entry->lastValidated);
    return elapsed.count() >= intervalHours;
}

void LicenseCache::updateLastValidation(const std::string& hardwareId)
{
    std::scoped_lock lock(cacheMutex_);
    auto entry = loadCache();
    if (!entry.has_value() || entry->hardwareId != hardwareId)
        return;

    entry->lastValidated = std::chrono::system_clock::now();
    entry->integrityHash = computeIntegrityHash(*entry);
    persistCache(*entry);
}

LicenseCache::GraceStatus LicenseCache::getOfflineGraceStatus(const std::string& hardwareId, int gracePeriodHours) const
{
    GraceStatus status{};
    const auto entry = retrieveLicense(hardwareId);
    if (!entry.has_value())
        return status;

    const auto now = std::chrono::system_clock::now();
    const auto graceDuration = std::chrono::hours(gracePeriodHours);
    const auto graceExpiry = entry->lastValidated + graceDuration;

    status.graceExpiresAt = graceExpiry;
    if (now <= graceExpiry)
    {
        status.hasGrace = true;
        status.remainingTime = std::chrono::duration_cast<std::chrono::minutes>(graceExpiry - now);
    }

    return status;
}

void LicenseCache::recordFailedAttempt(const std::string& username, const std::string& hardwareId)
{
    std::scoped_lock lock(cacheMutex_);

    auto attempts = loadFailedAttempts();
    auto* root = attempts.getDynamicObject();
    if (root == nullptr)
    {
        root = new juce::DynamicObject();
        attempts = juce::var(root);
    }

    const auto key = juce::Identifier(buildLockoutKey(username, hardwareId));
    auto entryVar = root->getProperty(key);
    auto* entryObj = entryVar.getDynamicObject();
    if (entryObj == nullptr)
    {
        entryObj = new juce::DynamicObject();
        entryObj->setProperty(kCountId, 0);
        entryObj->setProperty(kLastAttemptId, 0.0);
        root->setProperty(key, juce::var(entryObj));
        entryVar = root->getProperty(key);
        entryObj = entryVar.getDynamicObject();
    }

    const auto currentCount = static_cast<int>(entryObj->getProperty(kCountId));
    entryObj->setProperty(kCountId, currentCount + 1);
    entryObj->setProperty(kLastAttemptId, static_cast<double>(currentTimeMs()));

    persistFailedAttempts(attempts);
}

bool LicenseCache::isUserLockedOut(const std::string& username, const std::string& hardwareId) const
{
    std::scoped_lock lock(cacheMutex_);

    auto attempts = loadFailedAttempts();
    auto* root = attempts.getDynamicObject();
    if (root == nullptr)
        return false;

    const auto keyId = juce::Identifier(buildLockoutKey(username, hardwareId));
    auto entryVar = root->getProperty(keyId);
    auto* entryObj = entryVar.getDynamicObject();
    if (entryObj == nullptr)
        return false;

    const auto count = static_cast<int>(entryObj->getProperty(kCountId));
    const auto lastMs = static_cast<double>(entryObj->getProperty(kLastAttemptId));
    const auto nowMs = static_cast<double>(currentTimeMs());
    const auto lockoutWindowMs = static_cast<double>(LOCKOUT_DURATION_MINUTES) * 60.0 * 1000.0;

    if (count < MAX_FAILED_ATTEMPTS)
    {
        if (lastMs > 0.0 && (nowMs - lastMs) > lockoutWindowMs)
        {
            root->removeProperty(keyId);
            persistFailedAttempts(attempts);
        }
        return false;
    }

    if ((nowMs - lastMs) < lockoutWindowMs)
        return true;

    root->removeProperty(keyId);
    persistFailedAttempts(attempts);
    return false;
}

juce::File LicenseCache::getCacheDirectory() const
{
    auto overridePath = juce::SystemStats::getEnvironmentVariable("SAMPLAR_CACHE_DIR_OVERRIDE", {});
    juce::File dir = overridePath.isNotEmpty()
                         ? juce::File(overridePath)
                         : juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                               .getChildFile("Samplar");

    if (!dir.exists())
        dir.createDirectory();

    return dir;
}

juce::File LicenseCache::getLicenseCacheFile() const
{
    return getCacheDirectory().getChildFile(kCacheFilename);
}

juce::File LicenseCache::getFailedAttemptsFile() const
{
    return getCacheDirectory().getChildFile(kFailuresFilename);
}

std::string LicenseCache::computeIntegrityHash(const CacheEntry& entry) const
{
    const auto cachedMs = std::chrono::duration_cast<std::chrono::milliseconds>(entry.cachedAt.time_since_epoch()).count();
    const auto validatedMs = std::chrono::duration_cast<std::chrono::milliseconds>(entry.lastValidated.time_since_epoch()).count();

    juce::String payload;
    payload << entry.licenseKey << "|"
            << entry.hardwareId << "|"
            << cachedMs << "|"
            << validatedMs << "|"
            << juce::JSON::toString(entry.licenseData, false);

    const auto key = deriveKeyMaterial();
    return computeHMAC(key, payload);
}

std::string LicenseCache::computeHMAC(const juce::MemoryBlock& key, const juce::String& message) const
{
    constexpr size_t blockSize = 64; // SHA-256 block size

    // If key > blockSize, hash it first; otherwise use as-is
    juce::MemoryBlock keyBlock;
    if (key.getSize() > blockSize)
    {
        juce::SHA256 keyHash(key.getData(), key.getSize());
        auto raw = keyHash.getRawData();
        keyBlock = juce::MemoryBlock(raw.getData(), raw.getSize());
    }
    else
    {
        keyBlock = key;
    }

    // Zero-pad key to blockSize
    keyBlock.setSize(blockSize, true);

    // Build inner and outer padding
    juce::MemoryBlock ipad(blockSize, true);
    juce::MemoryBlock opad(blockSize, true);
    auto* keyBytes = static_cast<const uint8_t*>(keyBlock.getData());
    auto* ipadBytes = static_cast<uint8_t*>(ipad.getData());
    auto* opadBytes = static_cast<uint8_t*>(opad.getData());

    for (size_t i = 0; i < blockSize; ++i)
    {
        ipadBytes[i] = keyBytes[i] ^ 0x36;
        opadBytes[i] = keyBytes[i] ^ 0x5c;
    }

    // Inner hash: SHA256(ipad || message)
    const auto* msgRaw = message.toRawUTF8();
    const auto msgLen = static_cast<size_t>(message.getNumBytesAsUTF8());
    juce::MemoryBlock innerInput;
    innerInput.append(ipad.getData(), ipad.getSize());
    innerInput.append(msgRaw, msgLen);
    juce::SHA256 innerHash(innerInput.getData(), innerInput.getSize());
    auto innerResult = innerHash.getRawData();

    // Outer hash: SHA256(opad || innerHash)
    juce::MemoryBlock outerInput;
    outerInput.append(opad.getData(), opad.getSize());
    outerInput.append(innerResult.getData(), innerResult.getSize());
    juce::SHA256 outerHash(outerInput.getData(), outerInput.getSize());

    return outerHash.toHexString().toStdString();
}

juce::var LicenseCache::encryptData(const juce::var& data) const
{
    if (data.isVoid())
        return data;

    if (data.isObject())
        if (auto* obj = data.getDynamicObject())
            if (auto flag = obj->getProperty(kEncryptedId); !flag.isVoid() && static_cast<bool>(flag))
                return data;

    const auto json = juce::JSON::toString(data, false);
    juce::MemoryBlock plain(json.toRawUTF8(), static_cast<size_t>(json.getNumBytesAsUTF8()));

    const auto key = deriveKeyMaterial();
    juce::BlowFish cipher(key.getData(), static_cast<int>(key.getSize()));
    cipher.encrypt(plain);

    auto* obj = new juce::DynamicObject();
    obj->setProperty(kEncryptedId, true);
    obj->setProperty(kDataId, toBase64(plain));
    return juce::var(obj);
}

juce::var LicenseCache::decryptData(const juce::var& encryptedData) const
{
    if (!encryptedData.isObject())
        return encryptedData;

    auto* obj = encryptedData.getDynamicObject();
    if (obj == nullptr)
        return encryptedData;

    auto flag = obj->getProperty(kEncryptedId);
    if (flag.isVoid() || !static_cast<bool>(flag))
        return encryptedData;

    const auto cipherBlock = fromBase64(obj->getProperty(kDataId).toString());
    if (cipherBlock.getSize() == 0)
        return {};

    auto key = deriveKeyMaterial();
    juce::BlowFish cipher(key.getData(), static_cast<int>(key.getSize()));

    juce::MemoryBlock plain(cipherBlock);
    cipher.decrypt(plain);

    // BlowFish uses 8-byte blocks with padding - find actual content length
    // by looking for null bytes or finding the JSON end
    const char* data = static_cast<const char*>(plain.getData());
    size_t actualSize = plain.getSize();

    // Trim trailing null bytes (padding)
    while (actualSize > 0 && data[actualSize - 1] == '\0')
        --actualSize;

    if (actualSize == 0)
        return {};

    juce::String json = juce::String::fromUTF8(data, static_cast<int>(actualSize));

    // Additional cleanup: trim any trailing whitespace or garbage
    json = json.trim();

    auto parsed = juce::JSON::parse(json);
    if (parsed.isVoid())
    {
        juce::Logger::writeToLog("[LicenseCache] JSON parse failed, json length: " +
            juce::String(static_cast<int>(json.length())));
        return {};
    }

    return parsed;
}

std::optional<LicenseCache::CacheEntry> LicenseCache::loadCache() const
{
    const auto file = getLicenseCacheFile();
    if (!file.existsAsFile())
        return std::nullopt;

    const auto json = file.loadFileAsString();
    const auto parsed = juce::JSON::parse(json);
    if (!parsed.isObject())
        return std::nullopt;

    const auto* object = parsed.getDynamicObject();
    if (object == nullptr)
        return std::nullopt;

    CacheEntry entry;
    entry.licenseKey = object->getProperty("licenseKey").toString().toStdString();
    entry.hardwareId = object->getProperty("hardwareId").toString().toStdString();
    entry.integrityHash = object->getProperty("integrityHash").toString().toStdString();

    // Decrypt the blob which contains licenseData + timestamps
    auto encryptedBlob = object->getProperty("licenseData");
    auto decrypted = decryptData(encryptedBlob);
    if (decrypted.isVoid() || !decrypted.isObject())
    {
        juce::Logger::writeToLog("[LicenseCache] Failed to decrypt cache blob");
        return std::nullopt;
    }

    auto* inner = decrypted.getDynamicObject();
    if (inner == nullptr)
        return std::nullopt;

    entry.licenseData = inner->getProperty("licenseData");

    const auto cachedMs = static_cast<std::int64_t>(inner->getProperty("cachedAtMs"));
    const auto validatedMs = static_cast<std::int64_t>(inner->getProperty("lastValidatedMs"));
    entry.cachedAt = std::chrono::system_clock::time_point(std::chrono::milliseconds(cachedMs));
    entry.lastValidated = std::chrono::system_clock::time_point(std::chrono::milliseconds(validatedMs));

    return entry;
}

void LicenseCache::persistCache(const CacheEntry& entry) const
{
    // Bundle licenseData + timestamps into a single object, then encrypt
    auto* innerObj = new juce::DynamicObject();
    innerObj->setProperty("licenseData", entry.licenseData);
    innerObj->setProperty("cachedAtMs", static_cast<double>(
        std::chrono::duration_cast<std::chrono::milliseconds>(entry.cachedAt.time_since_epoch()).count()));
    innerObj->setProperty("lastValidatedMs", static_cast<double>(
        std::chrono::duration_cast<std::chrono::milliseconds>(entry.lastValidated.time_since_epoch()).count()));

    auto encryptedBlob = encryptData(juce::var(innerObj));

    // Write only encrypted blob — no plaintext timestamps on disk
    auto* object = new juce::DynamicObject();
    object->setProperty("licenseKey", juce::String(entry.licenseKey));
    object->setProperty("licenseData", encryptedBlob);
    object->setProperty("hardwareId", juce::String(entry.hardwareId));
    object->setProperty("integrityHash", juce::String(entry.integrityHash));

    const auto json = juce::JSON::toString(juce::var(object));
    getLicenseCacheFile().replaceWithText(json);
}

void LicenseCache::persistFailedAttempts(const juce::var& attempts) const
{
    auto target = attempts;
    if (!target.isObject())
        target = juce::var(new juce::DynamicObject());

    const auto json = juce::JSON::toString(target, false);
    getFailedAttemptsFile().replaceWithText(json);
}

juce::var LicenseCache::loadFailedAttempts() const
{
    const auto file = getFailedAttemptsFile();
    if (!file.existsAsFile())
        return juce::var(new juce::DynamicObject());

    const auto json = file.loadFileAsString();
    auto parsed = juce::JSON::parse(json);
    if (!parsed.isObject())
        return juce::var(new juce::DynamicObject());

    return parsed;
}

juce::MemoryBlock LicenseCache::deriveKeyMaterial() const
{
    std::call_once(keyInitFlag_, [this]() {
        const auto source = juce::String(kEncryptionSalt) + juce::SystemStats::getUniqueDeviceID();
        juce::SHA256 hash(source.toUTF8());
        auto raw = hash.getRawData();
        const auto bytes = static_cast<size_t>(juce::jmin<int>(static_cast<int>(raw.getSize()), static_cast<int>(kKeyBytes)));
        keyMaterial_ = juce::MemoryBlock(raw.getData(), bytes);
        if (keyMaterial_.getSize() < kKeyBytes)
            keyMaterial_.setSize(kKeyBytes, true);
    });

    return keyMaterial_;
}

std::vector<std::string> LicenseCache::getAllCachedHardwareIds() const
{
    std::scoped_lock lock(cacheMutex_);
    std::vector<std::string> result;

    juce::Logger::writeToLog("[LicenseCache] getAllCachedHardwareIds: Scanning for cached hardware IDs...");

    // Load the cache file directly without hardware ID validation
    auto entry = loadCache();
    if (entry.has_value() && !entry->hardwareId.empty())
    {
        juce::Logger::writeToLog("[LicenseCache] Found cached hardware ID: " +
            juce::String(entry->hardwareId.substr(0, 16)) + "...");
        result.push_back(entry->hardwareId);
    }
    else
    {
        juce::Logger::writeToLog("[LicenseCache] No cached hardware ID found");
    }

    return result;
}

} // namespace samplar









