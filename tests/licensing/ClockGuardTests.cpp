// ClockGuard (clock-rollback guard of the offline licence window). With
// SAMPLAR_SELFTEST the registry copy is off, so only session.idx (in the
// sandboxed SAMPLAR_CACHE_DIR_OVERRIDE folder) is exercised.
#include <juce_core/juce_core.h>

#include "security/ClockGuard.h"
#include "TestHelpers.h"

namespace samplar {

class ClockGuardTests : public juce::UnitTest
{
public:
    ClockGuardTests() : juce::UnitTest ("ClockGuard", "SAMPLAR Licensing") {}

    void runTest() override
    {
        test::ScopedLicenseSandbox sandbox;
        const juce::int64 day = 24LL * 60 * 60 * 1000;
        const juce::int64 t0 = 1792540800000LL;   // 2026-10-20T00:00:00Z

        beginTest ("fresh install: no mark, never rolled back");
        expectEquals (ClockGuard::highWater(), (juce::int64) 0);
        expect (! ClockGuard::rolledBack (t0));

        beginTest ("advance() only moves forward");
        ClockGuard::advance (t0);
        expectEquals (ClockGuard::highWater(), t0);
        ClockGuard::advance (t0 - 5 * day);
        expectEquals (ClockGuard::highWater(), t0, "an earlier time does not lower the mark");
        ClockGuard::advance (t0 + day);
        expectEquals (ClockGuard::highWater(), t0 + day);

        beginTest ("rollback detection honours the 1-day tolerance");
        expect (! ClockGuard::rolledBack (t0 + day), "same time");
        expect (! ClockGuard::rolledBack (t0 + 1), "just under a day behind (time zones / DST)");
        expect (ClockGuard::rolledBack (t0 - 1), "more than a day behind");
        expect (ClockGuard::rolledBack (t0 - 30 * day));

        beginTest ("reset() trusts a server time and may lower the mark");
        ClockGuard::reset (t0 - 10 * day);
        expectEquals (ClockGuard::highWater(), t0 - 10 * day);
        expect (! ClockGuard::rolledBack (t0 - 10 * day));

        beginTest ("mark lives in session.idx, obfuscated (no plain timestamp)");
        {
            const auto f = sandbox.dir().getChildFile ("session.idx");
            expect (f.existsAsFile());
            const auto text = f.loadFileAsString();
            expect (! text.contains (juce::String (t0 - 10 * day)), "timestamp not stored in clear");
            f.deleteFile();
            expectEquals (ClockGuard::highWater(), (juce::int64) 0, "deleting the file resets (registry copy covers this in the plugin)");
        }

        beginTest ("garbage in session.idx: no throw; a signed server time (reset) recovers");
        sandbox.dir().getChildFile ("session.idx").replaceWithText ("not a number");
        // NOTE (reported, src not owned by QA): garbage decodes to 0 ^ kMask, a huge
        // mark, so a corrupted file reads as "clock rolled back" until the next
        // successful online re-check calls reset().
        ClockGuard::highWater();
        ClockGuard::reset (t0);
        expectEquals (ClockGuard::highWater(), t0);
        expect (! ClockGuard::rolledBack (t0));
    }
};

static ClockGuardTests clockGuardTests;

} // namespace samplar
