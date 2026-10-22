// StackSampler - opt-in diagnostic for VST3SmokeHost (--stacks).
// From begin() to end(), a TIME_CRITICAL sampler thread suspends the main thread
// every ~5 ms, unwinds it (RtlVirtualUnwind, no allocation while the thread is
// suspended) and resumes it. report(), called AFTER the step was timed, prints
// the aggregated stacks ("[Prof]" lines) when the step took >= 200 ms. Plugin
// frames print as SAMPLAR+0x<rva> (resolve with the plugin's .map: preferred
// base 0x180000000 + rva) so they survive the unload. If a step runs past 3 s,
// every other thread is dumped once ("[Stack]" lines, symbolized).
// Windows/x64 only; a no-op elsewhere or when disabled.
#pragma once

#if JUCE_WINDOWS && defined (_M_X64)
 #include <windows.h>
 #include <dbghelp.h>
 #include <tlhelp32.h>
 #include <psapi.h>
 #include <algorithm>
 #include <array>
 #include <atomic>
 #include <map>
 #include <mutex>
 #include <vector>
 #include <cstdio>
 #include <string>
 #include <thread>
 #pragma comment (lib, "dbghelp.lib")
 #pragma comment (lib, "psapi.lib")

class StackSampler
{
public:
    explicit StackSampler (bool enabled) : enabled_ (enabled)
    {
        if (! enabled_) return;
        mainId_ = GetCurrentThreadId();
        mainThread_ = OpenThread (THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, mainId_);
        SymSetOptions (SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
        SymInitialize (GetCurrentProcess(), nullptr, TRUE);
        samples_.reserve (kMaxSamples);
        thread_ = std::thread ([this] { SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL); run(); });
    }

    ~StackSampler()
    {
        if (! enabled_) return;
        stop_ = true;
        thread_.join();
        CloseHandle (mainThread_);
        SymCleanup (GetCurrentProcess());
    }

    void begin (const char* step)
    {
        if (! enabled_) return;
        step_ = step;
        {
            std::scoped_lock lk (bufMutex_);
            samples_.clear();
            pluginBase_ = pluginSize_ = 0;
            if (HMODULE m = GetModuleHandleW (L"SAMPLAR.vst3"))
            {
                MODULEINFO mi = {};
                if (GetModuleInformation (GetCurrentProcess(), m, &mi, sizeof (mi)))
                {
                    pluginBase_ = (DWORD64) mi.lpBaseOfDll;
                    pluginSize_ = mi.SizeOfImage;
                }
            }
        }
        start_ = GetTickCount64();
        dumpedAll_ = false;
        active_ = true;
    }

    void end()
    {
        if (! enabled_) return;
        active_ = false;
        lastElapsed_ = GetTickCount64() - start_.load();
    }

    // Call AFTER the step's time was measured: symbolizing is slow under load.
    void report()
    {
        if (! enabled_) return;
        std::scoped_lock lk (bufMutex_);
        if (lastElapsed_ >= 200)
            printProfile (lastElapsed_);
        samples_.clear();
    }

private:
    static constexpr int kMaxFrames = 48;
    static constexpr size_t kMaxSamples = 8000;
    struct Sample { int n = 0; std::array<DWORD64, kMaxFrames> pcs {}; };

    std::string frameName (DWORD64 pc) const
    {
        if (pc >= pluginBase_ && pc < pluginBase_ + pluginSize_)
        {
            char b[64];
            std::snprintf (b, sizeof (b), "SAMPLAR+0x%llx", (unsigned long long) (pc - pluginBase_));
            return b;
        }
        return symbolize (pc);
    }

    void printProfile (unsigned long long elapsed)
    {
        const char nl = (char) 10;
        SymRefreshModuleList (GetCurrentProcess());
        std::map<std::string, int> counts;
        for (const auto& s : samples_)
        {
            std::string sig;
            for (int i = 0; i < s.n; ++i)
            {
                auto f = frameName (s.pcs[(size_t) i]);
                if (f.find ("VST3PluginInstance") != std::string::npos) break;
                sig += f;
                sig += nl;
            }
            ++counts[sig];
        }
        std::vector<std::pair<int, std::string>> v;
        for (auto& [k, c] : counts) v.emplace_back (c, k);
        std::sort (v.begin(), v.end(), [] (auto& a, auto& b) { return a.first > b.first; });
        std::printf ("[Prof] step='%s' %llu ms, %d samples (~%.1f ms each), pluginBase=0x%llx%c", step_.load(), elapsed,
                     (int) samples_.size(), samples_.empty() ? 0.0 : (double) elapsed / (double) samples_.size(),
                     (unsigned long long) pluginBase_, nl);
        for (size_t i = 0; i < v.size() && i < 12; ++i)
        {
            std::printf ("[Prof] -- %d samples (%.0f%%)%c", v[i].first, 100.0 * v[i].first / (double) samples_.size(), nl);
            size_t pos = 0;
            for (int k = 0; k < 18; ++k)
            {
                auto e = v[i].second.find (nl, pos);
                if (e == std::string::npos) break;
                std::printf ("[Prof]      %s%c", v[i].second.substr (pos, e - pos).c_str(), nl);
                pos = e + 1;
            }
        }
        std::fflush (stdout);
    }

