#include "OrtRuntime.h"
#include "GpuAdapterProbe.h"

#ifndef ORT_API_MANUAL_INIT
 #error "ORT_API_MANUAL_INIT must be defined for every translation unit that includes the ORT C++ API (set on the onnxruntime CMake target in cmake/extra_analysis.cmake)"
#endif

#ifdef _WIN32
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <delayimp.h>
#endif

#include <onnxruntime_cxx_api.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace samplar {

namespace {

// ---- Detached, cancellable session creation (review S-M3; contract in OrtRuntime.h) ----------
thread_local bool tlsDetachedCreator = false;
std::atomic<int> gAbandonedCreations { 0 };   // abandoned creations still running

struct CreationJob
{
    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    bool abandoned = false;
    std::unique_ptr<Ort::Session> session;   // handed to the caller when not abandoned
    std::string error;
    // Owned inputs (the caller may be gone when the creation finishes).
    std::shared_ptr<Ort::Env> env;
    std::wstring path;
    Ort::SessionOptions options { nullptr };
#ifdef _WIN32
    HMODULE pin = nullptr;
#endif
};

// Creator thread. No juce::Logger, no caller state: only the job.
void runCreation (CreationJob& job)
{
    std::unique_ptr<Ort::Session> s;
    std::string err;
    try
    {
       #ifdef _WIN32
        s = std::make_unique<Ort::Session> (*job.env, job.path.c_str(), job.options);
       #else
        s = std::make_unique<Ort::Session> (*job.env, juce::String (job.path.c_str()).toRawUTF8(), job.options);
       #endif
    }
    catch (const std::exception& e) { err = e.what(); }
    catch (...)                     { err = "unknown ONNX Runtime error"; }

    bool abandoned = false;
    {
        std::lock_guard<std::mutex> lk (job.m);
        abandoned = job.abandoned;
        if (! abandoned)
        {
            job.session = std::move (s);
            job.error = err;
        }
        job.done = true;
    }
    job.cv.notify_all();
    if (abandoned)
    {
        s.reset();                 // session (and its ORT pool threads) released here, before the Env
        --gAbandonedCreations;
    }
}

#ifdef _WIN32
DWORD WINAPI creatorThreadProc (LPVOID param)
{
    tlsDetachedCreator = true;
    auto* holder = static_cast<std::shared_ptr<CreationJob>*> (param);
    std::shared_ptr<CreationJob> job (std::move (*holder));
    delete holder;
    const HMODULE pin = job->pin;
    runCreation (*job);
    job.reset();                   // last owner when abandoned: options + Env released on this thread
    // Drops the module pin and exits WITHOUT returning into module code that may now be unmapped.
    FreeLibraryAndExitThread (pin, 0);
    return 0;
}
#endif

void createInline (CreationJob& job, OrtSessionCreation& out)
{
    try
    {
       #ifdef _WIN32
        out.session = std::make_unique<Ort::Session> (*job.env, job.path.c_str(), job.options);
       #else
        out.session = std::make_unique<Ort::Session> (*job.env, juce::String (job.path.c_str()).toRawUTF8(), job.options);
       #endif
    }
    catch (const std::exception& e) { out.error = e.what(); }
    catch (...)                     { out.error = "unknown ONNX Runtime error"; }
}

struct OrtState
{
    bool available = false;
    juce::String reason;
};

#ifdef _WIN32
int delayLoadFilter (unsigned long code)
{
    // 0xC06D007E (module not found) / 0xC06D007F (procedure not found), raised by the
    // delay-load helper when onnxruntime.dll cannot be loaded or lacks OrtGetApiBase.
    return (code == VcppException (ERROR_SEVERITY_ERROR, ERROR_MOD_NOT_FOUND)
            || code == VcppException (ERROR_SEVERITY_ERROR, ERROR_PROC_NOT_FOUND))
               ? EXCEPTION_EXECUTE_HANDLER
               : EXCEPTION_CONTINUE_SEARCH;
}

// Plain C data only in here: __try cannot share a frame with C++ objects that need unwinding.
const OrtApi* guardedGetApi (unsigned long* sehCode, const char** version)
{
    __try
    {
        const OrtApiBase* base = OrtGetApiBase();
        if (base == nullptr)
            return nullptr;
        *version = base->GetVersionString();
        return base->GetApi (ORT_API_VERSION);
    }
    __except (delayLoadFilter (GetExceptionCode()))
    {
        *sehCode = GetExceptionCode();
        return nullptr;
    }
}
#else
const OrtApi* guardedGetApi (unsigned long*, const char** version)
{
    const OrtApiBase* base = OrtGetApiBase();
    if (base == nullptr)
        return nullptr;
    *version = base->GetVersionString();
    return base->GetApi (ORT_API_VERSION);
}
#endif

const OrtState& state()
{
    static std::once_flag once;
    static OrtState s;
    std::call_once (once, []
    {
        juce::Logger::writeToLog ("[ORT] === binding ONNX Runtime API (lazy, guarded) ===");
        unsigned long sehCode = 0;
        const char* version = nullptr;
        const OrtApi* api = guardedGetApi (&sehCode, &version);
        const juce::String dll = juce::String (loadedModulePath (L"onnxruntime.dll").c_str());
        if (api != nullptr)
        {
            Ort::InitApi (api);
            s.available = true;
            juce::Logger::writeToLog ("[ORT]   OK: version=" + juce::String (version != nullptr ? version : "?")
                                      + " api=" + juce::String (ORT_API_VERSION) + " dll=" + dll);
            return;
        }
        if (sehCode != 0)
            s.reason = "onnxruntime.dll could not be loaded (delay-load exception 0x"
                     + juce::String::toHexString ((juce::int64) sehCode).toUpperCase() + ")";
        else if (version != nullptr)
            s.reason = "onnxruntime.dll version " + juce::String (version) + " does not provide ORT API "
                     + juce::String (ORT_API_VERSION) + " (wrong DLL loaded: " + dll + ")";
        else
            s.reason = "onnxruntime.dll returned no API base";
        juce::Logger::writeToLog ("[ORT]   FAILED: " + s.reason + " -> separation / BeatThis disabled for this process");
    });
    return s;
}

} // namespace

