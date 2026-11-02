#include "MidiMappingService.h"
#include "../audio/engine/OfflineRender.h"
#include "../audio/engine/SegmentFx.h"
#include "../utils/DeferredRelease.h"

#include <algorithm>
#include <cmath>

namespace samplar {

namespace
{
    engine::SlotDesc toSlotDesc(const PatternSlot& p)
    {
        engine::SlotDesc d;
        d.empty      = p.empty;
        d.stem       = p.stemIndex;
        d.segIndex   = p.segmentIndex;
        d.gridCell   = p.gridCell;
        d.cellSpan   = p.cellSpan;
        d.velocity   = p.velocity;
        d.pitchSemis = p.pitchSemitones;
        d.speed      = p.speed;
        d.reverse    = p.reverse;
        d.trimStart  = p.trimStart;
        d.trimEnd    = p.trimEnd;
        return d;
    }

    bool sameSlot(const engine::SlotDesc& a, const engine::SlotDesc& b)
    {
        return a.empty == b.empty && a.stem == b.stem && a.segIndex == b.segIndex
            && a.gridCell == b.gridCell && a.cellSpan == b.cellSpan && a.velocity == b.velocity
            && a.pitchSemis == b.pitchSemis && a.speed == b.speed && a.reverse == b.reverse
            && a.trimStart == b.trimStart && a.trimEnd == b.trimEnd;
    }

    bool sameLanes(const std::vector<engine::LaneDesc>& a, const std::vector<engine::LaneDesc>& b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
        {
            if (a[i].numCells != b[i].numCells || a[i].cellsPerBeat != b[i].cellsPerBeat || a[i].muted != b[i].muted
                || a[i].slots.size() != b[i].slots.size())
                return false;
            for (size_t j = 0; j < a[i].slots.size(); ++j)
                if (! sameSlot(a[i].slots[j], b[i].slots[j])) return false;
        }
        return true;
    }

    constexpr int kTimerHz = 30;
}

// ============================================================================
// Construction / IService
// ============================================================================

MidiMappingService::MidiMappingService(EventBus& bus)
    : bus_(bus),
      engine_(std::make_unique<engine::PlaybackEngine>(params_)),
      snapshots_(*engine_)
{
    engine_->setStallRecovery(true);   // live engine only (offline engines never stall)
    snapshots_.publish(std::make_shared<engine::Snapshot>());   // never a null snapshot
    juce::Logger::writeToLog("[MidiMappingService] Constructed (realtime engine, lock-free command/notification FIFOs)");
}

MidiMappingService::~MidiMappingService()
{
    destroyed_.store(true);
    stopTimer();
    resampleAbort_->store(true);
    if (resamplePool_ != nullptr)
        resamplePool_->removeAllJobs(true, 10000);

    // Plugin unload under CPU load (impl_unload.md): freeing the per-voice
    // stretcher FFT buffers and the stem / snapshot buffers was the largest
    // part of a slow delete. The audio thread can no longer run this engine,
    // so hand them to a module-pinned thread; members are destroyed empty.
    {
        auto heavy = std::make_shared<std::vector<std::shared_ptr<void>>>();
        heavy->push_back(engine_->detachStretchersForRelease());
        heavy->push_back(snapshots_.detachForRelease());
        heavy->push_back(std::make_shared<std::vector<ResampledStem>>(std::move(resampled_)));
        resampled_.clear();
        {
            std::scoped_lock lock(exportMutex_);
            heavy->push_back(std::make_shared<std::shared_ptr<const engine::Snapshot>>(std::move(exportSnap_)));
            exportSnap_.reset();
        }
        DeferredRelease::run([heavy = std::move(heavy)]() mutable { heavy.reset(); },
                             "MidiMappingService stretchers + stem buffers");
    }
    juce::Logger::writeToLog("[MidiMappingService] Destroyed");
}

void MidiMappingService::initialise()
{
    juce::Logger::writeToLog("[MidiMappingService] === initialise START ===");
    resamplePool_ = std::make_unique<juce::ThreadPool>(juce::ThreadPoolOptions{}
                                                           .withThreadName("SAMPLAR resample")
                                                           .withNumberOfThreads(1));
    startTimerHz(kTimerHz);
    juce::Logger::writeToLog("[MidiMappingService] notification/snapshot timer started ("
                             + juce::String(kTimerHz) + " Hz)");
    juce::Logger::writeToLog("[MidiMappingService] === initialise END ===");
}

void MidiMappingService::shutdown()
{
    juce::Logger::writeToLog("[MidiMappingService] shutdown");
    destroyed_.store(true, std::memory_order_seq_cst);
    stopTimer();
    params_.midiLearn.store(false);
    disconnectFromDeviceManager();

    resampleAbort_->store(true);
    if (resamplePool_ != nullptr)
    {
        const bool drained = resamplePool_->removeAllJobs(true, 10000);
        juce::Logger::writeToLog(drained ? "[MidiMappingService] resample pool drained"
                                         : "[MidiMappingService] resample pool drain TIMED OUT");
        if (drained)
        {
            // Idle + empty: joining its thread can take 100s of ms under CPU
            // load, so ~ThreadPool runs on a module-pinned thread.
            DeferredRelease::run([pool = std::shared_ptr<juce::ThreadPool>(std::move(resamplePool_))]() mutable { pool.reset(); },
                                 "MidiMappingService resample pool");
        }
        resamplePool_.reset();
    }
}

bool MidiMappingService::pushCommand(engine::Command c)
{
    if (c.snapSeq == 0) c.snapSeq = snapshots_.currentSeq();
    c.stampMs = juce::Time::getMillisecondCounter() | 1u;   // 0 means "never stale"
    // Keep headroom for stops / note-offs: a full FIFO must never swallow the
    // note-off of a note that did get in (review A3).
    constexpr int kReserve = 64;
    if (engine::isDroppableTrigger(c.type)
        && engine_->getQueuedCommandCount() >= engine::PlaybackEngine::kCommandCapacity - kReserve)
    {
        juce::Logger::writeToLog("[MidiMappingService] command FIFO nearly full (host not processing?) - trigger dropped (type="
                                 + juce::String((int) c.type) + ")");
        return false;
    }
    const bool ok = engine_->pushCommand(c);
    if (! ok)
        juce::Logger::writeToLog("[MidiMappingService] command FIFO full - command dropped (type="
                                 + juce::String((int) c.type) + ")");
    return ok;
}

// ============================================================================
// Audio
// ============================================================================

void MidiMappingService::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    juce::Logger::writeToLog("[MidiMappingService] === prepareToPlay === sr=" + juce::String(sampleRate)
                             + " maxBlock=" + juce::String(samplesPerBlockExpected));
    engine_->prepare(sampleRate, juce::jmax(32, samplesPerBlockExpected));
    hostRate_.store(sampleRate);
    // Licence gate ramp (N-18): 5 ms; starts at the current gate state so a
    // bounce that begins on sample 0 is not faded in.
    const bool gateOpen = audioGate_ == nullptr || audioGate_->load();
    gate_.prepare(sampleRate, gateOpen, 5.0);
    juce::Logger::writeToLog("[MidiMappingService] licence gate: " + juce::String(audioGate_ == nullptr ? "none (ungated)" : (gateOpen ? "open" : "closed"))
                             + ", 5 ms ramp on change; engine generation=" + juce::String((int) engine_->getLifecycleGeneration()));
    juce::Logger::writeToLog("[MidiMappingService] engine prepared: voices midi="
                             + juce::String(engine::kMidiPolyphony) + "(+" + juce::String(engine::kMidiVoiceSlots - engine::kMidiPolyphony)
                             + " fade reserve) seq=" + juce::String(engine::kSeqVoiceSlots)
                             + " transport=" + juce::String(engine::kTransportVoices)
                             + " stretchSeek=" + juce::String(engine_->getStretchSeekLength()) + " samples"
                             + " (pitched voices pre-rolled, latency reported: 0)");
}

