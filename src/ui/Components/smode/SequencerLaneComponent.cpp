#include "SequencerLaneComponent.h"
#include <random>

namespace samplar {

SequencerLaneComponent::SequencerLaneComponent(SModeSegmentModel& model, int laneIndex)
    : model_(model), laneIndex_(laneIndex)
{
    juce::Logger::writeToLog("[SequencerLane] === constructor lane=" + juce::String(laneIndex) + " ===");

    // Pattern dropdown
    addAndMakeVisible(patternCombo_);
    auto patterns = PatternGenerator::getAllPatterns();
    for (int i = 0; i < static_cast<int>(patterns.size()); ++i)
        patternCombo_.addItem(patterns[static_cast<size_t>(i)].second, i + 1);
    patternCombo_.setSelectedId(5);  // AABA default
    patternCombo_.onChange = [this]()
    {
        auto pats = PatternGenerator::getAllPatterns();
        int idx = patternCombo_.getSelectedId() - 1;
        if (idx >= 0 && idx < static_cast<int>(pats.size()))
        {
            if (pats[(size_t) idx].first != patternType_)
                model_.pushUndoState("Lane pattern");
            patternType_ = pats[static_cast<size_t>(idx)].first;
        }
    };

    // Bars dropdown (wider)
    addAndMakeVisible(barsCombo_);
    barsCombo_.addItem("2 bars", 1);
    barsCombo_.addItem("4 bars", 2);
    barsCombo_.addItem("8 bars", 3);
    barsCombo_.addItem("16 bars", 4);
    barsCombo_.setSelectedId(2);
    barsCombo_.onChange = [this]()
    {
        static const int barValues[] = { 2, 4, 8, 16 };
        int idx = barsCombo_.getSelectedId() - 1;
        if (idx >= 0 && idx < 4)
        {
            if (barValues[idx] != numBars_)
                model_.pushUndoState("Lane bars");
            numBars_ = barValues[idx];
        }
    };

    // Sub-division dropdown (wider, clearer labels)
    addAndMakeVisible(subdivCombo_);
    subdivCombo_.addItem("x1 (bar)", 1);
    subdivCombo_.addItem("x2 (half)", 2);
    subdivCombo_.addItem("x4 (beat)", 3);
    subdivCombo_.addItem("x8 (8th)", 4);
    subdivCombo_.addItem("x16 (16th)", 5);
    subdivCombo_.setSelectedId(1);
    subdivCombo_.onChange = [this]()
    {
        static const int subdivValues[] = { 1, 2, 4, 8, 16 };
        int idx = subdivCombo_.getSelectedId() - 1;
        if (idx >= 0 && idx < 5)
        {
            if (subdivValues[idx] != slotsPerBar_)
                model_.pushUndoState("Lane subdivision");
            slotsPerBar_ = subdivValues[idx];
        }
    };

    // Combo action button: Shuffle/Mutate/Slice — right-click to switch mode
    addAndMakeVisible(actionBtn_);
    actionBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3d3d6a));
    actionBtn_.setTooltip("Right-Click for options | Left-Click to activate");
    actionBtn_.setMouseClickGrabsKeyboardFocus(false);
    actionBtn_.onClick = [this]()
    {
        switch (actionMode_)
        {
            case ActionMode::Shuffle: shuffle(); break;   // shuffle() pushes its own undo
            case ActionMode::Mutate:
            {
                if (slots_.empty()) return;
                model_.pushUndoState("Lane mutate");
                std::vector<PatternGenerator::SegmentRef> available;
                collectAvailableSegments(available);
                PatternGenerator::mutate(slots_, available);
                buildSlotComponents();
                resized();
                repaint();
                break;
            }
            case ActionMode::Slice:
            {
                if (slots_.empty()) return;
                model_.pushUndoState("Lane slice");
                int totalCells = numBars_ * 4;
                if (totalCells <= 0) return;

                std::mt19937 rng(static_cast<unsigned>(juce::Time::currentTimeMillis()));
                std::vector<PatternSlot> filledPool;
                for (const auto& slot : slots_)
                    if (!slot.empty) filledPool.push_back(slot);
                if (filledPool.empty()) return;

                std::vector<PatternSlot> sliced;
                int cell = 0;
                const int possibleSpans[] = { 1, 1, 1, 2, 2, 3, 4 };
                const int numSpans = 7;

                while (cell < totalCells)
                {
                    int remaining = totalCells - cell;
                    std::vector<int> valid;
                    for (int s = 0; s < numSpans; ++s)
                        if (possibleSpans[s] <= remaining) valid.push_back(possibleSpans[s]);
                    if (valid.empty()) valid.push_back(remaining);

                    std::uniform_int_distribution<int> spanPick(0, static_cast<int>(valid.size()) - 1);
                    int span = valid[static_cast<size_t>(spanPick(rng))];
                    if (remaining - span > 0 && remaining - span < 2 && remaining <= 4)
                        span = remaining;

                    std::uniform_int_distribution<int> srcPick(0, static_cast<int>(filledPool.size()) - 1);
                    PatternSlot sub = filledPool[static_cast<size_t>(srcPick(rng))];
                    sub.gridCell = cell;
                    sub.cellSpan = span;
                    sub.empty = false;
                    sliced.push_back(sub);
                    cell += span;
                }

                slots_ = std::move(sliced);
                buildSlotComponents();
                resized();
                repaint();
                break;
            }
        }
    };

    // Right-click on action button shows mode picker
    actionBtn_.onStateChange = [this]()
    {
        if (actionBtn_.isDown() && juce::ModifierKeys::currentModifiers.isPopupMenu())
        {
            juce::PopupMenu menu;
            menu.addItem(1, "Shuffle", true, actionMode_ == ActionMode::Shuffle);
            menu.addItem(2, "Mutate", true, actionMode_ == ActionMode::Mutate);
            menu.addItem(3, "Slice", true, actionMode_ == ActionMode::Slice);
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&actionBtn_),
                [this](int result)
            {
                if (result == 1) { actionMode_ = ActionMode::Shuffle; actionBtn_.setButtonText("SHUFFLE");
                    actionBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3d3d6a)); }
                else if (result == 2) { actionMode_ = ActionMode::Mutate; actionBtn_.setButtonText("MUTATE");
                    actionBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff5a3d6a)); }
                else if (result == 3) { actionMode_ = ActionMode::Slice; actionBtn_.setButtonText("SLICE");
                    actionBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff6a5a2d)); }
            });
        }
    };

    // Clear
    addAndMakeVisible(clearBtn_);
    clearBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff4a2d2d));
    clearBtn_.setTooltip("Clear all slots in this lane");
    clearBtn_.onClick = [this]() { model_.pushUndoState("Lane clear"); clearAll(); };

    // Play
    addAndMakeVisible(playBtn_);
    playBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d6d3a));
    playBtn_.setTooltip("Play this lane");
    playBtn_.onClick = [this]()
    {
        if (onPlayRequested) onPlayRequested(laneIndex_);
    };

    // Stop
    addAndMakeVisible(stopBtn_);
    stopBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff6d2d2d));
    stopBtn_.setTooltip("Stop playback");
    stopBtn_.onClick = [this]()
    {
        if (onStopRequested) onStopRequested(laneIndex_);
    };

    // Mute
    addAndMakeVisible(muteBtn_);
    muteBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3d3d3d));
    muteBtn_.setTooltip("Mute/unmute this lane");
    muteBtn_.onClick = [this]()
    {
        model_.pushUndoState("Lane mute");
        muted_ = !muted_;
        muteBtn_.setColour(juce::TextButton::buttonColourId,
                            muted_ ? juce::Colour(0xffcc4444) : juce::Colour(0xff3d3d3d));
        repaint();
        // Retrigger playback so mute takes effect immediately
        if (onLaneChanged) onLaneChanged(laneIndex_);
    };

    // Drag WAV area — real draggable WAV export
    addAndMakeVisible(dragWavArea_);
    dragWavArea_.setHasContent(true);
    dragWavArea_.onCanRender = [this]() { return !slots_.empty(); };

    // Remove
    addAndMakeVisible(removeBtn_);
    removeBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff6d2d2d));
    removeBtn_.setTooltip("Remove this lane");
    removeBtn_.onClick = [this]()
    {
        if (onRemoveRequested) onRemoveRequested(laneIndex_);
    };
}

