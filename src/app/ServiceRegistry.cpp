#include "ServiceRegistry.h"

using namespace std;

namespace samplar {

void ServiceRegistry::registerService(const std::string& name, std::unique_ptr<IService> svc)
{
    services_[name] = std::move(svc);
}

IService* ServiceRegistry::get(const std::string& name) const
{
    auto it = services_.find(name);
    return it != services_.end() ? it->second.get() : nullptr;
}

void ServiceRegistry::initialiseAll()
{
    for (auto& kv : services_) {
        if (kv.second) kv.second->initialise();
    }
}

void ServiceRegistry::shutdownAll()
{
    for (auto& kv : services_) {
        if (kv.second) kv.second->shutdown();
    }
}

} // namespace samplar

