#include <juce_core/juce_core.h>

#include "app/ServiceRegistry.h"

#include <memory>
#include <string>
#include <vector>

namespace samplar {

namespace {

struct Journal
{
    std::vector<std::string> events;
    juce::String joined() const
    {
        juce::StringArray a;
        for (const auto& e : events) a.add (e);
        return a.joinIntoString (",");
    }
};

class RecordingService : public IService
{
public:
    RecordingService (std::string name, Journal& j) : name_ (std::move (name)), journal_ (j) {}
    ~RecordingService() override { journal_.events.push_back ("~" + name_); }
    void initialise() override { journal_.events.push_back ("init:" + name_); }
    void shutdown() override   { journal_.events.push_back ("down:" + name_); }
private:
    std::string name_;
    Journal& journal_;
};

} // namespace

class ServiceRegistryTests : public juce::UnitTest
{
public:
    ServiceRegistryTests() : juce::UnitTest ("ServiceRegistry", "SAMPLAR") {}

    void runTest() override
    {
        // Names chosen so that hash-map order would differ from registration order.
        const std::vector<std::string> names { "zeta", "alpha", "license", "midi", "beta", "smode", "update" };

        beginTest ("initialiseAll runs in registration order");
        {
            Journal j;
            {
                ServiceRegistry reg;
                for (const auto& n : names) reg.registerService (n, std::make_unique<RecordingService> (n, j));
                reg.initialiseAll();
                expectEquals (j.joined(), juce::String ("init:zeta,init:alpha,init:license,init:midi,init:beta,init:smode,init:update"));

                beginTest ("shutdownAll runs in reverse registration order");
                j.events.clear();
                reg.shutdownAll();
                expectEquals (j.joined(), juce::String ("down:update,down:smode,down:beta,down:midi,down:license,down:alpha,down:zeta"));
                j.events.clear();
            }
            beginTest ("destructor destroys services in reverse registration order");
            expectEquals (j.joined(), juce::String ("~update,~smode,~beta,~midi,~license,~alpha,~zeta"));
        }

        beginTest ("get() returns the registered instance, nullptr for unknown names");
        {
            Journal j;
            ServiceRegistry reg;
            auto svc = std::make_unique<RecordingService> ("a", j);
            auto* raw = svc.get();
            reg.registerService ("a", std::move (svc));
            expect (reg.get ("a") == raw);
            expect (reg.get ("nope") == nullptr);
        }

        beginTest ("re-registering a name replaces the service but keeps its original slot");
        {
            Journal j;
            {
                ServiceRegistry reg;
                reg.registerService ("a", std::make_unique<RecordingService> ("a1", j));
                reg.registerService ("b", std::make_unique<RecordingService> ("b", j));
                reg.registerService ("a", std::make_unique<RecordingService> ("a2", j));   // destroys a1
                expectEquals (j.joined(), juce::String ("~a1"));
                j.events.clear();
                reg.initialiseAll();
                expectEquals (j.joined(), juce::String ("init:a2,init:b"));
                j.events.clear();
            }
            expectEquals (j.joined(), juce::String ("~b,~a2"));
        }

        beginTest ("null service entries are skipped");
        {
            Journal j;
            ServiceRegistry reg;
            reg.registerService ("null", nullptr);
            reg.registerService ("x", std::make_unique<RecordingService> ("x", j));
            reg.initialiseAll();
            reg.shutdownAll();
            expectEquals (j.joined(), juce::String ("init:x,down:x"));
        }
    }
};

static ServiceRegistryTests serviceRegistryTests;

} // namespace samplar
