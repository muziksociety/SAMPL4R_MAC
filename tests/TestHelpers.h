#pragma once

// Shared helpers for SamplarTests (juce::UnitTest). Test-only.

#include <juce_core/juce_core.h>
#include <cstdlib>

namespace samplar::test {

/** A fresh, empty temp directory, deleted on destruction. */
class ScopedTempDir
{
public:
    explicit ScopedTempDir (const juce::String& prefix)
        : dir_ (juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getNonexistentChildFile (prefix, {}, false))
    {
        dir_.createDirectory();
    }
    ~ScopedTempDir() { dir_.deleteRecursively(); }

    const juce::File& get() const { return dir_; }

    JUCE_DECLARE_NON_COPYABLE (ScopedTempDir)
private:
    juce::File dir_;
};

/** Sets an environment variable for the scope, restores the previous value after. */
class ScopedEnv
{
public:
    ScopedEnv (const char* name, const juce::String& value)
        : name_ (name), previous_ (juce::SystemStats::getEnvironmentVariable (name, {}))
    {
        set (value);
    }
    ~ScopedEnv() { set (previous_); }

    JUCE_DECLARE_NON_COPYABLE (ScopedEnv)
private:
    void set (const juce::String& v)
    {
       #if JUCE_WINDOWS
        _wputenv_s (juce::String (name_).toWideCharPointer(), v.toWideCharPointer());
       #else
        if (v.isEmpty()) unsetenv (name_);
        else             setenv (name_, v.toRawUTF8(), 1);
       #endif
    }

    const char* name_;
    juce::String previous_;
};

/** Redirects every licence-related file (license_cache.json, session.idx,
    .hardware_id) into a private temp folder for the scope. */
class ScopedLicenseSandbox
{
public:
    ScopedLicenseSandbox() : dir_ ("SamplarTests_license"), env_ ("SAMPLAR_CACHE_DIR_OVERRIDE", dir_.get().getFullPathName()) {}
    const juce::File& dir() const { return dir_.get(); }
private:
    ScopedTempDir dir_;
    ScopedEnv env_;
};

} // namespace samplar::test
