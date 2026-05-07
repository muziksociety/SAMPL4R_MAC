#include "EventBus.h"

using namespace std;

namespace samplar {

EventBus::HandlerId EventBus::subscribe(const std::string& topic, Handler h)
{
    lock_guard<mutex> lock(mu_);
    auto id = nextId_++;
    map_[topic].emplace_back(id, std::move(h));
    return id;
}

void EventBus::unsubscribe(const std::string& topic, HandlerId id)
{
    lock_guard<mutex> lock(mu_);
    auto it = map_.find(topic);
    if (it == map_.end()) return;
    auto& vec = it->second;
    vec.erase(remove_if(vec.begin(), vec.end(), [id](auto& p){ return p.first == id; }), vec.end());
}

void EventBus::publish(const std::string& topic, const juce::var& payload)
{
    vector<Handler> handlers;
    {
        lock_guard<mutex> lock(mu_);
        auto it = map_.find(topic);
        if (it != map_.end())
            for (auto& p : it->second) handlers.push_back(p.second);
    }
    for (auto& h : handlers) h(payload);
}

} // namespace samplar

