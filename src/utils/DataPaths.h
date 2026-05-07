#pragma once

#include <juce_core/juce_core.h>

namespace samplar {

/**
 * Centralized path resolution for application resources.
 *
 * Phase 0 (VST3 prep): All resource lookups funnelled through here
 * so that a single change adapts standalone vs. plugin paths later.
 *
 * Resolution order for app directory:
 *   1. Explicit override (set via setAppDirectory — for VST3 / tests)
 *   2. Exe parent directory  (standalone builds)
 *   3. CWD fallback          (dev / portable builds)
 */
class DataPaths
{
public:
    // ── Override (for VST3 or testing) ──────────────────────────
    static void setAppDirectory(const juce::File& dir)
    {
        sOverrideAppDir = dir;
        juce::Logger::writeToLog("[DataPaths] App directory override set: "
                                 + dir.getFullPathName());
    }

    static void clearAppDirectory()
    {
        sOverrideAppDir = juce::File();
        juce::Logger::writeToLog("[DataPaths] App directory override cleared");
    }

    // ── Primary directory accessors ─────────────────────────────

    /** Root directory where the application's bundled resources live. */
    static juce::File getAppDirectory()
    {
        if (sOverrideAppDir.exists())
            return sOverrideAppDir;

        auto exeDir = juce::File::getSpecialLocation(
                          juce::File::currentExecutableFile)
                          .getParentDirectory();

        // Validate: exe dir should contain known sub-folders
        if (exeDir.getChildFile("data").isDirectory()
            || exeDir.getChildFile("models").isDirectory()
            || exeDir.getChildFile("python").isDirectory())
        {
            return exeDir;
        }

        // CWD fallback (dev / portable)
        auto cwd = juce::File::getCurrentWorkingDirectory();
        if (cwd.getChildFile("data").isDirectory())
            return cwd;

        // Last resort: exe dir anyway
        return exeDir;
    }

    /** data/ directory — patterns, JSON configs, etc. */
    static juce::File getDataDirectory()
    {
        return getAppDirectory().getChildFile("data");
    }

    /** models/ directory — ML model files & configs. */
    static juce::File getModelsDirectory()
    {
        return getAppDirectory().getChildFile("models");
    }

    /** python/ directory — embedded Python + packages. */
    static juce::File getPythonDirectory()
    {
        return getAppDirectory().getChildFile("python");
    }

    /** BACKGROUNDS/ directory — large image assets (GIFs, PNGs).
     *  Resolution: appDir → walk up tree (dev) → UserData (VST3/installer). */
    static juce::File getBackgroundsDirectory()
    {
        // 1. Next to the app (portable / installed)
        auto appDir = getAppDirectory();
        auto bg = appDir.getChildFile("BACKGROUNDS");
        if (bg.isDirectory())
            return bg;

        // 2. Walk up from exe (dev builds inside build/ sub-dirs)
        auto dir = juce::File::getSpecialLocation(
                       juce::File::currentExecutableFile)
                       .getParentDirectory();
        for (int i = 0; i < 10; ++i)
        {
            auto candidate = dir.getChildFile("BACKGROUNDS");
            if (candidate.isDirectory())
                return candidate;
            dir = dir.getParentDirectory();
        }

        // 3. AppData fallback (VST3 / installer)
        auto userBg = getUserDataDirectory().getChildFile("BACKGROUNDS");
        if (userBg.isDirectory())
            return userBg;

        // Last resort: return the appDir version (caller will log if missing)
        return appDir.getChildFile("BACKGROUNDS");
    }

    // ── User-writable directories (safe in both standalone & VST3) ──

    /** Per-user app-data directory (%APPDATA%/Samplar). */
    static juce::File getUserDataDirectory()
    {
        auto dir = juce::File::getSpecialLocation(
                       juce::File::userApplicationDataDirectory)
                       .getChildFile("Samplar");
        if (!dir.exists())
            dir.createDirectory();
        return dir;
    }

    /** Logs directory (delegates to existing LogPaths logic). */
    static juce::File getLogsDirectory()
    {
        auto exeDir = juce::File::getSpecialLocation(
                          juce::File::currentExecutableFile)
                          .getParentDirectory();

        if (exeDir.getFullPathName().containsIgnoreCase("Program Files"))
            return getUserDataDirectory().getChildFile("logs");

        auto logsDir = exeDir.getChildFile("logs");
        if (!logsDir.exists())
            logsDir.createDirectory();
        return logsDir;
    }

private:
    static inline juce::File sOverrideAppDir {};
};

} // namespace samplar