    // POD only (SEH): unwinds a suspended thread's stack into pcs.
    static int capture (HANDLE th, DWORD64* pcs, int max)
    {
        if (SuspendThread (th) == (DWORD) -1) return 0;
        int n = 0;
        CONTEXT ctx;
        ZeroMemory (&ctx, sizeof (ctx));
        ctx.ContextFlags = CONTEXT_FULL;
        __try
        {
            if (GetThreadContext (th, &ctx))
            {
                while (n < max && ctx.Rip != 0)
                {
                    pcs[n++] = ctx.Rip;
                    DWORD64 imageBase = 0;
                    auto* fe = RtlLookupFunctionEntry (ctx.Rip, &imageBase, nullptr);
                    if (fe == nullptr)
                    {
                        ctx.Rip = *(DWORD64*) ctx.Rsp;
                        ctx.Rsp += 8;
                    }
                    else
                    {
                        PVOID handlerData = nullptr;
                        DWORD64 establisher = 0;
                        RtlVirtualUnwind (UNW_FLAG_NHANDLER, imageBase, ctx.Rip, fe, &ctx, &handlerData, &establisher, nullptr);
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        ResumeThread (th);
        return n;
    }

public:
    static std::string symbolize (DWORD64 pc)
    {
        alignas (SYMBOL_INFO) char buf[sizeof (SYMBOL_INFO) + 512] = {};
        auto* si = reinterpret_cast<SYMBOL_INFO*> (buf);
        si->SizeOfStruct = sizeof (SYMBOL_INFO);
        si->MaxNameLen = 511;
        IMAGEHLP_MODULE64 mi = {};
        mi.SizeOfStruct = sizeof (mi);
        const bool haveMod = SymGetModuleInfo64 (GetCurrentProcess(), pc, &mi) != FALSE;
        char out[1024];
        DWORD64 disp = 0;
        if (SymFromAddr (GetCurrentProcess(), pc, &disp, si))
        {
            IMAGEHLP_LINE64 line = {};
            line.SizeOfStruct = sizeof (line);
            DWORD ld = 0;
            if (SymGetLineFromAddr64 (GetCurrentProcess(), pc, &ld, &line))
            {
                const char* f = line.FileName;
                for (const char* p = f; *p; ++p) if (*p == '\\' || *p == '/') f = p + 1;
                std::snprintf (out, sizeof (out), "%s!%s (%s:%lu)", haveMod ? mi.ModuleName : "?", si->Name, f, line.LineNumber);
            }
            else
                std::snprintf (out, sizeof (out), "%s!%s+0x%llx", haveMod ? mi.ModuleName : "?", si->Name, (unsigned long long) disp);
        }
        else
            std::snprintf (out, sizeof (out), "%s!0x%llx", haveMod ? mi.ModuleName : "?", (unsigned long long) pc);
        return out;
    }

private:
    static double cpuMs (HANDLE th)
    {
        FILETIME c, e, k, u;
        if (! GetThreadTimes (th, &c, &e, &k, &u)) return -1.0;
        auto toMs = [] (FILETIME f) { return (double) ((((ULONGLONG) f.dwHighDateTime) << 32) | f.dwLowDateTime) / 10000.0; };
        return toMs (k) + toMs (u);
    }

    void printStack (HANDLE th, DWORD tid, const char* tag, unsigned long long elapsed)
    {
        DWORD64 pcs[kMaxFrames];
        const int n = capture (th, pcs, kMaxFrames);
        SymRefreshModuleList (GetCurrentProcess());
        std::printf ("[Stack] %s step='%s' +%llu ms tid=%lu prio=%d cpu=%.1f ms frames=%d\n",
                     tag, step_.load(), elapsed, tid, GetThreadPriority (th), cpuMs (th), n);
        for (int i = 0; i < n; ++i)
            std::printf ("[Stack]   #%02d %s\n", i, symbolize (pcs[i]).c_str());
        std::fflush (stdout);
    }

    void dumpAllThreads (unsigned long long elapsed)
    {
        HANDLE snap = CreateToolhelp32Snapshot (TH32CS_SNAPTHREAD, 0);
        if (snap == INVALID_HANDLE_VALUE) return;
        THREADENTRY32 te = {};
        te.dwSize = sizeof (te);
        const DWORD pid = GetCurrentProcessId(), self = GetCurrentThreadId();
        for (BOOL ok = Thread32First (snap, &te); ok; ok = Thread32Next (snap, &te))
        {
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == self || te.th32ThreadID == mainId_) continue;
            HANDLE th = OpenThread (THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
            if (th == nullptr) continue;
            printStack (th, te.th32ThreadID, "other", elapsed);
            CloseHandle (th);
        }
        CloseHandle (snap);
    }

    void run()
    {
        while (! stop_)
        {
            Sleep (5);
            if (! active_) continue;
            {
                std::scoped_lock lk (bufMutex_);
                if (active_ && samples_.size() < kMaxSamples)
                {
                    samples_.emplace_back();
                    samples_.back().n = capture (mainThread_, samples_.back().pcs.data(), kMaxFrames);
                }
            }
            const auto elapsed = GetTickCount64() - start_.load();
            if (! dumpedAll_ && elapsed >= 3000)
            {
                dumpedAll_ = true;
                dumpAllThreads (elapsed);
            }
        }
    }

    bool enabled_ = false;
    DWORD mainId_ = 0;
    HANDLE mainThread_ = nullptr;
    std::thread thread_;
    std::atomic<bool> stop_ { false }, active_ { false }, dumpedAll_ { false };
    std::atomic<const char*> step_ { "" };
    std::atomic<unsigned long long> start_ { 0 };
    std::mutex bufMutex_;
    unsigned long long lastElapsed_ = 0;
    std::vector<Sample> samples_;
    DWORD64 pluginBase_ = 0, pluginSize_ = 0;
};

#else
class StackSampler
{
public:
    explicit StackSampler (bool) {}
    void begin (const char*) {}
    void end() {}
    void report() {}
};
#endif
