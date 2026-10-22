#include "AsyncFileLogger.h"

#include <cstring>
#include <utility>

namespace samplar
{
namespace
{
    void writeLine (std::ofstream& out, const juce::String& line)
    {
        const char* utf8 = line.toRawUTF8();
        out.write (utf8, (std::streamsize) std::strlen (utf8));
        out.write ("\r\n", 2);
    }
}

AsyncFileLogger::AsyncFileLogger (const juce::File& file, const juce::String& welcomeMessage)
    : file_ (file)
{
    (void) file_.getParentDirectory().createDirectory();
   #ifdef _WIN32
    out_.open (file_.getFullPathName().toWideCharPointer(), std::ios::binary | std::ios::app);
   #else
    out_.open (file_.getFullPathName().toStdString(), std::ios::binary | std::ios::app);
   #endif

    // Same header shape as juce::FileLogger.
    writeLine (out_, {});
    writeLine (out_, "**********************************************************");
    writeLine (out_, welcomeMessage);
    writeLine (out_, "Log started: " + juce::Time::getCurrentTime().toString (true, true));
    out_.flush();

    queue_.reserve (256);
    writer_ = std::thread ([this] { writerLoop(); });
}

AsyncFileLogger::~AsyncFileLogger()
{
    shutdown();
}

void AsyncFileLogger::shutdown()
{
    {
        std::scoped_lock lock (mutex_);
        stop_ = true;
    }
    wake_.notify_one();
    if (writer_.joinable())
        writer_.join();   // drains + flushes the queue first
    if (out_.is_open())
        out_.close();
}

void AsyncFileLogger::logMessage (const juce::String& message)
{
    {
        std::scoped_lock lock (mutex_);
        if (stop_)
            return;
        if (queue_.size() >= kMaxQueuedLines)
        {
            ++dropped_;
            return;
        }
        queue_.push_back (message);
    }
    wake_.notify_one();
}

void AsyncFileLogger::writerLoop()
{
    juce::Thread::setCurrentThreadName ("SAMPLAR log writer");
    std::vector<juce::String> batch;
    batch.reserve (256);

    for (;;)
    {
        size_t dropped = 0;
        bool stopping = false;
        {
            std::unique_lock lock (mutex_);
            wake_.wait (lock, [this] { return stop_ || ! queue_.empty(); });
            batch.swap (queue_);
            std::swap (dropped, dropped_);
            stopping = stop_;
        }

        if (out_.is_open())
        {
            for (const auto& line : batch)
                writeLine (out_, line);
            if (dropped > 0)
                writeLine (out_, "[AsyncFileLogger] queue full - " + juce::String ((juce::int64) dropped) + " line(s) dropped");
            out_.flush();   // to the OS cache only (no disk sync)
        }
        batch.clear();

        if (stopping)
        {
            std::scoped_lock lock (mutex_);
            if (queue_.empty())   // stop_ is set: logMessage() no longer queues
                return;
        }
    }
}
} // namespace samplar
