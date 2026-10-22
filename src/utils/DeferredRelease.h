#pragma once

#include <functional>

// DeferredRelease - runs a teardown step (free large buffers, join an idle,
// drained thread pool) on a detached, module-pinned thread so the host's
// message thread never waits for it during plugin unload.
//
// Evidence (impl_unload.md, 8 busy procs): the remaining slow unloads were
// spent inside shutdownServices freeing the SignalsmithStretch per-voice FFT
// buffers (ZwFreeVirtualMemory) and joining idle ThreadPool threads.
//
// Same pattern as GpuTeardownOffload: the thread pins the module
// (GetModuleHandleEx FROM_ADDRESS), runs the job, then exits through
// FreeLibraryAndExitThread, so no module code runs after the module is
// unmapped. If the pin or the thread cannot be created, the job runs inline
// (the old behaviour).
//
// Rules for the job:
//  - it touches ONLY objects it owns (captured by move);
//  - it NEVER logs (the shared logger may already be gone);
//  - it must not need the message thread.
namespace samplar::DeferredRelease
{
    /** Any thread except the audio thread. `what` is only used in the caller-side
        log line. Returns true if the job was handed off, false if it ran inline. */
    bool run (std::function<void()> job, const char* what);
}
