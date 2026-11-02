#include "PlaybackEngine.h"
#include "../MicroFade.h"

// signalsmith-stretch (MIT, header-only). Only this translation unit sees it.
#include "signalsmith-stretch.h"

#include <algorithm>
#include <cmath>

namespace samplar {
namespace engine {

struct StretchHolder
{
    // Fixed seed: renders are deterministic (offline export == realtime).
    signalsmith::stretch::SignalsmithStretch<float> s { 0x5A3B1EL };
};

namespace
{
    constexpr float kPitchEpsilon = 1.0e-3f;
    constexpr int   kTicksPerBeat = 4;   // sequencer clock resolution: 16th notes (B18)

    inline float hermite(float xm1, float x0, float x1, float x2, float t) noexcept
    {
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    template <typename Fn>
    inline void forEachStemForChannel(int channel, Fn&& fn) noexcept
    {
        // Channel 1 -> stem 0, channel 2 -> stem 1, ... (existing standalone
        // convention). Channel <= 0 = omni.
        if (channel <= 0) { for (int s = 0; s < kMaxStems; ++s) fn(s); return; }
        const int s = channel - 1;
        if (s >= 0 && s < kMaxStems) fn(s);
    }
}

// ============================================================================
// Lifecycle
// ============================================================================

PlaybackEngine::PlaybackEngine(const EngineParams& params)
    : params_(params)
{
    laneVoice_.fill(-1);
    laneVoiceOrder_.fill(0);
    for (int l = 0; l < kMaxLanes; ++l)
    {
        laneSlotUi_[(size_t) l].store(-1);
        laneProgUi_[(size_t) l].store(0.0f);
        laneHeadUi_[(size_t) l].store(-1.0f);
    }
}

PlaybackEngine::~PlaybackEngine() = default;

void PlaybackEngine::prepare(double sampleRate, int maxBlockSize)
{
    prepared_.store(false, std::memory_order_release);

    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    maxBlock_   = juce::jmax(16, maxBlockSize);

    if ((int) stretchers_.size() != kTotalVoices)
    {
        stretchers_.clear();
        stretchers_.reserve((size_t) kTotalVoices);
        for (int i = 0; i < kTotalVoices; ++i)
            stretchers_.push_back(std::make_unique<StretchHolder>());
    }
    for (auto& h : stretchers_)
        h->s.presetDefault(2, (float) sampleRate_, /*splitComputation*/ true);

    seekLen_    = juce::jmax(1, stretchers_.front()->s.outputSeekLength(1.0f));
    releaseLen_ = juce::jmax(1, (int) std::lround(kReleaseFadeMs * 0.001 * sampleRate_));
    xfadeLen_   = juce::jmax(1, (int) std::lround(kLoopCrossfadeMs * 0.001 * sampleRate_));
    killFadeLen_ = juce::jmax(1, (int) std::lround(kKillFadeMs * 0.001 * sampleRate_));

    scratchIn_  .setSize(2, maxBlock_, false, true, false);
    scratchOut_ .setSize(2, maxBlock_, false, true, false);
    scratchX_   .setSize(2, maxBlock_, false, true, false);
    scratchSeek_.setSize(2, seekLen_,  false, true, false);

    for (auto& v : voices_) v = Voice {};
    laneVoice_.fill(-1);
    laneVoiceOrder_.fill(0);
    for (auto& m : mod_) m = ModState {};
    transportActive_ = false;
    transportPosModel_ = 0.0;
    seqRunning_ = false;
    seqHostDriven_ = false;
    bypassed_ = false;
    declickL_ = declickR_ = declickStepL_ = declickStepR_ = 0.0f;
    declickRemain_ = 0;
    lastBlockMs_ = 0.0;
    lastProcessMs_ = 0;
    minVoiceSeq_.store(UINT64_MAX, std::memory_order_seq_cst);
    transportActiveUi_.store(false);
    seqRunningUi_.store(false);
    seqHostUi_.store(false);
    // N-12: transport + sequencer were reset without a notification; the UI
    // timer sees the new generation and clears its "playing" flags.
    lifecycleGen_.fetch_add(1, std::memory_order_acq_rel);

    prepared_.store(true, std::memory_order_release);
}

std::shared_ptr<void> PlaybackEngine::detachStretchersForRelease()
{
    prepared_.store(false, std::memory_order_release);
    auto held = std::make_shared<std::vector<std::unique_ptr<StretchHolder>>>(std::move(stretchers_));
    stretchers_.clear();
    return held;
}

void PlaybackEngine::release()
{
    prepared_.store(false, std::memory_order_release);
    for (auto& v : voices_) v = Voice {};
    laneVoice_.fill(-1);
    transportActive_ = false;
    seqRunning_ = false;
    minVoiceSeq_.store(UINT64_MAX, std::memory_order_seq_cst);
    declickRemain_ = 0;
    declickL_ = declickR_ = 0.0f;
    transportActiveUi_.store(false);
    seqRunningUi_.store(false);
    seqHostUi_.store(false);
    activeVoicesUi_.store(0);
    lifecycleGen_.fetch_add(1, std::memory_order_acq_rel);   // N-12
}

uint64_t PlaybackEngine::getReclaimBound() const noexcept
{
    // Dekker-style handshake with process(): it stores inProcess_ = true and
    // then loads published_. Either we see it busy (use its ack, which can
    // only be older than what it holds), or it will load the snapshot that
    // was published before this call.
    const bool busy = inProcess_.load(std::memory_order_seq_cst);
    uint64_t bound  = minVoiceSeq_.load(std::memory_order_seq_cst);
    if (busy)
        bound = std::min(bound, ackSeq_.load(std::memory_order_seq_cst));
    return bound;
}

void PlaybackEngine::getLaneState(int lane, int& playingSlot, float& slotProgress, float& playhead) const noexcept
{
    if (lane < 0 || lane >= kMaxLanes) { playingSlot = -1; slotProgress = 0.0f; playhead = -1.0f; return; }
    playingSlot  = laneSlotUi_[(size_t) lane].load(std::memory_order_relaxed);
    slotProgress = laneProgUi_[(size_t) lane].load(std::memory_order_relaxed);
    playhead     = laneHeadUi_[(size_t) lane].load(std::memory_order_relaxed);
}

bool PlaybackEngine::hasActiveVoices() const noexcept
{
    for (const auto& v : voices_)
        if (v.active) return true;
    return false;
}

// ============================================================================
// Audio thread entry points
// ============================================================================

void PlaybackEngine::reloadSnapshot() noexcept
{
    cur_ = published_.load(std::memory_order_seq_cst);
    ackSeq_.store(cur_ != nullptr ? cur_->seq : 0, std::memory_order_seq_cst);
}

void PlaybackEngine::switchSnapshotMidBlock() noexcept
{
    // ackSeq_ keeps the seq loaded at the start of this block: it is <= the seq
    // of every voice started during the block, so getReclaimBound() (while
    // inProcess_) can never free a snapshot such a voice reads. The voices'
    // own seqs are published through minVoiceSeq_ at the end of the block.
    cur_ = published_.load(std::memory_order_seq_cst);
}

void PlaybackEngine::process(juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                             const juce::MidiBuffer* midi, const HostInfo& hostIn) noexcept
{
    if (! prepared_.load(std::memory_order_acquire) || numSamples <= 0 || buffer.getNumChannels() <= 0)
        return;

    inProcess_.store(true, std::memory_order_seq_cst);
    reloadSnapshot();
    bypassed_ = false;
    stretchEntriesThisBlock_ = 0;

    // N-4: defensive copy of the host values (the processor validates too).
    HostInfo host = hostIn;
    if (sanitizeHostInfo(host))
        hostRejected_.fetch_add(1, std::memory_order_relaxed);

    // N-13: a command waits for the next block start, i.e. up to one block
    // period; the stale threshold scales with the block duration.
    const double blockMs = 1000.0 * numSamples / sampleRate_;
    {
        const double thr = juce::jmax((double) kStaleCommandMs, 3.0 * juce::jmax(blockMs, lastBlockMs_));
        staleThresholdMs_.store((uint32_t) juce::jmin(60000.0, thr), std::memory_order_relaxed);
    }
    lastBlockMs_ = blockMs;

    if (stallRecovery_)
    {
        const uint32_t nowMs = juce::Time::getMillisecondCounter();
        // N-24: an offline bounce may legitimately take longer than real time
        // per block - held notes must not be cut there.
        if (! host.offline && lastProcessMs_ != 0 && nowMs - lastProcessMs_ > 500)
        {
            // The host stopped calling us (smart-disable / suspended track):
            // do not resume one-shots and audition voices from where they froze.
            releasePool(PoolMidi);
        }
        lastProcessMs_ = nowMs;
    }

    drainCommands();
    drainMidiIn();

    float* outL = buffer.getWritePointer(0, startSample);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1, startSample) : nullptr;

