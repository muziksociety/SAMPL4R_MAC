#include "DeferredRelease.h"

#include <juce_core/juce_core.h>
#include <utility>

#ifdef _WIN32
 #include <windows.h>
#endif

namespace samplar::DeferredRelease
{
namespace
{
#ifdef _WIN32
    struct Job
    {
        std::function<void()> fn;
        HMODULE pin = nullptr;
    };

    // Detached, module-pinned. Never logs.
    DWORD WINAPI releaseThreadProc (LPVOID param)
    {
        auto* job = static_cast<Job*> (param);
        const HMODULE pin = job->pin;
        try { job->fn(); } catch (...) {}
        delete job;   // also destroys whatever the job captured
        FreeLibraryAndExitThread (pin, 0);
        return 0;
    }
#endif
}

bool run (std::function<void()> job, const char* what)
{
    if (! job)
        return false;

    const double t0 = juce::Time::getMillisecondCounterHiRes();
   #ifdef _WIN32
    HMODULE pin = nullptr;
    if (GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                            reinterpret_cast<LPCWSTR> (&releaseThreadProc), &pin))
    {
        auto* j = new Job { std::move (job), pin };
        if (HANDLE h = CreateThread (nullptr, 0, releaseThreadProc, j, 0, nullptr))
        {
            CloseHandle (h);
            juce::Logger::writeToLog (juce::String ("[DeferredRelease] ") + what + ": handed to a module-pinned thread in "
                                      + juce::String (juce::Time::getMillisecondCounterHiRes() - t0, 2) + " ms");
            return true;
        }
        job = std::move (j->fn);
        delete j;
        FreeLibrary (pin);
    }
   #endif
    job();
    job = nullptr;
    juce::Logger::writeToLog (juce::String ("[DeferredRelease] ") + what + ": ran inline in "
                              + juce::String (juce::Time::getMillisecondCounterHiRes() - t0, 1) + " ms");
    return false;
}
}
