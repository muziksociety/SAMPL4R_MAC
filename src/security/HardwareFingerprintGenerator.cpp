#include "HardwareFingerprintGenerator.h"

#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>

#include <fstream>
#include <optional>
#include <sstream>
#include <string>

#if JUCE_WINDOWS
#include <windows.h>
#endif

namespace samplar {

namespace {

constexpr auto kCacheFileName = ".hardware_id";
constexpr auto kAppDataFolderName = "Samplar";

juce::String pickFirstMeaningfulLine(const juce::String& output, juce::StringArray headers = {})
{
    juce::StringArray lines;
    lines.addLines(output);

    for (auto line : lines)
    {
        auto trimmed = line.trim();
        if (trimmed.isEmpty())
            continue;

        bool skip = false;
        for (const auto& header : headers)
        {
            if (trimmed.compareIgnoreCase(header) == 0 || trimmed.startsWithIgnoreCase(header))
            {
                skip = true;
                break;
            }
        }

        if (skip)
            continue;

        return trimmed.unquoted().trim();
    }

    return {};
}

std::string readFileTrimmed(const juce::File& file)
{
    if (!file.existsAsFile())
        return {};

    return file.loadFileAsString().trim().toStdString();
}

bool containsDisallowedShellChars(const juce::String& command)
{
    return command.containsChar(';')
        || command.contains("&&")
        || command.contains("||")
        || command.containsChar('`')
        || command.containsChar('\n')
        || command.containsChar('\r');
}

} // namespace

std::string HardwareFingerprintGenerator::generateHardwareId()
{
    std::scoped_lock lock(hardwareIdMutex_);

    juce::Logger::writeToLog("[HardwareFingerprint] === generateHardwareId START ===");

    // 1. Check in-memory cache first (fastest)
    if (cachedHardwareId_.has_value())
    {
        juce::Logger::writeToLog("[HardwareFingerprint] Using in-memory cached ID: " +
            juce::String(cachedHardwareId_->substr(0, 16)) + "...");
        return *cachedHardwareId_;
    }

    // 2. Try to load from persistent disk cache (survives app restarts)
    auto diskCached = loadCachedHardwareIdFromDisk();
    if (diskCached.has_value() && !diskCached->empty())
    {
        juce::Logger::writeToLog("[HardwareFingerprint] Loaded from disk cache: " +
            juce::String(diskCached->substr(0, 16)) + "...");
        cachedHardwareId_ = diskCached;
        return *cachedHardwareId_;
    }

    // 3. No cache found - generate new hardware ID
    juce::Logger::writeToLog("[HardwareFingerprint] No cache found, generating new hardware ID...");

    const auto components = gatherHardwareComponents();

    // Log all components for debugging
    juce::Logger::writeToLog("[HardwareFingerprint] Components gathered:");
    juce::Logger::writeToLog("[HardwareFingerprint]   CPU: " + juce::String(components.cpuInfo));
    juce::Logger::writeToLog("[HardwareFingerprint]   MAC: " +
        juce::String(components.macAddress.empty() ? "(empty)" : components.macAddress));
    juce::Logger::writeToLog("[HardwareFingerprint]   Motherboard: " +
        juce::String(components.motherboardSerial.empty() ? "(empty)" : components.motherboardSerial));
    juce::Logger::writeToLog("[HardwareFingerprint]   SystemUUID: " +
        juce::String(components.systemUuid.empty() ? "(empty)" : components.systemUuid));
    juce::Logger::writeToLog("[HardwareFingerprint]   DiskSerial: " +
        juce::String(components.diskSerial.empty() ? "(empty)" : components.diskSerial));

    const auto payload = buildFingerprintPayload(components);
    const auto newHardwareId = computeSha256Hash(payload);

    juce::Logger::writeToLog("[HardwareFingerprint] Generated new ID: " +
        juce::String(newHardwareId.substr(0, 16)) + "...");

    // 4. Save to persistent disk cache for future app launches
    if (saveCachedHardwareIdToDisk(newHardwareId))
    {
        juce::Logger::writeToLog("[HardwareFingerprint] Saved to disk cache successfully");
    }
    else
    {
        juce::Logger::writeToLog("[HardwareFingerprint] WARNING: Failed to save to disk cache!");
    }

    cachedHardwareId_ = newHardwareId;

    juce::Logger::writeToLog("[HardwareFingerprint] === generateHardwareId END ===");
    return *cachedHardwareId_;
}

bool HardwareFingerprintGenerator::verifyHardwareIdConsistency()
{
    constexpr int iterations = 10;
    std::optional<std::string> baseline;

    for (int i = 0; i < iterations; ++i)
    {
        const auto components = gatherHardwareComponents();
        const auto current = computeSha256Hash(buildFingerprintPayload(components));

        if (!baseline)
        {
            baseline = current;
        }
        else if (*baseline != current)
        {
            juce::Logger::writeToLog("[HardwareFingerprint] Fingerprint mismatch detected");
            return false;
        }

        juce::Thread::sleep(10);
    }

    if (baseline)
    {
        std::scoped_lock lock(hardwareIdMutex_);
        cachedHardwareId_ = baseline;
    }

    return baseline.has_value();
}

HardwareFingerprintGenerator::HardwareComponents HardwareFingerprintGenerator::gatherHardwareComponents() const
{
    HardwareComponents components{};
    components.cpuInfo = getCpuInfo();
    components.macAddress = getMacAddress();
    components.motherboardSerial = getMotherboardSerial();
    components.systemUuid = getSystemUuid();
    components.diskSerial = getDiskSerial();
    return components;
}

std::string HardwareFingerprintGenerator::getCpuInfo() const
{
    juce::String info = juce::SystemStats::getCpuVendor() + "|" + juce::SystemStats::getCpuModel();
    info << "|" << juce::SystemStats::getNumCpus();
    return info.toStdString();
}

std::string HardwareFingerprintGenerator::getMacAddress() const
{
    auto addresses = juce::MACAddress::getAllAddresses();

    juce::Logger::writeToLog("[HardwareFingerprint] getMacAddress: Found " +
        juce::String(addresses.size()) + " network adapters");

    int adapterIndex = 0;
    for (const auto& address : addresses)
    {
        auto addrStr = address.toString();
        bool isNull = address.isNull();

        juce::Logger::writeToLog("[HardwareFingerprint]   Adapter[" + juce::String(adapterIndex) +
            "]: " + addrStr + (isNull ? " (null - skipping)" : ""));

        if (isNull)
        {
            adapterIndex++;
            continue;
        }

        auto asString = addrStr.removeCharacters("-:");
        if (asString.trim().isNotEmpty())
        {
            juce::Logger::writeToLog("[HardwareFingerprint]   -> SELECTED Adapter[" +
                juce::String(adapterIndex) + "]: " + asString.toLowerCase());
            return asString.toLowerCase().toStdString();
        }

        adapterIndex++;
    }

    juce::Logger::writeToLog("[HardwareFingerprint] getMacAddress: No valid MAC address found!");
    return {};
}

std::string HardwareFingerprintGenerator::getMotherboardSerial() const
{
#if JUCE_WINDOWS
    auto output = juce::String(executeSystemCommand("wmic baseboard get serialnumber"));
    auto serial = pickFirstMeaningfulLine(output, juce::StringArray{ "SerialNumber", "Serial Number", "Serial" });
    if (serial.isNotEmpty())
        return serial.toStdString();

    output = juce::String(executeSystemCommand("powershell -NoProfile -Command \"(Get-CimInstance -ClassName Win32_BaseBoard).SerialNumber\""));
    serial = pickFirstMeaningfulLine(output);
    if (serial.isNotEmpty())
        return serial.toStdString();
#elif JUCE_MAC
    auto output = juce::String(executeSystemCommand("ioreg -rd1 -c IOPlatformExpertDevice | awk '/IOPlatformSerialNumber/ { print $4 }'"));
    auto serial = pickFirstMeaningfulLine(output);
    if (serial.isNotEmpty())
        return serial.toStdString();

    output = juce::String(executeSystemCommand("system_profiler SPHardwareDataType | awk '/Serial Number/ {print $4}'"));
    serial = pickFirstMeaningfulLine(output);
    if (serial.isNotEmpty())
        return serial.toStdString();
#elif JUCE_LINUX || JUCE_BSD
    auto serial = readFileTrimmed(juce::File("/sys/class/dmi/id/board_serial"));
    if (!serial.empty())
        return serial;

    serial = readFileTrimmed(juce::File("/sys/class/dmi/id/product_serial"));
    if (!serial.empty())
        return serial;
#endif
    return {};
}

std::string HardwareFingerprintGenerator::getSystemUuid() const
{
#if JUCE_WINDOWS
    auto output = juce::String(executeSystemCommand("wmic csproduct get uuid"));
    auto uuid = pickFirstMeaningfulLine(output, juce::StringArray{ "UUID" });
    if (uuid.isNotEmpty())
        return uuid.toStdString();

    output = juce::String(executeSystemCommand("powershell -NoProfile -Command \"(Get-CimInstance -ClassName Win32_ComputerSystemProduct).UUID\""));
    uuid = pickFirstMeaningfulLine(output);
    if (uuid.isNotEmpty())
        return uuid.toStdString();
#elif JUCE_MAC
    auto output = juce::String(executeSystemCommand("ioreg -rd1 -c IOPlatformExpertDevice | awk '/IOPlatformUUID/ { print $4 }'"));
    auto uuid = pickFirstMeaningfulLine(output);
    if (uuid.isNotEmpty())
        return uuid.toStdString();
#elif JUCE_LINUX || JUCE_BSD
    auto uuid = readFileTrimmed(juce::File("/sys/class/dmi/id/product_uuid"));
    if (!uuid.empty())
        return uuid;

    uuid = readFileTrimmed(juce::File("/proc/sys/kernel/random/boot_id"));
    if (!uuid.empty())
        return uuid;
#endif
    return juce::SystemStats::getUniqueDeviceID().toStdString();
}

std::string HardwareFingerprintGenerator::getDiskSerial() const
{
    juce::Logger::writeToLog("[HardwareFingerprint] getDiskSerial: Querying disk serials...");

#if JUCE_WINDOWS
    auto output = juce::String(executeSystemCommand("wmic diskdrive get serialnumber"));
    juce::Logger::writeToLog("[HardwareFingerprint] getDiskSerial wmic output:\n" + output);

    auto serial = pickFirstMeaningfulLine(output, juce::StringArray{ "SerialNumber", "Serial Number", "Serial" });
    if (serial.isNotEmpty())
    {
        juce::Logger::writeToLog("[HardwareFingerprint] getDiskSerial: Selected (wmic): " + serial);
        return serial.toStdString();
    }

    output = juce::String(executeSystemCommand("powershell -NoProfile -Command \"(Get-PhysicalDisk | Select -First 1 -ExpandProperty SerialNumber)\""));
    juce::Logger::writeToLog("[HardwareFingerprint] getDiskSerial PowerShell output: " + output);

    serial = pickFirstMeaningfulLine(output);
    if (serial.isNotEmpty())
    {
        juce::Logger::writeToLog("[HardwareFingerprint] getDiskSerial: Selected (PowerShell): " + serial);
        return serial.toStdString();
    }
#elif JUCE_MAC
    auto output = juce::String(executeSystemCommand("system_profiler SPStorageDataType | awk '/Serial Number/ { print $3 }'"));
    auto serial = pickFirstMeaningfulLine(output);
    if (serial.isNotEmpty())
        return serial.toStdString();
#elif JUCE_LINUX || JUCE_BSD
    auto serial = readFileTrimmed(juce::File("/sys/block/sda/device/serial"));
    if (!serial.empty())
        return serial;

    serial = pickFirstMeaningfulLine(juce::String(executeSystemCommand("lsblk -dn -o SERIAL")));
    if (!serial.empty())
        return serial;
#endif
    return {};
}

std::string HardwareFingerprintGenerator::executeSystemCommand(const std::string& command) const
{
    if (command.empty())
        return {};

    juce::String cmd(command);
    cmd = cmd.trim();

    if (cmd.isEmpty() || containsDisallowedShellChars(cmd))
        return {};

    juce::StringArray args;
#if JUCE_WINDOWS
    args.add("cmd.exe");
    args.add("/C");
#else
    args.add("/bin/sh");
    args.add("-c");
#endif
    args.add(cmd);

    juce::ChildProcess child;
    if (!child.start(args))
        return {};

    auto output = child.readAllProcessOutput();

    if (!child.waitForProcessToFinish(3000))
        child.kill();

    output += child.readAllProcessOutput();
    return juce::String(output).trim().toStdString();
}

std::string HardwareFingerprintGenerator::computeSha256Hash(const std::string& input) const
{
    juce::MemoryBlock block(input.data(), input.size());
    const juce::SHA256 digest(block);
    return digest.toHexString().toStdString();
}

std::string HardwareFingerprintGenerator::buildFingerprintPayload(const HardwareComponents& components) const
{
    std::ostringstream ss;
    ss << components.cpuInfo << "|"
       << components.macAddress << "|"
       << components.motherboardSerial << "|"
       << components.systemUuid << "|"
       << components.diskSerial;
    return ss.str();
}

std::string HardwareFingerprintGenerator::getCacheFilePath() const
{
#if JUCE_WINDOWS
    // Windows: %APPDATA%\Samplar\.hardware_id
    auto appDataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#elif JUCE_MAC
    // macOS: ~/Library/Application Support/Samplar/.hardware_id
    auto appDataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#else
    // Linux: ~/.config/Samplar/.hardware_id
    auto appDataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#endif

    auto appDir = appDataDir.getChildFile(kAppDataFolderName);
    return appDir.getChildFile(kCacheFileName).getFullPathName().toStdString();
}

std::optional<std::string> HardwareFingerprintGenerator::loadCachedHardwareIdFromDisk() const
{
    const auto cachePath = getCacheFilePath();
    juce::File cacheFile(cachePath);

    juce::Logger::writeToLog("[HardwareFingerprint] Checking disk cache at: " + juce::String(cachePath));

    if (!cacheFile.existsAsFile())
    {
        juce::Logger::writeToLog("[HardwareFingerprint] Disk cache file does not exist");
        return std::nullopt;
    }

    auto content = cacheFile.loadFileAsString().trim();
    if (content.isEmpty())
    {
        juce::Logger::writeToLog("[HardwareFingerprint] Disk cache file is empty");
        return std::nullopt;
    }

    // Validate: hardware ID should be a 64-character hex string (SHA256)
    if (content.length() != 64)
    {
        juce::Logger::writeToLog("[HardwareFingerprint] WARNING: Disk cache has invalid length (" +
            juce::String(content.length()) + "), expected 64");
        return std::nullopt;
    }

    // Check if it's valid hex (0-9, a-f, A-F)
    for (int i = 0; i < content.length(); ++i)
    {
        auto c = content[i];
        bool isHex = (c >= '0' && c <= '9') ||
                     (c >= 'a' && c <= 'f') ||
                     (c >= 'A' && c <= 'F');
        if (!isHex)
        {
            juce::Logger::writeToLog("[HardwareFingerprint] WARNING: Disk cache contains non-hex character");
            return std::nullopt;
        }
    }

    juce::Logger::writeToLog("[HardwareFingerprint] Disk cache is valid");
    return content.toStdString();
}

bool HardwareFingerprintGenerator::saveCachedHardwareIdToDisk(const std::string& hardwareId) const
{
    if (hardwareId.empty() || hardwareId.length() != 64)
    {
        juce::Logger::writeToLog("[HardwareFingerprint] Cannot save invalid hardware ID to disk");
        return false;
    }

    const auto cachePath = getCacheFilePath();
    juce::File cacheFile(cachePath);

    juce::Logger::writeToLog("[HardwareFingerprint] Saving to disk cache: " + juce::String(cachePath));

    // Ensure parent directory exists
    auto parentDir = cacheFile.getParentDirectory();
    if (!parentDir.exists())
    {
        juce::Logger::writeToLog("[HardwareFingerprint] Creating directory: " + parentDir.getFullPathName());
        if (!parentDir.createDirectory())
        {
            juce::Logger::writeToLog("[HardwareFingerprint] ERROR: Failed to create directory");
            return false;
        }
    }

    // Write the hardware ID to file
    if (!cacheFile.replaceWithText(juce::String(hardwareId)))
    {
        juce::Logger::writeToLog("[HardwareFingerprint] ERROR: Failed to write cache file");
        return false;
    }

    // Set file as hidden on Windows
#if JUCE_WINDOWS
    // The file already starts with "." which is the Unix convention
    // On Windows, we could additionally set the hidden attribute
    SetFileAttributesW(cacheFile.getFullPathName().toWideCharPointer(), FILE_ATTRIBUTE_HIDDEN);
#endif

    return true;
}

bool HardwareFingerprintGenerator::migrateFromExistingHardwareId(const std::string& existingHardwareId)
{
    std::scoped_lock lock(hardwareIdMutex_);

    juce::Logger::writeToLog("[HardwareFingerprint] === migrateFromExistingHardwareId START ===");
    juce::Logger::writeToLog("[HardwareFingerprint] Migrating hardware ID: " +
        juce::String(existingHardwareId.empty() ? "(empty)" : existingHardwareId.substr(0, 16) + "..."));

    if (existingHardwareId.empty() || existingHardwareId.length() != 64)
    {
        juce::Logger::writeToLog("[HardwareFingerprint] Invalid hardware ID for migration");
        return false;
    }

    // Check if we already have a cached ID
    auto diskCached = loadCachedHardwareIdFromDisk();
    if (diskCached.has_value() && !diskCached->empty())
    {
        juce::Logger::writeToLog("[HardwareFingerprint] Disk cache already exists, skipping migration");
        return false;
    }

    // Save the existing hardware ID to disk cache
    if (saveCachedHardwareIdToDisk(existingHardwareId))
    {
        cachedHardwareId_ = existingHardwareId;
        juce::Logger::writeToLog("[HardwareFingerprint] Migration successful");
        return true;
    }

    juce::Logger::writeToLog("[HardwareFingerprint] Migration failed");
    return false;
}

} // namespace samplar


