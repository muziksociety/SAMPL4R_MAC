# 🚨 CRITICAL BUG FIX - Installer Crash on Fresh Computer

## Problem

The installer crashed on a fresh computer with **Signal 11 (SIGSEGV - Segmentation Fault)** during service initialization:

```
???? [App] Registering variation services
?? [App] Initialising all services
???????????? [CRASH] Signal caught: 11
???????????? [CRASH] Application will terminate
```

## Root Cause

**File**: `src/services/FullSongService.cpp` (lines 15-18)

```cpp
void FullSongService::initialise() {
    // Create a log file for debugging
    juce::File logFile = juce::File::getCurrentWorkingDirectory().getChildFile("SAMPLAR_debug.log");
    auto logStream = logFile.createOutputStream();  // ❌ CRASHES HERE
```

### Why It Crashed:

1. App installed to `C:\Program Files\SAMPLAR\`
2. **Normal users don't have write permissions to `C:\Program Files\`**
3. `createOutputStream()` fails and returns `nullptr`
4. Code tries to use `logStream` without null-checking
5. **SEGFAULT** (Signal 11)

## Solution

**Removed the problematic file logging code** - the app already logs to the proper location via JUCE's logging system.

### Fixed Code:

```cpp
void FullSongService::initialise() {
    juce::Logger::writeToLog("🎵 [FullSongService] Initializing...");

#ifdef WITH_DEMUCS
    juce::Logger::writeToLog("🎵 [FullSongService] Demucs C++ support enabled");
#else
    juce::Logger::writeToLog("⚠️ [FullSongService] Demucs C++ not compiled - will use Python bridge for separation");
#endif

    // Log CLI path configuration
#ifdef DEMUCS_CPP_CLI_PATH
    juce::Logger::writeToLog("⚡ [FullSongService] DEMUCS_CPP_CLI_PATH defined as: " + juce::String(DEMUCS_CPP_CLI_PATH));
    juce::File cliFile(DEMUCS_CPP_CLI_PATH);
    if (cliFile.existsAsFile()) {
        juce::Logger::writeToLog("✅ [FullSongService] C++ Demucs CLI found and ready");
    } else {
        juce::Logger::writeToLog("❌ [FullSongService] C++ Demucs CLI path defined but file not found");
    }
#else
    juce::Logger::writeToLog("⚠️ [FullSongService] DEMUCS_CPP_CLI_PATH not defined - will check environment");
#endif
}
```

## Next Steps

### 1. Rebuild the Application

```powershell
# Clean build
Remove-Item -Recurse -Force build
mkdir build
cd build

# Configure with CMake
cmake .. -G "Visual Studio 17 2022" -A x64

# Build Release
cmake --build . --config Release -j 16
```

### 2. Rebuild the Installer

```powershell
# Return to root directory
cd ..

# Build installer (will use the newly built executable)
.\build_installer.ps1 -SkipBuild
```

### 3. Test on Fresh Computer

Copy `installer_output\Samplar_Setup_v1.0.0.exe` to a clean Windows machine and verify:

- ✅ Installation completes successfully
- ✅ App launches without crashing
- ✅ Services initialize properly
- ✅ Logs appear in `C:\Program Files\SAMPLAR\logs\SAMPLAR_console.log`

## Why This Happened

This is a **classic Windows permissions issue**:

- **Development machine**: You run as Administrator or have write access to your project folder
- **Production machine**: Normal users can't write to `C:\Program Files\`
- **The bug was hidden** during development because the working directory was writable

## Lesson Learned

**Always use proper user-writable locations for logs/data:**

- ✅ `juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)`
- ✅ `juce::File::getSpecialLocation(juce::File::tempDirectory)`
- ✅ JUCE's built-in logging system (already configured in `App.cpp`)
- ❌ `juce::File::getCurrentWorkingDirectory()` (might be read-only!)

## Status

- ✅ **Bug fixed** in source code
- ⏳ **Needs rebuild** of application
- ⏳ **Needs rebuild** of installer
- ⏳ **Needs testing** on fresh computer

---

**Date**: October 8, 2025  
**Severity**: CRITICAL (app crashes on startup)  
**Impact**: 100% of users on fresh installations  
**Fix**: Complete (code changed, needs rebuild)