void SequencerLaneComponent::setStemDotActive(int stemIndex, bool active)
{
    if (stemIndex >= 0 && stemIndex < static_cast<int>(stemDots_.size()))
    {
        stemDots_[static_cast<size_t>(stemIndex)].active = active;
        repaint();
    }
}

void SequencerLaneComponent::setStemInfo(int stemCount,
                                          std::function<juce::Colour(int)> colorFunc)
{
    stemColorFunc_ = colorFunc;
    stemDots_.resize(static_cast<size_t>(stemCount));
    for (int i = 0; i < stemCount; ++i)
    {
        stemDots_[static_cast<size_t>(i)].active = true;
        stemDots_[static_cast<size_t>(i)].colour = colorFunc(i);
    }
    repaint();
}

void SequencerLaneComponent::shuffle()
{
    model_.pushUndoState("Lane shuffle");
    juce::Logger::writeToLog("[SequencerLane] shuffle: lane=" + juce::String(laneIndex_)
                             + " pattern=" + PatternGenerator::getPatternString(patternType_)
                             + " bars=" + juce::String(numBars_)
                             + " slotsPerBar=" + juce::String(slotsPerBar_));

    std::vector<PatternGenerator::SegmentRef> available;
    collectAvailableSegments(available);
    slots_ = PatternGenerator::generate(available, patternType_, numBars_, slotsPerBar_);

    // Assign gridCell and cellSpan for the integer cell grid
    int totalCells = numBars_ * 4;
    int cellsPerSlot = juce::jmax(1, totalCells / juce::jmax(1, static_cast<int>(slots_.size())));
    for (int i = 0; i < static_cast<int>(slots_.size()); ++i)
    {
        slots_[static_cast<size_t>(i)].gridCell = i * cellsPerSlot;
        slots_[static_cast<size_t>(i)].cellSpan = cellsPerSlot;
    }

    buildSlotComponents();
    resized();
    repaint();
}

