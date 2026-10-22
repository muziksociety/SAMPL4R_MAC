#include "ServiceRegistry.h"
#include <juce_core/juce_core.h>
#include <algorithm>

using namespace std;

namespace samplar {

ServiceRegistry::~ServiceRegistry()
{
    // Destroy in reverse registration order (later services may hold raw
    // pointers into earlier ones).
    for (auto it = order_.rbegin(); it != order_.rend(); ++it)
        services_.erase(*it);
}

void ServiceRegistry::registerService(const std::string& name, std::unique_ptr<IService> svc)
{
    if (std::find(order_.begin(), order_.end(), name) == order_.end())
        order_.push_back(name);
    services_[name] = std::move(svc);
}

IService* ServiceRegistry::get(const std::string& name) const
{
    auto it = services_.find(name);
    return it != services_.end() ? it->second.get() : nullptr;
}

void ServiceRegistry::initialiseAll()
{
    juce::Logger::writeToLog("[ServiceRegistry] === initialiseAll START (" + juce::String((int) order_.size()) + " services) ===");
    for (const auto& name : order_) {
        auto it = services_.find(name);
        if (it == services_.end() || !it->second) continue;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        it->second->initialise();
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        juce::Logger::writeToLog("[ServiceRegistry]   initialise '" + juce::String(name)
                                 + "' took " + juce::String(ms, 1) + " ms");
    }
    juce::Logger::writeToLog("[ServiceRegistry] === initialiseAll END ===");
}

void ServiceRegistry::shutdownAll()
{
    // Reverse registration order; timed per service: plugin unload must stay
    // fast (smoke-host budget 1.5 s), so a slow shutdown() shows up by name.
    // One summary line (plus one per slow service) keeps unload logging small.
    juce::String timings;
    const auto tAll = juce::Time::getMillisecondCounterHiRes();
    for (auto rit = order_.rbegin(); rit != order_.rend(); ++rit) {
        auto it = services_.find(*rit);
        if (it == services_.end() || !it->second) continue;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        it->second->shutdown();
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        timings << " " << juce::String(*rit) << "=" << juce::String(ms, 1);
        if (ms >= 100.0)
            juce::Logger::writeToLog("[ServiceRegistry]   SLOW shutdown '" + juce::String(*rit)
                                     + "' took " + juce::String(ms, 1) + " ms");
    }
    juce::Logger::writeToLog("[ServiceRegistry] shutdownAll done in "
                             + juce::String(juce::Time::getMillisecondCounterHiRes() - tAll, 1) + " ms (ms:"
                             + timings + ")");
}

} // namespace samplar