void MidiMappingService::releaseResources()
{
    juce::Logger::writeToLog("[MidiMappingService] releaseResources");
    engine_->release();
}

void MidiMappingService::getNextAudioBlock(const juce::AudioSourceChannelInfo& info)
{
    // Standalone audio callback. MIDI arrives through engine_->pushMidi().
    info.clearActiveBufferRegion();
    if (info.buffer == nullptr) return;
    const engine::HostInfo noHost;
    engine_->process(*info.buffer, info.startSample, info.numSamples, nullptr, noHost);
    applyAudioGate(*info.buffer, info.startSample, info.numSamples);   // standalone was ungated before N-18
}

void MidiMappingService::renderBlock(juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi,
                                     const engine::HostInfo& host) noexcept
{
    hostBpm_.store(host.valid && host.bpm > 0.0 && std::isfinite(host.bpm) ? host.bpm : 0.0, std::memory_order_relaxed);
    engine_->process(buffer, 0, buffer.getNumSamples(), &midi, host);
    applyAudioGate(buffer, 0, buffer.getNumSamples());
}

void MidiMappingService::renderBlockBypassed(juce::AudioBuffer<float>& buffer) noexcept
{
    engine_->processBypassed(buffer, 0, buffer.getNumSamples());
    applyAudioGate(buffer, 0, buffer.getNumSamples());
}

void MidiMappingService::setAudioGate(const std::atomic<bool>* allowed) noexcept
{
    audioGate_ = allowed;
    const bool open = allowed == nullptr || allowed->load();
    gate_.gain = open ? 1.0f : 0.0f;
    juce::Logger::writeToLog(juce::String("[MidiMappingService] setAudioGate: ")
                             + (allowed == nullptr ? "none" : (open ? "attached (open)" : "attached (closed)")));
}

void MidiMappingService::applyAudioGate(juce::AudioBuffer<float>& buffer, int start, int num) noexcept
{
    // Audio thread. N-18: a licence flip ramps over 5 ms instead of a hard
    // cut / hard start; while closed the region is silent.
    if (audioGate_ == nullptr) return;
    if (gate_.apply(buffer, start, num, audioGate_->load(std::memory_order_relaxed)))
        gateFlips_.fetch_add(1, std::memory_order_relaxed);   // per ramping block; the timer logs changes
}

double MidiMappingService::getTailLengthSeconds() const noexcept
{
    // Longest chop (a one-shot keeps sounding after its note-off) + the
    // release fade and pitch-shifter flush, capped.
    const double longest = maxSegmentSeconds_.load(std::memory_order_relaxed);
    return juce::jlimit(0.25, 60.0, longest + 0.25);
}

// ============================================================================
// Timer: notifications, host tempo, snapshot rebuild, garbage collection
// ============================================================================

void MidiMappingService::timerCallback()
{
    if (destroyed_.load()) return;
    drainNotifications();
    checkEngineLifecycle();
    logEngineDiagnostics();
    publishHostTempoIfNeeded();
    collectResampleResults();
    rebuildSnapshotIfNeeded(false);
    snapshots_.collectGarbage();
}

void MidiMappingService::drainNotifications()
{
    engine::Notification n;
    int guard = 0;
    while (guard++ < 2048 && engine_->popNotification(n))
    {
        switch (n.type)
        {
            case engine::NoteType::SegmentTriggered:
            {
                auto* payload = new juce::DynamicObject();
                payload->setProperty("stemIndex", n.i0);
                payload->setProperty("segmentIndex", n.i1);
                payload->setProperty("noteNumber", n.i2);
                payload->setProperty("duration", n.d0);
                bus_.publish("smode.segment.triggered", juce::var(payload));
                break;
            }
            case engine::NoteType::SegmentStopped:
            {
                auto* payload = new juce::DynamicObject();
                payload->setProperty("stemIndex", n.i0);
                payload->setProperty("segmentIndex", n.i1);
                payload->setProperty("noteNumber", n.i2);
                bus_.publish("smode.segment.stopped", juce::var(payload));
                break;
            }
            case engine::NoteType::TransportStopped:
                juce::Logger::writeToLog("[MidiMappingService] engine: transport stopped (gen="
                                         + juce::String(n.i0) + ")");
                if (transportUiActive_ && n.i0 == transportGen_)
                {
                    transportUiActive_ = false;
                    bus_.publish("smode.transport.stopped", juce::var());
                }
                break;
            case engine::NoteType::SequencerStopped:
                juce::Logger::writeToLog("[MidiMappingService] engine: sequencer stopped (reason="
                                         + juce::String(n.i0 == 0 ? "host transport stopped" : "all sound off / bypass")
                                         + ")");
                if (n.i1 == seqGen_) seqUiRunning_ = false;
                break;
            case engine::NoteType::LearnNote:
                if (params_.midiLearn.load() && midiLearnSMode_)
                {
                    juce::Logger::writeToLog("[MidiMappingService] S-Mode MIDI learn: note " + juce::String(n.i0)
                                             + " (" + noteNumberToName(n.i0) + ")");
                    auto* payload = new juce::DynamicObject();
                    payload->setProperty("noteNumber", n.i0);
                    payload->setProperty("noteName", noteNumberToName(n.i0));
                    bus_.publish("smode.midi.learn.assigned", juce::var(payload));
                    endMidiLearn();
                }
                break;
            case engine::NoteType::Controller:
                onControllerFromEngine(n.i0, n.i1, n.f0);
                break;
        }
    }

    const int dropped = engine_->getDroppedNotificationCount();
    if (dropped != lastDroppedNotes_)
    {
        juce::Logger::writeToLog("[MidiMappingService] notification FIFO overflow: "
                                 + juce::String(dropped - lastDroppedNotes_) + " UI event(s) dropped");
        lastDroppedNotes_ = dropped;
    }
}

