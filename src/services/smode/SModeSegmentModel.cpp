#include "SModeSegmentModel.h"
#include <cstring>

namespace samplar {

// ── Stem Management ──────────────────────────────────────────────────

void SModeSegmentModel::clear()
{
    ++contentGeneration_;
    juce::Logger::writeToLog("🎵 [SModeSegmentModel] === clear === generation="
                             + juce::String(static_cast<juce::int64>(contentGeneration_)));
    stems_.clear();
    undoStack_.clear();
    redoStack_.clear();
    detectedBpm_ = 0.0;
    gridOffsetSeconds_ = 0.0;
    pendingExtra_ = juce::var();
    notifyUndoStackChanged();
}

std::shared_ptr<void> SModeSegmentModel::takeContentForRelease()
{
    struct Content
    {
        std::vector<StemData> stems;
        std::vector<SegmentSnapshot> undo, redo;
    };
    auto c = std::make_shared<Content>();
    c->stems = std::move(stems_);
    c->undo  = std::move(undoStack_);
    c->redo  = std::move(redoStack_);
    clear();
    return c;
}

void SModeSegmentModel::setExtraStateHooks(ExtraStateProvider get, ExtraStateRestorer set)
{
    extraGet_ = std::move(get);
    extraSet_ = std::move(set);
    if (extraSet_ && hasPendingExtraState())
    {
        const auto v = pendingExtra_;
        pendingExtra_ = juce::var();
        juce::Logger::writeToLog("[SModeSegmentModel] setExtraStateHooks: handing restored project UI state to the page");
        const bool wasRestoring = restoringSnapshot_;
        restoringSnapshot_ = true;   // the page's restore must not push undo entries
        extraSet_(v);
        restoringSnapshot_ = wasRestoring;
    }
}

uint64_t SModeSegmentModel::computeSessionHash() const
{
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&h](uint64_t v)
    {
        for (int i = 0; i < 8; ++i) { h ^= (v >> (i * 8)) & 0xFFu; h *= 1099511628211ULL; }
    };
    auto mixD = [&mix](double d) { uint64_t u; std::memcpy(&u, &d, sizeof(u)); mix(u); };
    auto mixF = [&mix](float f) { uint32_t u; std::memcpy(&u, &f, sizeof(u)); mix(u); };

    mix(contentGeneration_);
    mixD(sampleRate_);
    mixD(detectedBpm_);
    mixD(gridOffsetSeconds_);
    mix((uint64_t) stems_.size());
    for (const auto& st : stems_)
    {
        mix((uint64_t) st.audioBuffer.getNumSamples());
        mix((uint64_t) (st.linked ? 1 : 0) | (uint64_t) (st.solo ? 2 : 0) | (uint64_t) (st.mute ? 4 : 0));
        mixF(st.volume);
        mix((uint64_t) st.segments.size());
        for (const auto& seg : st.segments)
        {
            mix((uint64_t) seg.startSample);
            mix((uint64_t) seg.endSample);
            mix((uint64_t) (int64_t) seg.midiNote);
            const auto& fx = seg.effects;
            mix((uint64_t) (int64_t) fx.pitchSemitones);
            mixF(fx.volume);
            mixF(fx.pan);
            mixF(fx.filterCutoff);
            mix((uint64_t) (fx.filterIsHighPass ? 1 : 0) | (uint64_t) (fx.reverse ? 2 : 0) | (uint64_t) (fx.loop ? 4 : 0));
            mixF(fx.attackMs);
            mixF(fx.releaseMs);
            mix((uint64_t) (int64_t) fx.triggerMode);
        }
    }
    return h;
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

// ── Realtime engine snapshot ─────────────────────────────────────────

void SModeSegmentModel::setStemAudio(int stemIndex, juce::AudioBuffer<float>&& audio)
{
    auto* stem = getStem(stemIndex);
    if (stem == nullptr) return;

    auto storage = std::make_shared<juce::AudioBuffer<float>>(std::move(audio));
    stem->audioStorage = storage;
    stem->audioBuffer.setDataToReferTo(storage->getArrayOfWritePointers(),
                                       storage->getNumChannels(), storage->getNumSamples());
    juce::Logger::writeToLog("[SModeSegmentModel] setStemAudio stem=" + juce::String(stemIndex)
        + " samples=" + juce::String(storage->getNumSamples())
        + " ch=" + juce::String(storage->getNumChannels()) + " (shared storage)");
}

std::shared_ptr<const juce::AudioBuffer<float>> SModeSegmentModel::getSharedStemAudio(int stemIndex)
{
    auto* stem = getStem(stemIndex);
    if (stem == nullptr) return nullptr;
    auto& ab = stem->audioBuffer;
    if (ab.getNumChannels() <= 0 || ab.getNumSamples() <= 0) return nullptr;

    const auto& st = stem->audioStorage;
    if (st != nullptr && st->getNumChannels() == ab.getNumChannels()
        && st->getNumSamples() == ab.getNumSamples()
        && st->getReadPointer(0) == ab.getReadPointer(0))
        return st;

    std::shared_ptr<juce::AudioBuffer<float>> storage;
    if (st == nullptr)
    {
        // audioBuffer owns its samples (written directly): adopt them, no copy.
        storage = std::make_shared<juce::AudioBuffer<float>>(std::move(ab));
        juce::Logger::writeToLog("[SModeSegmentModel] getSharedStemAudio stem=" + juce::String(stemIndex)
                                 + ": adopted directly-written buffer");
    }
    else
    {
        // audioBuffer no longer refers to the shared storage (replaced or copied): copy once.
        storage = std::make_shared<juce::AudioBuffer<float>>(ab);
        juce::Logger::writeToLog("[SModeSegmentModel] getSharedStemAudio stem=" + juce::String(stemIndex)
                                 + ": buffer diverged from storage, copied once");
    }
    stem->audioStorage = storage;
    ab.setDataToReferTo(storage->getArrayOfWritePointers(), storage->getNumChannels(), storage->getNumSamples());
    return storage;
}

uint64_t SModeSegmentModel::computePlaybackHash() const
{
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&h](uint64_t v)
    {
        for (int i = 0; i < 8; ++i) { h ^= (v >> (i * 8)) & 0xFFu; h *= 1099511628211ULL; }
    };
    auto mixD = [&mix](double d) { uint64_t u; std::memcpy(&u, &d, sizeof(u)); mix(u); };
    auto mixF = [&mix](float f) { uint32_t u; std::memcpy(&u, &f, sizeof(u)); mix(u); };

