#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include "services/smode/SModeSession.h"

#include <cmath>
#include <limits>

namespace samplar {

namespace {

// A model with `stems` stems of `len` samples at `rate` (audio content is irrelevant).
void makeStems (SModeSegmentModel& m, int stems, int len, double rate)
{
    m.setStemCount (stems);
    for (int s = 0; s < stems; ++s)
    {
        juce::AudioBuffer<float> b (2, len);
        b.clear();
        m.setStemAudio (s, std::move (b));
        m.getStem (s)->name = "Stem" + juce::String (s);
    }
    m.setSampleRate (rate);
}

juce::ValueTree streamRoundTrip (const juce::ValueTree& t)
{
    juce::MemoryOutputStream mos;
    t.writeToStream (mos);
    juce::MemoryInputStream mis (mos.getData(), mos.getDataSize(), false);
    return juce::ValueTree::readFromStream (mis);
}

} // namespace

class SModeSessionTests : public juce::UnitTest
{
public:
    SModeSessionTests() : juce::UnitTest ("SModeSession (project persistence)", "SAMPLAR") {}

    void runTest() override
    {
        beginTest ("session serialize -> stream -> deserialize keeps every field");
        {
            SModeSegmentModel a;
            makeStems (a, 4, 100000, 44100.0);
            a.addSegment (0, 100, 5000);
            a.addSegment (0, 6000, 9000);
            a.addSegment (2, 0, 44100);
            a.addSegment (3, 50000, 99999);
            a.assignMidiNote (0, 0, 60);
            a.assignMidiNote (0, 1, 61);
            a.assignMidiNote (2, 0, 36);          // stem 3 keeps note -1 (unassigned)
            SegmentEffects fx;
            fx.pitchSemitones = -5; fx.volume = 0.5f; fx.pan = -0.75f; fx.filterCutoff = 0.25f;
            fx.filterIsHighPass = true; fx.reverse = true; fx.loop = true;
            fx.attackMs = 12.5f; fx.releaseMs = 250.0f; fx.triggerMode = 2;
            a.setSegmentEffects (0, 1, fx);
            a.setLinked (1, true);
            a.setSolo (2, true);
            a.setMute (3, true);
            a.setStemVolume (0, 0.33f);
            a.setDetectedTempo (127.25);
            a.setGridOffsetSeconds (0.4321);

            session::SessionSource src;
            src.sourceFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("song.wav");
            for (int s = 0; s < 4; ++s)
                src.stemFiles.push_back (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                             .getChildFile ("stem_cache_onnx").getChildFile ("abc123").getChildFile ("s" + juce::String (s) + ".wav"));
            src.uiExtraJson = R"({"v":1,"gridDivision":8,"lanes":[{"numBars":4,"slots":[]}]})";

            const auto saved = streamRoundTrip (session::writeSession (a, src));
            expect (saved.isValid());
            expectEquals (session::countSegments (saved), 4);
            expectEquals (saved.getProperty ("stemCacheKey").toString(), juce::String ("abc123"));

            SModeSegmentModel b;
            makeStems (b, 4, 100000, 44100.0);
            expectEquals (session::applySessionToModel (saved, b), 4);
            expectEquals (session::describeModel (b), session::describeModel (a), "model description identical");
            for (int s = 0; s < 4; ++s)
            {
                const auto* sa = a.getStem (s);
                const auto* sb = b.getStem (s);
                expectEquals ((int) sb->segments.size(), (int) sa->segments.size());
                expect (sb->linked == sa->linked && sb->solo == sa->solo && sb->mute == sa->mute);
                expectEquals (sb->volume, sa->volume);
                for (size_t i = 0; i < sa->segments.size(); ++i)
                {
                    const auto& x = sa->segments[i];
                    const auto& y = sb->segments[i];
                    expect (x.startSample == y.startSample && x.endSample == y.endSample && x.midiNote == y.midiNote);
                    expectEquals (y.effects.pitchSemitones, x.effects.pitchSemitones);
                    expectEquals (y.effects.volume, x.effects.volume);
                    expectEquals (y.effects.pan, x.effects.pan);
                    expectEquals (y.effects.filterCutoff, x.effects.filterCutoff);
                    expect (y.effects.filterIsHighPass == x.effects.filterIsHighPass);
                    expect (y.effects.reverse == x.effects.reverse && y.effects.loop == x.effects.loop);
                    expectEquals (y.effects.attackMs, x.effects.attackMs);
                    expectEquals (y.effects.releaseMs, x.effects.releaseMs);
                    expectEquals (y.effects.triggerMode, x.effects.triggerMode);
                }
            }
            expectEquals (b.getDetectedTempo(), 127.25);
            expectEquals (b.getGridOffsetSeconds(), 0.4321);
            expect (b.computeSessionHash() == a.computeSessionHash(), "same persisted content -> same hash");

            const auto rs = session::readSessionSource (saved);
            expectEquals (rs.sourceFile.getFullPathName(), src.sourceFile.getFullPathName());
            expectEquals ((int) rs.stemFiles.size(), 4);
            for (int s = 0; s < 4; ++s)
                expectEquals (rs.stemFiles[(size_t) s].getFullPathName(), src.stemFiles[(size_t) s].getFullPathName());
            expectEquals (rs.uiExtraJson, src.uiExtraJson);
            expectEquals (rs.stemCount, 4);
            expect (juce::JSON::parse (rs.uiExtraJson).getProperty ("lanes", {}).isArray(), "lanes survive as JSON text");
        }

        beginTest ("session hash changes on every persisted edit");
        {
            SModeSegmentModel m;
            makeStems (m, 2, 1000, 44100.0);
            m.addSegment (0, 0, 100);
            auto h = m.computeSessionHash();
            auto changed = [&] { const auto n = m.computeSessionHash(); const bool c = n != h; h = n; return c; };
            m.assignMidiNote (0, 0, 40);                 expect (changed(), "note");
            m.setLinked (1, true);                       expect (changed(), "link");
            m.setDetectedTempo (99.0);                   expect (changed(), "bpm");
            m.setGridOffsetSeconds (0.1);                expect (changed(), "grid");
            auto fx = m.getSegmentEffects (0, 0); fx.pan = 0.5f;
            m.setSegmentEffects (0, 0, fx);              expect (changed(), "pan");
            fx.attackMs = 3.0f; m.setSegmentEffects (0, 0, fx);  expect (changed(), "attack");
            fx.releaseMs = 9.0f; m.setSegmentEffects (0, 0, fx); expect (changed(), "release");
            fx.triggerMode = 1; m.setSegmentEffects (0, 0, fx);  expect (changed(), "trigger mode");
            m.setStemVolume (1, 0.2f);                   expect (changed(), "volume");
            m.resizeSegment (0, 0, 10, 100);             expect (changed(), "resize");
            expect (! changed(), "no edit -> same hash");
        }

        beginTest ("tolerant reader: rate rescale, clamping, garbage, newer schema");
        {
            juce::ValueTree t ("session");
            t.setProperty ("schema", 99, nullptr);
            t.setProperty ("sampleRate", 48000.0, nullptr);
            t.setProperty ("bpm", "not a number", nullptr);
            t.setProperty ("gridOffset", -3.0, nullptr);
            t.appendChild (juce::ValueTree ("somethingNew"), nullptr);
            juce::ValueTree st ("stem");
            st.setProperty ("index", 0, nullptr);
            st.setProperty ("volume", 1.0e9, nullptr);
            auto seg = [&] (juce::int64 a, juce::int64 b, int note)
            {
                juce::ValueTree g ("seg");
                g.setProperty ("start", a, nullptr);
                g.setProperty ("end", b, nullptr);
                g.setProperty ("note", note, nullptr);
                st.appendChild (g, nullptr);
            };
            seg (48000, 96000, 60);        // -> 44100..88200
            seg (90000, 200000, 200);      // end clamped to the stem length, note invalid -> -1
            seg (5000, 5000, 61);          // empty -> dropped
            seg (300000, 400000, 62);      // entirely past the end -> dropped
            st.appendChild (juce::ValueTree ("seg"), nullptr);   // no properties -> dropped
            t.appendChild (st, nullptr);
            juce::ValueTree ghost ("stem");
            ghost.setProperty ("index", 7, nullptr);            // stem that is not loaded
            ghost.appendChild (juce::ValueTree ("seg"), nullptr);
            t.appendChild (ghost, nullptr);

            SModeSegmentModel m;
            makeStems (m, 1, 100000, 44100.0);
            expectEquals (session::applySessionToModel (t, m), 2);
            const auto& segs = m.getStem (0)->segments;
            expectEquals ((int) segs[0].startSample, 44100);
            expectEquals ((int) segs[0].endSample, 88200);
            expectEquals (segs[0].midiNote, 60);
            expectEquals ((int) segs[1].endSample, 100000);
            expectEquals (segs[1].midiNote, -1);
            expectEquals (m.getDetectedTempo(), 0.0);
            expectEquals (m.getGridOffsetSeconds(), 0.0);
            expect (m.getStem (0)->volume <= 4.0f);

            SModeSegmentModel empty;
            expectEquals (session::applySessionToModel (juce::ValueTree(), empty), 0, "invalid tree is a no-op");
            const auto src = session::readSessionSource (juce::ValueTree());
            expect (src.sourceFile == juce::File() && src.stemFiles.empty());
        }

        beginTest ("play settings round trip + defaults");
        {
            session::PlaySettings p;
            p.mono = true; p.gate = true; p.loopGate = true; p.transportLoop = true;
            p.stems[0].pitch = 3.5f; p.stems[1].bendRange = 7.0f; p.stems[2].wheelDest = 2;
            p.stems[3].velCurve = 3; p.stems[7].atRange = 24.0f;
            const auto q = session::readPlay (streamRoundTrip (session::writePlay (p)));
            expect (q == p, "all play settings survive");

            const auto d = session::readPlay (juce::ValueTree());
            expect (d == session::PlaySettings(), "missing node -> defaults");
            juce::ValueTree bad ("play");
            juce::ValueTree c ("stemParam");
            c.setProperty ("index", 42, nullptr);
            bad.appendChild (c, nullptr);
            expect (session::readPlay (bad) == session::PlaySettings(), "out-of-range stem index ignored");
        }

        beginTest ("schema 2: DAW sync + MIDI CC bindings round trip; schema 1 reads with defaults");
        {
            expectEquals (session::kSchema, 2);
            session::PlaySettings p;
            p.mono = true;
            p.dawSync = true;
            p.ccBindings = { { 1, 74, "stem0.pitch" }, { 0, 1, "stem2.volume" }, { 16, 127, "seq.lane1.mute" } };
            const auto q = session::readPlay (streamRoundTrip (session::writePlay (p)));
            expect (q == p, "dawSync + every CC binding survive a stream round trip");
            expectEquals ((int) q.ccBindings.size(), 3);
            expect (q.ccBindings[0].target == "stem0.pitch" && q.ccBindings[0].cc == 74 && q.ccBindings[0].channel == 1);

            // A schema-1 <play> node (written before this field existed): defaults.
            juce::ValueTree v1 ("play");
            v1.setProperty ("mono", true, nullptr);
            v1.setProperty ("gate", true, nullptr);
            const auto r1 = session::readPlay (v1);
            expect (r1.mono && r1.gate, "schema-1 fields still read");
            expect (! r1.dawSync, "schema 1: DAW sync defaults to off");
            expect (r1.ccBindings.empty(), "schema 1: no CC bindings");

            // Garbage <cc> entries are dropped, never clamped onto another CC.
            juce::ValueTree bad ("play");
            auto addCc = [&bad] (const juce::var& ch, const juce::var& cc, const juce::var& target)
            {
                juce::ValueTree c ("cc");
                if (! ch.isVoid())     c.setProperty ("channel", ch, nullptr);
                if (! cc.isVoid())     c.setProperty ("cc", cc, nullptr);
                if (! target.isVoid()) c.setProperty ("target", target, nullptr);
                bad.appendChild (c, nullptr);
            };
            addCc (0, 128, "x");            // cc out of range
            addCc (17, 1, "x");             // channel out of range
            addCc (0, 7, "  ");             // empty target
            addCc (juce::var(), 7, "x");    // missing channel
            addCc (2, 7, "stem1.pitch");    // the only valid one
            const auto rb = session::readPlay (bad);
            expectEquals ((int) rb.ccBindings.size(), 1, "only the valid binding is kept");
            if (rb.ccBindings.size() == 1)
                expect (rb.ccBindings[0].channel == 2 && rb.ccBindings[0].cc == 7 && rb.ccBindings[0].target == "stem1.pitch");
        }

        beginTest ("save before restore writes the PENDING session back (A4)");
        {
            SModeSegmentModel saved;
            makeStems (saved, 2, 5000, 44100.0);
            saved.addSegment (0, 0, 100);
            saved.addSegment (1, 200, 300);
            session::SessionSource src;
            src.sourceFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("song.wav");
            const auto project = session::writeSession (saved, src);

            SModeSegmentModel live;   // nothing loaded yet (stems decoding / audio missing)
            auto buildLive = [&] { return session::writeSession (live, {}); };

            session::SessionState st;
            expect (! st.treeForSave (buildLive).isValid(), "Empty -> nothing written");

            st.beginRestore (project);
            expect (st.status() == session::SessionState::Status::Restoring);
            auto out = st.treeForSave (buildLive);
            expect (out.isEquivalentTo (project), "Restoring -> pending verbatim, not the empty live model");
            expectEquals (session::countSegments (out), 2);
            out.removeAllChildren (nullptr);
            expectEquals (session::countSegments (st.pending()), 2, "the written copy is independent of the pending tree");

            st.markMissing ("song and stems gone");
            expect (st.status() == session::SessionState::Status::MissingAudio);
            expect (st.treeForSave (buildLive).isEquivalentTo (project), "MissingAudio -> pending kept, never wiped");
            expectEquals (st.message(), juce::String ("song and stems gone"));

            st.completeRestore();
            expect (st.status() == session::SessionState::Status::Active && ! st.hasPending());
            expectEquals (session::countSegments (st.treeForSave (buildLive)), 0, "Active -> live model");

            st.clear();
            expect (! st.treeForSave (buildLive).isValid());
        }

        beginTest ("N-18: legacy-only project restores stem pitch / mod through the clamps (NaN, inf, enums)");
        {
            const double nan = std::numeric_limits<double>::quiet_NaN();
            const double inf = std::numeric_limits<double>::infinity();
            juce::ValueTree pitch ("smodeStemPitch");
            pitch.setProperty ("p0", 5.0, nullptr);          // valid
            pitch.setProperty ("p1", nan, nullptr);          // -> 0
            pitch.setProperty ("p2", inf, nullptr);          // -> 0 (not +24)
            pitch.setProperty ("p3", "1e999", nullptr);      // hand-edited text -> inf -> 0
            pitch.setProperty ("p4", 300.0, nullptr);        // -> clamped to +24
            pitch.setProperty ("p5", -300.0, nullptr);       // -> clamped to -24
            juce::ValueTree mod ("smodeStemMod");
            mod.setProperty ("vc0", 2, nullptr);             // valid
            mod.setProperty ("pbd0", 3, nullptr);            // valid
            mod.setProperty ("mwd0", 4, nullptr);            // valid
            mod.setProperty ("atd0", 2, nullptr);            // valid
            mod.setProperty ("pbr0", 7.0, nullptr);
            mod.setProperty ("mwr0", nan, nullptr);          // NaN -> 12
            mod.setProperty ("atr0", inf, nullptr);          // -> 12
            mod.setProperty ("vc1", 99, nullptr);            // enum out of range -> default 0
            mod.setProperty ("pbd1", -3, nullptr);           // -> default 1 (Pitch)
            mod.setProperty ("mwd1", nan, nullptr);          // NaN enum -> default 0 (no UB cast)
            mod.setProperty ("atd1", 1.0e300, nullptr);      // huge -> default 0
            mod.setProperty ("pbr1", -5.0, nullptr);         // -> 0
            mod.setProperty ("mwr1", 1000.0, nullptr);       // -> 48

            const auto p = session::readLegacyPlay (streamRoundTrip (pitch), streamRoundTrip (mod));
            expectEquals (p.stems[0].pitch, 5.0f);
            expectEquals (p.stems[1].pitch, 0.0f, "NaN pitch -> default");
            expectEquals (p.stems[2].pitch, 0.0f, "inf pitch -> default");
            expectEquals (p.stems[3].pitch, 0.0f, "'1e999' pitch -> default");
            expectEquals (p.stems[4].pitch, 24.0f);
            expectEquals (p.stems[5].pitch, -24.0f);
            expect (p.stems[0].velCurve == 2 && p.stems[0].bendDest == 3 && p.stems[0].wheelDest == 4 && p.stems[0].atDest == 2);
            expectEquals (p.stems[0].bendRange, 7.0f);
            expectEquals (p.stems[0].wheelRange, 12.0f, "NaN range -> default");
            expectEquals (p.stems[0].atRange, 12.0f, "inf range -> default");
            expectEquals (p.stems[1].velCurve, 0, "out-of-range enum -> default");
            expectEquals (p.stems[1].bendDest, 1, "negative enum -> default (Pitch)");
            expectEquals (p.stems[1].wheelDest, 0, "NaN enum -> default");
            expectEquals (p.stems[1].atDest, 0, "1e300 enum -> default");
            expectEquals (p.stems[1].bendRange, 0.0f);
            expectEquals (p.stems[1].wheelRange, 48.0f);
            for (const auto& s : p.stems)
                expect (std::isfinite (s.pitch) && std::isfinite (s.bendRange) && std::isfinite (s.wheelRange)
                        && std::isfinite (s.atRange), "every restored value is finite");

            // The processor's rule: no <play> -> the legacy nodes are used.
            juce::ValueTree smodeNoPlay ("smode");
            smodeNoPlay.setProperty ("schema", 1, nullptr);
            juce::String why;
            const auto viaRule = session::selectPlaySettings (smodeNoPlay, pitch, mod, &why);
            expect (viaRule == p, "selectPlaySettings reads the legacy nodes when <play> is absent");
            expect (why.startsWith ("legacy"), why);

            // A project without any "smode" node and without legacy nodes: defaults
            // (the previous project's settings must not leak, N-18).
            expect (session::selectPlaySettings ({}, {}, {}) == session::PlaySettings(), "nothing saved -> defaults");
        }

        beginTest ("N-2/N-18: <play> wins over stale legacy nodes");
        {
            session::PlaySettings saved;
            saved.stems[0].pitch = 5.0f;
            saved.stems[0].wheelDest = 3;
            juce::ValueTree smode ("smode");
            smode.setProperty ("schema", 2, nullptr);
            smode.appendChild (session::writePlay (saved), nullptr);

            juce::ValueTree stalePitch ("smodeStemPitch");
            stalePitch.setProperty ("p0", 3.0, nullptr);     // stale +3 (the bug: overrode +5)
            juce::ValueTree staleMod ("smodeStemMod");
            staleMod.setProperty ("mwd0", 1, nullptr);

            juce::String why;
            const auto p = session::selectPlaySettings (streamRoundTrip (smode), stalePitch, staleMod, &why);
            expectEquals (p.stems[0].pitch, 5.0f, "<play> pitch, not the stale legacy +3");
            expectEquals (p.stems[0].wheelDest, 3);
            expect (why.startsWith ("play"), why);
        }

        beginTest ("tolerant reader: non-finite / huge numbers in segments never reach the model");
        {
            const double nan = std::numeric_limits<double>::quiet_NaN();
            juce::ValueTree t ("session");
            t.setProperty ("sampleRate", 44100.0, nullptr);
            t.setProperty ("bpm", nan, nullptr);
            t.setProperty ("gridOffset", std::numeric_limits<double>::infinity(), nullptr);
            juce::ValueTree st ("stem");
            st.setProperty ("index", 0.0, nullptr);
            juce::ValueTree g ("seg");
            g.setProperty ("start", 100, nullptr);
            g.setProperty ("end", 2000, nullptr);
            g.setProperty ("note", nan, nullptr);            // -> -1
            g.setProperty ("pitch", 1.0e300, nullptr);       // -> 0
            g.setProperty ("trig", nan, nullptr);            // -> 0
            g.setProperty ("vol", nan, nullptr);             // -> 1
            g.setProperty ("rel", 1.0e9, nullptr);           // -> 5000
            st.appendChild (g, nullptr);
            juce::ValueTree bad ("seg");
            bad.setProperty ("start", std::numeric_limits<double>::infinity(), nullptr);   // -> 0
            bad.setProperty ("end", nan, nullptr);                                         // -> 0 -> dropped
            st.appendChild (bad, nullptr);
            t.appendChild (st, nullptr);

            SModeSegmentModel m;
            makeStems (m, 1, 10000, 44100.0);
            session::ApplyReport rep;
            expectEquals (session::applySessionToModel (t, m, &rep), 1);
            expectEquals (rep.dropped, 1);
            const auto& s = m.getStem (0)->segments[0];
            expectEquals (s.midiNote, -1);
            expectEquals (s.effects.pitchSemitones, 0);
            expectEquals (s.effects.triggerMode, 0);
            expectEquals (s.effects.volume, 1.0f);
            expectEquals (s.effects.releaseMs, 5000.0f);
            expectEquals (m.getDetectedTempo(), 0.0);
            expect (std::isfinite (m.getGridOffsetSeconds()));
        }

        beginTest ("N-10: relink check compares the content fingerprint / size and the duration");
        {
            const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("samplar_relink_test");
            dir.deleteRecursively();
            dir.createDirectory();
            auto writeBytes = [] (const juce::File& f, int n, int seed)
            {
                juce::MemoryBlock mb ((size_t) n);
                auto* p = static_cast<juce::uint8*> (mb.getData());
                juce::Random r (seed);
                for (int i = 0; i < n; ++i) p[i] = (juce::uint8) r.nextInt (256);
                f.replaceWithData (mb.getData(), mb.getSize());
            };
            const auto song   = dir.getChildFile ("song.wav");
            const auto copy   = dir.getChildFile ("elsewhere_song.wav");
            const auto remix  = dir.getChildFile ("remix.wav");      // same size, different content
            const auto bigger = dir.getChildFile ("bigger.wav");
            writeBytes (song, 300000, 1);
            song.copyFileTo (copy);
            writeBytes (remix, 300000, 2);
            writeBytes (bigger, 400000, 1);

            const auto fp = session::computeSourceFingerprint (song);
            expect (fp.startsWith ("v1:"), fp);
            expectEquals (session::computeSourceFingerprint (copy), fp, "a byte-identical copy has the same fingerprint");
            expect (session::computeSourceFingerprint (remix) != fp, "same size, different content -> different fingerprint");
            expect (session::computeSourceFingerprint (dir.getChildFile ("missing.wav")).isEmpty());

            // A session saved with the fingerprint: 2 chops, last one ends at 10 s (44.1 kHz).
            SModeSegmentModel m;
            makeStems (m, 1, 441000, 44100.0);
            m.addSegment (0, 0, 44100);
            m.addSegment (0, 400000, 441000);
            session::SessionSource src;
            src.sourceFile = song;
            src.sourceFingerprint = fp;
            const auto saved = streamRoundTrip (session::writeSession (m, src));
            expectEquals (saved.getProperty ("sourceFp").toString(), fp);
            expectEquals (session::lastChopEndSeconds (saved), 10.0);

            auto c = session::checkRelinkCandidate (saved, copy, 12.0);
            expect (c.accept && ! c.contentMismatch && ! c.tooShort, "identical copy, long enough -> accepted: " + c.reason);
            c = session::checkRelinkCandidate (saved, remix, 12.0);
            expect (! c.accept && c.contentMismatch, "same name/size but different audio -> rejected: " + c.reason);
            c = session::checkRelinkCandidate (saved, copy, 9.5);
            expect (! c.accept && c.tooShort && c.segsClamped == 1 && c.segsDropped == 0,
                    "shorter than the last chop end -> not auto-accepted, 1 chop would be shortened: " + c.reason);
            c = session::checkRelinkCandidate (saved, copy, 0.5);
            expect (! c.accept && c.segsClamped == 1 && c.segsDropped == 1, "0.5 s: 1 shortened, 1 removed: " + c.reason);
            c = session::checkRelinkCandidate (saved, copy, 0.0);
            expect (c.accept, "unknown duration is not checked");

            // Older session without a fingerprint: size decides.
            auto noFp = saved.createCopy();
            noFp.removeProperty ("sourceFp", nullptr);
            expect (session::checkRelinkCandidate (noFp, remix, 12.0).accept, "same size, no fingerprint -> accepted (size only)");
            expect (session::checkRelinkCandidate (noFp, bigger, 12.0).contentMismatch, "different size -> mismatch");
            // Nothing saved about the source (very old project): duration only.
            auto bare = noFp.createCopy();
            bare.removeProperty ("sourceSize", nullptr);
            const auto u = session::checkRelinkCandidate (bare, bigger, 12.0);
            expect (u.accept && u.identityUnknown, "no identity info -> accepted on duration: " + u.reason);
            expect (! session::checkRelinkCandidate (bare, bigger, 5.0).accept, "no identity info, too short -> rejected");

            // The pending tree is never modified by the check.
            expectEquals (session::countSegments (saved), 2);
            dir.deleteRecursively();
        }

        beginTest ("restored page UI state is handed to the page when it attaches");
        {
            SModeSegmentModel m;
            makeStems (m, 1, 1000, 44100.0);
            int calls = 0;
            juce::var got;
            m.setPendingExtraState (juce::JSON::parse (R"({"gridDivision":8})"));
            expect (m.hasPendingExtraState());
            m.setExtraStateHooks ([] { return juce::var (1); },
                                  [&] (const juce::var& v) { ++calls; got = v; m.pushUndoState ("from page restore"); });
            expectEquals (calls, 1);
            expectEquals ((int) got.getProperty ("gridDivision", 0), 8);
            expect (! m.hasPendingExtraState(), "consumed");
            expect (! m.canUndo(), "the page's restore cannot push undo entries");
            m.setExtraStateHooks (nullptr, nullptr);
            m.setExtraStateHooks ([] { return juce::var(); }, [&] (const juce::var&) { ++calls; });
            expectEquals (calls, 1, "handed over only once");
            expect (m.hasExtraStateHooks());
            m.setPendingExtraState (juce::var (5));
            m.clear();
            expect (! m.hasPendingExtraState(), "clear() drops it");
        }
    }
};

static SModeSessionTests sModeSessionTests;

} // namespace samplar