void MidiMappingService::checkEngineLifecycle()
{
    // N-12: prepare() / release() (sample-rate or device change, host
    // re-activation) reset the engine's transport and sequencer without a
    // notification. Clear the UI flags so Play does not stay lit.
    const uint32_t gen = engine_->getLifecycleGeneration();
    if (gen == seenEngineGen_) return;
    juce::Logger::writeToLog("[MidiMappingService] engine re-prepared / released (generation "
                             + juce::String((int) seenEngineGen_) + " -> " + juce::String((int) gen)
                             + "), transportUi=" + juce::String(transportUiActive_ ? 1 : 0)
                             + " seqUi=" + juce::String(seqUiRunning_ ? 1 : 0));
    seenEngineGen_ = gen;
    // A Play / sequencer start issued AFTER the reset (same generation) is live.
    if (transportUiActive_ && transportStartEngineGen_ != gen)
    {
        transportUiActive_ = false;
        // A start queued before the reset could still apply afterwards: make
        // the engine agree with the UI.
        engine::Command c;
        c.type = engine::CmdType::TransportStop;
        pushCommand(c);
        bus_.publish("smode.transport.stopped", juce::var());
        juce::Logger::writeToLog("[MidiMappingService] transport UI flag cleared after engine reset (smode.transport.stopped published)");
    }
    if (seqUiRunning_ && seqStartEngineGen_ != gen)
    {
        seqUiRunning_ = false;
        engine::Command c;
        c.type = engine::CmdType::SeqStop;
        pushCommand(c);
        juce::Logger::writeToLog("[MidiMappingService] sequencer UI flag cleared after engine reset");
    }
}

void MidiMappingService::logEngineDiagnostics()
{
    // Audio-thread counters, logged here on change (logging gate: never on the audio thread).
    auto logDelta = [](int now, int& seen, const char* what)
    {
        if (now == seen) return;
        juce::Logger::writeToLog(juce::String("[MidiMappingService] engine: ") + what + " +"
                                 + juce::String(now - seen) + " (total " + juce::String(now) + ")");
        seen = now;
    };
    logDelta(engine_->getForcedKillCount(), seenForcedKills_,
             "voice pool full - forced cut(s) with 1.5 ms declick");
    logDelta(hostInfoRejected_.load(std::memory_order_relaxed), seenHostRejected_,
             "host BPM / PPQ rejected or clamped by the processor (non-finite / out of range)");
    logDelta(engine_->getRejectedHostInfoCount(), seenEngineHostRejected_,
             "host BPM / PPQ rejected or clamped by the engine");
    logDelta(engine_->getStaleDroppedCount(), seenStaleDropped_,
             ("stale UI trigger(s) dropped (threshold " + juce::String((int) engine_->getStaleThresholdMs()) + " ms)").toRawUTF8());
    logDelta(engine_->getBypassDroppedCount(), seenBypassDropped_, "trigger(s) ignored while host-bypassed");
    logDelta(gateFlips_.load(std::memory_order_relaxed), seenGateFlips_,
             "licence gate ramp block(s) (5 ms fade in / out)");
    const int deferred = engine_->getDeferredStretchCount();
    const auto nowMs = juce::Time::getMillisecondCounter();
    if (deferred != seenDeferredStretch_ && nowMs - lastDeferredLogMs_ > 1000)
    {
        lastDeferredLogMs_ = nowMs;
        logDelta(deferred, seenDeferredStretch_, "pitch-shifter entries deferred to spread CPU (voice-chunks)");
    }
}

void MidiMappingService::publishHostTempoIfNeeded()
{
    const bool requested = tempoRequest_.exchange(false);
    double bpm = hostBpm_.load(std::memory_order_relaxed);
    if (bpm <= 0.0 && requested)
    {
        bpm = 120.0;
        juce::Logger::writeToLog("[MidiMappingService] No host BPM available - falling back to 120 BPM");
    }
    if (bpm > 0.0 && (requested || std::abs(bpm - lastPublishedBpm_) > 0.01))
    {
        lastPublishedBpm_ = bpm;
        juce::Logger::writeToLog("[MidiMappingService] Publishing host.tempo: " + juce::String(bpm, 3));
        bus_.publish("host.tempo", juce::var(bpm));
    }
}

void MidiMappingService::syncSnapshotNow()
{
    collectResampleResults();
    rebuildSnapshotIfNeeded(false);
}