    mix(contentGeneration_);
    mixD(sampleRate_);
    mix((uint64_t) stems_.size());
    for (const auto& st : stems_)
    {
        const auto& ab = st.audioBuffer;
        mix((uint64_t) (uintptr_t) (ab.getNumChannels() > 0 ? ab.getReadPointer(0) : nullptr));
        mix((uint64_t) ab.getNumSamples());
        mix((uint64_t) ab.getNumChannels());
        mix((uint64_t) (st.mute ? 1 : 0) | (uint64_t) (st.solo ? 2 : 0));
        mixF(st.volume);
        mix((uint64_t) st.segments.size());
        for (const auto& seg : st.segments)
        {
            mix((uint64_t) seg.startSample);
            mix((uint64_t) seg.endSample);
            mix((uint64_t) (int64_t) seg.midiNote);
            mix((uint64_t) (seg.effects.loop ? 1 : 0));
        }
    }
    return h;
}

std::shared_ptr<engine::Snapshot> SModeSegmentModel::buildPlaybackSnapshot(
    const std::vector<engine::LaneDesc>& lanes,
    const std::array<engine::StemOverride, engine::kMaxStems>* overrides)
{
    auto snap = std::make_shared<engine::Snapshot>();
    snap->modelRate = sampleRate_ > 0.0 ? sampleRate_ : 44100.0;
    snap->stemCount = juce::jmin((int) stems_.size(), engine::kMaxStems);

    for (int s = 0; s < snap->stemCount; ++s)
    {
        auto& stem = stems_[(size_t) s];
        auto& out  = snap->stems[(size_t) s];

        std::shared_ptr<const juce::AudioBuffer<float>> audio = getSharedStemAudio(s);
        // N-11: identity of the SOURCE (song generation + model buffer), kept
        // when a host-rate resampled override replaces the read pointers.
        out.sourceId = ((contentGeneration_ * 0x9E3779B97F4A7C15ULL)
                        ^ (uint64_t) (uintptr_t) audio.get()) | 1u;
        double rate = snap->modelRate;
        if (overrides != nullptr)
        {
            const auto& ov = (*overrides)[(size_t) s];
            if (ov.audio != nullptr && ov.rate > 0.0) { audio = ov.audio; rate = ov.rate; }
        }

        out.audio   = audio;
        out.rate    = rate;
        out.volume  = stem.volume;
        out.audible = isStemAudible(s);
        if (audio != nullptr && audio->getNumChannels() > 0 && audio->getNumSamples() > 0)
        {
            out.ch[0] = audio->getReadPointer(0);
            out.ch[1] = audio->getNumChannels() > 1 ? audio->getReadPointer(1) : out.ch[0];
            out.numSamples = audio->getNumSamples();
        }
        snap->transportLengthModel = juce::jmax(snap->transportLengthModel,
                                                (int64_t) stem.audioBuffer.getNumSamples());

        const double scale = rate / snap->modelRate;
        snap->stemSegBase[(size_t) s] = (int) snap->segs.size();
        for (int i = 0; i < (int) stem.segments.size(); ++i)
        {
            const auto& seg = stem.segments[(size_t) i];
            engine::SegSnap ss;
            ss.stem     = s;
            ss.segIndex = i;
            ss.start    = juce::jlimit((int64_t) 0, (int64_t) out.numSamples, (int64_t) std::llround((double) seg.startSample * scale));
            ss.end      = juce::jlimit(ss.start,    (int64_t) out.numSamples, (int64_t) std::llround((double) seg.endSample * scale));
            ss.loop     = seg.effects.loop;
            ss.midiNote = seg.midiNote;
            const int idx = (int) snap->segs.size();
            snap->segs.push_back(ss);
            if (ss.end > ss.start)
            {
                if (seg.midiNote >= 0 && seg.midiNote < 128)
                    snap->noteMap[(size_t) seg.midiNote].push_back(idx);
                snap->maxSegmentSeconds = juce::jmax(snap->maxSegmentSeconds, (double) (ss.end - ss.start) / rate);
            }
        }
    }
    for (int s = snap->stemCount; s <= engine::kMaxStems; ++s)
        snap->stemSegBase[(size_t) s] = (int) snap->segs.size();

    // Sequencer lanes -> trimmed windows in stem-buffer samples.
    for (const auto& ld : lanes)
    {
        engine::LaneSnap ls;
        ls.numCells = juce::jmax(1, ld.numCells);
        ls.muted    = ld.muted;
        ls.cellStart.assign((size_t) ls.numCells, -1);
        ls.cellCover.assign((size_t) ls.numCells, -1);
        for (int i = 0; i < (int) ld.slots.size(); ++i)
        {
            const auto& sd = ld.slots[(size_t) i];
            if (sd.empty || sd.stem < 0 || sd.stem >= snap->stemCount) continue;
            const int base = snap->stemSegBase[(size_t) sd.stem];
            const int next = snap->stemSegBase[(size_t) sd.stem + 1];
            if (sd.segIndex < 0 || base + sd.segIndex >= next) continue;
            const auto& seg = snap->segs[(size_t) (base + sd.segIndex)];
            const int64_t len = seg.end - seg.start;
            if (len <= 0) continue;

            engine::SlotSnap ss;
            ss.stem      = sd.stem;
            ss.segIndex  = sd.segIndex;
            ss.slotIndex = i;
            int64_t ts = seg.start + (int64_t) (sd.trimStart * (double) len);
            int64_t te = seg.start + (int64_t) (sd.trimEnd * (double) len);
            ts = juce::jlimit(seg.start, seg.end, ts);
            te = juce::jlimit(juce::jmin(ts + 1, seg.end), seg.end, te);
            if (te <= ts) continue;
            ss.start      = ts;
            ss.end        = te;
            ss.gridCell   = juce::jlimit(0, ls.numCells - 1, sd.gridCell);
            ss.cellSpan   = juce::jmax(1, sd.cellSpan);
            ss.velocity   = sd.velocity;
            ss.pitchSemis = sd.pitchSemis;
            ss.speed      = sd.speed > 0.0f ? sd.speed : 1.0f;
            ss.reverse    = sd.reverse;

            const int slotIdx = (int) ls.slots.size();
            ls.slots.push_back(ss);
            if (ls.cellStart[(size_t) ss.gridCell] < 0)
                ls.cellStart[(size_t) ss.gridCell] = slotIdx;
            for (int c = ss.gridCell; c < juce::jmin(ls.numCells, ss.gridCell + ss.cellSpan); ++c)
                ls.cellCover[(size_t) c] = slotIdx;
        }
        snap->lanes.push_back(std::move(ls));
    }
    return snap;
}

} // namespace samplar
