#pragma once

#include <unordered_map>
#include <memory>
#include <string>
#include "Service.h"

namespace samplar {

class ServiceRegistry {
public:
    void registerService(const std::string& name, std::unique_ptr<IService> svc);
    IService* get(const std::string& name) const;
    void initialiseAll();
    void shutdownAll();

private:
    std::unordered_map<std::string, std::unique_ptr<IService>> services_;
};

} // namespace samplar