void MidiMappingService::rebuildSnapshotIfNeeded(bool force)
{
    const uint64_t hash   = model_ != nullptr ? model_->computePlaybackHash() : 0;
    const uint64_t fxHash = model_ != nullptr ? engine::computeSegmentFxHash(*model_) : 0;
    const double hostRate = hostRate_.load();
    if (! force && ! snapshotDirty_ && hash == lastModelHash_ && fxHash == lastFxHash_
        && hostRate == lastBuiltHostRate_)
        return;

    lastModelHash_     = hash;
    lastFxHash_        = fxHash;
    lastBuiltHostRate_ = hostRate;
    snapshotDirty_     = false;

    if (model_ == nullptr)
    {
        snapshots_.publish(std::make_shared<engine::Snapshot>());
        { std::scoped_lock lock(exportMutex_); exportSnap_.reset(); }
        maxSegmentSeconds_.store(0.0);
        juce::Logger::writeToLog("[MidiMappingService] snapshot: model detached -> empty snapshot");
        return;
    }

    // A23: use a host-rate resampled copy of each stem when one is ready; until
    // then the engine converts on the fly (4-point Hermite).
    std::array<engine::StemOverride, engine::kMaxStems> overrides {};
    int resampledUsed = 0;
    const double modelRate = model_->getSampleRate() > 0.0 ? model_->getSampleRate() : 44100.0;
    if (hostRate > 0.0 && std::abs(hostRate - modelRate) > 0.5)
    {
        for (int s = 0; s < juce::jmin(model_->getStemCount(), engine::kMaxStems); ++s)
        {
            auto src = model_->getSharedStemAudio(s);
            if (src == nullptr) continue;
            bool found = false;
            for (const auto& r : resampled_)
            {
                if (r.key == src.get() && r.dstRate == hostRate && r.src.lock() == src)
                {
                    overrides[(size_t) s] = { r.out, hostRate };
                    ++resampledUsed;
                    found = true;
                    break;
                }
            }
            if (! found)
                scheduleResample(src, modelRate, hostRate);
        }
    }

    auto live = model_->buildPlaybackSnapshot(laneDescs_, &overrides);
    engine::fillSegmentFx(*live, *model_, laneDescs_);   // per-chop pitch/gain/pan/reverse/fades + lane resolution
    const int segs  = (int) live->segs.size();
    const int lanes = (int) live->lanes.size();
    const int stems = live->stemCount;
    maxSegmentSeconds_.store(live->maxSegmentSeconds);
    snapshots_.publish(live);

    auto exp = model_->buildPlaybackSnapshot(laneDescs_, nullptr);
    engine::fillSegmentFx(*exp, *model_, laneDescs_);
    { std::scoped_lock lock(exportMutex_); exportSnap_ = exp; }

    // Logging gate: rebuilds can happen at 30 Hz while dragging a chop edge.
    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastRebuildLogMs_ > 1000)
    {
        lastRebuildLogMs_ = now;
        juce::Logger::writeToLog("[MidiMappingService] snapshot seq=" + juce::String((juce::int64) snapshots_.currentSeq())
                                 + " stems=" + juce::String(stems) + " segs=" + juce::String(segs)
                                 + " lanes=" + juce::String(lanes)
                                 + " hostRate=" + juce::String(hostRate) + " modelRate=" + juce::String(modelRate)
                                 + " resampledStems=" + juce::String(resampledUsed)
                                 + " retired=" + juce::String(snapshots_.retiredCount()));
    }
}

void MidiMappingService::scheduleResample(const std::shared_ptr<const juce::AudioBuffer<float>>& src,
                                          double srcRate, double dstRate)
{
    if (resamplePool_ == nullptr || src == nullptr) return;
    const auto key = std::make_pair(src.get(), (int) std::lround(dstRate));
    if (resamplePending_.count(key) > 0) return;
    resamplePending_.insert(key);

    juce::Logger::writeToLog("[MidiMappingService] resample job queued: " + juce::String(src->getNumSamples())
                             + " samples " + juce::String(srcRate) + " -> " + juce::String(dstRate) + " Hz");
    auto inbox = resampleInbox_;
    auto abort = resampleAbort_;
    resamplePool_->addJob([src, srcRate, dstRate, inbox, abort]()
    {
        auto out = engine::resampleStem(*src, srcRate, dstRate, [abort] { return abort->load(); });
        if (out == nullptr) return;
        ResampledStem r;
        r.src = src;
        r.key = src.get();
        r.out = std::move(out);
        r.dstRate = dstRate;
        std::scoped_lock lock(inbox->lock);
        inbox->done.push_back(std::move(r));
    });
}

void MidiMappingService::collectResampleResults()
{
    std::vector<ResampledStem> done;
    {
        std::scoped_lock lock(resampleInbox_->lock);
        done.swap(resampleInbox_->done);
    }
    const double hostRate = hostRate_.load();
    // Drop cache entries whose source is gone or whose rate is stale.
    resampled_.erase(std::remove_if(resampled_.begin(), resampled_.end(),
                                    [hostRate](const ResampledStem& r) { return r.src.expired() || r.dstRate != hostRate; }),
                     resampled_.end());
    for (auto& r : done)
    {
        resamplePending_.erase(std::make_pair(r.key, (int) std::lround(r.dstRate)));
        if (r.src.expired() || r.dstRate != hostRate) continue;
        juce::Logger::writeToLog("[MidiMappingService] resample done: " + juce::String(r.out->getNumSamples())
                                 + " samples @ " + juce::String(r.dstRate) + " Hz");
        resampled_.push_back(std::move(r));
        snapshotDirty_ = true;
    }
}

// ============================================================================
// Model / notes
// ============================================================================

void MidiMappingService::setSModeModel(SModeSegmentModel* model)
{
    model_ = model;
    if (model == nullptr)
    {
        engine::Command c;
        c.type = engine::CmdType::AllSoundOff;
        pushCommand(c);
        c.type = engine::CmdType::SeqStop;
        pushCommand(c);
        transportUiActive_ = false;
        seqUiRunning_ = false;
        for (auto& sp : params_.stems) sp.pitchSemis.store(0.0f);
        rebuildSnapshotIfNeeded(true);
        juce::Logger::writeToLog("[MidiMappingService] setSModeModel(nullptr): voices released, transport + "
                                 "sequencer stopped, empty snapshot published, stem pitch reset");
    }
    else
    {
        rebuildSnapshotIfNeeded(true);
        juce::Logger::writeToLog("[MidiMappingService] setSModeModel: wired to model with "
                                 + juce::String(model->getStemCount()) + " stems");
    }
}

void MidiMappingService::handleNoteOn(int /*channel*/, int noteNumber, float velocity)
{
    if (noteNumber < 0 || noteNumber > 127) return;
    syncSnapshotNow();
    engine::Command c;
    c.type = engine::CmdType::NoteOn;
    c.i0 = noteNumber;
    c.f0 = juce::jlimit(0.0f, 1.0f, velocity);
    pushCommand(c);
}

void MidiMappingService::handleNoteOff(int /*channel*/, int noteNumber)
{
    if (noteNumber < 0 || noteNumber > 127) return;
    engine::Command c;
    c.type = engine::CmdType::NoteOff;
    c.i0 = noteNumber;
    pushCommand(c);
}

juce::String MidiMappingService::noteNumberToName(int noteNumber)
{
    // JUCE default (octaveForMiddleC = 3): 60 = C3, 36 = C1, 0 = C-2.
    return juce::MidiMessage::getMidiNoteName(juce::jlimit(0, 127, noteNumber), true, true, kOctaveForMiddleC);
}

void MidiMappingService::setGlobalTransposeSemitones(float semis)
{
    const float v = juce::jlimit(-24.0f, 24.0f, semis);
    if (std::abs(params_.globalSemis.load() - v) < 1.0e-4f) return;
    params_.globalSemis.store(v, std::memory_order_relaxed);
    juce::Logger::writeToLog("[MidiMappingService] global transpose (grid LOCK) = " + juce::String(v, 2) + " st");
}

void MidiMappingService::beginMidiLearnForSMode()
{
    juce::Logger::writeToLog("[MidiMappingService] beginMidiLearnForSMode");
    midiLearnSMode_ = true;
    params_.midiLearn.store(true);
    bus_.publish("smode.midi.learn.waiting", juce::var());
}

