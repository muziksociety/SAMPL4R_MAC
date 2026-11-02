#include <juce_core/juce_core.h>

#include "ui/Components/smode/LaneGridRescale.h"

namespace samplar {

// Review N-19: rescaling a lane to another cell resolution must never drop a
// slot silently, whatever order slots_ is in (copy appends, move edits in
// place, restore keeps the saved order).
class LaneGridRescaleTests : public juce::UnitTest
{
public:
    LaneGridRescaleTests() : juce::UnitTest ("LaneGridRescale", "SAMPLAR") {}

    static PatternSlot slot (int cell, int span, int seg, bool empty = false)
    {
        PatternSlot s;
        s.gridCell = cell;
        s.cellSpan = span;
        s.stemIndex = 0;
        s.segmentIndex = seg;
        s.empty = empty;
        return s;
    }

    static const PatternSlot* findSeg (const std::vector<PatternSlot>& v, int seg)
    {
        for (const auto& s : v)
            if (s.segmentIndex == seg && ! s.empty) return &s;
        return nullptr;
    }

    void runTest() override
    {
        beginTest ("review scenario: unsorted slots, x4 -> x8 keeps every slot");
        {
            // Slot at cell 8, then a copy at cell 0 appended LAST (old code dropped it).
            std::vector<PatternSlot> in { slot (8, 2, 1), slot (0, 1, 2) };
            LaneRescaleReport rep;
            auto out = rescaleLaneSlots (in, 4, 8, 4 * 8, rep);
            expectEquals ((int) out.size(), 2);
            expectEquals (rep.merged + rep.outOfRange, 0);
            expect (findSeg (out, 1) != nullptr && findSeg (out, 1)->gridCell == 16 && findSeg (out, 1)->cellSpan == 4);
            expect (findSeg (out, 2) != nullptr && findSeg (out, 2)->gridCell == 0 && findSeg (out, 2)->cellSpan == 2);
            expect (out[0].gridCell < out[1].gridCell, "output is sorted");
        }

        beginTest ("pre-B18 migration (4 -> 16 cells/bar) is lossless in any order");
        {
            std::vector<PatternSlot> in;
            for (int c : { 15, 3, 7, 0, 11, 1, 2, 14 })
                in.push_back (slot (c, 1, c));
            LaneRescaleReport rep;
            auto out = rescaleLaneSlots (in, 4, 16, 4 * 16, rep);
            expectEquals ((int) out.size(), 8);
            expectEquals (rep.merged + rep.outOfRange + rep.shortened, 0);
            for (int c : { 15, 3, 7, 0, 11, 1, 2, 14 })
            {
                auto* s = findSeg (out, c);
                expect (s != nullptr && s->gridCell == c * 4 && s->cellSpan == 4, "slot " + juce::String (c));
            }
        }

        beginTest ("coarser grid: collisions merge deterministically and are reported");
        {
            // 16ths 0,1,2,3 (one beat) + 4 -> x4 grid: cells 0,0,0,0,1.
            std::vector<PatternSlot> in { slot (2, 1, 2), slot (1, 1, 1), slot (4, 1, 4), slot (0, 1, 0), slot (3, 1, 3) };
            LaneRescaleReport rep;
            auto out = rescaleLaneSlots (in, 16, 4, 4 * 4, rep);
            expectEquals ((int) out.size(), 2);
            expectEquals (rep.merged, 3);
            expectEquals (rep.details.size(), 3);
            expect (findSeg (out, 0) != nullptr && findSeg (out, 0)->gridCell == 0, "earliest slot of the beat wins");
            expect (findSeg (out, 4) != nullptr && findSeg (out, 4)->gridCell == 1);
            // Same input in another order -> identical result.
            std::vector<PatternSlot> in2 { slot (3, 1, 3), slot (0, 1, 0), slot (4, 1, 4), slot (2, 1, 2), slot (1, 1, 1) };
            LaneRescaleReport rep2;
            auto out2 = rescaleLaneSlots (in2, 16, 4, 4 * 4, rep2);
            expectEquals ((int) out2.size(), 2);
            expectEquals (out2[0].segmentIndex, out[0].segmentIndex);
            expectEquals (out2[1].segmentIndex, out[1].segmentIndex);
        }

        beginTest ("an empty placeholder never displaces a filled slot");
        {
            std::vector<PatternSlot> in { slot (0, 1, 9, true), slot (1, 1, 1) };
            LaneRescaleReport rep;
            auto out = rescaleLaneSlots (in, 16, 4, 16, rep);
            expectEquals ((int) out.size(), 1);
            expect (! out[0].empty && out[0].segmentIndex == 1);
            expectEquals (rep.merged, 0);
            expectEquals (rep.emptyMerged, 1);
        }

        beginTest ("rounding overlap shortens the earlier slot instead of dropping the next");
        {
            // x16 -> x8: slot A at 0 span 3 -> span round(1.5)=2 ends at 2; B at 3 -> cell 1.
            std::vector<PatternSlot> in { slot (3, 1, 2), slot (0, 3, 1) };
            LaneRescaleReport rep;
            auto out = rescaleLaneSlots (in, 16, 8, 32, rep);
            expectEquals ((int) out.size(), 2);
            expectEquals (rep.merged, 0);
            expectEquals (rep.shortened, 1);
            expect (findSeg (out, 1)->gridCell == 0 && findSeg (out, 1)->cellSpan == 1);
            expect (findSeg (out, 2)->gridCell == 1);
        }

        beginTest ("slots past the lane end are reported, spans clamped to the lane");
        {
            std::vector<PatternSlot> in { slot (40, 1, 1), slot (14, 4, 2) };
            LaneRescaleReport rep;
            auto out = rescaleLaneSlots (in, 4, 4, 16, rep);
            expectEquals (rep.outOfRange, 1);
            expectEquals ((int) out.size(), 1);
            expectEquals (out[0].gridCell + out[0].cellSpan, 16);
        }

        beginTest ("round trip x4 -> x16 -> x4 restores the original grid");
        {
            std::vector<PatternSlot> in { slot (0, 2, 0), slot (2, 1, 1), slot (5, 3, 2) };
            LaneRescaleReport a, b;
            auto up = rescaleLaneSlots (in, 4, 16, 2 * 16, a);
            auto down = rescaleLaneSlots (up, 16, 4, 2 * 4, b);
            expectEquals ((int) down.size(), 3);
            expectEquals (b.merged, 0);
            for (size_t i = 0; i < in.size(); ++i)
            {
                auto* s = findSeg (down, in[i].segmentIndex);
                expect (s != nullptr && s->gridCell == in[i].gridCell && s->cellSpan == in[i].cellSpan);
            }
        }
    }
};

static LaneGridRescaleTests laneGridRescaleTests;

} // namespace samplar
