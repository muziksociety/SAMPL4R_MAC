#pragma once

#include <juce_core/juce_core.h>
#include "LicenseCache.h"

namespace samplar {

namespace clockguard_detail {
    /** Year this translation unit was compiled (__DATE__ = "Mmm dd yyyy"). */
    constexpr int buildYear()
    {
        return (__DATE__[7] - '0') * 1000 + (__DATE__[8] - '0') * 100 + (__DATE__[9] - '0') * 10 + (__DATE__[10] - '0');
    }
    /** Jan 1 of `year`, 00:00 UTC, in ms since the epoch (days_from_civil, H. Hinnant). */
    constexpr juce::int64 jan1Ms (int year)
    {
        const int y = year - 1;                     // January counts as month 13 of the previous year
        const int era = (y >= 0 ? y : y - 399) / 400;
        const int yoe = y - era * 400;
        const int doy = (153 * (1 + 9) + 2) / 5;    // day-of-year (March based) of Jan 1
        const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return ((juce::int64) era * 146097 + doe - 719468) * 86400000LL;
    }
    static_assert (jan1Ms (2024) == 1704067200000LL, "calendar maths");
    static_assert (jan1Ms (1970) == 0, "calendar maths");
}

// Detects the system clock being turned BACK to stretch the offline window
// forever. Keeps the latest time SAMPLAR has ever seen in several places; a
// pirate has to find and edit all of them. Ported from SM5 (RUIDO fa1aa12).
//   - a small file next to license_cache.json (session.idx)
//   - (Windows) an HKCU registry value
// Legitimate case: a clock that was wrong in the FUTURE and got corrected ->
// the next signed online reply calls reset(serverTime).
// Message thread only (startup validation / running-session timer / re-check
// result / sign-in result). An unreadable mark counts as "unknown", never as
// "future" (see decode()).
class ClockGuard
{
public:
    static constexpr juce::int64 kToleranceMs = 24LL * 60 * 60 * 1000;   // time zones / DST / small drift

    /** Latest time ever seen (ms since epoch), 0 if none. */
    static juce::int64 highWater() { return juce::jmax (readFile(), readRegistry()); }

    /** True if `nowMs` is more than a day behind the latest time ever seen. */
    static bool rolledBack (juce::int64 nowMs) { return nowMs + kToleranceMs < highWater(); }

    /** Record `nowMs` if it is later than what we have. Never writes a time
        lower than the existing (readable) high water. */
    static void advance (juce::int64 nowMs)
    {
        const auto hw = highWater();
        if (nowMs > hw && decodable (nowMs))
            write (nowMs);
    }

    /** Trust a server-confirmed time (a signed online reply): may LOWER the mark.
        Callers pass min(signed issued time, local receive time) so the mark is
        never put ahead of this computer's clock by the server/local skew. */
    static void reset (juce::int64 trustedMs)
    {
        if (decodable (trustedMs))
            write (trustedMs);
        else
            juce::Logger::writeToLog ("[ClockGuard] reset ignored: time outside the plausible range");
    }

private:
    // Stored lightly obfuscated so a text search for a timestamp finds nothing.
    static constexpr juce::int64 kMask = 0x3C5A69E1F00DB44BLL;

    // Fixed plausible range: [2024-01-01, Jan 1 of (build year + 30)].
    // The upper bound is a compile-time constant, NOT "now + N": rolling the
    // clock back any amount can never make a real mark read as "unknown".
    static constexpr int kMaxYearsAfterBuild = 30;

public:
    static constexpr juce::int64 kMinPlausibleMs = 1704067200000LL;            // 2024-01-01T00:00:00Z
    static constexpr juce::int64 kMaxPlausibleMs = clockguard_detail::jan1Ms (clockguard_detail::buildYear() + kMaxYearsAfterBuild);

    static constexpr bool decodable (juce::int64 ms) { return ms >= kMinPlausibleMs && ms <= kMaxPlausibleMs; }

    /** Stored text -> time mark, or 0 ("unknown") for anything that is not a
        plausible mark: empty / truncated / garbage (getLargeIntValue("") is 0,
        and 0 ^ kMask would read as a far-future mark = permanent "clock rolled
        back" lockout), or a decoded time outside the FIXED range
        [kMinPlausibleMs, kMaxPlausibleMs]. Independent of the current clock.
        Unknown never locks; it only means "no evidence of a rollback" (the
        service also keeps an in-memory maximum, see enforceRunningSession). */
    static juce::int64 decode (const juce::String& stored)
    {
        const auto s = stored.trim();
        if (s.isEmpty() || s.length() > 19 || ! s.containsOnly ("0123456789"))
            return 0;
        const juce::int64 ms = s.getLargeIntValue() ^ kMask;
        return decodable (ms) ? ms : 0;
    }

private:
    static juce::File file() { return LicenseCache::cacheDirectory().getChildFile ("session.idx"); }

    static juce::int64 readFile()
    {
        const auto f = file();
        if (! f.existsAsFile())
            return 0;
        const auto v = decode (f.loadFileAsString());
        if (v == 0 && f.getSize() > 0)
            juce::Logger::writeToLog ("[ClockGuard] session.idx unreadable - treated as unknown (no lock)");
        return v;
    }

    static juce::int64 readRegistry()
    {
       #if JUCE_WINDOWS && ! SAMPLAR_SELFTEST   // tests never touch the real registry
        return decode (juce::WindowsRegistry::getValue (regKey(), {}));
       #else
        return 0;
       #endif
    }

    static void write (juce::int64 ms)
    {
        const auto s = juce::String (ms ^ kMask);
        file().replaceWithText (s);
       #if JUCE_WINDOWS && ! SAMPLAR_SELFTEST
        juce::WindowsRegistry::setValue (regKey(), s);
       #endif
    }

   #if JUCE_WINDOWS
    static juce::String regKey() { return "HKEY_CURRENT_USER\\Software\\MuzikSociety\\Samplar\\ix"; }
   #endif
};

} // namespace samplar
