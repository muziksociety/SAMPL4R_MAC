#include "LicenseCleanerUtility.h"

namespace samplar {
namespace {
static LicenseCleanupTarget makeTarget(const juce::File& file, const juce::String& description, bool directory)
{
    return { file, description, directory };
}
}

juce::File LicenseCleanerUtility::getCacheRoot()
{
    const auto overrideValue = juce::SystemStats::getEnvironmentVariable("SAMPLAR_CACHE_DIR_OVERRIDE", {});
    if (overrideValue.isNotEmpty())
        return juce::File(overrideValue);

    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Samplar");
}

std::vector<LicenseCleanupTarget> LicenseCleanerUtility::getAllTargets(const juce::File& root)
{
    std::vector<LicenseCleanupTarget> targets;
    targets.push_back(makeTarget(root.getChildFile("license_cache.json"), "License cache", false));
    targets.push_back(makeTarget(root.getChildFile("license_failures.json"), "Failed-attempt log", false));

    auto sessionsDir = root.getChildFile("sessions");
    targets.push_back(makeTarget(sessionsDir, "Session storage", true));
    targets.push_back(makeTarget(sessionsDir.getChildFile("encryption.key"), "Session encryption key", false));
    return targets;
}

std::vector<LicenseCleanupTarget> LicenseCleanerUtility::getExistingTargets(const juce::File& root)
{
    std::vector<LicenseCleanupTarget> existing;
    for (const auto& target : getAllTargets(root))
    {
        if (target.file.exists())
            existing.push_back(target);
    }
    return existing;
}

LicenseCleanupResult LicenseCleanerUtility::performCleanup(bool dryRun)
{
    LicenseCleanupResult result;
    result.dryRun = dryRun;

    const auto root = getCacheRoot();
    result.root = root;

    const auto targets = getExistingTargets(root);
    for (const auto& target : targets)
    {
        const juce::String entry = target.description + " (" + target.file.getFullPathName() + ")";
        if (dryRun)
        {
            result.processed.add(entry);
            continue;
        }

        bool ok = target.isDirectory ? target.file.deleteRecursively()
                                     : target.file.deleteFile();
        if (ok)
            result.processed.add(entry);
        else
            result.failures.add(entry);
    }

    return result;
}

} // namespace samplar