void MidiMappingService::endMidiLearn()
{
    juce::Logger::writeToLog("[MidiMappingService] endMidiLearn");
    params_.midiLearn.store(false);
    midiLearnSMode_ = false;
}

// ============================================================================
// Standalone MIDI device input (MIDI thread -> lock-free FIFO, A27)
// ============================================================================

void MidiMappingService::connectToDeviceManager(juce::AudioDeviceManager* dm)
{
    if (dm == nullptr) return;
    disconnectFromDeviceManager();
    connectedDeviceManager_ = dm;
    dm->addMidiInputDeviceCallback({}, this);
    audioPlayer_.setSource(this);
    dm->addAudioCallback(&audioPlayer_);
    juce::Logger::writeToLog("[MidiMappingService] Connected to AudioDeviceManager for MIDI input + audio output");
}

void MidiMappingService::disconnectFromDeviceManager()
{
    if (connectedDeviceManager_ != nullptr)
    {
        connectedDeviceManager_->removeAudioCallback(&audioPlayer_);
        audioPlayer_.setSource(nullptr);
        connectedDeviceManager_->removeMidiInputDeviceCallback({}, this);
        connectedDeviceManager_ = nullptr;
        juce::Logger::writeToLog("[MidiMappingService] Disconnected from AudioDeviceManager");
    }
}

void MidiMappingService::setMidiChannelFilter(int channel)
{
    const int clamped = juce::jlimit(0, 16, channel);
    channelFilter_.store(clamped, std::memory_order_relaxed);
    juce::Logger::writeToLog("[MidiMappingService] Channel filter set to: "
                             + (clamped == 0 ? juce::String("ALL") : juce::String(clamped)));
}

void MidiMappingService::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message)
{
    if (destroyed_.load(std::memory_order_acquire)) return;
    const int filter = channelFilter_.load(std::memory_order_relaxed);
    if (filter > 0 && message.getChannel() != filter) return;
    const int size = message.getRawDataSize();
    if (size < 1 || size > 3) return;   // short messages only (no sysex)
    const auto* d = message.getRawData();
    engine::RawMidi m;
    m.b0 = d[0];
    m.b1 = size > 1 ? d[1] : (uint8_t) 0;
    m.b2 = size > 2 ? d[2] : (uint8_t) 0;
    engine_->pushMidi(m);   // drained by the audio thread at the next block
}

// ============================================================================
// Computer keyboard MIDI
// ============================================================================

namespace
{
    // Ableton-style layout: white keys on the home row, black keys above.
    constexpr int kNoteKeyCodes[MidiMappingService::kNumNoteKeys] =
        { 'A', 'W', 'S', 'E', 'D', 'F', 'T', 'G', 'Y', 'H', 'U', 'J', 'K', 'O', 'L', 'P', ';' };

    int noteKeyIndex(const juce::KeyPress& key)
    {
        int code = key.getKeyCode();
        if (code >= 'a' && code <= 'z') code -= 'a' - 'A';
        for (int i = 0; i < MidiMappingService::kNumNoteKeys; ++i)
            if (kNoteKeyCodes[i] == code) return i;
        return -1;
    }
}

int MidiMappingService::keyToSemitoneOffset(const juce::KeyPress& key)
{
    return noteKeyIndex(key);   // index == semitone offset from the base note
}

bool MidiMappingService::isOctaveKey(const juce::KeyPress& key)
{
    const int c = key.getKeyCode();
    return c == 'Z' || c == 'z' || c == 'X' || c == 'x';
}

int MidiMappingService::keyToMidiNote(const juce::KeyPress& key) const
{
    const int off = noteKeyIndex(key);
    if (off < 0) return -1;
    const int n = getComputerKeyboardBaseNote() + off;
    return (n >= 0 && n <= 127) ? n : -1;
}

void MidiMappingService::shiftComputerKeyboardOctave(int delta)
{
    const int base = juce::jlimit(0, 108, getComputerKeyboardBaseNote() + 12 * delta);
    kbBaseNote_.store(base, std::memory_order_relaxed);
    juce::Logger::writeToLog("[MidiMappingService] computer keyboard octave: A = " + noteNumberToName(base)
                             + " (" + noteNumberToName(base) + " - " + noteNumberToName(getComputerKeyboardTopNote()) + ")");
}

bool MidiMappingService::handleKeyPress(const juce::KeyPress& key)
{
    if (! computerKeyboardEnabled_.load(std::memory_order_relaxed)) return false;
    if (isOctaveKey(key))
    {
        const int c = key.getKeyCode();
        shiftComputerKeyboardOctave((c == 'Z' || c == 'z') ? -1 : +1);
        return true;
    }
    const int idx = noteKeyIndex(key);
    if (idx < 0) return false;
    if (heldKeyNote_[(size_t) idx] >= 0)
        return true;   // OS autorepeat: swallow
    const int note = keyToMidiNote(key);
    if (note < 0) return true;
    heldKeyNote_[(size_t) idx] = note;
    juce::Logger::writeToLog("[MidiMappingService] computer-keyboard keyDown note=" + juce::String(note)
                             + " (" + noteNumberToName(note) + ")");
    handleNoteOn(1, note, 0.8f);
    return true;
}

bool MidiMappingService::handleKeyRelease(const juce::KeyPress& key)
{
    if (! computerKeyboardEnabled_.load(std::memory_order_relaxed)) return false;
    const int idx = noteKeyIndex(key);
    if (idx < 0) return false;
    const int note = heldKeyNote_[(size_t) idx];
    if (note < 0) return false;
    heldKeyNote_[(size_t) idx] = -1;
    juce::Logger::writeToLog("[MidiMappingService] computer-keyboard keyUp note=" + juce::String(note));
    handleNoteOff(1, note);
    return true;
}

bool MidiMappingService::releaseUnheldComputerKeys()
{
    bool any = false;
    for (int i = 0; i < kNumNoteKeys; ++i)
    {
        const int note = heldKeyNote_[(size_t) i];
        if (note < 0 || juce::KeyPress::isKeyCurrentlyDown(kNoteKeyCodes[i])) continue;
        heldKeyNote_[(size_t) i] = -1;
        juce::Logger::writeToLog("[MidiMappingService] computer-keyboard keyUp (polled) note=" + juce::String(note));
        handleNoteOff(1, note);
        any = true;
    }
    return any;
}

void MidiMappingService::resetComputerKeyboardHeld()
{
    for (auto& n : heldKeyNote_)
        if (n >= 0) { handleNoteOff(1, n); n = -1; }
}

