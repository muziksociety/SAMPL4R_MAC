#include "GpuTeardownOffload.h"

#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>
#include <mutex>

#ifdef _WIN32
 #include <windows.h>
#endif

namespace samplar::GpuTeardownOffload
{
namespace
{
    class Holder;

    std::mutex& holderMutex()
    {
        static std::mutex m;   // trivially used; destroyed with the module
        return m;
    }

    Holder*& holderSlot()
    {
        static Holder* h = nullptr;
        return h;
    }

#ifdef _WIN32
    struct ReleaseJob
    {
        juce::Image keep;
        HMODULE pin = nullptr;
    };

    // Detached, module-pinned. Never logs (the logger may already be gone).
    DWORD WINAPI releaseThreadProc (LPVOID param)
    {
        auto* job = static_cast<ReleaseJob*> (param);
        const HMODULE pin = job->pin;
        delete job;   // last DirectX reference -> D3D11/D2D devices released on this thread
        FreeLibraryAndExitThread (pin, 0);
        return 0;
    }
#endif

    class Holder final : public juce::DeletedAtShutdown
    {
    public:
        Holder()
        {
            juce::Logger::writeToLog ("[GpuTeardownOffload] holder registered (before any editor window)");
        }

        ~Holder() override
        {
            {
                std::scoped_lock lock (holderMutex());
                if (holderSlot() == this)
                    holderSlot() = nullptr;
            }

            if (! keep.isValid())
            {
                juce::Logger::writeToLog ("[GpuTeardownOffload] shutdown: no GPU state retained (no editor was opened)");
                return;
            }

            const double t0 = juce::Time::getMillisecondCounterHiRes();
           #ifdef _WIN32
            HMODULE pin = nullptr;
            if (GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                                    reinterpret_cast<LPCWSTR> (&releaseThreadProc), &pin))
            {
                auto* job = new ReleaseJob { std::move (keep), pin };
                if (HANDLE h = CreateThread (nullptr, 0, releaseThreadProc, job, 0, nullptr))
                {
                    CloseHandle (h);
                    juce::Logger::writeToLog ("[GpuTeardownOffload] shutdown: Direct2D/D3D11 release handed to a module-pinned thread in "
                                              + juce::String (juce::Time::getMillisecondCounterHiRes() - t0, 2) + " ms");
                    return;
                }
                keep = std::move (job->keep);
                delete job;
                FreeLibrary (pin);
            }
            juce::Logger::writeToLog ("[GpuTeardownOffload] shutdown: cannot start the release thread - releasing inline");
           #endif
            keep = {};
            juce::Logger::writeToLog ("[GpuTeardownOffload] shutdown: inline release took "
                                      + juce::String (juce::Time::getMillisecondCounterHiRes() - t0, 1) + " ms");
        }

        juce::Image keep;
    };
}

void registerHolder()
{
    std::scoped_lock lock (holderMutex());
    if (holderSlot() == nullptr)
        holderSlot() = new Holder();   // owned by JUCE's DeletedAtShutdown list
}

void retainGpuState()
{
    std::scoped_lock lock (holderMutex());
    if (holderSlot() == nullptr)
    {
        // Not expected (the processor registers first); late registration still
        // works, it just may be deleted before JUCE's VBlankDispatcher.
        juce::Logger::writeToLog ("[GpuTeardownOffload] WARNING: retain without a registered holder - registering late");
        holderSlot() = new Holder();
    }
    if (! holderSlot()->keep.isValid())
    {
        holderSlot()->keep = juce::Image (juce::Image::ARGB, 1, 1, true, juce::NativeImageType());
        juce::Logger::writeToLog ("[GpuTeardownOffload] GPU state retained (native image type: "
                                  + juce::String (holderSlot()->keep.getPixelData()->createType()->getTypeID()) + ")");
    }
}
}
