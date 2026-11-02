#include <juce_audio_processors/juce_audio_processors.h>

#include "app/AppState.h"

#include <cstring>
#include <memory>

namespace samplar {

namespace {

// The smallest AudioProcessor AppState can hang off (AppState only calls updateHostDisplay()).
class DummyProcessor : public juce::AudioProcessor
{
public:
    const juce::String getName() const override { return "SamplarTestsDummy"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
};

// A feature with persisted state: a BPM and a list of stem files.
struct FakeFeature
{
    double bpm = 0.0;
    juce::StringArray stems;
    int restores = 0;

    StateBinding binding (const juce::Identifier& id = "smode")
    {
        return { id, 1,
                 [this] (juce::ValueTree& t)
                 {
                     t.setProperty ("bpm", bpm, nullptr);
                     t.removeAllChildren (nullptr);
                     juce::ValueTree list ("stems");
                     for (const auto& s : stems)
                     {
                         juce::ValueTree c ("stem");
                         c.setProperty ("file", s, nullptr);
                         list.appendChild (c, nullptr);
                     }
                     t.appendChild (list, nullptr);
                 },
                 [this] (const juce::ValueTree& t)
                 {
                     ++restores;
                     bpm = t.getProperty ("bpm", 0.0);
                     stems.clear();
                     const auto list = t.getChildWithName ("stems");
                     for (int i = 0; i < list.getNumChildren(); ++i)
                         stems.add (list.getChild (i).getProperty ("file").toString());
                 } };
    }
};

void pumpMessages (int ms)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
}

} // namespace

class AppStateTests : public juce::UnitTest
{
public:
    AppStateTests() : juce::UnitTest ("AppState", "SAMPLAR") {}

