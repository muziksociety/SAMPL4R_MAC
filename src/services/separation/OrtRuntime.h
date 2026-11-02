#pragma once

#include <juce_core/juce_core.h>
#include <functional>
#include <memory>

namespace Ort
{
    struct Env;
    struct Session;
    struct SessionOptions;
}

namespace samplar {

/**
 * ONNX Runtime availability (audit S-H2).
 *
 * The ORT C++ API is compiled with ORT_API_MANUAL_INIT (set on the imported `onnxruntime`
 * CMake target), so NOTHING touches onnxruntime.dll at plugin load: no static initialiser
 * calls OrtGetApiBase(). The API table is bound lazily by ensureOrtApi(), which performs the
 * first (delay-loaded) call inside a structured-exception guard. A missing / blocked /
 * wrong-version onnxruntime.dll therefore turns into "runtime unavailable" instead of the
 * delay-load exception 0xC06D007E killing the host.
 *
 * EVERY code path that uses Ort:: must call ensureOrtApi() first and bail out when it
 * returns false (Ort::GetApi() is a null table until then).
 */

/** Binds the ORT API once per process. Thread-safe; cheap after the first call. */
bool ensureOrtApi();

/** User-facing reason when ensureOrtApi() failed ("" when the runtime is available). */
juce::String ortUnavailableReason();

/**
 * Cancellable ONNX session creation (review S-M3).
 *
 * Ort::Session's constructor (model load + graph optimisation: htdemucs 3-5 s, BeatThis ~2 s)
 * cannot be interrupted, and unloading the plugin joins the threads that run it. This helper
 * runs the constructor on a DETACHED Win32 thread and lets the caller stop waiting as soon as
 * `cancel` returns true. An abandoned creation finishes on its own thread, destroys the session
 * there and exits.
 *
 * Why it is safe for that thread to outlive the plugin instance:
 *  - it pins the plugin module (GetModuleHandleEx on its own code) and leaves through
 *    FreeLibraryAndExitThread, so the module cannot be unmapped while the thread runs even if
 *    the host frees it - the unload then completes when the thread drops the last reference;
 *  - it only touches what it owns (shared_ptr to the Env, the options, a wide path), never
 *    the caller, the instance, the message thread or juce::Logger (which an unloading
 *    instance may delete) - see isDetachedOrtCreatorThread();
 *  - a later creation waits (cancellably) until abandoned creations are done, so two model
 *    loads never compete for memory / VRAM.
 * Rejected alternatives: joining in a static destructor (runs under the loader lock at DLL
 * detach; the joined thread needs that lock to exit -> deadlock) and a plain bounded wait
 * (still blocks unload for the bound).
 * Residual risk: after an unload the abandoned load keeps using CPU / RAM (and VRAM with
 * DirectML) for at most the remaining creation time; if the module's refcount reaches zero on
 * that thread, the module's DLL_PROCESS_DETACH (static destructors) runs there instead of on
 * the host thread - the same code a normal unload runs, just on another thread.
 *
 * No cancel hook (or the platform cannot pin the module / start the thread): creates inline.
 */
struct OrtSessionCreation
{
    std::unique_ptr<Ort::Session> session;   // null on failure / cancel
    bool cancelled = false;
    juce::String error;                      // ORT exception text on failure
    double ms = 0.0;
};

OrtSessionCreation createOrtSessionCancellable (std::shared_ptr<Ort::Env> env,
                                                const juce::File& modelFile,
                                                Ort::SessionOptions&& options,
                                                const std::function<bool()>& cancel,
                                                const char* what);

/** True on the detached creator thread: code ORT calls back on that thread (custom pool-thread
 *  creation) must not log through juce::Logger. */
bool isDetachedOrtCreatorThread() noexcept;

/** Message shown to users when separation/analysis cannot run because ORT is unavailable. */
inline juce::String ortUnavailableUserMessage()
{
    return "Stem separation unavailable - the ONNX Runtime (onnxruntime.dll) could not be loaded. "
           "Please reinstall SAMPLAR.";
}

} // namespace samplar
