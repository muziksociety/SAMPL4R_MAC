#include <juce_events/juce_events.h>

#include "app/EventBus.h"

#include <atomic>
#include <thread>
#include <vector>

namespace samplar {

class EventBusTests : public juce::UnitTest
{
public:
    EventBusTests() : juce::UnitTest ("EventBus", "SAMPLAR") {}

    void runTest() override
    {
        beginTest ("publish reaches every subscriber of the topic, with the payload");
        {
            EventBus bus;
            int a = 0, b = 0, other = 0;
            juce::var seen;
            bus.subscribe ("smode.test", [&] (const juce::var& v) { ++a; seen = v; });
            bus.subscribe ("smode.test", [&] (const juce::var&) { ++b; });
            bus.subscribe ("smode.other", [&] (const juce::var&) { ++other; });

            bus.publish ("smode.test", juce::var (42));
            expectEquals (a, 1);
            expectEquals (b, 1);
            expectEquals (other, 0, "other topics are not called");
            expectEquals ((int) seen, 42);

            bus.publish ("nobody.listens");   // no subscriber: no-op
            expectEquals (a, 1);
        }

        beginTest ("handler ids are unique; unsubscribe removes only that handler");
        {
            EventBus bus;
            int a = 0, b = 0;
            const auto idA = bus.subscribe ("t", [&] (const juce::var&) { ++a; });
            const auto idB = bus.subscribe ("t", [&] (const juce::var&) { ++b; });
            expect (idA != idB);
            bus.unsubscribe ("t", idA);
            bus.publish ("t");
            expectEquals (a, 0);
            expectEquals (b, 1);
            bus.unsubscribe ("t", idA);          // twice: harmless
            bus.unsubscribe ("missing", idB);    // unknown topic: harmless
            bus.unsubscribe ("t", idB);
            bus.publish ("t");
            expectEquals (b, 1);
        }

        beginTest ("a handler may unsubscribe itself / subscribe others during publish (no deadlock)");
        {
            EventBus bus;
            int calls = 0, late = 0;
            EventBus::HandlerId self = 0;
            self = bus.subscribe ("t", [&] (const juce::var&)
            {
                ++calls;
                bus.unsubscribe ("t", self);
                bus.subscribe ("t", [&] (const juce::var&) { ++late; });
            });
            bus.publish ("t");     // snapshot semantics: late subscriber not called this round
            expectEquals (calls, 1);
            expectEquals (late, 0);
            bus.publish ("t");
            expectEquals (calls, 1, "self-unsubscribed handler is gone");
            expectEquals (late, 1);
        }

        beginTest ("concurrent publish / subscribe from several threads");
        {
            EventBus bus;
            std::atomic<int> hits { 0 };
            bus.subscribe ("t", [&] (const juce::var&) { hits.fetch_add (1); });
            std::vector<std::thread> threads;
            for (int t = 0; t < 4; ++t)
                threads.emplace_back ([&bus, t]
                {
                    for (int i = 0; i < 500; ++i)
                    {
                        bus.publish ("t");
                        const auto id = bus.subscribe ("x" + std::to_string (t), [] (const juce::var&) {});
                        bus.unsubscribe ("x" + std::to_string (t), id);
                    }
                });
            for (auto& th : threads) th.join();
            expectEquals (hits.load(), 2000);
        }
    }
};

static EventBusTests eventBusTests;

} // namespace samplar