void SequencerLaneComponent::clearAll()
{
    slots_.clear();
    for (auto& sc : slotComponents_)
        removeChildComponent(sc.get());
    slotComponents_.clear();
    resized();
    repaint();
    juce::Logger::writeToLog("[SequencerLane] Cleared lane " + juce::String(laneIndex_));
}

void SequencerLaneComponent::collectAvailableSegments(
    std::vector<PatternGenerator::SegmentRef>& out) const
{
    out.clear();
    for (int s = 0; s < model_.getStemCount(); ++s)
    {
        if (s < static_cast<int>(stemDots_.size()) && !stemDots_[static_cast<size_t>(s)].active)
            continue;
        auto* stem = model_.getStem(s);
        if (!stem || stem->mute) continue;
        const auto& buf = stem->audioBuffer;
        int numCh = buf.getNumChannels();

        for (int seg = 0; seg < static_cast<int>(stem->segments.size()); ++seg)
        {
            const auto& segment = stem->segments[static_cast<size_t>(seg)];
            int64_t len = segment.getLengthSamples();
            if (len <= 0) continue;

            // RMS filter: skip silent segments (same as Quick Assign, -40dB threshold)
            int64_t st = juce::jlimit((int64_t)0, (int64_t)buf.getNumSamples(), segment.startSample);
            int64_t en = juce::jlimit(st, (int64_t)buf.getNumSamples(), segment.endSample);
            int smpLen = static_cast<int>(en - st);
            if (smpLen <= 0) continue;

            double sumSq = 0.0;
            int total = 0;
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float* p = buf.getReadPointer(ch) + st;
                for (int j = 0; j < smpLen; ++j)
                    sumSq += p[j] * p[j];
                total += smpLen;
            }
            float rms = total > 0 ? static_cast<float>(std::sqrt(sumSq / total)) : 0.0f;
            float db = rms > 0.0f ? 20.0f * std::log10(rms) : -100.0f;
            if (db <= -40.0f) continue;  // Skip silent segments

            out.push_back({ s, seg, len });
        }
    }
}

