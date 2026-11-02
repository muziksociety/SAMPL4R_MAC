#include <juce_core/juce_core.h>

#include "utils/DataPaths.h"
#include "TestHelpers.h"

#if ! JUCE_WINDOWS
 #include <climits>
 #include <cstdlib>
#endif

namespace samplar {

namespace {
// Canonical path for comparisons: the CWD comes back through getcwd(), which
// resolves symlinks (e.g. /Users vs /Volumes/Macintosh_HD/Users on macOS).
juce::String resolvedPath (const juce::File& f)
{
   #if JUCE_WINDOWS
    return f.getFullPathName();
   #else
    char buf[PATH_MAX];
    if (::realpath (f.getFullPathName().toRawUTF8(), buf) != nullptr)
        return juce::String::fromUTF8 (buf);
    return f.getFullPathName();
   #endif
}
} // namespace

class DataPathsTests : public juce::UnitTest
{
public:
    DataPathsTests() : juce::UnitTest ("DataPaths", "SAMPLAR") {}

    void runTest() override
    {
        test::ScopedTempDir bundle ("SamplarTests_bundle");
        const auto root = bundle.get();

        beginTest ("override (VST3 bundle) wins and data/ + models/ hang off it");
        {
            root.getChildFile ("data").createDirectory();
            root.getChildFile ("models").createDirectory();
            DataPaths::setAppDirectory (root);
            expectEquals (DataPaths::getAppDirectory().getFullPathName(), root.getFullPathName());
            expectEquals (DataPaths::getDataDirectory().getFullPathName(), root.getChildFile ("data").getFullPathName());
            expectEquals (DataPaths::getModelsDirectory().getFullPathName(), root.getChildFile ("models").getFullPathName());
            DataPaths::clearAppDirectory();
        }

        beginTest ("an override that does not exist is ignored (falls back, never returns the bogus dir)");
        {
            const auto missing = root.getChildFile ("does_not_exist");
            DataPaths::setAppDirectory (missing);
            expect (DataPaths::getAppDirectory() != missing);
            DataPaths::clearAppDirectory();
        }

        beginTest ("without override: exe dir (if it has data/ or models/), else CWD with data/, else exe dir");
        {
            DataPaths::clearAppDirectory();
            const auto exeDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
            const bool exeHasResources = exeDir.getChildFile ("data").isDirectory() || exeDir.getChildFile ("models").isDirectory();

            const auto previousCwd = juce::File::getCurrentWorkingDirectory();
            root.setAsCurrentWorkingDirectory();   // root has data/
            const auto resolved = DataPaths::getAppDirectory();
            previousCwd.setAsCurrentWorkingDirectory();

            if (exeHasResources)
                expectEquals (resolvedPath (resolved), resolvedPath (exeDir));
            else
                expectEquals (resolvedPath (resolved), resolvedPath (root), "CWD fallback");

            if (! exeHasResources && ! previousCwd.getChildFile ("data").isDirectory())
                expectEquals (resolvedPath (DataPaths::getAppDirectory()), resolvedPath (exeDir), "last resort: exe dir");
        }

        beginTest ("user data directory is <userAppData>/Samplar and exists");
        {
            const auto dir = DataPaths::getUserDataDirectory();
            expectEquals (dir.getFileName(), juce::String ("Samplar"));
            expect (dir.isDirectory());
            expectEquals (dir.getParentDirectory().getFullPathName(),
                          juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getFullPathName());
        }
    }
};

static DataPathsTests dataPathsTests;

} // namespace samplar