// ============================================================================
// Audition / transport / sequencer (message thread -> commands)
// ============================================================================

void MidiMappingService::playSegment(int stemIdx, int segIdx, bool looping, float velocity)
{
    syncSnapshotNow();
    engine::Command c;
    c.type = engine::CmdType::PlaySegment;
    c.i0 = stemIdx;
    c.i1 = segIdx;
    c.b0 = looping;
    c.f0 = juce::jlimit(0.0f, 1.0f, velocity);
    pushCommand(c);
    juce::Logger::writeToLog("[MidiMappingService] playSegment stem=" + juce::String(stemIdx)
                             + " seg=" + juce::String(segIdx) + " loop=" + juce::String(looping ? 1 : 0)
                             + " pitch=" + juce::String(getStemPitchSemitones(stemIdx), 2));
}

void MidiMappingService::auditionSlot(const PatternSlot& slot)
{
    if (slot.empty) return;
    syncSnapshotNow();
    engine::Command c;
    c.type = engine::CmdType::PlaySlot;
    c.i0 = slot.stemIndex;
    c.i1 = slot.segmentIndex;
    c.f0 = slot.trimStart;
    c.f1 = slot.trimEnd;
    c.f2 = slot.speed;
    c.f3 = slot.pitchSemitones;
    c.f4 = slot.velocity;
    c.b0 = slot.reverse;
    pushCommand(c);
    juce::Logger::writeToLog("[MidiMappingService] auditionSlot stem=" + juce::String(slot.stemIndex)
                             + " seg=" + juce::String(slot.segmentIndex)
                             + " speed=" + juce::String(slot.speed, 2)
                             + " pitch=" + juce::String(slot.pitchSemitones, 2)
                             + " rev=" + juce::String(slot.reverse ? 1 : 0));
}

void MidiMappingService::stopAllVoices()
{
    engine::Command c;
    c.type = engine::CmdType::StopVoices;
    pushCommand(c);
    juce::Logger::writeToLog("[MidiMappingService] stopAllVoices (queued, 3 ms release)");
}

void MidiMappingService::startSModeTransport(double startTimeSeconds)
{
    syncSnapshotNow();
    engine::Command c;
    c.type = engine::CmdType::TransportStart;
    c.d0 = juce::jmax(0.0, startTimeSeconds);
    c.i0 = ++transportGen_;
    pushCommand(c);
    transportUiActive_ = true;
    transportStartEngineGen_ = engine_->getLifecycleGeneration();
    juce::Logger::writeToLog("[MidiMappingService] Transport START at " + juce::String(startTimeSeconds, 2) + "s");
}

void MidiMappingService::stopSModeTransport()
{
    engine::Command c;
    c.type = engine::CmdType::TransportStop;
    pushCommand(c);
    transportUiActive_ = false;
    juce::Logger::writeToLog("[MidiMappingService] Transport STOP");
}

void MidiMappingService::setSequencerLanes(const std::vector<LaneInput>& lanes)
{
    std::vector<engine::LaneDesc> descs;
    descs.reserve(lanes.size());
    for (const auto& l : lanes)
    {
        engine::LaneDesc d;
        d.cellsPerBeat = juce::jlimit(1, 4, l.cellsPerBeat);
        d.numCells = juce::jmax(1, l.numBars * 4 * d.cellsPerBeat);
        d.muted    = l.muted;
        d.slots.reserve(l.slots.size());
        for (const auto& s : l.slots) d.slots.push_back(toSlotDesc(s));
        descs.push_back(std::move(d));
    }
    if (sameLanes(descs, laneDescs_)) return;
    laneDescs_ = std::move(descs);
    snapshotDirty_ = true;
    rebuildSnapshotIfNeeded(false);
}

void MidiMappingService::setSequencerBpm(double bpm)
{
    if (bpm > 0.0)
        params_.seqBpm.store(juce::jlimit(20.0, 400.0, bpm));
}

void MidiMappingService::startSequencer(uint32_t laneMask)
{
    syncSnapshotNow();
    engine::Command c;
    c.type = engine::CmdType::SeqStart;
    c.i0 = (int) laneMask;
    c.i1 = ++seqGen_;
    c.d0 = 0.0;   // follow params.seqBpm (or the host tempo while the DAW plays)
    pushCommand(c);
    seqUiRunning_ = true;
    seqStartEngineGen_ = engine_->getLifecycleGeneration();
    juce::Logger::writeToLog("[MidiMappingService] sequencer START mask=0x" + juce::String::toHexString((int) laneMask)
                             + " bpm=" + juce::String(params_.seqBpm.load(), 2)
                             + " (host-synced while the DAW transport plays)");
}

void MidiMappingService::stopSequencer()
{
    engine::Command c;
    c.type = engine::CmdType::SeqStop;
    pushCommand(c);
    seqUiRunning_ = false;
    juce::Logger::writeToLog("[MidiMappingService] sequencer STOP");
}

void MidiMappingService::getSequencerLaneState(int lane, int& playingSlot, float& slotProgress, float& playhead) const
{
    engine_->getLaneState(lane, playingSlot, slotProgress, playhead);
}

std::unique_ptr<juce::AudioBuffer<float>> MidiMappingService::renderSequencerOffline(uint32_t laneMask,
                                                                                     double& outSampleRate,
                                                                                     std::function<void(float)> progress)
{
    if (! isAudioAllowedForExport("renderSequencerOffline"))
        return nullptr;
    std::shared_ptr<const engine::Snapshot> snap;
    { std::scoped_lock lock(exportMutex_); snap = exportSnap_; }
    if (snap == nullptr) return nullptr;

    int cells = 0;   // length in BEATS of the longest lane (cells / cellsPerBeat, B18)
    for (int l = 0; l < (int) snap->lanes.size() && l < engine::kMaxLanes; ++l)
        if ((laneMask & (1u << l)) != 0 && ! snap->lanes[(size_t) l].muted)
        {
            const auto& ln = snap->lanes[(size_t) l];
            const int cpb = juce::jmax(1, ln.cellsPerBeat);
            cells = juce::jmax(cells, (ln.numCells + cpb - 1) / cpb);
        }
    if (cells <= 0) return nullptr;

    const double bpm = params_.seqBpm.load();
    outSampleRate = snap->modelRate;
    juce::Logger::writeToLog("[MidiMappingService] renderSequencerOffline mask=0x" + juce::String::toHexString((int) laneMask)
                             + " cells=" + juce::String(cells) + " bpm=" + juce::String(bpm, 2)
                             + " sr=" + juce::String(outSampleRate) + " (same engine as playback)");
    auto out = engine::renderSequence(params_, *snap, snap->modelRate, laneMask, cells, bpm, progress);
    // The licence can expire while a queued export renders: re-check before handing audio out.
    if (out != nullptr && ! isAudioAllowedForExport("renderSequencerOffline (after render)"))
        return nullptr;
    return out;
}

