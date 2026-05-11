#include "SequencerSlotComponent.h"
#include "../../../app/ServiceLocator.h"
#include "../../../app/ServiceRegistry.h"
#include "../../../services/MidiMappingService.h"
#include <algorithm>

namespace samplar {

SequencerSlotComponent::SequencerSlotComponent()
{
    setInterceptsMouseClicks(true, false);
}

void SequencerSlotComponent::setSlotData(const PatternSlot& slot,
                                          SModeSegmentModel* model,
                                          juce::Colour stemColour)
{
    slot_ = slot;
    model_ = model;
    stemColour_ = stemColour;
    pathValid_ = false;
    repaint();
}

void SequencerSlotComponent::clearSlot()
{
    slot_ = PatternSlot{};
    model_ = nullptr;
    stemColour_ = juce::Colours::grey;
    pathValid_ = false;
    repaint();
}

void SequencerSlotComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(1.0f);

    if (slot_.empty)
    {
        // Empty slot — dashed outline
        g.setColour(juce::Colours::white.withAlpha(0.15f));
        g.fillRoundedRectangle(bounds, 3.0f);

        g.setColour(juce::Colours::white.withAlpha(0.3f));
        float dashLengths[] = { 4.0f, 3.0f };
        g.drawDashedLine(juce::Line<float>(bounds.getX(), bounds.getCentreY(),
                                            bounds.getRight(), bounds.getCentreY()),
                         dashLengths, 2, 1.0f);
        return;
    }

    // Filled slot background
    g.setColour(stemColour_.withAlpha(0.2f));
    g.fillRoundedRectangle(bounds, 3.0f);
    g.setColour(stemColour_.withAlpha(0.6f));
    g.drawRoundedRectangle(bounds, 3.0f, 1.0f);

    // Trim shading — dark overlay on trimmed regions
    if (slot_.trimStart > 0.01f)
    {
        float trimX = bounds.getX() + bounds.getWidth() * slot_.trimStart;
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.fillRect(bounds.getX(), bounds.getY(), trimX - bounds.getX(), bounds.getHeight());
    }
    if (slot_.trimEnd < 0.99f)
    {
        float trimX = bounds.getX() + bounds.getWidth() * slot_.trimEnd;
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.fillRect(trimX, bounds.getY(), bounds.getRight() - trimX, bounds.getHeight());
    }

    // Draw mini waveform
    if (!pathValid_)
        rebuildWaveformPath();

    if (!waveformPath_.isEmpty())
    {
        auto waveArea = bounds.reduced(2.0f, 4.0f);
        g.setColour(stemColour_.withAlpha(0.85f));

        // Scale path to fit the slot area
        auto pathBounds = waveformPath_.getBounds();
        if (pathBounds.getWidth() > 0 && pathBounds.getHeight() > 0)
        {
            auto transform = juce::AffineTransform::translation(-pathBounds.getX(), -pathBounds.getY())
                .scaled(waveArea.getWidth() / pathBounds.getWidth(),
                        waveArea.getHeight() / pathBounds.getHeight())
                .translated(waveArea.getX(), waveArea.getY());

            g.fillPath(waveformPath_, transform);
        }
    }

    // Role label (A/B/C/D) in corner
    g.setColour(juce::Colours::white.withAlpha(0.7f));
    g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    g.drawText(juce::String::charToString(slot_.role),
               getLocalBounds().reduced(3, 1),
               juce::Justification::topLeft);

