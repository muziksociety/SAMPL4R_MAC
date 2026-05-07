#pragma once

#include <functional>
#include <unordered_map>
#include <vector>
#include <string>
#include <atomic>
#include <mutex>
#include <juce_events/juce_events.h>

namespace samplar {

// Minimal event bus: string topic + juce::var payload
class EventBus {
public:
    using HandlerId = uint64_t;
    using Handler = std::function<void(const juce::var&)>;

    HandlerId subscribe(const std::string& topic, Handler h);
    void unsubscribe(const std::string& topic, HandlerId id);

    // Synchronous publish for skeleton; can be adapted to queue + pump
    void publish(const std::string& topic, const juce::var& payload = {});

private:
    std::atomic<HandlerId> nextId_ {1};
    std::unordered_map<std::string, std::vector<std::pair<HandlerId, Handler>>> map_;
    std::mutex mu_;
};

} // namespace samplar

