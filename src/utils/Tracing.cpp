#include "Tracing.h"

#ifdef SAMPLAR_ENABLE_TRACING

#include "DataPaths.h"
#include <chrono>
#include <thread>
#include <sstream>

namespace samplar::tracing {

std::atomic<bool> Session::active_{ false };
std::ofstream     Session::out_;
std::mutex        Session::mutex_;
int64_t           Session::startTimeUs_{ 0 };
bool              Session::firstEvent_{ true };

int64_t Session::nowMicros()
{
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

int64_t Session::currentThreadId()
{
    std::ostringstream oss;
    oss << std::this_thread::get_id();
    // Hash the thread ID string to an int64 — Perfetto/Chrome tracing expect integer TIDs.
    return static_cast<int64_t>(std::hash<std::string>{}(oss.str()));
}

void Session::start()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_.load()) return;

    auto traceDir = DataPaths::getUserDataDirectory().getChildFile("traces");
    if (! traceDir.exists())
        traceDir.createDirectory();

    auto name = "trace_" + juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H-%M-%S") + ".json";
    auto file = traceDir.getChildFile(name);

    out_.open(file.getFullPathName().toStdString(), std::ios::out | std::ios::trunc);
    if (! out_.is_open())
    {
        juce::Logger::writeToLog("[Tracing] Failed to open trace file: " + file.getFullPathName());
        return;
    }

    // Chrome Trace Event JSON: an array of event objects.
    out_ << "[\n";
    startTimeUs_ = nowMicros();
    firstEvent_  = true;
    active_.store(true, std::memory_order_release);

    juce::Logger::writeToLog("[Tracing] Session started — writing to " + file.getFullPathName());
}

void Session::stop()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (! active_.load()) return;
    active_.store(false, std::memory_order_release);

    out_ << "\n]\n";
    out_.flush();
    out_.close();

    juce::Logger::writeToLog("[Tracing] Session stopped");
}

static void writePrefix(std::ofstream& out, bool& first)
{
    if (! first) out << ",\n";
    first = false;
}

void Session::writeComplete(const char* category, const char* name, int64_t startUs, int64_t durUs)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (! out_.is_open()) return;
    writePrefix(out_, firstEvent_);
    out_ << "{\"ph\":\"X\",\"cat\":\"" << category << "\",\"name\":\"" << name
         << "\",\"ts\":" << (startUs - startTimeUs_)
         << ",\"dur\":" << durUs
         << ",\"pid\":1,\"tid\":" << currentThreadId() << "}";
}

void Session::writeInstant(const char* category, const char* name)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (! out_.is_open()) return;
    writePrefix(out_, firstEvent_);
    out_ << "{\"ph\":\"i\",\"s\":\"t\",\"cat\":\"" << category << "\",\"name\":\"" << name
         << "\",\"ts\":" << (nowMicros() - startTimeUs_)
         << ",\"pid\":1,\"tid\":" << currentThreadId() << "}";
}

void Session::writeCounter(const char* category, const char* name, double value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (! out_.is_open()) return;
    writePrefix(out_, firstEvent_);
    out_ << "{\"ph\":\"C\",\"cat\":\"" << category << "\",\"name\":\"" << name
         << "\",\"ts\":" << (nowMicros() - startTimeUs_)
         << ",\"args\":{\"value\":" << value << "}"
         << ",\"pid\":1,\"tid\":" << currentThreadId() << "}";
}

} // namespace samplar::tracing

#endif // SAMPLAR_ENABLE_TRACING