    // Per-slot effect indicators
    if (slot_.reverse)
    {
        g.setColour(juce::Colours::cyan.withAlpha(0.8f));
        g.setFont(juce::FontOptions(8.0f));
        g.drawText("REV", getLocalBounds().reduced(3, 1), juce::Justification::bottomLeft);
    }
    if (std::abs(slot_.speed - 1.0f) > 0.01f)
    {
        g.setColour(juce::Colours::green.withAlpha(0.8f));
        g.setFont(juce::FontOptions(7.0f));
        g.drawText(juce::String(slot_.speed, 2) + "x", getLocalBounds().reduced(2, 1), juce::Justification::centredLeft);
    }
    if (std::abs(slot_.pitchSemitones) > 0.1f)
    {
        g.setColour(juce::Colours::yellow.withAlpha(0.8f));
        g.setFont(juce::FontOptions(8.0f));
        juce::String pitchStr = (slot_.pitchSemitones > 0 ? "+" : "") + juce::String((int)slot_.pitchSemitones);
        g.drawText(pitchStr, getLocalBounds().reduced(3, 1), juce::Justification::bottomRight);
    }

    // Cell span indicator (show if wider than 1 cell)
    if (slot_.cellSpan > 1)
    {
        g.setColour(juce::Colours::orange.withAlpha(0.8f));
        g.setFont(juce::FontOptions(7.0f));
        g.drawText(juce::String(slot_.cellSpan) + "x", getLocalBounds().reduced(2, 1), juce::Justification::topRight);
    }

    // Playing highlight — progress fill + glow border
    if (playing_)
    {
        // Progress fill left-to-right
        if (progress_ > 0.0f)
        {
            auto fillBounds = bounds.withWidth(bounds.getWidth() * progress_);
            g.setColour(juce::Colours::cyan.withAlpha(0.25f));
            g.fillRoundedRectangle(fillBounds, 3.0f);
        }
        g.setColour(juce::Colours::cyan);
        g.drawRoundedRectangle(bounds, 3.0f, 2.0f);
    }

    // 4-corner zone indicators (top = trim, bottom = speed-stretch)
    if (!slot_.empty)
    {
        float halfH = bounds.getHeight() * 0.5f;
        // Top-left (trim start) — white
        g.setColour(juce::Colours::white.withAlpha(dragging_ ? 0.6f : 0.2f));
        g.fillRect(bounds.getX(), bounds.getY() + 1.0f, 3.0f, halfH - 2.0f);
        // Top-right (trim end) — white
        g.fillRect(bounds.getRight() - 3.0f, bounds.getY() + 1.0f, 3.0f, halfH - 2.0f);
        // Bottom-left (speed-stretch) — green
        g.setColour(juce::Colours::green.withAlpha(dragging_ ? 0.6f : 0.2f));
        g.fillRect(bounds.getX(), bounds.getY() + halfH + 1.0f, 3.0f, halfH - 2.0f);
        // Bottom-right (speed-stretch) — green
        g.fillRect(bounds.getRight() - 3.0f, bounds.getY() + halfH + 1.0f, 3.0f, halfH - 2.0f);
    }

    // Tooltip handled by JUCE TooltipClient (getTooltip()) — renders above all components
}