void SequencerLaneComponent::buildSlotComponents()
{
    for (auto& sc : slotComponents_)
        removeChildComponent(sc.get());
    slotComponents_.clear();

    for (int i = 0; i < static_cast<int>(slots_.size()); ++i)
    {
        const auto& slot = slots_[static_cast<size_t>(i)];
        if (slot.empty) continue;  // No components for empty slots — grid shows empty space

        auto sc = std::make_unique<SequencerSlotComponent>();
        sc->slotIndex = i;

        {
            juce::Colour col = stemColorFunc_ ? stemColorFunc_(slot.stemIndex) : juce::Colours::grey;
            double sr = model_.getSampleRate();
            if (sr <= 0.0) sr = 44100.0;
            sc->setTempoInfo(bpm_, sr);
            int totalCells = numBars_ * 4;
            float cellW = (totalCells > 0 && slotArea_.getWidth() > 0)
                ? static_cast<float>(slotArea_.getWidth()) / static_cast<float>(totalCells)
                : 20.0f;
            sc->setCellWidth(cellW);
            sc->setTotalCells(totalCells);
            sc->setMaxCellSpan(4);
            sc->setSlotData(slot, &model_, col);
        }

        sc->onClicked = [this](int stemIdx, int segIdx)
        {
            if (onSlotClicked) onSlotClicked(stemIdx, segIdx);
        };

        // Sync slot modifications back to the data vector (with overlap check)
        sc->onSlotModified = [this](int sIdx, const PatternSlot& updated)
        {
            if (sIdx < 0 || sIdx >= static_cast<int>(slots_.size())) return;

            // Cleared slot → remove entirely (grid shows empty space)
            if (updated.empty)
            {
                slots_.erase(slots_.begin() + sIdx);
                buildSlotComponents();
                resized();
                repaint();
                return;
            }

            // Overlap check for grid position changes
            int newEnd = updated.gridCell + updated.cellSpan;
            for (int j = 0; j < static_cast<int>(slots_.size()); ++j)
            {
                if (j == sIdx || slots_[j].empty) continue;
                int otherEnd = slots_[j].gridCell + slots_[j].cellSpan;
                if (updated.gridCell < otherEnd && newEnd > slots_[j].gridCell)
                    return;  // Reject — would overlap
            }

            slots_[static_cast<size_t>(sIdx)] = updated;
            resized();
            repaint();
            if (onLaneChanged) onLaneChanged(laneIndex_);
        };

        // Move: reposition slot to absolute target cell
        sc->onSlotMoved = [this](int fromIdx, int targetCell)
        {
            if (fromIdx < 0 || fromIdx >= static_cast<int>(slots_.size())) return;

            auto& slot = slots_[static_cast<size_t>(fromIdx)];
            int totalCells = numBars_ * 4;

            int newCell = juce::jlimit(0, totalCells - slot.cellSpan, targetCell);
            if (newCell == slot.gridCell) return;

            // Overlap check
            int newEnd = newCell + slot.cellSpan;
            for (int j = 0; j < static_cast<int>(slots_.size()); ++j)
            {
                if (j == fromIdx || slots_[j].empty) continue;
                int otherEnd = slots_[j].gridCell + slots_[j].cellSpan;
                if (newCell < otherEnd && newEnd > slots_[j].gridCell)
                    return;  // Blocked
            }

            slot.gridCell = newCell;

            // Sync component's internal data + visual update
            if (fromIdx < static_cast<int>(slotComponents_.size()))
            {
                auto& comp = slotComponents_[static_cast<size_t>(fromIdx)];
                comp->setGridPosition(newCell, slot.cellSpan);
                float cellW = (totalCells > 0 && slotArea_.getWidth() > 0)
                    ? static_cast<float>(slotArea_.getWidth()) / static_cast<float>(totalCells) : 20.0f;
                int newX = slotArea_.getX() + static_cast<int>(newCell * cellW);
                comp->setBounds(newX, comp->getY(), comp->getWidth(), comp->getHeight());
            }
            repaint();
            if (onLaneChanged) onLaneChanged(laneIndex_);
        };

        // Resize: change gridCell/cellSpan with overlap checking
        sc->onSlotResized = [this](int fromIdx, int newGridCell, int newCellSpan)
        {
            if (fromIdx < 0 || fromIdx >= static_cast<int>(slots_.size())) return;

            auto& slot = slots_[static_cast<size_t>(fromIdx)];
            int totalCells = numBars_ * 4;

            newGridCell = juce::jlimit(0, totalCells - 1, newGridCell);
            newCellSpan = juce::jlimit(1, totalCells - newGridCell, newCellSpan);

            // Overlap check
            int newEnd = newGridCell + newCellSpan;
            for (int j = 0; j < static_cast<int>(slots_.size()); ++j)
            {
                if (j == fromIdx || slots_[j].empty) continue;
                int otherEnd = slots_[j].gridCell + slots_[j].cellSpan;
                if (newGridCell < otherEnd && newEnd > slots_[j].gridCell)
                    return;  // Blocked
            }

            slot.gridCell = newGridCell;
            slot.cellSpan = newCellSpan;

            // Sync speed/trim from component back to lane data
            if (fromIdx < static_cast<int>(slotComponents_.size()))
            {
                auto& comp = slotComponents_[static_cast<size_t>(fromIdx)];
                const auto& compSlot = comp->getSlotData();
                slot.speed = compSlot.speed;
                slot.trimStart = compSlot.trimStart;
                slot.trimEnd = compSlot.trimEnd;

                comp->setGridPosition(newGridCell, newCellSpan);
                float cellW = (totalCells > 0 && slotArea_.getWidth() > 0)
                    ? static_cast<float>(slotArea_.getWidth()) / static_cast<float>(totalCells) : 20.0f;
                int newX = slotArea_.getX() + static_cast<int>(newGridCell * cellW);
                int newW = juce::jmax(6, static_cast<int>(newCellSpan * cellW));
                comp->setBounds(newX, comp->getY(), newW, comp->getHeight());
            }
            repaint();
            if (onLaneChanged) onLaneChanged(laneIndex_);
        };

        // Speed change: resize cell (growth blocked if neighbors in the way)
        sc->onSpeedChanged = [this](int fromIdx, float speed, int baseCellSpan)
        {
            if (fromIdx < 0 || fromIdx >= static_cast<int>(slots_.size())) return;

            auto& slot = slots_[static_cast<size_t>(fromIdx)];
            int totalCells = numBars_ * 4;

            // New span: inverse of speed (0.5x = 2x cells, 2x = 0.5x cells)
            int newSpan = juce::jlimit(1, totalCells - slot.gridCell,
                                        static_cast<int>(std::round(baseCellSpan / speed)));

            // If growing, check there's empty space available
            if (newSpan > slot.cellSpan)
            {
                int slotEnd = slot.gridCell + newSpan;
                for (int j = 0; j < static_cast<int>(slots_.size()); ++j)
                {
                    if (j == fromIdx || slots_[j].empty) continue;
                    int otherStart = slots_[j].gridCell;
                    int otherEnd = otherStart + slots_[j].cellSpan;
                    if (slot.gridCell < otherEnd && slotEnd > otherStart)
                        return;  // Blocked — neighbor in the way
                }
            }

            slot.speed = speed;
            slot.cellSpan = newSpan;

            juce::Logger::writeToLog("[SequencerLane] Speed " + juce::String(speed, 2)
                                     + "x → cellSpan=" + juce::String(newSpan));
            buildSlotComponents();
            resized();
            repaint();
        };

        // Copy: one-shot — append new slot, then full rebuild so all callbacks are wired.
        // sourceIdx = the index of the slot that was shift/alt-dragged from;
        // we EXCLUDE that slot from the overlap check so a short drag adjacent
        // to the source isn't always rejected.
        sc->onSlotCopied = [this](int sourceIdx, const PatternSlot& copy)
        {
            int copyEnd = copy.gridCell + copy.cellSpan;
            for (int j = 0; j < static_cast<int>(slots_.size()); ++j)
            {
                if (j == sourceIdx) continue;        // skip the source slot
                if (slots_[j].empty) continue;
                int otherStart = slots_[j].gridCell;
                int otherEnd = otherStart + slots_[j].cellSpan;
                if (copy.gridCell < otherEnd && copyEnd > otherStart)
                {
                    juce::Logger::writeToLog("[SequencerLane] Copy REJECTED — overlaps slot "
                        + juce::String(j) + " at cell " + juce::String(otherStart));
                    return;
                }
            }

            slots_.push_back(copy);
            juce::Logger::writeToLog("[SequencerLane] Copied slot from " + juce::String(sourceIdx)
                + " to cell " + juce::String(copy.gridCell));

            buildSlotComponents();
            resized();
            repaint();
        };

        addAndMakeVisible(sc.get());
        slotComponents_.push_back(std::move(sc));
    }

    juce::Logger::writeToLog("[SequencerLane] buildSlotComponents: " + juce::String((int)slotComponents_.size())
                             + " components, slotArea=" + slotArea_.toString());

    // Notify parent that lane content changed (for live playback update)
    if (onLaneChanged) onLaneChanged(laneIndex_);
}