    // Transport voices follow the live loop switch and mute / solo.
    if (transportActive_)
    {
        const bool loop = params_.transportLoop.load(std::memory_order_relaxed);
        for (int i = kTransportBase; i < kTotalVoices; ++i)
        {
            auto& v = voices_[(size_t) i];
            if (! v.active) continue;
            v.loop = loop;
            if (cur_ != nullptr && v.stem >= 0 && v.stem < cur_->stemCount)
            {
                const auto& st = cur_->stems[(size_t) v.stem];
                // N-11: same SOURCE, even when the newer snapshot reads a host-rate
                // resampled copy (different pointer) of it.
                const bool sameSource = v.src != nullptr
                    && ((st.sourceId != 0 && st.sourceId == v.src->sourceId) || st.ch[0] == v.src->ch[0]);
                if (sameSource)
                    v.tgainTarget = st.audible ? 1.0f : 0.0f;
            }
        }
    }

    prepareSequencerEvents(numSamples, host);

    // Sample-accurate event loop: render up to each MIDI / sequencer event,
    // apply it, continue (A10).
    int pos = 0;
    int se  = 0;
    if (midi != nullptr)
    {
        for (const auto meta : *midi)
        {
            const int evPos = juce::jlimit(0, numSamples - 1, meta.samplePosition);
            while (se < seqEventCount_ && seqEvents_[(size_t) se].offset < evPos)
            {
                const auto& e = seqEvents_[(size_t) se++];
                if (e.offset > pos)
                {
                    renderRange(outL + pos, outR != nullptr ? outR + pos : nullptr, e.offset - pos);
                    pos = e.offset;
                }
                triggerSlot(e.lane, e.slot);
            }
            if (evPos > pos)
            {
                renderRange(outL + pos, outR != nullptr ? outR + pos : nullptr, evPos - pos);
                pos = evPos;
            }
            if (meta.numBytes >= 1 && meta.numBytes <= 3)
                handleRawMidi(meta.data[0],
                              meta.numBytes > 1 ? meta.data[1] : (uint8_t) 0,
                              meta.numBytes > 2 ? meta.data[2] : (uint8_t) 0);
        }
    }
    while (se < seqEventCount_)
    {
        const auto& e = seqEvents_[(size_t) se++];
        if (e.offset > pos)
        {
            renderRange(outL + pos, outR != nullptr ? outR + pos : nullptr, e.offset - pos);
            pos = e.offset;
        }
        triggerSlot(e.lane, e.slot);
    }
    if (numSamples > pos)
        renderRange(outL + pos, outR != nullptr ? outR + pos : nullptr, numSamples - pos);

    // -- clocks ---------------------------------------------------------------
    if (seqRunning_ && ! seqHostDriven_)
        seqSamples_ += numSamples;
    publishSequencerUi(numSamples);

    if (transportActive_)
    {
        const double modelRate = cur_ != nullptr ? cur_->modelRate : 44100.0;
        const double lenModel  = cur_ != nullptr ? (double) cur_->transportLengthModel : 0.0;
        transportPosModel_ += numSamples * modelRate / sampleRate_;
        if (lenModel > 0.0 && transportPosModel_ >= lenModel)
            transportPosModel_ = params_.transportLoop.load(std::memory_order_relaxed)
                                     ? std::fmod(transportPosModel_, lenModel) : lenModel;

        bool anyTransportVoice = false;
        for (int i = kTransportBase; i < kTotalVoices; ++i)
            if (voices_[(size_t) i].active && ! voices_[(size_t) i].releasing) { anyTransportVoice = true; break; }
        if (! anyTransportVoice)
            stopTransport(true);

        if (modelRate > 0.0)
            transportSecUi_.store(transportPosModel_ / modelRate, std::memory_order_relaxed);
    }
    transportActiveUi_.store(transportActive_, std::memory_order_relaxed);