void SequencerSlotComponent::rebuildWaveformPath()
{
    waveformPath_.clear();
    pathValid_ = true;

    if (slot_.empty || !model_) return;

    auto* stem = model_->getStem(slot_.stemIndex);
    auto* seg = model_->getSegment(slot_.stemIndex, slot_.segmentIndex);
    if (!stem || !seg || seg->getLengthSamples() <= 0) return;

    const auto& buf = stem->audioBuffer;
    int numCh = buf.getNumChannels();
    if (numCh == 0) return;

    int64_t segStart = juce::jlimit((int64_t)0, (int64_t)buf.getNumSamples(), seg->startSample);
    int64_t segEnd = juce::jlimit(segStart, (int64_t)buf.getNumSamples(), seg->endSample);

    // Show trimmed portion of the segment
    int64_t totalSegLen = segEnd - segStart;
    int64_t startSmp = segStart + static_cast<int64_t>(slot_.trimStart * totalSegLen);
    int64_t endSmp = segStart + static_cast<int64_t>(slot_.trimEnd * totalSegLen);
    startSmp = juce::jlimit(segStart, segEnd, startSmp);
    endSmp = juce::jlimit(startSmp + 1, segEnd, endSmp);

    int segLen = static_cast<int>(endSmp - startSmp);
    if (segLen <= 0) return;

    // Build waveform envelope at ~64 pixel resolution
    constexpr int kResolution = 64;
    int samplesPerBin = juce::jmax(1, segLen / kResolution);
    int numBins = juce::jmin(kResolution, segLen);

    // Build the waveform as a filled path (positive half mirrored)
    float pathW = static_cast<float>(numBins);
    float pathH = 20.0f;  // Arbitrary height, will be scaled

    waveformPath_.startNewSubPath(0.0f, pathH * 0.5f);

    // Top half (positive peak)
    for (int bin = 0; bin < numBins; ++bin)
    {
        int smpStart = static_cast<int>(startSmp) + bin * samplesPerBin;
        int smpEnd = juce::jmin(smpStart + samplesPerBin, static_cast<int>(endSmp));

        float peak = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* data = buf.getReadPointer(ch);
            for (int s = smpStart; s < smpEnd; ++s)
                peak = juce::jmax(peak, std::abs(data[s]));
        }

        float y = pathH * 0.5f * (1.0f - juce::jmin(peak, 1.0f));
        waveformPath_.lineTo(static_cast<float>(bin), y);
    }

    // Bottom half (mirror)
    for (int bin = numBins - 1; bin >= 0; --bin)
    {
        int smpStart = static_cast<int>(startSmp) + bin * samplesPerBin;
        int smpEnd = juce::jmin(smpStart + samplesPerBin, static_cast<int>(endSmp));

        float peak = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* data = buf.getReadPointer(ch);
            for (int s = smpStart; s < smpEnd; ++s)
                peak = juce::jmax(peak, std::abs(data[s]));
        }

        float y = pathH * 0.5f * (1.0f + juce::jmin(peak, 1.0f));
        waveformPath_.lineTo(static_cast<float>(bin), y);
    }

    waveformPath_.closeSubPath();
}

// ── Tooltip (JUCE TooltipClient — renders on top of everything) ──────────

juce::String SequencerSlotComponent::getTooltip()
{
    if (slot_.empty || !tooltipsEnabled_) return {};

    auto mousePos = getMouseXYRelative();
    int edgeZ = juce::jmin(kEdgeZone, getWidth() / 4);
    bool inLeft = (edgeZ >= 3 && mousePos.x <= edgeZ);
    bool inRight = (edgeZ >= 3 && mousePos.x >= getWidth() - edgeZ);
    bool inTop = (mousePos.y < getHeight() / 2);

    if (inLeft && inTop)        return "TRIM START";
    if (inRight && inTop)       return "TRIM END";
    if (inLeft && !inTop)       return "STRETCH";
    if (inRight && !inTop)      return "STRETCH";
    return "Drag=Move | Shift=Copy | Ctrl=Pitch | DblClick=Play";
}

// ── Mouse: right-click context menu ──────────────────────────────────────

