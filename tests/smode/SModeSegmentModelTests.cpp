#include <juce_core/juce_core.h>

#include "services/smode/SModeSegmentModel.h"

namespace samplar {

class SModeSegmentModelTests : public juce::UnitTest
{
public:
    SModeSegmentModelTests() : juce::UnitTest ("SModeSegmentModel", "SAMPLAR") {}

    void runTest() override
    {
        beginTest ("add / remove / resize segments with bounds checks");
        {
            SModeSegmentModel m;
            m.setStemCount (4);
            expectEquals (m.getStemCount(), 4);
            expectEquals (m.addSegment (0, 0, 1000), 0);
            expectEquals (m.addSegment (0, 1000, 2500), 1);
            expectEquals (m.addSegment (1, 10, 20), 0);
            expectEquals (m.addSegment (0, 500, 500), -1, "empty range rejected");
            expectEquals (m.addSegment (0, 600, 100), -1, "reversed range rejected");
            expectEquals (m.addSegment (9, 0, 10), -1, "bad stem rejected");
            expectEquals ((int) m.getStem (0)->segments.size(), 2);

            m.resizeSegment (0, 1, 1200, 3000);
            expectEquals ((int) m.getSegment (0, 1)->startSample, 1200);
            expectEquals ((int) m.getSegment (0, 1)->getLengthSamples(), 1800);
            m.resizeSegment (0, 1, 3000, 3000);   // invalid: unchanged
            expectEquals ((int) m.getSegment (0, 1)->endSample, 3000);
            m.resizeSegment (0, 7, 0, 10);        // bad index: no-op, no crash

            expectEquals (m.getSegment (0, 1)->getDurationSeconds (1000.0), 1.8);
            expectEquals (m.getSegment (0, 1)->getDurationSeconds (0.0), 0.0);

            m.removeSegment (0, 0);
            expectEquals ((int) m.getStem (0)->segments.size(), 1);
            expectEquals ((int) m.getSegment (0, 0)->startSample, 1200, "remaining segment shifted down");
            m.removeSegment (0, 5);               // bad index: no-op
            m.removeSegment (-1, 0);
            expect (m.getSegment (0, 1) == nullptr);
            expect (m.getStem (-1) == nullptr && m.getStem (4) == nullptr);

            m.clearSegments (1);
            expect (m.getStem (1)->segments.empty());
        }

        beginTest ("MIDI assignment, lookup and LINK ALL multi-lookup");
        {
            SModeSegmentModel m;
            m.setStemCount (2);
            m.addSegment (0, 0, 10);
            m.addSegment (0, 10, 20);
            m.addSegment (1, 0, 10);
            m.quickAssignMidiNotes (36);
            expectEquals (m.getSegment (0, 0)->midiNote, 36);
            expectEquals (m.getSegment (0, 1)->midiNote, 37);
            expectEquals (m.getSegment (1, 0)->midiNote, 38);
            expect (m.findSegmentByMidiNote (37) == std::make_pair (0, 1));
            expect (m.findSegmentByMidiNote (99) == std::make_pair (-1, -1));

            m.assignMidiNote (1, 0, 36);
            expectEquals ((int) m.findAllSegmentsByMidiNote (36).size(), 2);
            m.clearMidiNote (1, 0);
            expectEquals ((int) m.findAllSegmentsByMidiNote (36).size(), 1);
        }

        beginTest ("undo / redo restore segments, effects, mix state and tempo");
        {
            SModeSegmentModel m;
            m.setStemCount (2);
            int notifications = 0;
            m.onUndoStackChanged = [&] { ++notifications; };
            expect (! m.canUndo() && ! m.canRedo());
            expect (! m.undo() && ! m.redo(), "empty stacks");

            m.pushUndoState ("add segment");
            m.addSegment (0, 0, 100);
            m.setDetectedTempo (120.0);
            expect (m.canUndo());
            expectEquals (m.peekUndoLabel(), juce::String ("add segment"));

            m.pushUndoState ("edit");
            SegmentEffects fx;
            fx.pitchSemitones = 7;
            fx.reverse = true;
            m.setSegmentEffects (0, 0, fx);
            m.resizeSegment (0, 0, 10, 90);
            m.setMute (1, true);

            expect (m.undo());
            expectEquals (m.getSegmentEffects (0, 0).pitchSemitones, 0, "effects undone");
            expectEquals ((int) m.getSegment (0, 0)->startSample, 0, "resize undone");
            expect (! m.getStem (1)->mute, "mute undone");
            expect (m.canRedo());
            expectEquals (m.peekRedoLabel(), juce::String ("edit"));

            expect (m.undo());
            expect (m.getStem (0)->segments.empty(), "segment add undone");
            expectEquals (m.getDetectedTempo(), 0.0);
            expect (! m.canUndo());

            expect (m.redo());
            expectEquals ((int) m.getStem (0)->segments.size(), 1);
            expectEquals (m.getDetectedTempo(), 120.0);
            expect (m.redo());
            expectEquals (m.getSegmentEffects (0, 0).pitchSemitones, 7);
            expect (m.getSegmentEffects (0, 0).reverse);
            expectEquals ((int) m.getSegment (0, 0)->startSample, 10);
            expect (m.getStem (1)->mute);
            expect (! m.canRedo());
            expect (notifications >= 6, "UI notified on every stack change");

            beginTest ("a new push after undo clears redo");
            expect (m.undo());
            expect (m.canRedo());
            m.pushUndoState ("branch");
            expect (! m.canRedo());
        }

        beginTest ("pushes are suppressed while a restore is in progress");
        {
            SModeSegmentModel m;
            m.setStemCount (1);
            m.beginSuppressPush();
            m.pushUndoState ("ignored");
            m.endSuppressPush();
            expect (! m.canUndo());
        }

        beginTest ("undo history is bounded (50 levels)");
        {
            SModeSegmentModel m;
            m.setStemCount (1);
            for (int i = 0; i < 80; ++i)
            {
                m.pushUndoState ("step " + juce::String (i));
                m.addSegment (0, i * 10, i * 10 + 5);
            }
            int undos = 0;
            while (m.undo()) ++undos;
            expectEquals (undos, 50);
            expectEquals ((int) m.getStem (0)->segments.size(), 30, "oldest 30 steps are no longer undoable");
        }

        beginTest ("extra state hooks are captured and restored with each snapshot");
        {
            SModeSegmentModel m;
            m.setStemCount (1);
            juce::var extra (1);
            m.setExtraStateHooks ([&] { return extra; }, [&] (const juce::var& v) { extra = v; });
            m.pushUndoState ("x");
            extra = 2;
            m.undo();
            expectEquals ((int) extra, 1);
            m.redo();
            expectEquals ((int) extra, 2);
        }

        beginTest ("clear() bumps contentGeneration and drops everything (stale async results)");
        {
            SModeSegmentModel m;
            const auto g0 = m.getContentGeneration();
            m.setStemCount (3);
            m.addSegment (0, 0, 10);
            m.pushUndoState ("x");
            m.setDetectedTempo (99.0);
            m.setGridOffsetSeconds (0.25);
            expectEquals ((juce::int64) m.getContentGeneration(), (juce::int64) g0, "edits do not bump the generation");
            m.clear();
            expectEquals ((juce::int64) m.getContentGeneration(), (juce::int64) (g0 + 1));
            expectEquals (m.getStemCount(), 0);
            expect (! m.canUndo() && ! m.canRedo());
            expectEquals (m.getDetectedTempo(), 0.0);
            expectEquals (m.getGridOffsetSeconds(), 0.0);
            m.clear();
            expectEquals ((juce::int64) m.getContentGeneration(), (juce::int64) (g0 + 2));
        }

        beginTest ("solo / mute audibility");
        {
            SModeSegmentModel m;
            m.setStemCount (3);
            expect (m.isStemAudible (0) && m.isStemAudible (1) && m.isStemAudible (2));
            m.setMute (1, true);
            expect (! m.isStemAudible (1));
            m.setSolo (2, true);
            expect (! m.isStemAudible (0), "another stem soloed");
            expect (m.isStemAudible (2));
        }
    }
};

static SModeSegmentModelTests sModeSegmentModelTests;

} // namespace samplar