bool MidiMappingService::isAudioAllowedForExport(const char* what) const
{
    // N-18: exports produce audio just like playback - same licence gate.
    if (audioGate_ == nullptr || audioGate_->load(std::memory_order_relaxed))
        return true;
    juce::Logger::writeToLog(juce::String("[MidiMappingService] ") + what
                             + " REFUSED: licence does not allow audio (unlicensed / expired / locked) - no audio rendered");
    return false;
}

std::unique_ptr<juce::AudioBuffer<float>> MidiMappingService::renderSegmentOffline(int stemIdx, int segIdx,
                                                                                   double& outSampleRate)
{
    if (! isAudioAllowedForExport("renderSegmentOffline"))
        return nullptr;
    std::shared_ptr<const engine::Snapshot> snap;
    { std::scoped_lock lock(exportMutex_); snap = exportSnap_; }
    if (snap == nullptr) return nullptr;
    outSampleRate = snap->modelRate;
    juce::Logger::writeToLog("[MidiMappingService] renderSegmentOffline stem=" + juce::String(stemIdx)
                             + " seg=" + juce::String(segIdx) + " pitch=" + juce::String(getStemPitchSemitones(stemIdx), 2));
    auto out = engine::renderSegment(params_, *snap, snap->modelRate, stemIdx, segIdx);
    if (out != nullptr && ! isAudioAllowedForExport("renderSegmentOffline (after render)"))
        return nullptr;
    return out;
}

// ============================================================================
// Per-stem pitch / modulation / velocity
// ============================================================================

float MidiMappingService::getStemPitchSemitones(int stemIdx) const
{
    if (stemIdx < 0 || stemIdx >= MAX_STEMS) return 0.0f;
    return params_.stems[(size_t) stemIdx].pitchSemis.load(std::memory_order_relaxed);
}

void MidiMappingService::setStemPitchSemitones(int stemIdx, float semitones)
{
    if (stemIdx < 0 || stemIdx >= MAX_STEMS)
    {
        juce::Logger::writeToLog("[MidiMappingService] setStemPitchSemitones invalid stemIdx=" + juce::String(stemIdx));
        return;
    }
    const float clamped = juce::jlimit(-12.0f, 12.0f, semitones);
    auto& slot = params_.stems[(size_t) stemIdx].pitchSemis;
    if (std::abs(slot.load() - clamped) < 1.0e-4f) return;
    slot.store(clamped, std::memory_order_relaxed);

    juce::Logger::writeToLog("[MidiMappingService] setStemPitchSemitones stem=" + juce::String(stemIdx)
                             + " semi=" + juce::String(clamped, 2) + " (realtime)");
    auto* payload = new juce::DynamicObject();
    payload->setProperty("stemIndex", stemIdx);
    payload->setProperty("semi", clamped);
    bus_.publish("smode.stem.pitch.changed", juce::var(payload));
}

#define SAMPLAR_STEM_GUARD(ret) if (stemIdx < 0 || stemIdx >= MAX_STEMS) return ret;

void MidiMappingService::setStemPitchBendDest(int stemIdx, ModDestination d)  { SAMPLAR_STEM_GUARD() params_.stems[(size_t) stemIdx].bendDest.store((uint8_t) d); }
void MidiMappingService::setStemModWheelDest(int stemIdx, ModDestination d)   { SAMPLAR_STEM_GUARD() params_.stems[(size_t) stemIdx].wheelDest.store((uint8_t) d); }
void MidiMappingService::setStemAftertouchDest(int stemIdx, ModDestination d) { SAMPLAR_STEM_GUARD() params_.stems[(size_t) stemIdx].atDest.store((uint8_t) d); }

MidiMappingService::ModDestination MidiMappingService::getStemPitchBendDest(int stemIdx) const
{ SAMPLAR_STEM_GUARD(ModDestination::None) return (ModDestination) params_.stems[(size_t) stemIdx].bendDest.load(); }
MidiMappingService::ModDestination MidiMappingService::getStemModWheelDest(int stemIdx) const
{ SAMPLAR_STEM_GUARD(ModDestination::None) return (ModDestination) params_.stems[(size_t) stemIdx].wheelDest.load(); }
MidiMappingService::ModDestination MidiMappingService::getStemAftertouchDest(int stemIdx) const
{ SAMPLAR_STEM_GUARD(ModDestination::None) return (ModDestination) params_.stems[(size_t) stemIdx].atDest.load(); }

void MidiMappingService::setStemPitchBendRange(int stemIdx, float r)  { SAMPLAR_STEM_GUARD() params_.stems[(size_t) stemIdx].bendRange.store(juce::jlimit(0.0f, 48.0f, r)); }
void MidiMappingService::setStemModWheelRange(int stemIdx, float r)   { SAMPLAR_STEM_GUARD() params_.stems[(size_t) stemIdx].wheelRange.store(juce::jlimit(0.0f, 48.0f, r)); }
void MidiMappingService::setStemAftertouchRange(int stemIdx, float r) { SAMPLAR_STEM_GUARD() params_.stems[(size_t) stemIdx].atRange.store(juce::jlimit(0.0f, 48.0f, r)); }
float MidiMappingService::getStemPitchBendRange(int stemIdx) const  { SAMPLAR_STEM_GUARD(0.0f) return params_.stems[(size_t) stemIdx].bendRange.load(); }
float MidiMappingService::getStemModWheelRange(int stemIdx) const   { SAMPLAR_STEM_GUARD(0.0f) return params_.stems[(size_t) stemIdx].wheelRange.load(); }
float MidiMappingService::getStemAftertouchRange(int stemIdx) const { SAMPLAR_STEM_GUARD(0.0f) return params_.stems[(size_t) stemIdx].atRange.load(); }

void MidiMappingService::setStemVelocityCurve(int stemIdx, VelocityCurve c) { SAMPLAR_STEM_GUARD() params_.stems[(size_t) stemIdx].velCurve.store((uint8_t) c); }
MidiMappingService::VelocityCurve MidiMappingService::getStemVelocityCurve(int stemIdx) const
{ SAMPLAR_STEM_GUARD(VelocityCurve::Linear) return (VelocityCurve) params_.stems[(size_t) stemIdx].velCurve.load(); }

#undef SAMPLAR_STEM_GUARD

