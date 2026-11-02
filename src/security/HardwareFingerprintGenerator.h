#pragma once

#include <atomic>
#include <string>

namespace samplar {

class HardwareFingerprintGenerator {
public:
    // ---- HWID v2 (stable binding; same scheme as SM5 / RUIDO) ----
    // The v1 fingerprint (first MAC + wmic / PowerShell serials, cached in
    // %APPDATA%\Samplar\.hardware_id) was removed: SAMPLAR never shipped v1
    // licence bindings and its child-process queries could hang the sign-in
    // thread (review L-H2 / L-M2). v2 ignores network adapters.

    /** upper(hex(MD5("VSTK-HWID-v2|" + OS machine id))), 32 chars, or empty if the
        OS gives no machine id. OS id: Windows MachineGuid (64-bit registry view),
        macOS IOPlatformUUID, Linux /etc/machine-id. Only the salted hash leaves
        the machine. Fast (no child processes on Windows). */
    std::string generateHardwareIdV2() const;

    /** THE licence machine ID (what the server binds and signs): HWID v2,
        computed ONCE per process and cached (thread-safe). If the OS gives no
        machine id, the same v2 hash over JUCE's SMBIOS device id. Never runs a
        child process on Windows (registry read), so it is safe on the message
        thread. */
    static std::string licenceMachineId();

    // ---- Bounded child processes ----
    struct CommandResult
    {
        bool started = false;
        bool timedOut = false;    // killed after timeoutMs
        bool cancelled = false;   // killed because *cancel became true
        std::string output;       // stdout+stderr, trimmed (empty unless it exited by itself)
    };

    /** Runs `commandLine` (Windows: passed to CreateProcess directly, no
        cmd.exe, so kill() reaches the real process; elsewhere /bin/sh -c) and
        polls it: never waits longer than `timeoutMs`, kills it on timeout or
        when `*cancel` turns true. Output is read only after it exited. */
    static CommandResult runBoundedCommand(const std::string& commandLine, int timeoutMs,
                                           const std::atomic<bool>* cancel = nullptr);

    static constexpr int kCommandTimeoutMs = 3000;

private:
    std::string getOsMachineId() const;
    std::string executeSystemCommand(const std::string& command) const;   // macOS machine-id query
};

} // namespace samplar