    uint64_t minSeq = UINT64_MAX;
    int active = 0;
    for (const auto& v : voices_)
        if (v.active) { ++active; minSeq = std::min(minSeq, v.snapSeq); }
    activeVoicesUi_.store(active, std::memory_order_relaxed);
    minVoiceSeq_.store(minSeq, std::memory_order_seq_cst);
    inProcess_.store(false, std::memory_order_seq_cst);
}

void PlaybackEngine::processBypassed(juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept
{
    if (numSamples <= 0 || buffer.getNumChannels() <= 0)
        return;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.clear(ch, startSample, numSamples);   // instrument: never pass host input through
    if (! prepared_.load(std::memory_order_acquire))
        return;

    inProcess_.store(true, std::memory_order_seq_cst);
    reloadSnapshot();
    stretchEntriesThisBlock_ = 0;
    const bool entering = ! bypassed_;
    // N-18: while bypassed, triggers drained here are ignored (they would all
    // start at once on un-bypass); note-offs and stops still apply.
    bypassed_ = true;
    lastBlockMs_ = 1000.0 * numSamples / sampleRate_;
    drainCommands();
    drainMidiIn();

    if (entering)
    {
        // Entering bypass: release everything with the normal fade once (A26),
        // so voices do not freeze and resume with stale state afterwards.
        allSoundOff(true);
        float* outL = buffer.getWritePointer(0, startSample);
        float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1, startSample) : nullptr;
        renderRange(outL, outR, numSamples);
    }
    transportActiveUi_.store(transportActive_, std::memory_order_relaxed);
    publishSequencerUi(0);

    uint64_t minSeq = UINT64_MAX;
    int active = 0;
    for (const auto& v : voices_)
        if (v.active) { ++active; minSeq = std::min(minSeq, v.snapSeq); }
    activeVoicesUi_.store(active, std::memory_order_relaxed);
    minVoiceSeq_.store(minSeq, std::memory_order_seq_cst);
    inProcess_.store(false, std::memory_order_seq_cst);
}

// ============================================================================
// Commands / MIDI
// ============================================================================

void PlaybackEngine::notify(const Notification& n) noexcept
{
    if (! notes_.push(n))
        droppedNotes_.fetch_add(1, std::memory_order_relaxed);
}

void PlaybackEngine::drainCommands() noexcept
{
    Command c;
    int guard = 0;
    const uint32_t nowMs = juce::Time::getMillisecondCounter();
    const int32_t staleMs = (int32_t) staleThresholdMs_.load(std::memory_order_relaxed);
    while (guard++ < kCommandCapacity && commands_.pop(c))
    {
        // A3: UI triggers queued while the host was not processing would all
        // fire in the same sample when it resumes - drop the stale ones.
        // Stops / note-offs / transport / sequencer commands always apply.
        // N-13: the threshold is max(150 ms, 3 x block duration).
        if (c.stampMs != 0 && isDroppableTrigger(c.type) && (int32_t) (nowMs - c.stampMs) > staleMs)
        {
            staleDropped_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        if (bypassed_)
        {
            // N-18: nothing may start while the host bypasses us.
            if (isDroppableTrigger(c.type))
            {
                bypassDropped_.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            if (c.type == CmdType::TransportStart)
            {
                transportGen_ = c.i0;   // tell the UI it did not start
                Notification n; n.type = NoteType::TransportStopped; n.i0 = c.i0;
                notify(n);
                bypassDropped_.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            if (c.type == CmdType::SeqStart)
            {
                Notification n; n.type = NoteType::SequencerStopped; n.i0 = 1; n.i1 = c.i1;
                notify(n);
                bypassDropped_.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
        }
        if (c.snapSeq != 0 && (cur_ == nullptr || c.snapSeq > cur_->seq))
            switchSnapshotMidBlock();   // the UI published a newer snapshot before sending this
        applyCommand(c);
    }
}

void PlaybackEngine::drainMidiIn() noexcept
{
    RawMidi m;
    int guard = 0;
    while (guard++ < 1024 && midiIn_.pop(m))
        handleRawMidi(m.b0, m.b1, m.b2);
}

void PlaybackEngine::applyCommand(const Command& c) noexcept
{
    switch (c.type)
    {
        case CmdType::NoteOn:      noteOn(c.i0, c.f0); break;
        case CmdType::NoteOff:     noteOff(c.i0); break;
        case CmdType::StopVoices:  releasePool(PoolMidi); releasePool(PoolSeq); break;
        case CmdType::AllSoundOff: allSoundOff(true); break;

        case CmdType::PlaySegment:
        case CmdType::PlaySlot:
        {
            if (cur_ == nullptr || c.i0 < 0 || c.i0 >= cur_->stemCount) break;
            const int base = cur_->stemSegBase[(size_t) c.i0];
            const int next = cur_->stemSegBase[(size_t) c.i0 + 1];
            const int idx  = base + c.i1;
            if (c.i1 < 0 || idx >= next || idx >= (int) cur_->segs.size()) break;
            const auto& seg = cur_->segs[(size_t) idx];
            const auto& st  = cur_->stems[(size_t) c.i0];
            if (! st.audible || st.ch[0] == nullptr) break;

            Voice* v = nullptr;
            if (c.type == CmdType::PlaySegment)
            {
                // Same per-chop settings as a MIDI note (pitch, gain, pan, reverse, fades).
                v = startWindowVoice(PoolMidi, st, c.i0, seg.start, seg.end, seg.reverse, c.b0 || seg.loop, 1.0,
                                     c.f0 * st.volume * seg.gain, seg.pitchSemis);
            }
            else
            {
                const double len = (double) (seg.end - seg.start);
                int64_t ts = seg.start + (int64_t) (juce::jlimit(0.0f, 1.0f, c.f0) * len);
                int64_t te = seg.start + (int64_t) (juce::jlimit(0.0f, 1.0f, c.f1) * len);
                ts = juce::jlimit(seg.start, seg.end, ts);
                te = juce::jlimit(ts + 1, juce::jmax(ts + 1, seg.end), te);
                v = startWindowVoice(PoolMidi, st, c.i0, ts, te, c.b0 != seg.reverse, false, c.f2 > 0.0f ? c.f2 : 1.0,
                                     c.f4 * st.volume * seg.gain, c.f3 + seg.pitchSemis);
            }
            if (v != nullptr) { applySegFx(*v, seg); v->segIndex = c.i1; v->note = -1; v->gateRelease = false; }
            break;
        }

        case CmdType::TransportStart: transportGen_ = c.i0; startTransport(c.d0); break;
        case CmdType::TransportStop:  stopTransport(false); break;

        case CmdType::SeqStart:
            releasePool(PoolSeq);
            laneVoice_.fill(-1);
            seqRunning_     = true;
            seqHostDriven_  = false;
            seqMask_        = (uint32_t) c.i0;
            seqGen_         = c.i1;
            seqBpmOverride_ = c.d0;
            seqSamples_     = 0;
            seqSampleBase_  = 0;
            seqBeatBase_    = 0.0;
            seqBpmUsed_     = 0.0;
            hostNextBeat_   = -1.0;
            lastTrigTick_   = -1;
            break;

        case CmdType::SeqStop: stopSequencer(false, 1); break;
    }
}

void PlaybackEngine::handleRawMidi(uint8_t b0, uint8_t b1, uint8_t b2) noexcept
{
    const int status  = b0 & 0xF0;
    const int channel = (b0 & 0x0F) + 1;

    switch (status)
    {
        case 0x90:
            if (b2 > 0) noteOn(b1 & 0x7F, (float) (b2 & 0x7F) / 127.0f);
            else        noteOff(b1 & 0x7F);
            break;
        case 0x80:
            noteOff(b1 & 0x7F);
            break;
        case 0xB0:
        {
            const int   cc = b1 & 0x7F;
            const float v  = (float) (b2 & 0x7F) / 127.0f;
            if (cc == 1)
                forEachStemForChannel(channel, [this, v](int s) { mod_[(size_t) s].wheel = v; });
            else if (cc == 120)
                allSoundOff(false);
            else if (cc == 123)
                allNotesOff(false);   // a note-off for every note: chop release
            else if (params_.forwardCcToUi.load(std::memory_order_relaxed))
            {
                Notification n; n.type = NoteType::Controller; n.i0 = channel; n.i1 = cc; n.f0 = v;
                notify(n);
            }
            break;
        }
        case 0xE0:
        {
            const int raw = ((int) (b2 & 0x7F) << 7) | (int) (b1 & 0x7F);
            const float norm = juce::jlimit(-1.0f, 1.0f, ((float) raw - 8192.0f) / 8192.0f);
            forEachStemForChannel(channel, [this, norm](int s) { mod_[(size_t) s].bend = norm; });
            break;
        }
        case 0xA0:
        {
            const float v = (float) (b2 & 0x7F) / 127.0f;
            forEachStemForChannel(channel, [this, v](int s) { mod_[(size_t) s].at = v; });
            break;
        }
        case 0xD0:
        {
            const float v = (float) (b1 & 0x7F) / 127.0f;
            forEachStemForChannel(channel, [this, v](int s) { mod_[(size_t) s].at = v; });
            break;
        }
        default: break;
    }
}

void PlaybackEngine::noteOn(int note, float velocity) noexcept
{
    if (note < 0 || note > 127) return;

    if (params_.midiLearn.load(std::memory_order_relaxed))
    {
        // Learn consumes the note; the UI timer ends learn mode.
        Notification n; n.type = NoteType::LearnNote; n.i0 = note;
        notify(n);
        return;
    }

    if (bypassed_)
    {
        bypassDropped_.fetch_add(1, std::memory_order_relaxed);   // N-18 (standalone MIDI during bypass)
        return;
    }
    if (cur_ == nullptr) return;
    const auto& list = cur_->noteMap[(size_t) note];
    if (list.empty()) return;

    if (params_.mono.load(std::memory_order_relaxed))
        releasePool(PoolMidi);   // MONO is scoped to the MIDI pool (A15); a choke = short fade (N-3)

    const bool gate = params_.gate.load(std::memory_order_relaxed);
    for (const int idx : list)
    {
        if (idx < 0 || idx >= (int) cur_->segs.size()) continue;
        const auto& seg = cur_->segs[(size_t) idx];
        if (seg.stem < 0 || seg.stem >= cur_->stemCount) continue;
        const auto& st = cur_->stems[(size_t) seg.stem];
        if (! st.audible || st.ch[0] == nullptr) continue;

        const auto curve = (VelCurve) params_.stems[(size_t) seg.stem].velCurve.load(std::memory_order_relaxed);
        const float gain = applyVelCurve(velocity, curve) * st.volume * seg.gain;
        Voice* v = startWindowVoice(PoolMidi, st, seg.stem, seg.start, seg.end, seg.reverse, seg.loop, 1.0, gain,
                                    seg.pitchSemis);
        if (v == nullptr) continue;
        applySegFx(*v, seg);
        v->note        = note;
        v->segIndex    = seg.segIndex;
        // A11: loops always release on note-off. The per-chop trigger mode
        // overrides the global 1SH/GATE switch (1 = one-shot, 2 = gate).
        const bool chopGate = seg.trigger == 2 || (seg.trigger == 0 && gate);
        v->gateRelease = seg.loop || chopGate;

        Notification n;
        n.type = NoteType::SegmentTriggered;
        n.i0 = seg.stem; n.i1 = seg.segIndex; n.i2 = note;
        n.d0 = st.rate > 0.0 ? (double) (seg.end - seg.start) / st.rate : 0.0;
        notify(n);
    }
}

void PlaybackEngine::noteOff(int note) noexcept
{
    for (int i = 0; i < kMidiVoiceSlots; ++i)
    {
        auto& v = voices_[(size_t) i];
        if (! v.active || v.releasing || v.note != note || ! v.gateRelease) continue;
        releaseVoice(v);
        Notification n; n.type = NoteType::SegmentStopped; n.i0 = v.stem; n.i1 = v.segIndex; n.i2 = note;
        notify(n);
    }
}

void PlaybackEngine::allNotesOff(bool fast) noexcept
{
    // CC123: release every MIDI-triggered voice (gated, looped and one-shots).
    // As part of all-sound-off / bypass: short fixed fade, and also shortens
    // chop releases already running (N-3).
    for (int i = 0; i < kMidiVoiceSlots; ++i)
    {
        auto& v = voices_[(size_t) i];
        if (! v.active) continue;
        if (v.releasing)
        {
            if (fast) fastReleaseVoice(v);
            continue;
        }
        if (fast) fastReleaseVoice(v); else releaseVoice(v);
        if (v.note >= 0)
        {
            Notification n; n.type = NoteType::SegmentStopped; n.i0 = v.stem; n.i1 = v.segIndex; n.i2 = v.note;
            notify(n);
        }
    }
}

void PlaybackEngine::allSoundOff(bool stopSeq) noexcept
{
    allNotesOff(true);
    releasePool(PoolSeq);
    if (transportActive_) stopTransport(true);
    if (stopSeq && seqRunning_) stopSequencer(true, 1);
}

void PlaybackEngine::releasePool(uint8_t pool) noexcept
{
    // Every pool-wide release is a stop or a choke (MONO retrigger, UI stop,
    // sequencer start / stop / locate, host stall, transport restart): short
    // fixed fade, also for voices already in a long chop release (N-3).
    for (auto& v : voices_)
        if (v.active && v.pool == pool)
            fastReleaseVoice(v);
}

// ============================================================================
// Voices
// ============================================================================

PlaybackEngine::Voice* PlaybackEngine::allocVoice(uint8_t pool) noexcept
{
    int base = 0, count = kMidiVoiceSlots, limit = kMidiPolyphony;
    if (pool == PoolSeq)            { base = kSeqBase;       count = kSeqVoiceSlots;   limit = kSeqVoiceSlots - kMaxLanes; }
    else if (pool == PoolTransport) { base = kTransportBase; count = kTransportVoices; limit = kMaxStems; }

    int nonReleasing = 0;
    Voice* oldest = nullptr;
    for (int i = base; i < base + count; ++i)
    {
        auto& v = voices_[(size_t) i];
        if (v.active && ! v.releasing)
        {
            ++nonReleasing;
            if (oldest == nullptr || v.order < oldest->order) oldest = &v;
        }
    }
    // A15: steal the OLDEST voice of this pool, with the short fixed fade (never
    // the sequencer or the transport - they have their own pools). N-3: never
    // the chop's long release here, or the pool fills with multi-second tails.
    if (nonReleasing >= limit && oldest != nullptr)
        fastReleaseVoice(*oldest);

    Voice* freeSlot = nullptr;
    for (int i = base; i < base + count; ++i)
        if (! voices_[(size_t) i].active) { freeSlot = &voices_[(size_t) i]; break; }

    if (freeSlot == nullptr)
    {
        // Every slot is busy (more steals inside one fade time than the
        // reserve holds): cut the quietest fading voice. Its last output is
        // ramped out by the declick (kKillFadeMs), so the output never steps.
        Voice* best = nullptr;
        for (int i = base; i < base + count; ++i)
        {
            auto& v = voices_[(size_t) i];
            if (v.releasing && (best == nullptr || v.env < best->env)) best = &v;
        }
        if (best == nullptr) best = oldest != nullptr ? oldest : &voices_[(size_t) base];
        forceCutVoice(*best);
        freeSlot = best;
    }

    // The new voice will take `freeSlot`; keep headroom for the next ones.
    freeSlot->active = true;            // reserve it while scanning (startWindowVoice resets it)
    ensurePoolHeadroom(base, count);
    freeSlot->active = false;
    return freeSlot;
}

void PlaybackEngine::ensurePoolHeadroom(int base, int count) noexcept
{
    // Slots that are free, or whose fade ends within the fixed release time.
    int soon = 0;
    for (int i = base; i < base + count; ++i)
    {
        const auto& v = voices_[(size_t) i];
        if (! v.active) { ++soon; continue; }
        if (v.releasing && v.env <= v.relStep * (float) releaseLen_ + 1.0e-6f) ++soon;
    }
    // Shorten the quietest long releases until enough slots free up soon.
    while (soon < kPoolHeadroom)
    {
        Voice* best = nullptr;
        for (int i = base; i < base + count; ++i)
        {
            auto& v = voices_[(size_t) i];
            if (! v.active || ! v.releasing) continue;
            if (v.env <= v.relStep * (float) releaseLen_ + 1.0e-6f) continue;   // already fast
            if (best == nullptr || v.env < best->env) best = &v;
        }
        if (best == nullptr) break;   // nothing long-releasing left: the steal rule covers the rest
        fastReleaseVoice(*best);
        ++soon;
    }
}

void PlaybackEngine::forceCutVoice(Voice& v) noexcept
{
    if (v.active)
    {
        // Continue from the voice's last output sample and ramp it to zero.
        declickL_ += v.lastL;
        declickR_ += v.lastR;
        declickRemain_ = killFadeLen_;
        declickStepL_ = declickL_ / (float) killFadeLen_;
        declickStepR_ = declickR_ / (float) killFadeLen_;
        forcedKills_.fetch_add(1, std::memory_order_relaxed);
    }
    killVoice(v);
}

PlaybackEngine::Voice* PlaybackEngine::startWindowVoice(uint8_t pool, const StemSnap& stem, int stemIdx,
                                                        int64_t start, int64_t end, bool reverse, bool loop,
                                                        double speed, float gain, float slotSemis) noexcept
{
    if (cur_ == nullptr || stem.ch[0] == nullptr || end <= start || stem.rate <= 0.0)
        return nullptr;

    Voice* v = allocVoice(pool);
    if (v == nullptr) return nullptr;

    *v = Voice {};   // A25: every realtime field starts fresh on reuse
    v->active    = true;
    v->pool      = pool;
    v->snapSeq   = cur_->seq;
    v->order     = ++orderCounter_;
    v->src       = &stem;
    v->stem      = stemIdx;
    v->wStart    = start;
    v->wEnd      = end;
    v->len       = (double) (end - start);
    v->reverse   = reverse;
    v->loop      = loop;
    v->rate      = stem.rate / sampleRate_ * juce::jlimit(0.05, 8.0, speed);
    v->slotSemis = slotSemis;
    v->gain      = gain;
    v->relStep   = 1.0f / (float) releaseLen_;
    v->relLen    = releaseLen_;

    const double outToSrc = v->rate;   // output samples -> source samples
    if (microFadeEnabledFlag().load(std::memory_order_relaxed))
        v->edgeFade = std::min(juce::jmax(1.0, kMicroFadeMs * 0.001 * sampleRate_ * outToSrc), v->len * 0.5);
    v->fadeIn  = v->edgeFade;
    v->fadeOut = v->edgeFade;

    // Loop crossfade (A13): blend the loop tail with the audio just before the
    // loop start so the wrap is continuous; needs pre-roll audio in the stem.
    const double avail = reverse ? (double) (stem.numSamples - end) : (double) start;
    const double want  = kLoopCrossfadeMs * 0.001 * sampleRate_ * outToSrc;
    v->loopXf = std::min({ want, avail, v->len * 0.25 });
    if (v->loopXf < 8.0)
    {
        v->loopXf  = 0.0;
        v->loopDip = std::min(juce::jmax(1.0, kMicroFadeMs * 0.001 * sampleRate_ * outToSrc), v->len * 0.25);
    }
    return v;
}

void PlaybackEngine::releaseVoice(Voice& v) noexcept
{
    if (! v.active || v.releasing) return;
    v.releasing = true;
    v.attacking = false;
    v.relStep   = juce::jmax(1.0e-6f, v.env / (float) juce::jmax(releaseLen_, v.relLen));
}

void PlaybackEngine::fastReleaseVoice(Voice& v, int len) noexcept
{
    if (! v.active) return;
    const int n = juce::jmax(1, len > 0 ? len : releaseLen_);
    const float step = juce::jmax(1.0e-6f, v.env / (float) n);
    if (v.releasing)
    {
        if (step > v.relStep) v.relStep = step;   // shorten a long chop release, never lengthen
        return;
    }
    v.releasing = true;
    v.attacking = false;
    v.relStep   = step;
}

const SegSnap* PlaybackEngine::findSeg(int stem, int segIndex) const noexcept
{
    if (cur_ == nullptr || stem < 0 || stem >= cur_->stemCount || segIndex < 0) return nullptr;
    const int idx  = cur_->stemSegBase[(size_t) stem] + segIndex;
    const int next = cur_->stemSegBase[(size_t) stem + 1];
    if (idx >= next || idx >= (int) cur_->segs.size()) return nullptr;
    return &cur_->segs[(size_t) idx];
}

void PlaybackEngine::applySegFx(Voice& v, const SegSnap& seg) noexcept
{
    // Attack / release are fades on the played window (source samples, so they
    // follow speed and run before the pitch shifter); the release also sets the
    // note-off / stop fade length. Never longer than half the chop.
    v.pan = juce::jlimit(-1.0f, 1.0f, seg.pan);
    const double srcPerMs = 0.001 * sampleRate_ * v.rate;
    if (seg.attackMs > 0.0f)
        v.fadeIn = juce::jmax(v.fadeIn, std::min((double) seg.attackMs * srcPerMs, v.len * 0.5));
    if (seg.releaseMs > 0.0f)
    {
        if (! v.loop)
            v.fadeOut = juce::jmax(v.fadeOut, std::min((double) seg.releaseMs * srcPerMs, v.len * 0.5));
        v.relLen = juce::jmax(releaseLen_, (int) std::lround(seg.releaseMs * 0.001 * sampleRate_));
    }
}

void PlaybackEngine::killVoice(Voice& v) noexcept
{
    if (v.pool == PoolSeq && v.lane >= 0 && v.lane < kMaxLanes
        && laneVoice_[(size_t) v.lane] == voiceIndex(v))
        laneVoice_[(size_t) v.lane] = -1;
    v.active = false;
    v.releasing = false;
}

void PlaybackEngine::readFrame(const Voice& v, double p, float& l, float& r) noexcept
{
    const double phys = v.reverse ? ((double) (v.wEnd - 1) - p) : ((double) v.wStart + p);
    const double fl   = std::floor(phys);
    const int64_t i   = (int64_t) fl;
    const float t     = (float) (phys - fl);
    const float* L    = v.src->ch[0];
    const float* R    = v.src->ch[1];
    const int64_t n   = v.src->numSamples;

    if (t == 0.0f)
    {
        const bool in = (i >= 0 && i < n);
        l = in ? L[i] : 0.0f;
        r = in ? R[i] : 0.0f;
        return;
    }
    auto at = [n](const float* d, int64_t k) noexcept { return (k >= 0 && k < n) ? d[k] : 0.0f; };
    l = hermite(at(L, i - 1), at(L, i), at(L, i + 1), at(L, i + 2), t);
    r = hermite(at(R, i - 1), at(R, i), at(R, i + 1), at(R, i + 2), t);
}

void PlaybackEngine::generate(const Voice& v, GenState& g, int n, float* L, float* R) noexcept
{
    const double len = v.len;
    for (int i = 0; i < n; ++i)
    {
        if (g.ended) { L[i] = 0.0f; R[i] = 0.0f; continue; }

        double c = g.cursor;
        if (c >= len)
        {
            if (v.loop && len > 0.0) { c = std::fmod(c, len); g.wrapped = true; }
            else { g.ended = true; L[i] = 0.0f; R[i] = 0.0f; continue; }
        }

        float l, r;
        readFrame(v, c, l, r);

        float gain = 1.0f;
        if (v.fadeIn > 0.0 && ! g.wrapped && c < v.fadeIn)
            gain = (float) (c / v.fadeIn);
        if (v.fadeOut > 0.0 && ! v.loop)
        {
            const double rem = len - c;
            if (rem < v.fadeOut) gain = std::min(gain, (float) (rem / v.fadeOut));
        }
        if (v.loop)
        {
            if (v.loopXf > 0.0)
            {
                const double xs = len - v.loopXf;
                if (c >= xs)
                {
                    const float t = (float) ((c - xs) / v.loopXf);
                    float l2, r2;
                    readFrame(v, c - len, l2, r2);
                    l += (l2 - l) * t;
                    r += (r2 - r) * t;
                }
            }
            else if (v.loopDip > 0.0)
            {
                const double rem = len - c;
                if (rem < v.loopDip) gain = std::min(gain, (float) (rem / v.loopDip));
                if (g.wrapped && c < v.loopDip) gain = std::min(gain, (float) (c / v.loopDip));
            }
        }
        L[i] = l * gain;
        R[i] = r * gain;
        g.cursor = c + v.rate;
    }
}

void PlaybackEngine::enterStretch(Voice& v, float semis) noexcept
{
    auto& s = stretchers_[(size_t) voiceIndex(v)]->s;
    s.setTransposeSemitones(semis);
    v.lastSemi = semis;

    // A12: outputSeek() pre-rolls the stretcher with `seekLen_` frames of
    // look-ahead so its output is aligned with the source position - pitched
    // voices start on the same sample as unpitched ones; no latency to report.
    GenState g { v.cursor, v.wrapped, v.ended };
    float* sL = scratchSeek_.getWritePointer(0);
    float* sR = scratchSeek_.getWritePointer(1);
    generate(v, g, seekLen_, sL, sR);
    const float* ins[2] = { sL, sR };
    s.outputSeek(ins, seekLen_);

    v.inCursor  = g.cursor;
    v.inWrapped = g.wrapped;
    v.inEnded   = g.ended;
    v.tailRemain = g.ended ? (int64_t) seekLen_ : 0;
    v.stretch   = true;
    v.xfadeRemain = v.outCount > 0 ? xfadeLen_ : 0;   // mid-voice switch: crossfade from the direct path
}

void PlaybackEngine::tickModSmoothing(int n) noexcept
{
    // One-pole toward the MIDI targets, ~15 Hz; exact for any chunk length.
    const float a = (float) (1.0 - std::exp(-2.0 * juce::MathConstants<double>::pi * 15.0 * n / sampleRate_));
    for (auto& m : mod_)
    {
        m.sBend  += a * (m.bend  - m.sBend);
        m.sWheel += a * (m.wheel - m.sWheel);
        m.sAt    += a * (m.at    - m.sAt);
    }
}

float PlaybackEngine::modPitch(int stem) const noexcept
{
    if (stem < 0 || stem >= kMaxStems) return 0.0f;
    const auto& p = params_.stems[(size_t) stem];
    const auto& m = mod_[(size_t) stem];
    float semis = 0.0f;
    if ((ModDest) p.bendDest.load(std::memory_order_relaxed)  == ModDest::Pitch) semis += m.sBend  * p.bendRange.load(std::memory_order_relaxed);
    if ((ModDest) p.wheelDest.load(std::memory_order_relaxed) == ModDest::Pitch) semis += m.sWheel * p.wheelRange.load(std::memory_order_relaxed);
    if ((ModDest) p.atDest.load(std::memory_order_relaxed)    == ModDest::Pitch) semis += m.sAt    * p.atRange.load(std::memory_order_relaxed);
    return semis;
}

float PlaybackEngine::modVolume(int stem) const noexcept
{
    if (stem < 0 || stem >= kMaxStems) return 1.0f;
    const auto& p = params_.stems[(size_t) stem];
    const auto& m = mod_[(size_t) stem];
    float g = 1.0f;
    if ((ModDest) p.bendDest.load(std::memory_order_relaxed)  == ModDest::Volume) g += m.sBend  * p.bendRange.load(std::memory_order_relaxed);
    if ((ModDest) p.wheelDest.load(std::memory_order_relaxed) == ModDest::Volume) g += m.sWheel * p.wheelRange.load(std::memory_order_relaxed);
    if ((ModDest) p.atDest.load(std::memory_order_relaxed)    == ModDest::Volume) g += m.sAt    * p.atRange.load(std::memory_order_relaxed);
    return juce::jlimit(0.0f, 2.0f, g);
}

float PlaybackEngine::modPan(int stem) const noexcept
{
    if (stem < 0 || stem >= kMaxStems) return 0.0f;
    const auto& p = params_.stems[(size_t) stem];
    const auto& m = mod_[(size_t) stem];
    float pan = 0.0f;
    if ((ModDest) p.bendDest.load(std::memory_order_relaxed)  == ModDest::Pan) pan += m.sBend  * p.bendRange.load(std::memory_order_relaxed);
    if ((ModDest) p.wheelDest.load(std::memory_order_relaxed) == ModDest::Pan) pan += m.sWheel * p.wheelRange.load(std::memory_order_relaxed);
    if ((ModDest) p.atDest.load(std::memory_order_relaxed)    == ModDest::Pan) pan += m.sAt    * p.atRange.load(std::memory_order_relaxed);
    return juce::jlimit(-1.0f, 1.0f, pan);
}

void PlaybackEngine::renderRange(float* outL, float* outR, int n) noexcept
{
    // A22: never hand more than the prepared size to the scratch buffers.
    while (n > 0)
    {
        const int c = juce::jmin(n, maxBlock_);
        renderChunk(outL, outR, c);
        outL += c;
        if (outR != nullptr) outR += c;
        n -= c;
    }
}

void PlaybackEngine::renderChunk(float* outL, float* outR, int n) noexcept
{
    tickModSmoothing(n);
    for (auto& v : voices_)
        if (v.active)
            renderVoice(v, outL, outR, n);

    // Forced-cut declick (N-3): the cut voices' last output, ramped to zero.
    for (int i = 0; i < n && declickRemain_ > 0; ++i, --declickRemain_)
    {
        declickL_ -= declickStepL_;
        declickR_ -= declickStepR_;
        outL[i] += declickL_;
        if (outR != nullptr) outR[i] += declickR_;
    }
    if (declickRemain_ <= 0) { declickL_ = declickR_ = 0.0f; declickRemain_ = 0; }
}

void PlaybackEngine::renderVoice(Voice& v, float* outL, float* outR, int n) noexcept
{
    float* bL = scratchOut_.getWritePointer(0);
    float* bR = scratchOut_.getWritePointer(1);

    const bool stemOk = (v.stem >= 0 && v.stem < kMaxStems);
    const float semis = v.slotSemis
                      + params_.globalSemis.load(std::memory_order_relaxed)
                      + (stemOk ? params_.stems[(size_t) v.stem].pitchSemis.load(std::memory_order_relaxed)
                                  + modPitch(v.stem)
                                : 0.0f);

    if (! v.stretch && std::abs(semis) >= kPitchEpsilon)
    {
        // N-18: a new voice enters at once (sample-aligned onset, A12); a
        // sounding voice whose pitch just moved off 0 waits for the per-block
        // budget, so 80 voices do not all pre-roll in the same block.
        if (v.outCount == 0 || stretchEntriesThisBlock_ < kMaxStretchEntriesPerBlock)
        {
            if (v.outCount > 0) ++stretchEntriesThisBlock_;
            enterStretch(v, semis);
        }
        else
        {
            deferredStretch_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    bool finished = false;
    if (v.stretch)
    {
        auto& s = stretchers_[(size_t) voiceIndex(v)]->s;
        if (std::abs(semis - v.lastSemi) > 1.0e-4f)
        {
            s.setTransposeSemitones(semis);
            v.lastSemi = semis;
        }

        float* iL = scratchIn_.getWritePointer(0);
        float* iR = scratchIn_.getWritePointer(1);
        GenState g { v.inCursor, v.inWrapped, v.inEnded };
        const bool wasEnded = v.inEnded;
        generate(v, g, n, iL, iR);
        v.inCursor = g.cursor; v.inWrapped = g.wrapped; v.inEnded = g.ended;
        if (! wasEnded && v.inEnded)
            v.tailRemain = (int64_t) seekLen_ + n;   // output still owed after the last input frame

        const float* ins[2] = { iL, iR };
        float* outs[2] = { bL, bR };
        s.process(ins, n, outs, n);

        if (v.xfadeRemain > 0)
        {
            float* xL = scratchX_.getWritePointer(0);
            float* xR = scratchX_.getWritePointer(1);
            GenState d { v.cursor, v.wrapped, v.ended };
            generate(v, d, n, xL, xR);
            v.cursor = d.cursor; v.wrapped = d.wrapped; v.ended = d.ended;
            for (int i = 0; i < n && v.xfadeRemain > 0; ++i, --v.xfadeRemain)
            {
                const float t = 1.0f - (float) v.xfadeRemain / (float) xfadeLen_;
                bL[i] = xL[i] + (bL[i] - xL[i]) * t;
                bR[i] = xR[i] + (bR[i] - xR[i]) * t;
            }
        }

        if (v.inEnded)
        {
            v.tailRemain -= n;
            if (v.tailRemain <= 0)
                finished = true;
            else if (v.tailRemain <= releaseLen_ && ! v.loop)
                fastReleaseVoice(v, (int) v.tailRemain);   // fade must END with the flushed tail (A12, N-3)
        }
    }
    else
    {
        GenState d { v.cursor, v.wrapped, v.ended };
        generate(v, d, n, bL, bR);
        v.cursor = d.cursor; v.wrapped = d.wrapped; v.ended = d.ended;
        if (v.ended) finished = true;   // samples past the end are zero (edge-faded)
    }

    float g = v.gain;
    float pan = v.pan;
    if (stemOk)
    {
        g  *= modVolume(v.stem);
        pan = juce::jlimit(-1.0f, 1.0f, pan + modPan(v.stem));
    }
    const float gL = g * (pan <= 0.0f ? 1.0f : 1.0f - pan);
    const float gR = g * (pan >= 0.0f ? 1.0f : 1.0f + pan);
    const bool  isTransport = (v.pool == PoolTransport);
    const float tStep = 1.0f / (float) juce::jmax(1, xfadeLen_);

    for (int i = 0; i < n; ++i)
    {
        if (v.releaseAt >= 0 && v.outCount >= v.releaseAt)
        {
            v.releaseAt = -1;
            fastReleaseVoice(v);   // sequencer cell end: completes exactly at the cell end (N-3)
        }
        if (v.attacking)
        {
            v.env += v.envStep;
            if (v.env >= 1.0f) { v.env = 1.0f; v.attacking = false; }
        }
        if (v.releasing)
        {
            v.env -= v.relStep;
            if (v.env <= 0.0f) { v.env = 0.0f; finished = true; break; }
        }
        float e = v.env;
        if (isTransport)
        {
            if (v.tgain < v.tgainTarget)      v.tgain = std::min(v.tgainTarget, v.tgain + tStep);
            else if (v.tgain > v.tgainTarget) v.tgain = std::max(v.tgainTarget, v.tgain - tStep);
            e *= v.tgain;
        }
        const float cl = bL[i] * gL * e;
        const float cr = bR[i] * gR * e;
        outL[i] += cl;
        if (outR != nullptr) outR[i] += cr;
        v.lastL = cl;
        v.lastR = cr;
        ++v.outCount;
    }

    if (finished)
        killVoice(v);
}

// ============================================================================
// Transport (S-Mode "play all stems")
// ============================================================================

void PlaybackEngine::startTransport(double seconds) noexcept
{
    releasePool(PoolTransport);
    if (cur_ == nullptr || cur_->transportLengthModel <= 0 || cur_->stemCount <= 0 || cur_->modelRate <= 0.0)
    {
        if (transportActive_) stopTransport(true);
        return;
    }

    const double modelRate = cur_->modelRate;
    const double lenModel  = (double) cur_->transportLengthModel;
    const double posModel  = juce::jlimit(0.0, juce::jmax(0.0, lenModel - 1.0), seconds * modelRate);
    const bool   loop      = params_.transportLoop.load(std::memory_order_relaxed);

    bool started = false;
    for (int s = 0; s < cur_->stemCount && s < kMaxStems; ++s)
    {
        const auto& st = cur_->stems[(size_t) s];
        if (st.ch[0] == nullptr || st.rate <= 0.0) continue;
        const int64_t lenStem = (int64_t) std::ceil(lenModel * st.rate / modelRate);
        Voice* v = startWindowVoice(PoolTransport, st, s, 0, lenStem, false, loop, 1.0, st.volume, 0.0f);
        if (v == nullptr) continue;
        v->cursor    = posModel * st.rate / modelRate;
        v->wrapped   = v->cursor > 0.0;   // no edge fade-in when starting mid-song...
        v->env       = 0.0f;              // ...a short attack instead (A13)
        v->attacking = true;
        v->envStep   = 1.0f / (float) releaseLen_;
        v->tgain = v->tgainTarget = st.audible ? 1.0f : 0.0f;
        started = true;
    }
    transportActive_   = started;
    transportPosModel_ = posModel;
    transportSecUi_.store(posModel / modelRate, std::memory_order_relaxed);
}

void PlaybackEngine::stopTransport(bool notifyUi) noexcept
{
    releasePool(PoolTransport);
    const bool was = transportActive_;
    transportActive_ = false;
    if (notifyUi && was)
    {
        Notification n; n.type = NoteType::TransportStopped; n.i0 = transportGen_;
        notify(n);
    }
}

// ============================================================================
// Sequencer (B36): clock on the audio thread, host-synced when the DAW plays
// ============================================================================

void PlaybackEngine::stopSequencer(bool notifyUi, int reason) noexcept
{
    const bool was = seqRunning_;
    seqRunning_    = false;
    seqHostDriven_ = false;
    releasePool(PoolSeq);
    laneVoice_.fill(-1);
    if (notifyUi && was)
    {
        Notification n; n.type = NoteType::SequencerStopped; n.i0 = reason; n.i1 = seqGen_;
        notify(n);
    }
}

void PlaybackEngine::prepareSequencerEvents(int numSamples, const HostInfo& host) noexcept
{
    seqEventCount_ = 0;
    if (! seqRunning_) return;

    // `host` is sanitised by process() (N-4): bpm is 0 or within
    // [kMinHostBpm, kMaxHostBpm], ppq finite and bounded when ppqValid.
    const bool hostRolling = host.valid && host.playing && host.ppqValid && host.bpm > 0.0
                          && std::isfinite(host.bpm) && std::isfinite(host.ppq);

    if (seqHostDriven_ && ! hostRolling)
    {
        // A11: the DAW stopped while it was driving the sequencer -> stop everything.
        stopSequencer(true, 0);
        releasePool(PoolMidi);
        return;
    }
    if (! seqHostDriven_ && hostRolling)
    {
        seqHostDriven_ = true;
        hostNextBeat_  = -1.0;
        releasePool(PoolSeq);
        laneVoice_.fill(-1);
    }

    // The clock runs in ticks of 1/kTicksPerBeat beat; a lane with
    // cellsPerBeat = c fires on every (kTicksPerBeat / c)-th tick (B18).
    auto addEvents = [this](int64_t tick, int offset) noexcept
    {
        if (cur_ == nullptr) return;
        const int lanes = juce::jmin((int) cur_->lanes.size(), kMaxLanes);
        for (int l = 0; l < lanes; ++l)
        {
            if ((seqMask_ & (1u << l)) == 0) continue;
            const auto& lane = cur_->lanes[(size_t) l];
            if (lane.muted || lane.numCells <= 0 || (int) lane.cellStart.size() < lane.numCells) continue;
            const int cpb = juce::jlimit(1, kTicksPerBeat, lane.cellsPerBeat);
            const int ticksPerCell = kTicksPerBeat / cpb;
            if (((tick % ticksPerCell) + ticksPerCell) % ticksPerCell != 0) continue;
            const int64_t cellAbs = tick / ticksPerCell;
            const int cell = (int) (((cellAbs % lane.numCells) + lane.numCells) % lane.numCells);
            const int si = lane.cellStart[(size_t) cell];
            if (si >= 0 && seqEventCount_ < (int) seqEvents_.size())
                seqEvents_[(size_t) seqEventCount_++] = { offset, l, si };
        }
    };

    if (seqHostDriven_)
    {
        const double bpm = juce::jlimit(kMinHostBpm, kMaxHostBpm, host.bpm);
        const double bps = bpm / 60.0 / sampleRate_;   // beats per sample (finite, > 0)
        const double b0  = juce::jlimit(-kMaxHostPpq, kMaxHostPpq, host.ppq);
        seqSpb_ = 1.0 / bps;
        const int64_t firstTick = (int64_t) std::ceil(b0 * kTicksPerBeat - 1.0e-9);
        // Locate tolerance ~10 ms: tempo ramps and hosts that round PPQ (FL)
        // must not look like a jump every block (N-18). hostNextBeat_ is
        // negative only before the first host-driven block (or for a host
        // position before 0, which then simply resyncs every block).
        const double tol = juce::jmax(0.010 * sampleRate_ * bps, 1.0e-6);
        if (hostNextBeat_ < 0.0 || std::abs(b0 - hostNextBeat_) > tol
            || lastTrigTick_ < firstTick - 2 || lastTrigTick_ > firstTick + 2)
        {
            if (hostNextBeat_ >= 0.0) { releasePool(PoolSeq); laneVoice_.fill(-1); }   // DAW loop / locate
            lastTrigTick_ = firstTick - 1;
        }
        hostNextBeat_ = b0 + numSamples * bps;
        // N-4: bounded - at most the ticks this block spans, plus slack.
        const int64_t maxTicks = (int64_t) std::ceil(numSamples * bps * kTicksPerBeat) + 4;
        for (int64_t k = lastTrigTick_ + 1, it = 0; it < maxTicks; ++k, ++it)
        {
            const double offD = std::ceil(((double) k / kTicksPerBeat - b0) / bps - 1.0e-6);
            if (! (offD < (double) numSamples)) break;   // also stops on NaN
            const int off = offD < 0.0 ? 0 : (int) offD;
            addEvents(k, off);
            lastTrigTick_ = k;
        }
        seqBeatAtEnd_ = hostNextBeat_;
    }
    else
    {
        double bpm = seqBpmOverride_ > 0.0 ? seqBpmOverride_ : params_.seqBpm.load(std::memory_order_relaxed);
        if (! std::isfinite(bpm)) bpm = 120.0;   // N-4: jlimit passes NaN through
        bpm = juce::jlimit(20.0, 400.0, bpm);
        if (bpm != seqBpmUsed_)
        {
            if (seqBpmUsed_ > 0.0)
            {
                // Rebase so a tempo change continues from the current beat.
                seqBeatBase_  += (double) (seqSamples_ - seqSampleBase_) / seqSpb_;
                seqSampleBase_ = seqSamples_;
            }
            seqBpmUsed_ = bpm;
        }
        seqSpb_ = 60.0 * sampleRate_ / bpm;   // samples per beat
        const int64_t maxTicks = (int64_t) std::ceil(numSamples / seqSpb_ * kTicksPerBeat) + 4;   // N-4: bounded
        for (int64_t k = lastTrigTick_ + 1, it = 0; it < maxTicks; ++k, ++it)
        {
            const double offD = (double) seqSampleBase_ - (double) seqSamples_
                              + std::ceil(((double) k / kTicksPerBeat - seqBeatBase_) * seqSpb_ - 1.0e-6);
            if (! (offD < (double) numSamples)) break;
            const int off = offD < 0.0 ? 0 : (int) offD;
            addEvents(k, off);
            lastTrigTick_ = k;
        }
        seqBeatAtEnd_ = seqBeatBase_ + (double) (seqSamples_ + numSamples - seqSampleBase_) / seqSpb_;
    }
}

void PlaybackEngine::triggerSlot(int lane, int slot) noexcept
{
    if (cur_ == nullptr || lane < 0 || lane >= (int) cur_->lanes.size() || lane >= kMaxLanes) return;
    const auto& L = cur_->lanes[(size_t) lane];
    if (slot < 0 || slot >= (int) L.slots.size()) return;
    const auto& sl = L.slots[(size_t) slot];

    // Choke the lane's previous voice (fade) - never MIDI voices (B36).
    const int prev = laneVoice_[(size_t) lane];
    if (prev >= 0)
    {
        auto& pv = voices_[(size_t) prev];
        if (pv.active && pv.order == laneVoiceOrder_[(size_t) lane]) fastReleaseVoice(pv);   // N-3: lane stays monophonic
        laneVoice_[(size_t) lane] = -1;
    }

    if (sl.stem < 0 || sl.stem >= cur_->stemCount) return;
    const auto& st = cur_->stems[(size_t) sl.stem];
    if (! st.audible || st.ch[0] == nullptr) return;

    // The chop's own settings stack with the slot's (pitch adds, gain
    // multiplies, reverse flips) - B26 / UX gap 4.
    const SegSnap* seg = findSeg(sl.stem, sl.segIndex);
    const float segGain  = seg != nullptr ? seg->gain : 1.0f;
    const float segSemis = seg != nullptr ? seg->pitchSemis : 0.0f;
    const bool  segRev   = seg != nullptr && seg->reverse;
    Voice* v = startWindowVoice(PoolSeq, st, sl.stem, sl.start, sl.end, sl.reverse != segRev, false,
                                sl.speed > 0.0f ? sl.speed : 1.0, sl.velocity * st.volume * segGain,
                                sl.pitchSemis + segSemis);
    if (v == nullptr) return;
    if (seg != nullptr) applySegFx(*v, *seg);
    v->lane      = lane;
    v->slotIndex = sl.slotIndex;
    v->segIndex  = sl.segIndex;
    const int cpb = juce::jlimit(1, kTicksPerBeat, L.cellsPerBeat);
    const int64_t cellLen = (int64_t) std::llround(juce::jmax(1, sl.cellSpan) * seqSpb_ / cpb);
    // Fixed short fade (not the chop's release, N-3) that completes at the cell end.
    v->releaseAt = juce::jmax((int64_t) 1, cellLen - releaseLen_);
    laneVoice_[(size_t) lane]      = voiceIndex(*v);
    laneVoiceOrder_[(size_t) lane] = v->order;
}

void PlaybackEngine::publishSequencerUi(int /*numSamples*/) noexcept
{
    seqRunningUi_.store(seqRunning_, std::memory_order_relaxed);
    seqHostUi_.store(seqRunning_ && seqHostDriven_, std::memory_order_relaxed);

    for (int l = 0; l < kMaxLanes; ++l)
    {
        int slot = -1; float prog = 0.0f; float head = -1.0f;
        if (seqRunning_ && cur_ != nullptr && (seqMask_ & (1u << l)) != 0 && l < (int) cur_->lanes.size())
        {
            const auto& lane = cur_->lanes[(size_t) l];
            if (lane.numCells > 0 && (int) lane.cellCover.size() >= lane.numCells)
            {
                const int cpb = juce::jlimit(1, kTicksPerBeat, lane.cellsPerBeat);
                double pos = std::fmod(seqBeatAtEnd_ * cpb, (double) lane.numCells);
                if (pos < 0.0) pos += lane.numCells;
                head = (float) (pos / lane.numCells);
                const int cell = juce::jlimit(0, lane.numCells - 1, (int) pos);
                const int cover = lane.cellCover[(size_t) cell];
                if (cover >= 0 && cover < (int) lane.slots.size())
                {
                    const auto& s = lane.slots[(size_t) cover];
                    slot = s.slotIndex;
                    prog = (float) juce::jlimit(0.0, 1.0, (pos - s.gridCell) / juce::jmax(1, s.cellSpan));
                }
            }
        }
        laneSlotUi_[(size_t) l].store(slot, std::memory_order_relaxed);
        laneProgUi_[(size_t) l].store(prog, std::memory_order_relaxed);
        laneHeadUi_[(size_t) l].store(head, std::memory_order_relaxed);
    }
}

// ============================================================================
// SnapshotManager
// ============================================================================

SnapshotManager::~SnapshotManager()
{
    // Owner guarantees the audio thread is no longer running this engine.
    engine_.publishSnapshot(nullptr);
}

void SnapshotManager::publish(std::shared_ptr<Snapshot> s)
{
    std::shared_ptr<const Snapshot> old;
    {
        std::scoped_lock lock(mutex_);
        if (s != nullptr) s->seq = nextSeq_++;
        std::shared_ptr<const Snapshot> c = std::move(s);
        engine_.publishSnapshot(c.get());
        if (current_ != nullptr) retired_.push_back(current_);
        current_ = std::move(c);
    }
}

std::shared_ptr<const Snapshot> SnapshotManager::current() const
{
    std::scoped_lock lock(mutex_);
    return current_;
}

uint64_t SnapshotManager::currentSeq() const
{
    std::scoped_lock lock(mutex_);
    return current_ != nullptr ? current_->seq : 0;
}

int SnapshotManager::collectGarbage()
{
    std::vector<std::shared_ptr<const Snapshot>> toFree;   // destroyed outside the lock
    {
        std::scoped_lock lock(mutex_);
        if (retired_.empty()) return 0;
        const uint64_t bound = engine_.getReclaimBound();
        for (auto it = retired_.begin(); it != retired_.end();)
        {
            if ((*it)->seq < bound) { toFree.push_back(std::move(*it)); it = retired_.erase(it); }
            else ++it;
        }
    }
    return (int) toFree.size();
}

int SnapshotManager::retiredCount() const
{
    std::scoped_lock lock(mutex_);
    return (int) retired_.size();
}

std::shared_ptr<void> SnapshotManager::detachForRelease()
{
    // Owner guarantees the audio thread is no longer running this engine.
    engine_.publishSnapshot(nullptr);
    auto held = std::make_shared<std::vector<std::shared_ptr<const Snapshot>>>();
    std::scoped_lock lock(mutex_);
    held->reserve(retired_.size() + 1);
    if (current_ != nullptr) held->push_back(std::move(current_));
    for (auto& r : retired_) held->push_back(std::move(r));
    current_.reset();
    retired_.clear();
    return held;
}

} // namespace engine
} // namespace samplar