void SequencerSlotComponent::mouseDown(const juce::MouseEvent& e)
{
    if (slot_.empty) return;
    tooltipVisible_ = false;

    // Right-click → context menu
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        menu.addItem(1, slot_.reverse ? "Un-Reverse" : "Reverse");
        menu.addSeparator();
        menu.addItem(10, "Pitch +1");
        menu.addItem(11, "Pitch -1");
        menu.addItem(12, "Pitch +3");
        menu.addItem(13, "Pitch -3");
        menu.addItem(14, "Reset Pitch");
        menu.addSeparator();

        juce::PopupMenu spanMenu;
        spanMenu.addItem(20, "1 cell");
        spanMenu.addItem(21, "2 cells");
        spanMenu.addItem(22, "3 cells");
        spanMenu.addItem(23, "4 cells");
        menu.addSubMenu("Cell Span", spanMenu);

        menu.addSeparator();
        menu.addItem(30, "Velocity 100%");
        menu.addItem(31, "Velocity 75%");
        menu.addItem(32, "Velocity 50%");
        menu.addItem(33, "Velocity 25%");
        menu.addSeparator();
        menu.addItem(38, "Reset Trim");
        menu.addSeparator();
        juce::PopupMenu speedMenu;
        speedMenu.addItem(50, "1x (normal)");
        speedMenu.addSeparator();
        speedMenu.addSectionHeader("Slower (expand)");
        speedMenu.addItem(51, "0.75x");
        speedMenu.addItem(52, "0.5x");
        speedMenu.addItem(53, "0.25x");
        speedMenu.addSeparator();
        speedMenu.addSectionHeader("Faster (shrink)");
        speedMenu.addItem(54, "1.5x");
        speedMenu.addItem(55, "2x");
        speedMenu.addItem(56, "3x");
        speedMenu.addItem(57, "4x");
        menu.addSubMenu("Speed", speedMenu);
        menu.addSeparator();
        menu.addItem(40, "Clear Slot");

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
            localAreaToGlobal(getLocalBounds()).toFloat().toNearestInt()),
            [this](int result)
        {
            if (result == 0) return;
            switch (result)
            {
                case 1:  slot_.reverse = !slot_.reverse; break;
                case 10: slot_.pitchSemitones += 1.0f; break;
                case 11: slot_.pitchSemitones -= 1.0f; break;
                case 12: slot_.pitchSemitones += 3.0f; break;
                case 13: slot_.pitchSemitones -= 3.0f; break;
                case 14: slot_.pitchSemitones = 0.0f; break;
                case 20: slot_.cellSpan = 1; break;
                case 21: slot_.cellSpan = 2; break;
                case 22: slot_.cellSpan = 3; break;
                case 23: slot_.cellSpan = 4; break;
                case 30: slot_.velocity = 1.0f; break;
                case 31: slot_.velocity = 0.75f; break;
                case 32: slot_.velocity = 0.5f; break;
                case 33: slot_.velocity = 0.25f; break;
                case 38: slot_.trimStart = 0.0f; slot_.trimEnd = 1.0f; break;
                case 50: case 51: case 52: case 53:
                case 54: case 55: case 56: case 57:
                {
                    float speeds[] = { 1.0f, 0.75f, 0.5f, 0.25f, 1.5f, 2.0f, 3.0f, 4.0f };
                    float spd = speeds[result - 50];
                    if (onSpeedChanged)
                    {
                        onSpeedChanged(slotIndex, spd, slot_.cellSpan);
                        return;
                    }
                    break;
                }
                case 40: clearSlot(); notifyModified(); return;
            }
            notifyModified();
        });
        return;
    }

    // Determine drag mode based on position and modifiers
    dragMode_ = DragMode::None;
    dragging_ = false;
    dragStartX_ = e.getScreenX();
    dragStartY_ = e.getScreenY();
    dragStartGridCell_ = slot_.gridCell;
    dragStartCellSpan_ = slot_.cellSpan;
    dragStartPitch_ = slot_.pitchSemitones;
    dragStartTrimStart_ = slot_.trimStart;
    dragStartTrimEnd_ = slot_.trimEnd;
    lastAppliedCellSteps_ = 0;

    // 4-corner zone detection: top = trim, bottom = speed-stretch
    int edgeZone = juce::jmin(kEdgeZone, getWidth() / 4);
    bool inLeftEdge = (edgeZone >= 3 && e.x <= edgeZone);
    bool inRightEdge = (edgeZone >= 3 && e.x >= getWidth() - edgeZone);
    bool inTopHalf = (e.y < getHeight() / 2);

    if (inLeftEdge)
    {
        dragMode_ = inTopHalf ? DragMode::TrimLeft : DragMode::SpeedLeft;
        dragging_ = true;
        repaint();
        return;
    }
    if (inRightEdge)
    {
        dragMode_ = inTopHalf ? DragMode::TrimRight : DragMode::SpeedRight;
        dragging_ = true;
        repaint();
        return;
    }

    // Modifier-based modes:
    // Shift+click → copy
    // Ctrl+click → pitch
    // Plain click → move
    if (e.mods.isShiftDown())
        dragMode_ = DragMode::Copy;
    else if (e.mods.isCtrlDown())
        dragMode_ = DragMode::Pitch;
    else
        dragMode_ = DragMode::Move;

    juce::Logger::writeToLog("[SlotDown] idx=" + juce::String(slotIndex)
                             + " mode=" + juce::String((int)dragMode_)
                             + " x=" + juce::String(e.x)
                             + " y=" + juce::String(e.y)
                             + " w=" + juce::String(getWidth())
                             + " h=" + juce::String(getHeight()));
}