void SequencerLaneComponent::setPlayingSlot(int slotIndex, float progress)
{
    for (int i = 0; i < static_cast<int>(slotComponents_.size()); ++i)
    {
        bool isPlaying = (i == slotIndex);
        slotComponents_[static_cast<size_t>(i)]->setPlaying(isPlaying);
        if (isPlaying)
            slotComponents_[static_cast<size_t>(i)]->setProgress(progress);
    }
}

void SequencerLaneComponent::setSlot(int slotIndex, int stemIndex, int segIndex)
{
    if (slotIndex < 0 || slotIndex >= static_cast<int>(slots_.size())) return;
    model_.pushUndoState("Slot assign");
    auto& slot = slots_[static_cast<size_t>(slotIndex)];
    slot.stemIndex = stemIndex;
    slot.segmentIndex = segIndex;
    slot.empty = false;
    if (slotIndex < static_cast<int>(slotComponents_.size()))
    {
        juce::Colour col = stemColorFunc_ ? stemColorFunc_(stemIndex) : juce::Colours::grey;
        slotComponents_[static_cast<size_t>(slotIndex)]->setSlotData(slot, &model_, col);
    }
}

void SequencerLaneComponent::fillWithSegments(const std::vector<std::pair<int,int>>& pairs)
{
    if (pairs.empty()) return;
    model_.pushUndoState("Lane fill");

    // Fill the grid with randomly-sized slots using the provided segments
    slots_.clear();
    int totalCells = numBars_ * 4;

    std::mt19937 rng(static_cast<unsigned>(juce::Time::currentTimeMillis()));
    const int possibleSpans[] = { 1, 1, 1, 2, 2, 3, 4 };
    const int numSpans = 7;
    int cell = 0;
    int segIdx = 0;

    // Shuffle the segment order for variety on each call
    auto shuffledPairs = pairs;
    std::shuffle(shuffledPairs.begin(), shuffledPairs.end(), rng);

    while (cell < totalCells)
    {
        int remaining = totalCells - cell;

        // Pick a random span that fits
        std::vector<int> valid;
        for (int s = 0; s < numSpans; ++s)
            if (possibleSpans[s] <= remaining) valid.push_back(possibleSpans[s]);
        if (valid.empty()) valid.push_back(remaining);

        std::uniform_int_distribution<int> spanPick(0, static_cast<int>(valid.size()) - 1);
        int span = valid[static_cast<size_t>(spanPick(rng))];

        // Don't leave tiny remainder
        if (remaining - span > 0 && remaining - span < 2 && remaining <= 4)
            span = remaining;

        auto& p = shuffledPairs[static_cast<size_t>(segIdx) % shuffledPairs.size()];
        PatternSlot slot;
        slot.stemIndex = p.first;
        slot.segmentIndex = p.second;
        slot.empty = false;
        slot.gridCell = cell;
        slot.cellSpan = span;
        slot.role = static_cast<char>('A' + (segIdx % 4));
        slots_.push_back(slot);

        cell += span;
        ++segIdx;
    }

    buildSlotComponents();
    resized();
    repaint();
}

void SequencerLaneComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour(juce::Colour(0xff0e0e1e));
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(juce::Colour(0xff3a3a5a));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

    // Lane label
    g.setColour(juce::Colours::white.withAlpha(0.4f));
    g.setFont(juce::FontOptions(9.0f));
    // SEQ label at top-left (dots live in the bottom control row)
    g.drawText("SEQ " + juce::String(laneIndex_ + 1),
               getLocalBounds().reduced(4, 2), juce::Justification::topLeft);

    // Stem dots — bottom-anchored to match the relocated control row
    int dotStartX = 8;
    int dotY = getHeight() - 4 - kControlRowHeight + (kControlRowHeight - kDotSize) / 2;
    for (int i = 0; i < static_cast<int>(stemDots_.size()); ++i)
    {
        auto dotArea = juce::Rectangle<int>(dotStartX + i * (kDotSize + kDotGap),
                                             dotY, kDotSize, kDotSize);
        paintDot(g, i, dotArea);
    }

    // ── Grid divider lines in slot area ──
    if (!slotArea_.isEmpty() && numBars_ > 0)
    {
        int totalBeatsInPattern = numBars_ * 4;
        float pixelsPerBeat = static_cast<float>(slotArea_.getWidth()) / static_cast<float>(totalBeatsInPattern);
        float top = static_cast<float>(slotArea_.getY());
        float bot = static_cast<float>(slotArea_.getBottom());
        float left = static_cast<float>(slotArea_.getX());

        // Draw ALL grid lines (bar + beat)
        for (int beat = 0; beat <= totalBeatsInPattern; ++beat)
        {
            float x = left + beat * pixelsPerBeat;
            bool isBarLine = (beat % 4 == 0);

            if (isBarLine)
            {
                g.setColour(juce::Colours::white.withAlpha(0.4f));
                g.fillRect(x - 0.5f, top, 1.5f, bot - top);
            }
            else
            {
                g.setColour(juce::Colours::white.withAlpha(0.13f));
                g.fillRect(x, top, 1.0f, bot - top);
            }
        }

        // Draw bar numbers above grid
        g.setColour(juce::Colours::white.withAlpha(0.25f));
        g.setFont(juce::FontOptions(8.0f));
        for (int bar = 0; bar < numBars_; ++bar)
        {
            float bx = left + bar * 4 * pixelsPerBeat + 2;
            g.drawText(juce::String(bar + 1), static_cast<int>(bx),
                       slotArea_.getY(), 12, 10, juce::Justification::centredLeft);
        }

        // Playhead line — cyan vertical bar sweeping across full grid
        if (playheadPos_ >= 0.0f && playheadPos_ <= 1.0f)
        {
            float phX = left + playheadPos_ * static_cast<float>(slotArea_.getWidth());
            g.setColour(juce::Colours::cyan.withAlpha(0.9f));
            g.fillRect(phX - 1.0f, top, 2.5f, bot - top);
        }
    }

}

void SequencerLaneComponent::paintOverChildren(juce::Graphics& g)
{
    // ── Copy-drag destination ghost ─────────────────────────────────────
    // Any slot currently in Copy preview paints a faint cyan rectangle
    // at the destination cell, following the cursor. Shows the user
    // where the copy will land BEFORE they release.
    if (!slotArea_.isEmpty() && numBars_ > 0)
    {
        const int   totalCells = numBars_ * 4;
        const float cellW      = (totalCells > 0)
            ? static_cast<float>(slotArea_.getWidth()) / static_cast<float>(totalCells)
            : 20.0f;

        for (auto& sc : slotComponents_)
        {
            if (!sc) continue;
            const int destCell = sc->getCopyPreviewCell();
            if (destCell < 0) continue;
            const int span = juce::jmax(1, sc->getCopyPreviewCellSpan());

            const float ghostX = static_cast<float>(slotArea_.getX()) + destCell * cellW;
            const float ghostW = span * cellW;
            juce::Rectangle<float> ghost(ghostX + 1.0f,
                                         static_cast<float>(slotArea_.getY()) + 2.0f,
                                         ghostW - 2.0f,
                                         static_cast<float>(slotArea_.getHeight()) - 4.0f);

            const auto cyan = juce::Colour::fromRGB(0, 229, 255);
            g.setColour(cyan.withAlpha(0.18f));
            g.fillRoundedRectangle(ghost, 4.0f);
            g.setColour(cyan.withAlpha(0.85f));
            float dashes[] = { 6.0f, 4.0f };
            juce::Path outline;
            outline.addRoundedRectangle(ghost, 4.0f);
            juce::PathStrokeType stroke(2.0f);
            float dashArray[] = { 6.0f, 4.0f };
            juce::Path dashed;
            stroke.createDashedStroke(dashed, outline, dashArray, 2);
            g.strokePath(dashed, juce::PathStrokeType(2.0f));
            g.setColour(cyan);
            g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
            g.drawText("DROP HERE", ghost.toNearestInt(),
                       juce::Justification::centred);
            juce::ignoreUnused(dashes);
        }
    }

    // Mute overlay — paints OVER child slot components but NOT the control row
    if (muted_ && !slotArea_.isEmpty())
    {
        auto muteBounds = slotArea_.toFloat();
        g.setColour(juce::Colour(0xdd1a1a1a));  // Very opaque dark gray
        g.fillRect(muteBounds);
        g.setColour(juce::Colour(0xaa888888));
        g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        g.drawText("MUTED", slotArea_, juce::Justification::centred);
    }
}

