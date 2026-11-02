#pragma once

#include <unordered_map>
#include <memory>
#include <string>
#include <vector>
#include "Service.h"

namespace samplar {

/**
 * Owns the services. Lifecycle order is deterministic:
 *   initialiseAll() - registration order
 *   shutdownAll()   - reverse registration order
 *   destructor      - services destroyed in reverse registration order
 * (A hash-map iteration order used to decide this, so it changed whenever a
 * service was added or renamed.)
 */
class ServiceRegistry {
public:
    ServiceRegistry() = default;
    ~ServiceRegistry();

    void registerService(const std::string& name, std::unique_ptr<IService> svc);
    IService* get(const std::string& name) const;
    void initialiseAll();
    void shutdownAll();

private:
    std::unordered_map<std::string, std::unique_ptr<IService>> services_;
    std::vector<std::string> order_;   // registration order (unique names)
};

} // namespace samplar
