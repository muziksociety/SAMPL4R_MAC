#include "HardwareFingerprintGenerator.h"

#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

#if JUCE_WINDOWS
#include <windows.h>
#endif

namespace samplar {

namespace {

#if ! JUCE_WINDOWS
// (macOS / Linux machine-id query only)
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
#endif

// Commands are run without a shell on Windows; refuse anything shell-like anyway.
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

std::string HardwareFingerprintGenerator::executeSystemCommand(const std::string& command) const
{
    if (command.empty())
        return {};

    juce::String cmd(command);
    cmd = cmd.trim();

    if (cmd.isEmpty() || containsDisallowedShellChars(cmd))
        return {};

    const auto result = runBoundedCommand(cmd.toStdString(), kCommandTimeoutMs);
    if (result.timedOut)
        juce::Logger::writeToLog("[HardwareFingerprint] hardware query timed out after " +
                                 juce::String(kCommandTimeoutMs) + " ms - killed, value treated as empty");
    return result.output;
}

HardwareFingerprintGenerator::CommandResult HardwareFingerprintGenerator::runBoundedCommand(
    const std::string& commandLine, int timeoutMs, const std::atomic<bool>* cancel)
{
    CommandResult result;
    juce::ChildProcess child;

#if JUCE_WINDOWS
    // Straight to CreateProcess (no cmd.exe wrapper): kill() then terminates the
    // query itself instead of leaving an orphaned wmic / PowerShell behind.
    result.started = child.start(juce::String(commandLine));
#else
    result.started = child.start(juce::StringArray { "/bin/sh", "-c", juce::String(commandLine) });
#endif
    if (!result.started)
        return result;

    // Poll; never block in a read (ChildProcess reads wait for the pipe to close,
    // which a hung WMI provider never does). Our queries print a few bytes, far
    // below the pipe buffer, so the child is never stalled on a full pipe.
    const auto start = juce::Time::getMillisecondCounter();
    while (child.isRunning())
    {
        if (cancel != nullptr && cancel->load())
        {
            result.cancelled = true;
            break;
        }
        if ((int) (juce::Time::getMillisecondCounter() - start) >= timeoutMs)
        {
            result.timedOut = true;
            break;
        }
        juce::Thread::sleep(10);
    }

    if (result.timedOut || result.cancelled)
    {
        child.kill();
        return result;   // partial output is never used as an identifier
    }

    result.output = juce::String(child.readAllProcessOutput()).trim().toStdString();
    return result;
}

std::string HardwareFingerprintGenerator::licenceMachineId()
{
    // Once per process: every plugin instance (and the update checker) shares it.
    static const std::string id = []
    {
        HardwareFingerprintGenerator generator;
        auto v2 = generator.generateHardwareIdV2();
        if (v2.empty())
        {
            // No OS machine id (should not happen on Windows): same v2 scheme over
            // JUCE's SMBIOS-based device id. Still no child process.
            const auto device = juce::SystemStats::getUniqueDeviceID().trim().toUpperCase();
            if (device.isNotEmpty())
            {
                const juce::MD5 md5((juce::String("VSTK-HWID-v2|") + device).toUTF8());
                v2 = md5.toHexString().substring(0, 32).toUpperCase().toStdString();
            }
            juce::Logger::writeToLog("[HardwareFingerprint] licenceMachineId: no OS machine id - device-id fallback " +
                                     juce::String(v2.empty() ? "FAILED" : "used"));
        }
        return v2;
    }();
    return id;
}

// ---- HWID v2 (ported from SM5 362b611b) -------------------------------------

std::string HardwareFingerprintGenerator::getOsMachineId() const
{
    // Raw OS identifier - NEVER log or send it; only its salted hash leaves the machine.
    juce::String id;
#if JUCE_WINDOWS
    id = juce::WindowsRegistry::getValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Cryptography\\MachineGuid",
                                         {}, juce::WindowsRegistry::WoW64_64bit);
#elif JUCE_MAC
    id = pickFirstMeaningfulLine(juce::String(executeSystemCommand(
        "ioreg -rd1 -c IOPlatformExpertDevice | awk '/IOPlatformUUID/ { print $4 }'")));
#elif JUCE_LINUX || JUCE_BSD
    for (auto* path : { "/etc/machine-id", "/var/lib/dbus/machine-id" })
    {
        const auto content = readFileTrimmed(juce::File(path));
        if (!content.empty())
        {
            id = content;
            break;
        }
    }
#endif
    // Same normalisation as SM5 / RUIDO (trim + uppercase) so all products hash alike.
    return id.trim().toUpperCase().toStdString();
}

std::string HardwareFingerprintGenerator::generateHardwareIdV2() const
{
    juce::Logger::writeToLog("[HardwareFingerprint] === generateHardwareIdV2 START ===");

    const auto osId = getOsMachineId();
    if (osId.empty())
    {
        juce::Logger::writeToLog("[HardwareFingerprint] generateHardwareIdV2: no OS machine id - v2 omitted");
        return {};
    }

    const juce::MD5 md5((juce::String("VSTK-HWID-v2|") + juce::String(osId)).toUTF8());
    const auto v2 = md5.toHexString().substring(0, 32).toUpperCase().toStdString();

    juce::Logger::writeToLog("[HardwareFingerprint] === generateHardwareIdV2 END === v2: " +
        juce::String(v2.substr(0, 8)) + "...");
    return v2;
}

} // namespace samplar


