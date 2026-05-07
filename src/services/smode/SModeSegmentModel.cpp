#include "SModeSegmentModel.h"

namespace samplar {

// ── Stem Management ──────────────────────────────────────────────────

void SModeSegmentModel::clear()
{
    juce::Logger::writeToLog("🎵 [SModeSegmentModel] === clear ===");
    stems_.clear();
    undoStack_.clear();
    redoStack_.clear();
    detectedBpm_ = 0.0;
    notifyUndoStackChanged();
}

void SModeSegmentModel::setStemCount(int count)
{
    juce::Logger::writeToLog("🎵 [SModeSegmentModel] setStemCount: " + juce::String(count));
    stems_.resize(static_cast<size_t>(count));
}

StemData* SModeSegmentModel::getStem(int index)
{
    if (index < 0 || index >= static_cast<int>(stems_.size())) return nullptr;
    return &stems_[static_cast<size_t>(index)];
}

const StemData* SModeSegmentModel::getStem(int index) const
{
    if (index < 0 || index >= static_cast<int>(stems_.size())) return nullptr;
    return &stems_[static_cast<size_t>(index)];
}

// ── Segment CRUD ─────────────────────────────────────────────────────

int SModeSegmentModel::addSegment(int stemIndex, int64_t startSample, int64_t endSample)
{
    auto* stem = getStem(stemIndex);
    if (!stem || startSample >= endSample)
    {
        juce::Logger::writeToLog("❌ [SModeSegmentModel] addSegment failed: invalid params");
        return -1;
    }

    Segment seg;
    seg.startSample = startSample;
    seg.endSample = endSample;
    stem->segments.push_back(seg);

    int idx = static_cast<int>(stem->segments.size()) - 1;
    juce::Logger::writeToLog("🎵 [SModeSegmentModel] addSegment stem=" + juce::String(stemIndex)
        + " idx=" + juce::String(idx)
        + " range=[" + juce::String(startSample) + "," + juce::String(endSample) + "]");
    return idx;
}

void SModeSegmentModel::removeSegment(int stemIndex, int segmentIndex)
{
    auto* stem = getStem(stemIndex);
    if (!stem || segmentIndex < 0 || segmentIndex >= static_cast<int>(stem->segments.size()))
        return;

    juce::Logger::writeToLog("🎵 [SModeSegmentModel] removeSegment stem=" + juce::String(stemIndex)
        + " seg=" + juce::String(segmentIndex));
    stem->segments.erase(stem->segments.begin() + segmentIndex);
}

void SModeSegmentModel::resizeSegment(int stemIndex, int segmentIndex, int64_t newStart, int64_t newEnd)
{
    auto* seg = getSegment(stemIndex, segmentIndex);
    if (!seg || newStart >= newEnd) return;

    seg->startSample = newStart;
    seg->endSample = newEnd;
}

void SModeSegmentModel::clearSegments(int stemIndex)
{
    auto* stem = getStem(stemIndex);
    if (!stem) return;

    juce::Logger::writeToLog("🎵 [SModeSegmentModel] clearSegments stem=" + juce::String(stemIndex));
    stem->segments.clear();
}

Segment* SModeSegmentModel::getSegment(int stemIndex, int segmentIndex)
{
    auto* stem = getStem(stemIndex);
    if (!stem || segmentIndex < 0 || segmentIndex >= static_cast<int>(stem->segments.size()))
        return nullptr;
    return &stem->segments[static_cast<size_t>(segmentIndex)];
}

const Segment* SModeSegmentModel::getSegment(int stemIndex, int segmentIndex) const
{
    auto* stem = getStem(stemIndex);
    if (!stem || segmentIndex < 0 || segmentIndex >= static_cast<int>(stem->segments.size()))
        return nullptr;
    return &stem->segments[static_cast<size_t>(segmentIndex)];
}

// ── MIDI Assignment ──────────────────────────────────────────────────

void SModeSegmentModel::quickAssignMidiNotes(int startNote)
{
    juce::Logger::writeToLog("🎵 [SModeSegmentModel] === quickAssignMidiNotes startNote=" + juce::String(startNote) + " ===");
    int note = startNote;
    for (auto& stem : stems_)
    {
        for (auto& seg : stem.segments)
        {
            seg.midiNote = note++;
            if (note > 127) note = 127; // MIDI ceiling
        }
    }
    juce::Logger::writeToLog("✅ [SModeSegmentModel] Assigned " + juce::String(note - startNote) + " MIDI notes");
}

void SModeSegmentModel::assignMidiNote(int stemIndex, int segmentIndex, int midiNote)
{
    auto* seg = getSegment(stemIndex, segmentIndex);
    if (!seg) return;

    juce::Logger::writeToLog("🎵 [SModeSegmentModel] assignMidiNote stem=" + juce::String(stemIndex)
        + " seg=" + juce::String(segmentIndex) + " note=" + juce::String(midiNote));
    seg->midiNote = midiNote;
}

std::pair<int, int> SModeSegmentModel::findSegmentByMidiNote(int midiNote) const
{
    for (int s = 0; s < static_cast<int>(stems_.size()); ++s)
    {
        const auto& segs = stems_[static_cast<size_t>(s)].segments;
        for (int i = 0; i < static_cast<int>(segs.size()); ++i)
        {
            if (segs[static_cast<size_t>(i)].midiNote == midiNote)
                return { s, i };
        }
    }
    return { -1, -1 };
}

std::vector<std::pair<int, int>> SModeSegmentModel::findAllSegmentsByMidiNote(int midiNote) const
{
    std::vector<std::pair<int, int>> results;
    for (int s = 0; s < static_cast<int>(stems_.size()); ++s)
    {
        const auto& segs = stems_[static_cast<size_t>(s)].segments;
        for (int i = 0; i < static_cast<int>(segs.size()); ++i)
        {
            if (segs[static_cast<size_t>(i)].midiNote == midiNote)
                results.push_back({ s, i });
        }
    }
    return results;
}

void SModeSegmentModel::clearMidiNote(int stemIndex, int segmentIndex)
{
    auto* seg = getSegment(stemIndex, segmentIndex);
    if (seg) seg->midiNote = -1;
}

// ── Effects ──────────────────────────────────────────────────────────

void SModeSegmentModel::setSegmentEffects(int stemIndex, int segmentIndex, const SegmentEffects& fx)
{
    auto* seg = getSegment(stemIndex, segmentIndex);
    if (seg) seg->effects = fx;
}

SegmentEffects SModeSegmentModel::getSegmentEffects(int stemIndex, int segmentIndex) const
{
    auto* seg = getSegment(stemIndex, segmentIndex);
    return seg ? seg->effects : SegmentEffects{};
}

// ── Stem Linking ─────────────────────────────────────────────────────

void SModeSegmentModel::setLinked(int stemIndex, bool linked)
{
    auto* stem = getStem(stemIndex);
    if (stem) stem->linked = linked;
}

bool SModeSegmentModel::isLinked(int stemIndex) const
{
    auto* stem = getStem(stemIndex);
    return stem ? stem->linked : false;
}

std::vector<int> SModeSegmentModel::getLinkedStems() const
{
    std::vector<int> result;
    for (int i = 0; i < static_cast<int>(stems_.size()); ++i)
    {
        if (stems_[static_cast<size_t>(i)].linked)
            result.push_back(i);
    }
    return result;
}

// ── Solo / Mute ──────────────────────────────────────────────────────

void SModeSegmentModel::setSolo(int stemIndex, bool solo)
{
    auto* stem = getStem(stemIndex);
    if (stem) stem->solo = solo;
}

void SModeSegmentModel::setMute(int stemIndex, bool mute)
{
    auto* stem = getStem(stemIndex);
    if (stem) stem->mute = mute;
}

void SModeSegmentModel::setStemVolume(int stemIndex, float volume)
{
    auto* stem = getStem(stemIndex);
    if (stem) stem->volume = juce::jlimit(0.0f, 1.0f, volume);
}

bool SModeSegmentModel::isStemAudible(int stemIndex) const
{
    auto* stem = getStem(stemIndex);
    if (!stem || stem->mute) return false;

    // If ANY stem is soloed, only soloed stems are audible
    bool anySoloed = false;
    for (const auto& s : stems_)
        if (s.solo) { anySoloed = true; break; }

    if (anySoloed && !stem->solo) return false;
    return true;
}

// ── Undo / Redo ──────────────────────────────────────────────────────

void SModeSegmentModel::pushUndoState(const juce::String& label)
{
    if (restoringSnapshot_) return;   // Re-entrancy guard

    undoStack_.push_back(captureSnapshot(label));
    if (static_cast<int>(undoStack_.size()) > kMaxUndoLevels)
        undoStack_.erase(undoStack_.begin());
    redoStack_.clear(); // New action invalidates redo

    juce::Logger::writeToLog("[SMode-Undo] push: \"" + label
        + "\"  (undo=" + juce::String((int) undoStack_.size())
        + " redo=" + juce::String((int) redoStack_.size()) + ")");

    notifyUndoStackChanged();
}

bool SModeSegmentModel::undo()
{
    if (undoStack_.empty())
    {
        juce::Logger::writeToLog("[SMode-Undo] undo requested but stack empty");
        return false;
    }

    // Capture current state INTO redo so the user can walk back forward.
    // Use the undo-top's label so the redo tooltip reads "Redo: <label>".
    const juce::String label = undoStack_.back().label;
    redoStack_.push_back(captureSnapshot(label));

    restoringSnapshot_ = true;
    restoreSnapshot(undoStack_.back());
    restoringSnapshot_ = false;

    undoStack_.pop_back();

    juce::Logger::writeToLog("[SMode-Undo] undo applied: \"" + label
        + "\"  (undo=" + juce::String((int) undoStack_.size())
        + " redo=" + juce::String((int) redoStack_.size()) + ")");

    notifyUndoStackChanged();
    return true;
}

bool SModeSegmentModel::redo()
{
    if (redoStack_.empty())
    {
        juce::Logger::writeToLog("[SMode-Undo] redo requested but stack empty");
        return false;
    }

    const juce::String label = redoStack_.back().label;
    undoStack_.push_back(captureSnapshot(label));

    restoringSnapshot_ = true;
    restoreSnapshot(redoStack_.back());
    restoringSnapshot_ = false;

    redoStack_.pop_back();

    juce::Logger::writeToLog("[SMode-Undo] redo applied: \"" + label
        + "\"  (undo=" + juce::String((int) undoStack_.size())
        + " redo=" + juce::String((int) redoStack_.size()) + ")");

    notifyUndoStackChanged();
    return true;
}

void SModeSegmentModel::clearUndoHistory()
{
    undoStack_.clear();
    redoStack_.clear();
    juce::Logger::writeToLog("[SMode-Undo] history cleared");
    notifyUndoStackChanged();
}

void SModeSegmentModel::notifyUndoStackChanged()
{
    if (onUndoStackChanged)
        onUndoStackChanged();
}

SModeSegmentModel::SegmentSnapshot SModeSegmentModel::captureSnapshot(const juce::String& label) const
{
    SegmentSnapshot snap;
    snap.label       = label;
    snap.detectedBpm = detectedBpm_;
    snap.sampleRate  = sampleRate_;
    for (const auto& stem : stems_)
    {
        SegmentSnapshot::StemSnapshot ss;
        ss.segments = stem.segments;   // segments carry their own SegmentEffects — FX captured here
        ss.linked = stem.linked;
        ss.solo   = stem.solo;
        ss.mute   = stem.mute;
        ss.volume = stem.volume;
        snap.stems.push_back(std::move(ss));
    }
    if (extraGet_)
        snap.extra = extraGet_();
    return snap;
}

void SModeSegmentModel::restoreSnapshot(const SegmentSnapshot& snap)
{
    for (size_t i = 0; i < snap.stems.size() && i < stems_.size(); ++i)
    {
        stems_[i].segments = snap.stems[i].segments;
        stems_[i].linked = snap.stems[i].linked;
        stems_[i].solo   = snap.stems[i].solo;
        stems_[i].mute   = snap.stems[i].mute;
        stems_[i].volume = snap.stems[i].volume;
    }
    detectedBpm_ = snap.detectedBpm;
    sampleRate_  = snap.sampleRate;
    if (extraSet_ && !snap.extra.isVoid())
        extraSet_(snap.extra);
}

} // namespace samplar
