#pragma once

#include <juce_core/juce_core.h>

#include <condition_variable>
#include <cstddef>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

namespace samplar
{
/** Buffered, asynchronously flushed log file shared by all plugin instances.

    juce::FileLogger opens and closes the file for EVERY line. Under heavy CPU
    load each CloseHandle cost 10-200 ms on the calling thread (impl_unload.md:
    ~28% of the slow unload time). This logger keeps one stream open on a
    background writer thread it owns:

    - logMessage() only appends to a bounded in-memory queue (short mutex, no
      I/O) and wakes the writer. When the queue is full the line is dropped and
      counted; the writer reports the count. Logging from the audio thread stays
      forbidden (the mutex is not realtime-safe).
    - The writer drains the queue in batches and flushes each batch to the OS
      (no disk sync), so lines reach the file within milliseconds and survive a
      host crash once written.
    - shutdown() (also run by the destructor) flushes everything still queued,
      joins the writer and closes the file; later logMessage() calls are
      ignored, so a thread that read the global logger pointer just before it
      was cleared cannot write into a freed object if the owner keeps the
      stopped logger alive (the plugin keeps it until module unload).
      The owner must unset it as the current juce::Logger first.
*/
class AsyncFileLogger final : public juce::Logger
{
public:
    static constexpr size_t kMaxQueuedLines = 20000;

    AsyncFileLogger (const juce::File& file, const juce::String& welcomeMessage);
    ~AsyncFileLogger() override;

    void logMessage (const juce::String& message) override;

    /** Flush + join + close. Idempotent. Not from the writer thread. */
    void shutdown();

    const juce::File& getFile() const noexcept { return file_; }

private:
    void writerLoop();

    const juce::File file_;
    std::ofstream out_;              // writer thread only (after construction)

    std::mutex mutex_;
    std::condition_variable wake_;
    std::vector<juce::String> queue_;
    size_t dropped_ { 0 };
    bool stop_ { false };

    std::thread writer_;

    JUCE_DECLARE_NON_COPYABLE (AsyncFileLogger)   // no leak detector: kept until module unload
};
} // namespace samplar