void SequencerLaneComponent::paintDot(juce::Graphics& g, int dotIndex,
                                       juce::Rectangle<int> area)
{
    if (dotIndex < 0 || dotIndex >= static_cast<int>(stemDots_.size())) return;
    const auto& dot = stemDots_[static_cast<size_t>(dotIndex)];
    auto centre = area.getCentre().toFloat();
    float radius = static_cast<float>(kDotSize) * 0.5f;

    if (dot.active)
    {
        g.setColour(dot.colour);
        g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2, radius * 2);
    }
    else
    {
        g.setColour(dot.colour.withAlpha(0.3f));
        g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2, radius * 2, 1.5f);
    }

    // White letter inside dot: V=Vocals, B=Bass, D=Drums, O=Other
    // Derive from stem name if available
    juce::String letter;
    if (dotIndex < model_.getStemCount())
    {
        auto* stem = model_.getStem(dotIndex);
        if (stem)
        {
            auto name = stem->name.toLowerCase();
            if (name.contains("vocal"))      letter = "V";
            else if (name.contains("bass"))  letter = "B";
            else if (name.contains("drum"))  letter = "D";
            else if (name.contains("melody") || name.contains("other")) letter = "M";
            else                             letter = stem->name.substring(0, 1).toUpperCase();
        }
    }
    if (letter.isNotEmpty())
    {
        g.setColour(juce::Colours::white.withAlpha(dot.active ? 0.9f : 0.4f));
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.drawText(letter, area, juce::Justification::centred);
    }
}

void SequencerLaneComponent::mouseDown(const juce::MouseEvent& e)
{
    int dotStartX = 8;
    int dotY = getHeight() - 4 - kControlRowHeight + (kControlRowHeight - kDotSize) / 2;
    for (int i = 0; i < static_cast<int>(stemDots_.size()); ++i)
    {
        auto dotArea = juce::Rectangle<int>(dotStartX + i * (kDotSize + kDotGap),
                                             dotY, kDotSize, kDotSize);
        if (dotArea.expanded(2).contains(e.getPosition()))
        {
            stemDots_[static_cast<size_t>(i)].active = !stemDots_[static_cast<size_t>(i)].active;
            repaint();
            return;
        }
    }
}

void SequencerLaneComponent::mouseMove(const juce::MouseEvent& e)
{
    // Check if hovering over a stem dot
    int dotStartX = 8;
    int dotY = getHeight() - 4 - kControlRowHeight + (kControlRowHeight - kDotSize) / 2;
    for (int i = 0; i < static_cast<int>(stemDots_.size()); ++i)
    {
        auto dotArea = juce::Rectangle<int>(dotStartX + i * (kDotSize + kDotGap),
                                             dotY, kDotSize, kDotSize);
        if (dotArea.expanded(3).contains(e.getPosition()))
        {
            if (onStemHovered) onStemHovered(i);
            return;
        }
    }
    if (onStemHovered) onStemHovered(-1);
}

void SequencerLaneComponent::mouseExit(const juce::MouseEvent&)
{
    if (onStemHovered) onStemHovered(-1);
}

void SequencerLaneComponent::resized()
{
    auto area = getLocalBounds().reduced(4);
    // Top-right utility row: CLEAR | WAV drag | X (sit above the grid).
    auto topRow = area.removeFromTop(kControlRowHeight);
    removeBtn_.setBounds(topRow.removeFromRight(22));
    topRow.removeFromRight(2);
    dragWavArea_.setBounds(topRow.removeFromRight(70));
    topRow.removeFromRight(2);
    clearBtn_.setBounds(topRow.removeFromRight(45));
    area.removeFromTop(4);

    // Bottom control row: stem dots + play/stop/M + action + pattern + bars + subdiv.
    auto controlRow = area.removeFromBottom(kControlRowHeight);

    // Dots area
    int dotsWidth = static_cast<int>(stemDots_.size()) * (kDotSize + kDotGap) + 8;
    controlRow.removeFromLeft(dotsWidth);

    // Left group: Play | Stop | Mute
    playBtn_.setBounds(controlRow.removeFromLeft(24));
    controlRow.removeFromLeft(2);
    stopBtn_.setBounds(controlRow.removeFromLeft(24));
    controlRow.removeFromLeft(2);
    muteBtn_.setBounds(controlRow.removeFromLeft(22));
    controlRow.removeFromLeft(4);

    // Action combo button (Shuffle/Mutate/Slice)
    actionBtn_.setBounds(controlRow.removeFromLeft(62));
    controlRow.removeFromLeft(4);

    // Pattern combo
    patternCombo_.setBounds(controlRow.removeFromLeft(78));
    controlRow.removeFromLeft(2);

    // Bars combo — wide enough to show "4 bars"
    barsCombo_.setBounds(controlRow.removeFromLeft(75));
    controlRow.removeFromLeft(2);

    // Sub-division combo — wide enough to show "x1 (bar)"
    subdivCombo_.setBounds(controlRow.removeFromLeft(82));
    controlRow.removeFromLeft(3);

    area.removeFromBottom(4);
    slotArea_ = area;

    // ── Fixed grid layout: totalCells = numBars * 4, each cell = equal pixel width ──
    if (!slotComponents_.empty() && !slots_.empty())
    {
        int totalCells = numBars_ * 4;
        if (totalCells <= 0) totalCells = 16;
        float cellWidth = static_cast<float>(area.getWidth()) / static_cast<float>(totalCells);

        for (int i = 0; i < static_cast<int>(slots_.size()); ++i)
        {
            if (i >= static_cast<int>(slotComponents_.size())) break;

            const auto& slot = slots_[static_cast<size_t>(i)];
            int x = area.getX() + static_cast<int>(slot.gridCell * cellWidth);
            int w = juce::jmax(6, static_cast<int>(slot.cellSpan * cellWidth));

            slotComponents_[static_cast<size_t>(i)]->setBounds(x, area.getY(), w, area.getHeight());
            slotComponents_[static_cast<size_t>(i)]->setCellWidth(cellWidth);
            slotComponents_[static_cast<size_t>(i)]->setTotalCells(totalCells);
        }
    }
}

