#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace samplar {

struct LicenseCleanupTarget
{
    juce::File file;
    juce::String description;
    bool isDirectory{ false };
};

struct LicenseCleanupResult
{
    juce::File root;
    bool dryRun{ false };
    juce::StringArray processed;
    juce::StringArray failures;
};

class LicenseCleanerUtility
{
public:
    static juce::File getCacheRoot();
    static std::vector<LicenseCleanupTarget> getAllTargets(const juce::File& root);
    static std::vector<LicenseCleanupTarget> getExistingTargets(const juce::File& root);
    static LicenseCleanupResult performCleanup(bool dryRun);
};

} // namespace samplar