// ── Mouse: drag ──────────────────────────────────────────────────────────

void SequencerSlotComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (slot_.empty || dragMode_ == DragMode::None) return;

    float deltaX = static_cast<float>(e.getScreenX() - dragStartX_);
    float deltaY = static_cast<float>(e.getScreenY() - dragStartY_);
    float cw = cellWidth_ > 1.0f ? cellWidth_ : 20.0f;

    juce::Logger::writeToLog("[SlotDRAG] idx=" + juce::String(slotIndex)
                             + " mode=" + juce::String((int)dragMode_)
                             + " dX=" + juce::String(deltaX));

    // ── Trim (top edges): resize box + crop audio ──
    if (dragMode_ == DragMode::TrimLeft || dragMode_ == DragMode::TrimRight)
    {
        int cellDelta = static_cast<int>(std::round(deltaX / cw));
        int newGridCell, newSpan;

        if (dragMode_ == DragMode::TrimRight)
        {
            // Trim right: can shrink AND restore (up to totalCells_ bounds)
            newGridCell = dragStartGridCell_;
            newSpan = juce::jlimit(1, totalCells_ - newGridCell, dragStartCellSpan_ + cellDelta);
        }
        else
        {
            // Trim left: can shrink AND restore
            int fixedRight = dragStartGridCell_ + dragStartCellSpan_;
            newGridCell = juce::jlimit(0, fixedRight - 1, dragStartGridCell_ + cellDelta);
            newSpan = fixedRight - newGridCell;
        }

        // Update trimStart/trimEnd to crop audio proportionally
        float origSpan = static_cast<float>(dragStartCellSpan_);
        if (dragMode_ == DragMode::TrimLeft)
            slot_.trimStart = juce::jlimit(0.0f, 0.95f,
                static_cast<float>(newGridCell - dragStartGridCell_) / origSpan);
        else
            slot_.trimEnd = juce::jlimit(slot_.trimStart + 0.05f, 1.0f,
                static_cast<float>(newSpan) / origSpan + slot_.trimStart);

        if ((newGridCell != slot_.gridCell || newSpan != slot_.cellSpan) && onSlotResized)
            onSlotResized(slotIndex, newGridCell, newSpan);

        return;
    }

    // ── Speed-stretch (bottom edges): resize cell + set speed ratio ──
    if (dragMode_ == DragMode::SpeedLeft || dragMode_ == DragMode::SpeedRight)
    {
        int cellDelta = static_cast<int>(std::round(deltaX / cw));
        int newGridCell, newSpan;

        if (dragMode_ == DragMode::SpeedRight)
        {
            // Allow growing past original — up to grid bounds
            newGridCell = dragStartGridCell_;
            newSpan = juce::jlimit(1, totalCells_ - newGridCell, dragStartCellSpan_ + cellDelta);
        }
        else
        {
            int fixedRight = dragStartGridCell_ + dragStartCellSpan_;
            newGridCell = juce::jlimit(0, fixedRight - 1, dragStartGridCell_ + cellDelta);
            newSpan = fixedRight - newGridCell;
        }

        // Speed ratio: original span / new span
        slot_.speed = static_cast<float>(dragStartCellSpan_) / static_cast<float>(juce::jmax(1, newSpan));

        if ((newGridCell != slot_.gridCell || newSpan != slot_.cellSpan) && onSlotResized)
            onSlotResized(slotIndex, newGridCell, newSpan);

        return;
    }

    // ── Pitch: Ctrl+drag up/down ──
    if (dragMode_ == DragMode::Pitch)
    {
        float pitchDelta = -deltaY / 30.0f;  // 30px = 1 semitone
        slot_.pitchSemitones = dragStartPitch_ + std::round(pitchDelta);
        slot_.pitchSemitones = juce::jlimit(-12.0f, 12.0f, slot_.pitchSemitones);
        repaint();
        return;
    }

    // ── Move: click+drag → snap to cell grid (absolute target) ──
    if (dragMode_ == DragMode::Move)
    {
        int totalCellSteps = static_cast<int>(std::round(deltaX / cw));
        int targetCell = juce::jlimit(0, totalCells_ - slot_.cellSpan,
                                       dragStartGridCell_ + totalCellSteps);

        if (targetCell != slot_.gridCell && onSlotMoved)
        {
            // Pass absolute target cell (not a delta)
            onSlotMoved(slotIndex, targetCell);
        }
        return;
    }

    // ── Copy: shift+drag → one-shot copy by cell offset ──
    if (dragMode_ == DragMode::Copy)
    {
        int totalCellSteps = static_cast<int>(std::round(deltaX / cw));
        if (totalCellSteps != 0 && onSlotCopied)
        {
            auto copySlot = slot_;
            copySlot.empty = false;
            // Reset effects on copy — start fresh
            copySlot.trimStart = 0.0f;
            copySlot.trimEnd = 1.0f;
            copySlot.speed = 1.0f;
            copySlot.gridCell = juce::jlimit(0, totalCells_ - copySlot.cellSpan,
                                              dragStartGridCell_ + totalCellSteps);
            onSlotCopied(-1, copySlot);
            dragMode_ = DragMode::None;  // One-shot
        }
        return;
    }
}

