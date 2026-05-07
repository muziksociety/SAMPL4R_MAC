#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// SAMPLAR Tracing — lightweight Chrome Trace Event (JSON) profiler.
//
// Compile-time gated via CMake option SAMPLAR_ENABLE_TRACING (default OFF).
// When OFF, every macro below compiles to nothing — zero runtime overhead
// and zero binary size increase in release builds.
//
// When ON:
//   - Scoped events via SAMPLAR_TRACE_SCOPE("category", "name") — RAII begin/end
//   - Instant events via SAMPLAR_TRACE_INSTANT("category", "name")
//   - Counter events via SAMPLAR_TRACE_COUNTER("category", "name", value)
//
// Output: %APPDATA%/Samplar/traces/trace_<timestamp>.json, one file per
// session. Open with https://ui.perfetto.dev (drag-and-drop) or chrome://tracing.
//
// Thread-safe writer uses a single lock-protected output stream; overhead of
// one trace event is ~1 μs when enabled. Suitable for UI and background
// threads — NOT for the realtime audio thread at high frequency (the lock
// and file I/O will block the audio thread). Use SAMPLAR_TRACE_INSTANT sparingly
// from processBlock, or better, use a lock-free ring buffer → flusher thread
// when that optimization lands (C1 + C2 in the hardening roadmap).
// ─────────────────────────────────────────────────────────────────────────────

#ifdef SAMPLAR_ENABLE_TRACING

#include <juce_core/juce_core.h>
#include <atomic>
#include <mutex>
#include <fstream>

namespace samplar::tracing {

class Session
{
public:
    // Opens the trace file. Call once at app startup (SAMPLARAudioProcessor ctor).
    static void start();

    // Flushes and closes the trace file. Call on shutdown.
    static void stop();

    // Low-level writers — prefer the macros below.
    static void writeComplete(const char* category, const char* name,
                               int64_t startUs, int64_t durUs);
    static void writeInstant(const char* category, const char* name);
    static void writeCounter(const char* category, const char* name, double value);

    static bool isActive() { return active_.load(std::memory_order_acquire); }

    // Public utility helpers used by ScopedEvent below.
    static int64_t nowMicros();
    static int64_t currentThreadId();

private:
    static std::atomic<bool>   active_;
    static std::ofstream       out_;
    static std::mutex          mutex_;
    static int64_t             startTimeUs_;
    static bool                firstEvent_;
};

// RAII helper for scoped events. Records begin timestamp in ctor, writes
// complete event (with duration) in dtor.
class ScopedEvent
{
public:
    ScopedEvent(const char* category, const char* name)
        : category_(category), name_(name), startUs_(Session::isActive() ? Session::nowMicros() : 0) {}
    ~ScopedEvent()
    {
        if (Session::isActive())
            Session::writeComplete(category_, name_, startUs_, Session::nowMicros() - startUs_);
    }
    ScopedEvent(const ScopedEvent&) = delete;
    ScopedEvent& operator=(const ScopedEvent&) = delete;
private:
    const char* category_;
    const char* name_;
    int64_t     startUs_;
};

} // namespace samplar::tracing

// Stringify helper for unique var names across macro expansions
#define SAMPLAR_TRACE_CONCAT_INNER(a, b) a ## b
#define SAMPLAR_TRACE_CONCAT(a, b) SAMPLAR_TRACE_CONCAT_INNER(a, b)

#define SAMPLAR_TRACE_SCOPE(category, name) \
    ::samplar::tracing::ScopedEvent SAMPLAR_TRACE_CONCAT(_SAMPLAR_trace_scope_, __LINE__)(category, name)

#define SAMPLAR_TRACE_INSTANT(category, name) \
    do { if (::samplar::tracing::Session::isActive()) ::samplar::tracing::Session::writeInstant(category, name); } while (0)

#define SAMPLAR_TRACE_COUNTER(category, name, value) \
    do { if (::samplar::tracing::Session::isActive()) ::samplar::tracing::Session::writeCounter(category, name, static_cast<double>(value)); } while (0)

#define SAMPLAR_TRACE_SESSION_START() ::samplar::tracing::Session::start()
#define SAMPLAR_TRACE_SESSION_STOP()  ::samplar::tracing::Session::stop()

#else   // SAMPLAR_ENABLE_TRACING not defined — compile to nothing

#define SAMPLAR_TRACE_SCOPE(category, name)              ((void) 0)
#define SAMPLAR_TRACE_INSTANT(category, name)            ((void) 0)
#define SAMPLAR_TRACE_COUNTER(category, name, value)     ((void) 0)
#define SAMPLAR_TRACE_SESSION_START()                    ((void) 0)
#define SAMPLAR_TRACE_SESSION_STOP()                     ((void) 0)

#endif