// ── Snapshot / Restore (for undo stack) ────────────────────────────────────

static juce::var slotToVar(const PatternSlot& s)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("stemIndex", s.stemIndex);
    obj->setProperty("segmentIndex", s.segmentIndex);
    obj->setProperty("role", (int) s.role);
    obj->setProperty("empty", s.empty);
    obj->setProperty("velocity", s.velocity);
    obj->setProperty("pitch", s.pitchSemitones);
    obj->setProperty("reverse", s.reverse);
    obj->setProperty("probability", s.probability);
    obj->setProperty("trimStart", s.trimStart);
    obj->setProperty("trimEnd", s.trimEnd);
    obj->setProperty("cellSpan", s.cellSpan);
    obj->setProperty("gridCell", s.gridCell);
    return juce::var(obj);
}

static PatternSlot slotFromVar(const juce::var& v)
{
    PatternSlot s;
    if (auto* obj = v.getDynamicObject())
    {
        s.stemIndex      = (int)   obj->getProperty("stemIndex");
        s.segmentIndex   = (int)   obj->getProperty("segmentIndex");
        s.role           = (char)(int) obj->getProperty("role");
        s.empty          = (bool)  obj->getProperty("empty");
        s.velocity       = (float)(double) obj->getProperty("velocity");
        s.pitchSemitones = (float)(double) obj->getProperty("pitch");
        s.reverse        = (bool)  obj->getProperty("reverse");
        s.probability    = (float)(double) obj->getProperty("probability");
        s.trimStart      = (float)(double) obj->getProperty("trimStart");
        s.trimEnd        = (float)(double) obj->getProperty("trimEnd");
        s.cellSpan       = (int)   obj->getProperty("cellSpan");
        s.gridCell       = (int)   obj->getProperty("gridCell");
    }
    return s;
}

juce::var SequencerLaneComponent::captureState() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("patternType", (int) patternType_);
    obj->setProperty("numBars",     numBars_);
    obj->setProperty("slotsPerBar", slotsPerBar_);
    obj->setProperty("muted",       muted_);
    obj->setProperty("bpm",         bpm_);

    juce::Array<juce::var> slotArr;
    for (const auto& s : slots_) slotArr.add(slotToVar(s));
    obj->setProperty("slots", slotArr);

    return juce::var(obj);
}

void SequencerLaneComponent::restoreState(const juce::var& v)
{
    auto* obj = v.getDynamicObject();
    if (obj == nullptr) return;

    patternType_ = (PatternGenerator::PatternType)(int) obj->getProperty("patternType");
    numBars_     = (int) obj->getProperty("numBars");
    slotsPerBar_ = (int) obj->getProperty("slotsPerBar");
    muted_       = (bool) obj->getProperty("muted");
    bpm_         = (double) obj->getProperty("bpm");

    slots_.clear();
    if (auto* slotArr = obj->getProperty("slots").getArray())
        for (auto& sv : *slotArr)
            slots_.push_back(slotFromVar(sv));

    // Sync combo boxes with restored state (no notification to avoid re-firing handlers).
    {
        auto pats = PatternGenerator::getAllPatterns();
        for (int i = 0; i < (int) pats.size(); ++i)
            if (pats[(size_t) i].first == patternType_)
            { patternCombo_.setSelectedId(i + 1, juce::dontSendNotification); break; }
    }
    {
        static const int barValues[]   = { 2, 4, 8, 16 };
        for (int i = 0; i < 4; ++i)
            if (barValues[i] == numBars_)
            { barsCombo_.setSelectedId(i + 1, juce::dontSendNotification); break; }
    }
    {
        static const int subdivValues[] = { 1, 2, 4, 8, 16 };
        for (int i = 0; i < 5; ++i)
            if (subdivValues[i] == slotsPerBar_)
            { subdivCombo_.setSelectedId(i + 1, juce::dontSendNotification); break; }
    }

    muteBtn_.setColour(juce::TextButton::buttonColourId,
        muted_ ? juce::Colours::darkred : juce::Colour(0xff2d3d2d));

    buildSlotComponents();
    resized();
    repaint();
}

} // namespace samplar