// ── Mouse: release ───────────────────────────────────────────────────────

void SequencerSlotComponent::mouseUp(const juce::MouseEvent&)
{
    // Trim and speed-stretch go through onSlotResized — sync trim values back
    if (dragMode_ == DragMode::TrimLeft || dragMode_ == DragMode::TrimRight)
        notifyModified();  // Push trimStart/trimEnd to lane
    else if (dragMode_ == DragMode::Pitch)
        notifyModified();

    dragMode_ = DragMode::None;
    dragging_ = false;
}

// ── Mouse: double-click → play ──────────────────────────────────────────

void SequencerSlotComponent::mouseDoubleClick(const juce::MouseEvent&)
{
    if (slot_.empty || !model_) return;

    // Play the trimmed/speed-adjusted portion directly
    auto* stem = model_->getStem(slot_.stemIndex);
    auto* seg = model_->getSegment(slot_.stemIndex, slot_.segmentIndex);
    if (!stem || !seg) return;

    // Realtime pitch path: read raw stem buffer; pitch is applied via
    // SoundTouch in the voice rendering loop when we pass stemIdx below.
    const auto& srcBuf = stem->audioBuffer;

    juce::Logger::writeToLog("[SequencerSlot] playSegment stem=" + juce::String(slot_.stemIndex)
        + " (realtime pitch via voice)");

    int64_t segStart = juce::jlimit((int64_t)0, (int64_t)srcBuf.getNumSamples(), (int64_t) seg->startSample);
    int64_t segEnd   = juce::jlimit(segStart,   (int64_t)srcBuf.getNumSamples(), (int64_t) seg->endSample);
    int64_t segLen = segEnd - segStart;
    if (segLen <= 0) return;

    // Apply trim
    int64_t trimStart = segStart + static_cast<int64_t>(slot_.trimStart * segLen);
    int64_t trimEnd = segStart + static_cast<int64_t>(slot_.trimEnd * segLen);
    trimStart = juce::jlimit(segStart, segEnd, trimStart);
    trimEnd = juce::jlimit(trimStart + 1, segEnd, trimEnd);
    int inLen = static_cast<int>(trimEnd - trimStart);

    int numCh = srcBuf.getNumChannels();

    // Apply speed: resample if speed != 1.0
    int outLen = inLen;
    if (std::abs(slot_.speed - 1.0f) > 0.01f && slot_.speed > 0.0f)
        outLen = static_cast<int>(inLen / slot_.speed);
    if (outLen <= 0) outLen = inLen;

    auto buf = std::make_shared<juce::AudioBuffer<float>>(numCh, outLen);
    buf->clear();

    if (outLen != inLen)
    {
        double ratio = static_cast<double>(inLen) / static_cast<double>(outLen);
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float* rd = srcBuf.getReadPointer(ch, static_cast<int>(trimStart));
            float* wr = buf->getWritePointer(ch);
            for (int j = 0; j < outLen; ++j)
            {
                double sp = j * ratio;
                int i0 = juce::jmin(static_cast<int>(sp), inLen - 1);
                int i1 = juce::jmin(i0 + 1, inLen - 1);
                float f = static_cast<float>(sp - i0);
                wr[j] = rd[i0] * (1.0f - f) + rd[i1] * f;
            }
        }
    }
    else
    {
        for (int ch = 0; ch < numCh; ++ch)
            buf->copyFrom(ch, 0, srcBuf, ch, static_cast<int>(trimStart), inLen);
    }

    // Apply reverse
    if (slot_.reverse)
        for (int ch = 0; ch < numCh; ++ch)
            std::reverse(buf->getWritePointer(ch), buf->getWritePointer(ch) + outLen);

    // Apply velocity (volume)
    if (std::abs(slot_.velocity - 1.0f) > 0.01f)
        buf->applyGain(slot_.velocity);

    // Play via MidiMappingService
    auto* reg = samplar::ServiceLocator::getRegistry();
    if (reg)
    {
        auto* midiSvc = dynamic_cast<samplar::MidiMappingService*>(reg->get("midi.mapping"));
        if (midiSvc)
        {
            midiSvc->stopAllVoices();
            midiSvc->playBufferDirect(buf, sampleRate_, 1.0f, false, slot_.stemIndex);
        }
    }

    // Trigger play animation
    setPlaying(true);
    juce::Timer::callAfterDelay(static_cast<int>(1000.0 * outLen / sampleRate_), [this]()
    {
        setPlaying(false);
    });
}

void SequencerSlotComponent::mouseExit(const juce::MouseEvent&)
{
    if (tooltipVisible_)
    {
        tooltipVisible_ = false;
        repaint();
    }
}

// ── Mouse: hover cursor + tooltip ────────────────────────────────────────

void SequencerSlotComponent::mouseMove(const juce::MouseEvent& e)
{
    if (slot_.empty)
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        tooltipVisible_ = false;
        repaint();
        return;
    }

    // Cursor — different for trim (top) vs speed-stretch (bottom)
    int edgeZone = juce::jmin(kEdgeZone, getWidth() / 4);
    bool inEdge = edgeZone >= 3 && (e.x <= edgeZone || e.x >= getWidth() - edgeZone);
    if (inEdge)
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    else
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);

    // Tooltip
    if (tooltipsEnabled_)
    {
        tooltipVisible_ = true;
        tooltipPos_ = e.getPosition();
        repaint();
    }
}

} // namespace samplar