float MidiMappingService::applyVelocityCurve(float v, VelocityCurve c)
{
    return engine::applyVelCurve(v, (engine::VelCurve) c);
}

// ============================================================================
// CC learn / bindings (message thread; CCs arrive from the engine FIFO)
// ============================================================================

void MidiMappingService::updateCcForwarding()
{
    params_.forwardCcToUi.store(ccLearnActive_ || ! ccBindings_.empty());
}

void MidiMappingService::onControllerFromEngine(int channel, int cc, float v)
{
    if (ccLearnActive_)
    {
        const auto target = ccLearnPendingTarget_;
        ccLearnPendingTarget_ = {};
        ccLearnActive_ = false;
        // One target per (channel, CC), and one CC per target: a re-learn
        // replaces both the CC's old binding and the target's old CC.
        ccBindings_.erase(std::remove_if(ccBindings_.begin(), ccBindings_.end(),
                              [channel, cc, &target](const CcBindingInfo& b)
                              { return (b.cc == cc && b.channel == channel) || b.target == target; }),
                          ccBindings_.end());
        ccBindings_.push_back({ channel, cc, target });
        ++ccGeneration_;
        updateCcForwarding();
        juce::Logger::writeToLog("[MidiMappingService] CC learn bound: ch=" + juce::String(channel)
                                 + " cc=" + juce::String(cc) + " target=" + target
                                 + " (" + juce::String((int) ccBindings_.size()) + " binding(s))");
        auto* p = new juce::DynamicObject();
        p->setProperty("channel", channel);
        p->setProperty("cc", cc);
        p->setProperty("label", target);
        bus_.publish("midi.cc.bound", juce::var(p));
    }
    for (const auto& b : ccBindings_)
    {
        if (b.cc != cc || (b.channel != 0 && b.channel != channel))
            continue;
        const auto it = ccTargets_.find(b.target);
        if (it != ccTargets_.end() && it->second)
            it->second(v);
        else
            applyBuiltInCcTarget(b.target, v);   // inert unless built in (editor-closed safe)
    }
}

namespace
{
    // "stem<N>.pitch" -> N, or -1.
    int parseStemPitchTarget(const juce::String& target)
    {
        if (! target.startsWith("stem") || ! target.endsWith(".pitch")) return -1;
        const auto num = target.substring(4, target.length() - 6);
        if (num.isEmpty() || ! num.containsOnly("0123456789") || num.length() > 2) return -1;
        const int idx = num.getIntValue();
        return idx >= 0 && idx < MidiMappingService::MAX_STEMS ? idx : -1;
    }
}

bool MidiMappingService::isBuiltInCcTarget(const juce::String& target)
{
    return parseStemPitchTarget(target) >= 0;
}

bool MidiMappingService::applyBuiltInCcTarget(const juce::String& target, float value01)
{
    const int stem = parseStemPitchTarget(target);
    if (stem < 0 || ! std::isfinite(value01)) return false;
    const float semis = (float) juce::roundToInt(-12.0f + 24.0f * juce::jlimit(0.0f, 1.0f, value01));
    if (std::abs(getStemPitchSemitones(stem) - semis) < 1.0e-4f) return true;
    setStemPitchSemitones(stem, semis);
    return true;
}

int MidiMappingService::clearCcBindingsForTarget(const juce::String& target)
{
    const auto before = ccBindings_.size();
    ccBindings_.erase(std::remove_if(ccBindings_.begin(), ccBindings_.end(),
                          [&target](const CcBindingInfo& b) { return b.target == target; }),
                      ccBindings_.end());
    const int removed = (int) (before - ccBindings_.size());
    if (ccLearnActive_ && ccLearnPendingTarget_ == target)
    {
        ccLearnActive_ = false;
        ccLearnPendingTarget_ = {};
    }
    ++ccGeneration_;
    updateCcForwarding();
    juce::Logger::writeToLog("[MidiMappingService] CC bindings cleared for " + target + ": " + juce::String(removed));
    return removed;
}

juce::String MidiMappingService::describeCcBindings(const juce::String& target) const
{
    juce::StringArray parts;
    for (const auto& b : ccBindings_)
        if (b.target == target)
            parts.add("CC " + juce::String(b.cc) + (b.channel == 0 ? juce::String(" any ch") : " ch " + juce::String(b.channel)));
    return parts.joinIntoString(", ");
}

void MidiMappingService::registerCcTarget(const juce::String& target, std::function<void(float)> apply)
{
    if (target.isEmpty()) return;
    ccTargets_[target] = std::move(apply);
}

void MidiMappingService::unregisterCcTarget(const juce::String& target)
{
    ccTargets_.erase(target);
}

void MidiMappingService::beginCcLearn(const juce::String& target, std::function<void(float)> apply)
{
    if (target.isEmpty())
    {
        juce::Logger::writeToLog("[MidiMappingService] CC learn ignored: empty target");
        return;
    }
    if (apply) registerCcTarget(target, std::move(apply));
    ccLearnPendingTarget_ = target;
    ccLearnActive_ = true;
    ++ccGeneration_;
    updateCcForwarding();
    juce::Logger::writeToLog("[MidiMappingService] CC learn ARMED: " + target);
}

void MidiMappingService::endCcLearn()
{
    ccLearnPendingTarget_ = {};
    ccLearnActive_ = false;
    ++ccGeneration_;
    updateCcForwarding();
    juce::Logger::writeToLog("[MidiMappingService] CC learn cancelled");
}

void MidiMappingService::clearCcBindings()
{
    ccBindings_.clear();
    ++ccGeneration_;
    updateCcForwarding();
    juce::Logger::writeToLog("[MidiMappingService] CC bindings cleared");
}

std::vector<MidiMappingService::CcBindingInfo> MidiMappingService::getCcBindings() const
{
    return ccBindings_;
}

void MidiMappingService::setCcBindings(const std::vector<CcBindingInfo>& bindings)
{
    std::vector<CcBindingInfo> valid;
    int dropped = 0;
    for (const auto& b : bindings)
    {
        const bool ok = b.channel >= 0 && b.channel <= 16 && b.cc >= 0 && b.cc <= 127 && b.target.isNotEmpty();
        if (ok) valid.push_back(b); else ++dropped;
    }
    ccBindings_ = std::move(valid);
    ++ccGeneration_;
    updateCcForwarding();
    juce::Logger::writeToLog("[MidiMappingService] CC bindings restored: " + juce::String((int) ccBindings_.size())
                             + (dropped > 0 ? " (" + juce::String(dropped) + " invalid dropped)" : juce::String()));
}

} // namespace samplar