bool ensureOrtApi()
{
    return state().available;
}

juce::String ortUnavailableReason()
{
    const auto& s = state();
    return s.available ? juce::String() : s.reason;
}

bool isDetachedOrtCreatorThread() noexcept
{
    return tlsDetachedCreator;
}

OrtSessionCreation createOrtSessionCancellable (std::shared_ptr<Ort::Env> env,
                                                const juce::File& modelFile,
                                                Ort::SessionOptions&& options,
                                                const std::function<bool()>& cancel,
                                                const char* what)
{
    OrtSessionCreation out;
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    const juce::String tag = juce::String ("[ORT] session (") + what + ")";

    auto job = std::make_shared<CreationJob>();
    job->env = std::move (env);
    job->path = modelFile.getFullPathName().toWideCharPointer();
    job->options = std::move (options);

    auto finishInline = [&] (const char* why)
    {
        juce::Logger::writeToLog (tag + ": creating inline (" + why + ")");
        createInline (*job, out);
        out.ms = juce::Time::getMillisecondCounterHiRes() - t0;
        return std::move (out);
    };

    // One model load at a time: let an abandoned creation finish first (cancellable wait when
    // the caller has a cancel hook; otherwise bounded by that creation's remaining time).
    if (gAbandonedCreations.load() > 0)
    {
        juce::Logger::writeToLog (tag + ": waiting for " + juce::String (gAbandonedCreations.load())
                                  + " abandoned model load(s) to finish");
        while (gAbandonedCreations.load() > 0)
        {
            if (cancel && cancel())
            {
                out.cancelled = true;
                out.ms = juce::Time::getMillisecondCounterHiRes() - t0;
                juce::Logger::writeToLog (tag + ": CANCELLED while waiting for the abandoned load");
                return out;
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (25));
        }
    }
    if (! cancel)
        return finishInline ("no cancel hook");
    if (cancel())
    {
        out.cancelled = true;
        juce::Logger::writeToLog (tag + ": CANCELLED before start");
        return out;
    }

#ifdef _WIN32
    HMODULE pin = nullptr;
    if (! GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                              reinterpret_cast<LPCWSTR> (&creatorThreadProc), &pin))
        return finishInline ("cannot pin the module");
    job->pin = pin;
    auto* holder = new std::shared_ptr<CreationJob> (job);
    HANDLE h = CreateThread (nullptr, 0, creatorThreadProc, holder, 0, nullptr);
    if (h == nullptr)
    {
        delete holder;
        FreeLibrary (pin);
        return finishInline ("cannot start the creator thread");
    }
    CloseHandle (h);   // detached: it pins the module itself and exits via FreeLibraryAndExitThread
    juce::Logger::writeToLog (tag + ": creating on a detached, module-pinned thread (cancellable)");

    for (;;)
    {
        {
            std::unique_lock<std::mutex> lk (job->m);
            if (job->done || job->cv.wait_for (lk, std::chrono::milliseconds (25), [&] { return job->done; }))
            {
                out.session = std::move (job->session);
                out.error = job->error;
                break;
            }
        }
        if (cancel())
        {
            std::lock_guard<std::mutex> lk (job->m);
            if (job->done)
            {
                out.session = std::move (job->session);   // finished meanwhile: caller decides
                out.error = job->error;
                break;
            }
            job->abandoned = true;
            ++gAbandonedCreations;
            out.cancelled = true;
            break;
        }
    }
    out.ms = juce::Time::getMillisecondCounterHiRes() - t0;
    juce::Logger::writeToLog (tag + (out.cancelled ? ": CANCELLED after " : out.session != nullptr ? ": ready in " : ": FAILED after ")
                              + juce::String (out.ms, 0) + " ms"
                              + (out.cancelled ? juce::String (" (abandoned; finished + released on its own thread)") : juce::String())
                              + (out.error.isNotEmpty() ? " - " + out.error : juce::String()));
    return out;
#else
    return finishInline ("detached creation is Windows-only");
#endif
}

} // namespace samplar
