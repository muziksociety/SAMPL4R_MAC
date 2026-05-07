#include <juce_core/juce_core.h>

#include <iostream>
#include <vector>

#include "security/LicenseCleanerUtility.h"

namespace
{
void printTargets(const std::vector<samplar::LicenseCleanupTarget>& targets)
{
    for (const auto& target : targets)
        std::cout << "  - " << target.description << " (" << target.file.getFullPathName() << ")" << std::endl;
}

bool confirm()
{
    std::cout << "Proceed with deletion? [y/N]: " << std::flush;
    std::string line;
    if (!std::getline(std::cin, line))
        return false;

    auto answer = juce::String(line).trim().toLowerCase();
    return answer == "y" || answer == "yes";
}
}

int main(int argc, char* argv[])
{
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add(juce::String::fromUTF8(argv[i]));

    const bool dryRun = args.contains("--dry-run");
    const bool force = args.contains("--force") || args.contains("-f");

    const auto root = samplar::LicenseCleanerUtility::getCacheRoot();
    std::cout << "SAMPLAR License Cleaner" << std::endl;
    std::cout << "====================" << std::endl;
    std::cout << "Cache root: " << root.getFullPathName() << std::endl;

    const auto existingTargets = samplar::LicenseCleanerUtility::getExistingTargets(root);
    if (existingTargets.empty())
    {
        std::cout << "No license-related artifacts found." << std::endl;
        return 0;
    }

    std::cout << "The following items will be " << (dryRun ? "evaluated" : "removed") << ":" << std::endl;
    printTargets(existingTargets);

    if (!dryRun && !force && !confirm())
    {
        std::cout << "Aborted." << std::endl;
        return 1;
    }

    const auto result = samplar::LicenseCleanerUtility::performCleanup(dryRun);
    if (result.processed.isEmpty())
        std::cout << (dryRun ? "Nothing to remove." : "No files were removed.") << std::endl;
    else
    {
        std::cout << (dryRun ? "Would remove:" : "Removed:") << std::endl;
        for (const auto& entry : result.processed)
            std::cout << "  - " << entry << std::endl;
    }

    if (!result.failures.isEmpty())
    {
        std::cout << "Failed to remove:" << std::endl;
        for (const auto& entry : result.failures)
            std::cout << "  - " << entry << std::endl;
    }

    return result.failures.isEmpty() ? 0 : 2;
}

