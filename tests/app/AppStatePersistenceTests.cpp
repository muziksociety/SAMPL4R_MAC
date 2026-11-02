#include <juce_audio_processors/juce_audio_processors.h>

#include "app/AppState.h"

#include <atomic>
#include <thread>

namespace samplar {

namespace {

class PersistDummyProcessor : public juce::AudioProcessor
{
public:
    const juce::String getName() const override { return "SamplarPersistDummy"; }
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

// Counts "project dirty" notifications (what a VST3 host turns into its modified flag).
struct DirtyListener : juce::AudioProcessorListener
{
    std::atomic<int> dirty { 0 };
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& d) override
    {
        if (d.nonParameterStateChanged) ++dirty;
    }
};

struct Feature
{
    int value = 0;
    std::atomic<int> saves { 0 }, restores { 0 };
    std::atomic<bool> restoreOnMessageThread { true };

    StateBinding binding (const juce::Identifier& id)
    {
        return { id, 1,
                 [this] (juce::ValueTree& t) { ++saves; t.setProperty ("value", value, nullptr); },
                 [this] (const juce::ValueTree& t)
                 {
                     ++restores;
                     restoreOnMessageThread = restoreOnMessageThread && juce::MessageManager::existsAndIsCurrentThread();
                     value = (int) t.getProperty ("value", -1);
                 } };
    }
};

juce::ValueTree parseState (const juce::MemoryBlock& mb)
{
    if (mb.getSize() < 6) return {};
    juce::MemoryInputStream mis (static_cast<const char*> (mb.getData()) + 6, mb.getSize() - 6, false);
    return juce::ValueTree::readFromStream (mis);
}

void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

} // namespace

class AppStatePersistenceTests : public juce::UnitTest
{
public:
    AppStatePersistenceTests() : juce::UnitTest ("AppState persistence (threads / dirty / superseded)", "SAMPLAR") {}

