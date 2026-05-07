#pragma once

#include <string>

namespace samplar {

struct IService {
    virtual ~IService() = default;
    virtual void initialise() = 0;
    virtual void shutdown() = 0;
};

} // namespace samplar

