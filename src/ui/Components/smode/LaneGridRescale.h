#pragma once

// LaneGridRescale - moves sequencer-lane slots to a new cell resolution
// (review N-19). Pure function, no UI: used by SequencerLaneComponent for a
// subdivision change and for restoring lanes saved with another resolution
// (pre-B18 lanes implicitly used 4 cells per bar), and unit tested.
//
// Rules (deterministic, nothing disappears silently):
//  1. Every slot keeps its musical position: cell = floor(gridCell * to / from),
//     span = max(1, round(cellSpan * to / from)).
//  2. Slots are processed in (new cell, filled before empty, old cell, index)
//     order - the input order does not matter (copy appends at the end, move
//     edits in place, restore keeps the saved order).
//  3. Two slots landing on the SAME cell (only possible when the grid gets
//     coarser) are merged: the filled slot that came first musically stays,
//     the other one is reported in `merged`. An empty placeholder never
//     displaces a filled slot.
//  4. A slot that would overlap the next one after rounding is shortened to
//     end where the next one starts (it is never dropped for that).
//  5. Slots past the end of the lane are reported in `outOfRange`.
// Upscaling (x4 -> x8 / x16, the pre-B18 migration) is lossless.

#include "../../../services/smode/PatternGenerator.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace samplar {

struct LaneRescaleReport
{
    int kept { 0 };
    int merged { 0 };        // filled slots merged into an earlier slot on the same cell
    int emptyMerged { 0 };   // empty placeholders dropped on a collision (no data)
    int outOfRange { 0 };    // slots starting past the end of the lane
    int shortened { 0 };     // spans cut so slots do not overlap
    juce::StringArray details;   // one ASCII line per merged / out-of-range slot
};

inline std::vector<PatternSlot> rescaleLaneSlots(const std::vector<PatternSlot>& in, int fromCellsPerBar,
                                                 int toCellsPerBar, int totalCells, LaneRescaleReport& rep)
{
    rep = {};
    if (fromCellsPerBar <= 0 || toCellsPerBar <= 0 || totalCells <= 0)
    {
        rep.kept = (int) in.size();
        return in;
    }

    struct Mapped { PatternSlot s; int oldCell; size_t order; };
    std::vector<Mapped> mapped;
    mapped.reserve(in.size());
    const double k = (double) toCellsPerBar / (double) fromCellsPerBar;
    for (size_t i = 0; i < in.size(); ++i)
    {
        PatternSlot s = in[i];
        const int oldCell = juce::jmax(0, s.gridCell);
        const int oldSpan = juce::jmax(1, s.cellSpan);
        // +1e-9: 3 * 4 / 4 must stay 3 even with floating error.
        s.gridCell = (int) std::floor((double) oldCell * k + 1.0e-9);
        s.cellSpan = juce::jmax(1, (int) std::lround((double) oldSpan * k));
        if (s.gridCell >= totalCells)
        {
            if (! s.empty)
            {
                ++rep.outOfRange;
                rep.details.add("slot " + juce::String((int) i) + " (stem " + juce::String(s.stemIndex) + " seg "
                                + juce::String(s.segmentIndex) + ") at cell " + juce::String(oldCell)
                                + " is past the lane end");
            }
            continue;
        }
        mapped.push_back({ s, oldCell, i });
    }

    std::stable_sort(mapped.begin(), mapped.end(), [](const Mapped& a, const Mapped& b)
    {
        if (a.s.gridCell != b.s.gridCell) return a.s.gridCell < b.s.gridCell;
        if (a.s.empty != b.s.empty) return ! a.s.empty;   // filled first
        if (a.oldCell != b.oldCell) return a.oldCell < b.oldCell;   // earlier in the bar first
        return a.order < b.order;
    });

    std::vector<PatternSlot> out;
    out.reserve(mapped.size());
    for (const auto& m : mapped)
    {
        if (! out.empty() && out.back().gridCell == m.s.gridCell)
        {
            // Same cell: the earlier (filled-first) slot stays.
            if (m.s.empty) ++rep.emptyMerged;
            else
            {
                ++rep.merged;
                rep.details.add("slot " + juce::String((int) m.order) + " (stem " + juce::String(m.s.stemIndex)
                                + " seg " + juce::String(m.s.segmentIndex) + ") merged into the slot on cell "
                                + juce::String(m.s.gridCell));
            }
            continue;
        }
        if (! out.empty() && out.back().gridCell + out.back().cellSpan > m.s.gridCell)
        {
            out.back().cellSpan = m.s.gridCell - out.back().gridCell;   // >= 1: cells differ
            ++rep.shortened;
        }
        out.push_back(m.s);
    }
    for (auto& s : out)
        s.cellSpan = juce::jlimit(1, juce::jmax(1, totalCells - s.gridCell), s.cellSpan);
    rep.kept = (int) out.size();
    return out;
}

} // namespace samplar