    void runTest() override
    {
        beginTest ("off-thread setState defers restores to the message thread; a save before they ran keeps the LOADED data");
        {
            PersistDummyProcessor proc;
            AppState state (proc);
            Feature f;
            ScopedStateBinding b (state, f.binding ("smode"));

            f.value = 42;
            juce::MemoryBlock project;
            state.serialize (project);          // the project on disk: value 42

            f.value = 0;                        // a fresh instance: live state empty
            std::thread t ([&] { state.deserialize (project.getData(), (int) project.getSize()); });
            t.join();
            expectEquals (f.restores.load(), 0, "no restore ran on the host thread (A18)");
            expect (state.isRestoreDispatchPending());

            const int savesBefore = f.saves.load();
            juce::MemoryBlock early;
            state.serialize (early);            // host saves before the post ran
            expectEquals (f.saves.load(), savesBefore, "live (empty) state not snapshotted while restore pending");
            expectEquals ((int) parseState (early).getChildWithName ("smode").getProperty ("value", -1), 42,
                          "the loaded data is written back, not the empty live state (A4)");

            pump (200);
            expectEquals (f.restores.load(), 1);
            expect (f.restoreOnMessageThread.load(), "restore ran on the message thread");
            expectEquals (f.value, 42);
            expect (! state.isRestoreDispatchPending());
        }

        beginTest ("off-thread getState never calls bindings; streams the last snapshot");
        {
            PersistDummyProcessor proc;
            AppState state (proc);
            Feature f;
            ScopedStateBinding b (state, f.binding ("smode"));
            f.value = 7;
            expect (state.snapshotBinding ("smode"));
            f.value = 8;                        // live change not yet snapshotted
            const int savesBefore = f.saves.load();
            juce::MemoryBlock mb;
            std::thread t ([&] { state.serialize (mb); });
            t.join();
            expectEquals (f.saves.load(), savesBefore, "no save() off the message thread");
            expectEquals ((int) parseState (mb).getChildWithName ("smode").getProperty ("value", -1), 7);

            state.serialize (mb);               // message thread: refreshed
            expectEquals ((int) parseState (mb).getChildWithName ("smode").getProperty ("value", -1), 8);
        }

        beginTest ("two off-thread setStates: only the newest is dispatched");
        {
            PersistDummyProcessor proc;
            AppState state (proc);
            Feature f;
            ScopedStateBinding b (state, f.binding ("smode"));
            f.value = 1; juce::MemoryBlock p1; state.serialize (p1);
            f.value = 2; juce::MemoryBlock p2; state.serialize (p2);
            std::thread t ([&] { state.deserialize (p1.getData(), (int) p1.getSize());
                                 state.deserialize (p2.getData(), (int) p2.getSize()); });
            t.join();
            f.restores = 0;
            pump (200);
            expectEquals (f.restores.load(), 1);
            expectEquals (f.value, 2);
        }

        beginTest ("superseded node: never restored, never written");
        {
            PersistDummyProcessor proc;
            AppState writer (proc);
            Feature old;
            ScopedStateBinding bOld (writer, old.binding ("smodePage"));
            old.value = 5;
            juce::MemoryBlock legacy;
            writer.serialize (legacy);           // an old project containing "smodePage"
            expect (parseState (legacy).getChildWithName ("smodePage").isValid());

            AppState state (proc);
            state.addSupersededNode ("smodePage");
            Feature page;
            ScopedStateBinding b (state, page.binding ("smodePage"));
            state.deserialize (legacy.getData(), (int) legacy.getSize());
            pump (100);
            expectEquals (page.restores.load(), 0, "not restored (also no late drain)");

            juce::MemoryBlock out;
            state.serialize (out);
            expect (page.saves.load() > 0, "the binding may still save");
            expect (! parseState (out).getChildWithName ("smodePage").isValid(), "but the node is stripped from the project");
        }

        beginTest ("markDirty -> nonParameterStateChanged; serialize / deserialize never dirty the project (A16)");
        {
            PersistDummyProcessor proc;
            DirtyListener l;
            proc.addListener (&l);
            {
                AppState state (proc);
                Feature f;
                ScopedStateBinding b (state, f.binding ("smode"));

                state.markDirty();
                expectEquals (l.dirty.load(), 1);

                f.value = 3;
                juce::MemoryBlock mb;
                state.serialize (mb);                 // rewrites the tree (listener fires) - suppressed
                state.deserialize (mb.getData(), (int) mb.getSize());
                state.snapshotAllBindings();
                state.snapshotBinding ("smode");
                expectEquals (l.dirty.load(), 1, "saving / loading / snapshots did not mark the project dirty");

                std::thread t ([&] { state.markDirty(); });   // any thread: hops to the message thread
                t.join();
                pump (100);
                expectEquals (l.dirty.load(), 2);
            }
            proc.removeListener (&l);
        }

        beginTest ("N-15: an off-thread save never swallows a message-thread markDirty");
        {
            PersistDummyProcessor proc;
            DirtyListener l;
            proc.addListener (&l);
            {
                AppState state (proc);
                Feature f;
                ScopedStateBinding b (state, f.binding ("smode"));
                f.value = 1;
                expect (state.snapshotBinding ("smode"));

                // A host thread saving in a tight loop holds AppState's dirty
                // suppression most of the time; before N-15 that suppression was
                // shared across threads and ate the watcher's markDirty().
                std::atomic<bool> stop { false };
                std::atomic<int> saves { 0 };
                std::thread host ([&]
                {
                    juce::MemoryBlock mb;
                    while (! stop.load()) { state.serialize (mb); ++saves; }
                });
                while (saves.load() < 5) std::this_thread::yield();

                int notified = 0;
                constexpr int kEdits = 200;
                for (int i = 0; i < kEdits; ++i)
                    if (state.markDirty()) ++notified;
                stop = true;
                host.join();
                expectEquals (notified, kEdits, "every message-thread markDirty() was delivered");
                expectEquals (l.dirty.load(), kEdits, "host saw every dirty notification ("
                              + juce::String (saves.load()) + " concurrent off-thread saves)");

                // Still suppressed for AppState's OWN rewrites on this thread.
                const int before = l.dirty.load();
                juce::MemoryBlock mb;
                state.serialize (mb);
                state.snapshotBinding ("smode");
                expectEquals (l.dirty.load(), before, "own save / snapshot does not dirty");
            }
            proc.removeListener (&l);
        }

        beginTest ("N-14: an off-thread setState is never overwritten by a racing message-thread snapshot");
        {
            PersistDummyProcessor proc;
            AppState state (proc);
            Feature f;
            ScopedStateBinding b (state, f.binding ("smode"));

            f.value = 42; juce::MemoryBlock project;  state.serialize (project);   // the project to load
            f.value = 0;  juce::MemoryBlock blank;    state.serialize (blank);     // "live" old state

            // Each round: the message thread is mid-snapshot-spam (live value 0)
            // while a host thread loads the project. Once deserialize returned and
            // before its restore dispatch ran, the tree must hold the PROJECT (42):
            // a snapshot that passed its "pending?" check before the swap must not
            // write the old live value over it (check-then-lock race).
            int failures = 0, rounds = 0;
            for (int round = 0; round < 300; ++round)
            {
                state.deserialize (blank.getData(), (int) blank.getSize());   // message thread: pending=false, value 0
                f.value = 0;
                std::atomic<bool> loaded { false };
                std::thread host ([&] { state.deserialize (project.getData(), (int) project.getSize()); loaded = true; });
                while (! loaded.load())
                    state.snapshotBinding ("smode");
                host.join();
                state.snapshotBinding ("smode");   // after the load: must be skipped (dispatch pending)
                juce::MemoryBlock out;
                state.serialize (out);             // message thread, dispatch pending -> streams the tree
                if ((int) parseState (out).getChildWithName ("smode").getProperty ("value", -1) != 42)
                    ++failures;
                ++rounds;
            }
            expectEquals (failures, 0, "loaded project kept in all " + juce::String (rounds) + " racing rounds");
            pump (100);                            // stale posts are dropped by seq; the newest restores 42
            expectEquals (f.value, 42);
            expect (! state.isRestoreDispatchPending());
        }

        beginTest ("N-18: restoreWhenMissing binding is reset by a project without its node; superseded legacy nodes stay readable but are never saved");
        {
            PersistDummyProcessor proc;
            AppState writer (proc);
            Feature legacy;
            ScopedStateBinding bl (writer, legacy.binding ("smodeStemPitch"));
            legacy.value = 7;
            juce::MemoryBlock oldProject;            // has "smodeStemPitch" but no "smode"
            writer.serialize (oldProject);

            AppState state (proc);
            state.addSupersededNode ("smodeStemPitch");
            int resets = 0, restores = 0;
            int legacySeen = -1;
            StateBinding sb { "smode", 1,
                              [] (juce::ValueTree& t) { t.setProperty ("x", 1, nullptr); },
                              [&] (const juce::ValueTree& t)
                              {
                                  if (! t.isValid()) ++resets; else ++restores;
                                  legacySeen = (int) state.getChildCopy ("smodeStemPitch").getProperty ("value", -1);
                              },
                              true };
            ScopedStateBinding b (state, sb);
            state.deserialize (oldProject.getData(), (int) oldProject.getSize());
            expectEquals (resets, 1, "project without the node -> restore(invalid) = reset");
            expectEquals (restores, 0);
            expectEquals (legacySeen, 7, "the superseded legacy node is still readable during the restore (migration)");

            juce::MemoryBlock out;
            state.serialize (out);
            expect (! parseState (out).getChildWithName ("smodeStemPitch").isValid(), "legacy node not written back");
            expect (parseState (out).getChildWithName ("smode").isValid());

            // A normal binding (restoreWhenMissing=false) is still skipped.
            Feature plain;
            ScopedStateBinding bp (state, plain.binding ("other"));
            state.deserialize (oldProject.getData(), (int) oldProject.getSize());
            expectEquals (plain.restores.load(), 0);
            expectEquals (resets, 2);
        }
    }
};

static AppStatePersistenceTests appStatePersistenceTests;

} // namespace samplar