    void runTest() override
    {
        DummyProcessor proc;

        juce::MemoryBlock saved;
        beginTest ("serialize writes the SMAS header + every binding's save()");
        {
            AppState state (proc);
            FakeFeature f;
            f.bpm = 128.5;
            f.stems = { "C:/stems/vocals.wav", "C:/stems/drums.wav" };
            ScopedStateBinding b (state, f.binding());
            state.serialize (saved);
            expect (saved.getSize() > 6);
            expect (std::memcmp (saved.getData(), "SMAS", 4) == 0, "magic");
            const auto* bytes = static_cast<const juce::uint8*> (saved.getData());
            expectEquals ((int) (bytes[4] | (bytes[5] << 8)), 1, "format version 1 (little endian)");
        }

        beginTest ("deserialize restores bindings registered BEFORE the state arrives");
        {
            AppState state (proc);
            FakeFeature f;
            ScopedStateBinding b (state, f.binding());
            expect (! state.isRestoreReady());
            state.deserialize (saved.getData(), (int) saved.getSize());
            expect (state.isRestoreReady());
            expectEquals (f.restores, 1);
            expectEquals (f.bpm, 128.5);
            expectEquals (f.stems.joinIntoString ("|"), juce::String ("C:/stems/vocals.wav|C:/stems/drums.wav"));
        }

        beginTest ("late drain: a binding registered AFTER deserialize is restored asynchronously");
        {
            AppState state (proc);
            state.deserialize (saved.getData(), (int) saved.getSize());
            FakeFeature f;
            ScopedStateBinding b (state, f.binding());
            expectEquals (f.restores, 0, "restore is deferred so the caller's constructor can finish");
            pumpMessages (200);
            expectEquals (f.restores, 1);
            expectEquals (f.bpm, 128.5);
            expectEquals (f.stems.size(), 2);
        }

        beginTest ("late drain is skipped when the binding unregisters before the async post runs");
        {
            AppState state (proc);
            state.deserialize (saved.getData(), (int) saved.getSize());
            FakeFeature f;
            {
                ScopedStateBinding b (state, f.binding());
            }
            pumpMessages (200);
            expectEquals (f.restores, 0);
        }

        beginTest ("late drain is skipped (no crash) when AppState is destroyed first");
        {
            FakeFeature f;
            {
                auto state = std::make_unique<AppState> (proc);
                state->deserialize (saved.getData(), (int) saved.getSize());
                state->registerBinding (f.binding());   // token intentionally leaked
            }
            pumpMessages (200);
            expectEquals (f.restores, 0);
        }

        beginTest ("late drain does nothing for a node that is not in the restored tree");
        {
            AppState state (proc);
            state.deserialize (saved.getData(), (int) saved.getSize());
            FakeFeature f;
            ScopedStateBinding b (state, f.binding ("notSaved"));
            pumpMessages (100);
            expectEquals (f.restores, 0);
        }

        beginTest ("snapshotAllBindings + restoreAllBindings (editor close / reopen)");
        {
            AppState state (proc);
            state.deserialize (nullptr, 0);
            expect (state.isRestoreReady(), "empty payload counts as restored");
            FakeFeature live;
            live.bpm = 90.0;
            live.stems = { "a.wav" };
            {
                ScopedStateBinding b (state, live.binding());
                state.snapshotAllBindings();
            }
            FakeFeature reopened;
            ScopedStateBinding b2 (state, reopened.binding());
            state.restoreAllBindings();
            expectEquals (reopened.bpm, 90.0);
            expectEquals (reopened.stems.joinIntoString ("|"), juce::String ("a.wav"));
        }

        beginTest ("legacy JSON state is imported (ui / smode.stemFiles / midi slots)");
        {
            AppState state (proc);
            FakeFeature f;
            juce::ValueTree midiSeen;
            ScopedStateBinding b (state, f.binding());
            ScopedStateBinding m (state, { "midi", 1, {}, [&] (const juce::ValueTree& t) { midiSeen = t.createCopy(); } });
            const juce::String json = R"({"ui":{"zoom":2},"smode":{"bpm":100,"stemFiles":["x.wav","y.wav","z.wav"]},"midi":[{"note":36},{"note":37}]})";
            state.deserialize (json.toRawUTF8(), (int) json.getNumBytesAsUTF8());
            expectEquals (f.restores, 1);
            expectEquals (f.bpm, 100.0);
            expectEquals (f.stems.joinIntoString ("|"), juce::String ("x.wav|y.wav|z.wav"));
            expectEquals (midiSeen.getNumChildren(), 2);
            expectEquals ((int) midiSeen.getChild (1).getProperty ("note"), 37);
            expect (state.getOrCreateChild ("ui").getProperty ("zoom") == juce::var (2));
        }

        beginTest ("garbage / truncated payloads are ignored without throwing");
        {
            AppState state (proc);
            FakeFeature f;
            ScopedStateBinding b (state, f.binding());
            const char junk[] = "\x01\x02\x03garbage";
            state.deserialize (junk, (int) sizeof (junk));
            expect (state.isRestoreReady());
            expectEquals (f.restores, 0);

            const char truncated[] = { 'S', 'M', 'A', 'S', 1 };
            state.deserialize (truncated, (int) sizeof (truncated));
            expectEquals (f.restores, 0);

            juce::MemoryBlock corrupt (saved);
            corrupt.setSize (8);   // header + 2 bytes of tree
            state.deserialize (corrupt.getData(), (int) corrupt.getSize());
            expectEquals (f.restores, 0);
        }

        beginTest ("serialize -> deserialize -> serialize is stable");
        {
            AppState a (proc);
            a.deserialize (saved.getData(), (int) saved.getSize());
            juce::MemoryBlock again;
            a.serialize (again);
            AppState b (proc);
            FakeFeature f;
            ScopedStateBinding sb (b, f.binding());
            b.deserialize (again.getData(), (int) again.getSize());
            expectEquals (f.bpm, 128.5);
            expectEquals (f.stems.size(), 2);
        }
    }
};

static AppStateTests appStateTests;

} // namespace samplar
