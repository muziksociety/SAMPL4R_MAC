#pragma once

// SAMPLAR realtime playback engine (A1/A2/A3/A10-A15/A22/A25-A28, B26/B36).
//
// ONE renderer for every playback path: DAW MIDI, computer keyboard, segment
// audition, sequencer lanes, the S-Mode stem transport and offline export
// (drag WAV). An offline export is just another PlaybackEngine instance fed
// the same snapshot and parameters, so what you hear is what you export.
//
// Threading
//   audio thread  : process() / processBypassed() only. No allocation, no
//                   locks, no logging, no std::function, no shared_ptr.
//   other threads : prepare()/release() (audio stopped), pushCommand(),
//                   pushMidi() (one standalone MIDI thread), popNotification()
//                   (message thread), publishSnapshot() + getReclaimBound()
//                   (SnapshotManager), UI getters (atomics).
//
// Voices play a [start,end) window of an immutable shared stem buffer that
// lives in a Snapshot; a voice remembers the snapshot sequence number it was
// started from and the engine reports the oldest one still in use, so the
// SnapshotManager frees old snapshots on the message thread only.

#include "EngineTypes.h"
#include "EngineSnapshot.h"
#include "RtFifo.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace samplar {
namespace engine {

struct StretchHolder;   // wraps signalsmith::stretch::SignalsmithStretch<float> (pimpl)
struct PlaybackEngineTestAccess;   // EngineTests only: drives one block step by step

class PlaybackEngine
{
public:
    explicit PlaybackEngine(const EngineParams& params);
    ~PlaybackEngine();

    // -- lifecycle (never concurrent with process) ---------------------------
    void   prepare(double sampleRate, int maxBlockSize);
    void   release();
    /** Owner teardown only (the audio thread can no longer run this engine):
        moves the per-voice time-stretchers (the large FFT buffers) out so the
        owner can free them off the message thread. Leaves the engine unprepared. */
    std::shared_ptr<void> detachStretchersForRelease();
    bool   isPrepared() const noexcept       { return prepared_.load(std::memory_order_acquire); }
    double getSampleRate() const noexcept    { return sampleRate_; }
    int    getMaxBlockSize() const noexcept  { return maxBlock_; }
    /** Input frames a pitched voice needs ahead of its output (outputSeek). */
    int    getStretchSeekLength() const noexcept { return seekLen_; }

    // -- snapshot RCU --------------------------------------------------------
    void publishSnapshot(const Snapshot* s) noexcept { published_.store(s, std::memory_order_seq_cst); }
    /** Snapshots with seq < the returned value are no longer referenced by the
     *  audio thread (call AFTER publishing the newer snapshot). */
    uint64_t getReclaimBound() const noexcept;

    // -- UI -> audio ---------------------------------------------------------
    bool pushCommand(const Command& c) noexcept   { return commands_.push(c); }
    static constexpr int kCommandCapacity = 512;
    int  getQueuedCommandCount() const noexcept   { return commands_.getNumReady(); }
    /** Live engine only: after the host stopped calling process() for a while
     *  (FL smart-disable, suspended track), release the MIDI / audition voices
     *  on the first block instead of resuming them (review A3). */
    void setStallRecovery(bool on) noexcept       { stallRecovery_ = on; }
    /** True while process() / processBypassed() runs (tests, diagnostics). */
    bool isProcessingBlock() const noexcept       { return inProcess_.load(std::memory_order_seq_cst); }
    bool pushMidi(const RawMidi& m) noexcept      { return midiIn_.push(m); }   // non-audio producers

    // -- audio -> UI ---------------------------------------------------------
    bool popNotification(Notification& n) noexcept { return notes_.pop(n); }
    int  getDroppedNotificationCount() const noexcept { return droppedNotes_.load(std::memory_order_relaxed); }

    // -- audio thread --------------------------------------------------------
    /** Adds into buffer[startSample, startSample + numSamples). MIDI sample
     *  positions are relative to startSample. */
    void process(juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                 const juce::MidiBuffer* midi, const HostInfo& host) noexcept;
    /** Host bypass: fade out once, then stay silent; keeps draining commands. */
    void processBypassed(juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept;

    // -- UI state (atomics, any thread) --------------------------------------
    bool   isTransportActive() const noexcept     { return transportActiveUi_.load(std::memory_order_relaxed); }
    double getTransportPositionSeconds() const noexcept { return transportSecUi_.load(std::memory_order_relaxed); }
    bool   isSequencerRunning() const noexcept    { return seqRunningUi_.load(std::memory_order_relaxed); }
    bool   isSequencerHostDriven() const noexcept { return seqHostUi_.load(std::memory_order_relaxed); }
    void   getLaneState(int lane, int& playingSlot, float& slotProgress, float& playhead) const noexcept;
    int    getActiveVoiceCountUi() const noexcept { return activeVoicesUi_.load(std::memory_order_relaxed); }

    // -- diagnostics (audio thread counts, message-thread timer logs) --------
    /** Bumped by prepare() and release(): both reset the transport and the
     *  sequencer without a notification, so the UI compares this (N-12). */
    uint32_t getLifecycleGeneration() const noexcept { return lifecycleGen_.load(std::memory_order_acquire); }
    /** Voices that had to be cut because every slot of their pool was busy
     *  (each one is replaced by a 1.5 ms declick ramp, never a hard step). */
    int getForcedKillCount() const noexcept      { return forcedKills_.load(std::memory_order_relaxed); }
    /** Blocks whose host BPM / PPQ the engine had to reject or clamp (N-4). */
    int getRejectedHostInfoCount() const noexcept { return hostRejected_.load(std::memory_order_relaxed); }
    /** UI triggers dropped as stale (A3 / N-13) or because the host bypassed us. */
    int getStaleDroppedCount() const noexcept     { return staleDropped_.load(std::memory_order_relaxed); }
    int getBypassDroppedCount() const noexcept    { return bypassDropped_.load(std::memory_order_relaxed); }
    /** Mid-voice pitch-shifter entries deferred to a later block (CPU budget). */
    int getDeferredStretchCount() const noexcept  { return deferredStretch_.load(std::memory_order_relaxed); }
    /** Current stale-command threshold in ms (max(kStaleCommandMs, 3 x block)). */
    uint32_t getStaleThresholdMs() const noexcept { return staleThresholdMs_.load(std::memory_order_relaxed); }

    /** Mid-voice pitch-shifter entries allowed per block (outputSeek pre-roll
     *  is expensive): moving LOCK / a stem knob off 0 with 80 voices sounding
     *  spreads the entries over several blocks instead of one spike. New
     *  voices always enter immediately (sample-aligned onsets, A12). */
    static constexpr int kMaxStretchEntriesPerBlock = 8;
    /** Last-resort voice cut: the voice's last output is ramped to 0 over this. */
    static constexpr double kKillFadeMs = 1.5;

    /** Audio-thread / offline-owner only. */
    bool hasActiveVoices() const noexcept;

private:
    friend struct PlaybackEngineTestAccess;
    enum Pool : uint8_t { PoolMidi = 0, PoolSeq = 1, PoolTransport = 2 };

    struct Voice
    {
        bool     active { false };
        bool     releasing { false };
        uint8_t  pool { PoolMidi };
        uint64_t snapSeq { 0 };
        uint64_t order { 0 };
        const StemSnap* src { nullptr };
        int      stem { -1 }, segIndex { -1 }, note { -1 }, lane { -1 }, slotIndex { -1 };

        // window (stem-buffer samples) and read parameters
        int64_t  wStart { 0 }, wEnd { 0 };
        double   len { 0.0 };          // window length (source samples)
        bool     reverse { false };
        bool     loop { false };
        bool     gateRelease { false }; // released by note-off
        double   rate { 1.0 };          // source samples per output sample
        float    slotSemis { 0.0f };
        float    gain { 1.0f };
        float    pan { 0.0f };          // per-chop pan, -1..+1 (added to the mod-wheel pan)
        double   edgeFade { 0.0 };      // source samples, 0 = off (MicroFade)
        double   fadeIn { 0.0 };        // source samples: max(edgeFade, chop attack)
        double   fadeOut { 0.0 };       // source samples: max(edgeFade, chop release)
        int      relLen { 0 };          // output samples of the note-off / stop release (>= releaseLen_)
        double   loopXf { 0.0 };        // loop crossfade length (source samples)
        double   loopDip { 0.0 };       // loop dip fade when no pre-roll audio exists

        // direct path / output cursor (logical source position)
        double   cursor { 0.0 };
        bool     wrapped { false };
        bool     ended { false };

        // stretch path
        bool     stretch { false };
        double   inCursor { 0.0 };
        bool     inWrapped { false };
        bool     inEnded { false };
        int64_t  tailRemain { 0 };
        int      xfadeRemain { 0 };
        float    lastSemi { 0.0f };

        // envelope
        float    env { 1.0f };
        float    envStep { 0.0f };      // > 0 while attacking (rising), used with attack flag
        bool     attacking { false };
        float    relStep { 0.0f };
        int64_t  outCount { 0 };
        int64_t  releaseAt { -1 };      // output sample at which release starts (seq cell end)

        // transport mute/solo smoothing
        float    tgain { 1.0f }, tgainTarget { 1.0f };

        // last sample this voice added to the output (declick on a forced cut)
        float    lastL { 0.0f }, lastR { 0.0f };
    };

    struct GenState { double cursor; bool wrapped; bool ended; };

    struct SeqEvent { int offset; int lane; int slot; };

    // -- helpers -------------------------------------------------------------
    void   drainCommands() noexcept;
    void   drainMidiIn() noexcept;
    void   applyCommand(const Command& c) noexcept;
    void   handleMidi(const juce::MidiMessage& m) noexcept;
    void   handleRawMidi(uint8_t b0, uint8_t b1, uint8_t b2) noexcept;
    void   noteOn(int note, float velocity) noexcept;
    void   noteOff(int note) noexcept;
    /** CC123 (fast = false: chop release) or part of all-sound-off (fast). */
    void   allNotesOff(bool fast) noexcept;
    void   allSoundOff(bool stopSequencerToo) noexcept;
    /** Stop / choke a whole pool: always the short fixed fade (N-3). */
    void   releasePool(uint8_t pool) noexcept;
    void   startTransport(double seconds) noexcept;
    void   stopTransport(bool notify) noexcept;
    void   stopSequencer(bool notify, int reason) noexcept;

    Voice* allocVoice(uint8_t pool) noexcept;
    int    voiceIndex(const Voice& v) const noexcept { return (int) (&v - voices_.data()); }
    Voice* startWindowVoice(uint8_t pool, const StemSnap& stem, int stemIdx, int64_t start, int64_t end,
                            bool reverse, bool loop, double speed, float gain, float slotSemis) noexcept;
    /** Note-off / gate / natural-end release: the chop's own release (relLen). */
    void   releaseVoice(Voice& v) noexcept;
    /** Choke / steal / cell end / stop: fade over `len` output samples (the
     *  fixed 3 ms releaseLen_ by default). Shortens an already running, longer
     *  release; never lengthens one (N-3). */
    void   fastReleaseVoice(Voice& v, int len = 0) noexcept;
    /** Last resort when a pool is full: frees the slot NOW and hands the
     *  voice's last output to the declick ramp (no step in the output). */
    void   forceCutVoice(Voice& v) noexcept;
    /** After an allocation: keep `kPoolHeadroom` slots of the pool free or
     *  freeing within 3 ms by shortening the quietest long releases. */
    void   ensurePoolHeadroom(int base, int count) noexcept;
    static constexpr int kPoolHeadroom = 4;
    /** Per-chop pan / attack / release (SegSnap) on a freshly started voice. */
    void   applySegFx(Voice& v, const SegSnap& seg) noexcept;
    const SegSnap* findSeg(int stem, int segIndex) const noexcept;
    void   killVoice(Voice& v) noexcept;

    void   prepareSequencerEvents(int numSamples, const HostInfo& host) noexcept;
    void   triggerSlot(int lane, int slot) noexcept;
    void   publishSequencerUi(int numSamples) noexcept;

    void   renderRange(float* outL, float* outR, int n) noexcept;
    void   renderChunk(float* outL, float* outR, int n) noexcept;
    void   renderVoice(Voice& v, float* outL, float* outR, int n) noexcept;
    void   enterStretch(Voice& v, float semis) noexcept;
    void   tickModSmoothing(int n) noexcept;
    float  modPitch(int stem) const noexcept;
    float  modVolume(int stem) const noexcept;
    float  modPan(int stem) const noexcept;

    static void readFrame(const Voice& v, double p, float& l, float& r) noexcept;
    static void generate(const Voice& v, GenState& g, int n, float* L, float* R) noexcept;

    void   notify(const Notification& n) noexcept;
    /** Block start: load the published snapshot AND acknowledge it. */
    void   reloadSnapshot() noexcept;
    /** Mid-block (a command issued against a newer snapshot): load it but do
     *  NOT raise the acknowledged seq - voices started earlier in this block
     *  still read the older snapshot and are not in minVoiceSeq_ yet (A1). */
    void   switchSnapshotMidBlock() noexcept;

    // -- configuration -------------------------------------------------------
    const EngineParams& params_;
    std::atomic<bool> prepared_ { false };
    double sampleRate_ { 44100.0 };
    int    maxBlock_   { 0 };
    int    seekLen_    { 0 };
    int    releaseLen_ { 0 };   // samples
    int    xfadeLen_   { 0 };   // direct -> stretch crossfade, samples

    // -- voices + stretchers -------------------------------------------------
    static constexpr int kSeqBase       = kMidiVoiceSlots;
    static constexpr int kTransportBase = kMidiVoiceSlots + kSeqVoiceSlots;
    static constexpr int kTotalVoices   = kMidiVoiceSlots + kSeqVoiceSlots + kTransportVoices;
    std::array<Voice, kTotalVoices> voices_ {};
    std::vector<std::unique_ptr<StretchHolder>> stretchers_;   // one per voice
    uint64_t orderCounter_ { 0 };

    // scratch (allocated in prepare)
    juce::AudioBuffer<float> scratchIn_, scratchOut_, scratchX_, scratchSeek_;

    // -- snapshot ------------------------------------------------------------
    std::atomic<const Snapshot*> published_ { nullptr };
    const Snapshot* cur_ { nullptr };                     // audio thread, valid during process()
    std::atomic<bool>     inProcess_ { false };
    std::atomic<uint64_t> ackSeq_ { 0 };
    std::atomic<uint64_t> minVoiceSeq_ { UINT64_MAX };

    // -- FIFOs -----------------------------------------------------------------
    MpscFifo<Command, kCommandCapacity> commands_;
    MpscFifo<RawMidi, 1024>      midiIn_;     // standalone MIDI device threads
    SpscFifo<Notification, 2048> notes_;
    std::atomic<int>             droppedNotes_ { 0 };

    // -- modulation (audio thread) ------------------------------------------
    struct ModState { float bend { 0 }, wheel { 0 }, at { 0 }, sBend { 0 }, sWheel { 0 }, sAt { 0 }; };
    std::array<ModState, kMaxStems> mod_ {};

    // -- transport ------------------------------------------------------------
    bool   transportActive_ { false };
    double transportPosModel_ { 0.0 };    // model samples
    int    transportGen_ { 0 };

    // -- sequencer ------------------------------------------------------------
    bool     seqRunning_ { false };
    bool     seqHostDriven_ { false };
    uint32_t seqMask_ { 0 };
    int      seqGen_ { 0 };
    double   seqBpmOverride_ { 0.0 };
    double   seqBpmUsed_ { 0.0 };
    int64_t  seqSamples_ { 0 };      // free-run: samples since start
    int64_t  seqSampleBase_ { 0 };   // free-run tempo rebase point
    double   seqBeatBase_ { 0.0 };
    double   hostNextBeat_ { -1.0 };
    int64_t  lastTrigTick_ { -1 };   // sequencer clock ticks (kTicksPerBeat per beat, B18)
    double   seqBeatAtEnd_ { 0.0 };
    double   seqSpb_ { 22050.0 };     // samples per beat of the current block
    std::array<int, kMaxLanes> laneVoice_ {};         // voice index or -1
    std::array<uint64_t, kMaxLanes> laneVoiceOrder_ {};
    std::array<SeqEvent, 512> seqEvents_ {};
    int seqEventCount_ { 0 };

    // -- bypass / host stall ------------------------------------------------------
    bool bypassed_ { false };
    bool stallRecovery_ { false };
    uint32_t lastProcessMs_ { 0 };
    double   lastBlockMs_ { 0.0 };          // duration of the previous block (N-13)

    // -- forced-cut declick ramp (audio thread) --------------------------------
    float declickL_ { 0.0f }, declickR_ { 0.0f };
    float declickStepL_ { 0.0f }, declickStepR_ { 0.0f };
    int   declickRemain_ { 0 };
    int   killFadeLen_ { 1 };
    int   stretchEntriesThisBlock_ { 0 };

    // -- diagnostics ------------------------------------------------------------
    std::atomic<uint32_t> lifecycleGen_ { 0 };
    std::atomic<int>      forcedKills_ { 0 };
    std::atomic<int>      hostRejected_ { 0 };
    std::atomic<int>      staleDropped_ { 0 };
    std::atomic<int>      bypassDropped_ { 0 };
    std::atomic<int>      deferredStretch_ { 0 };
    std::atomic<uint32_t> staleThresholdMs_ { kStaleCommandMs };

    // -- UI atomics -----------------------------------------------------------
    std::atomic<bool>   transportActiveUi_ { false };
    std::atomic<double> transportSecUi_ { 0.0 };
    std::atomic<bool>   seqRunningUi_ { false };
    std::atomic<bool>   seqHostUi_ { false };
    std::atomic<int>    activeVoicesUi_ { 0 };
    std::array<std::atomic<int>,   kMaxLanes> laneSlotUi_ {};
    std::array<std::atomic<float>, kMaxLanes> laneProgUi_ {};
    std::array<std::atomic<float>, kMaxLanes> laneHeadUi_ {};

    JUCE_DECLARE_NON_COPYABLE(PlaybackEngine)
};

/** Owns snapshots on the message side: publish, keep alive, free when the
 *  engine no longer uses them. All methods are for non-audio threads. */
class SnapshotManager
{
public:
    explicit SnapshotManager(PlaybackEngine& engine) : engine_(engine) {}
    ~SnapshotManager();

    /** Assigns the next sequence number, publishes, retires the previous one. */
    void publish(std::shared_ptr<Snapshot> s);
    std::shared_ptr<const Snapshot> current() const;
    uint64_t currentSeq() const;
    /** Free retired snapshots no longer referenced by the audio thread. */
    int  collectGarbage();
    int  retiredCount() const;
    /** Owner teardown only: unpublishes and hands over every snapshot this
        manager keeps alive (they may hold the last stem buffer references),
        so the owner can free them off the message thread. */
    std::shared_ptr<void> detachForRelease();

private:
    PlaybackEngine& engine_;
    mutable std::mutex mutex_;
    std::shared_ptr<const Snapshot> current_;
    std::vector<std::shared_ptr<const Snapshot>> retired_;
    uint64_t nextSeq_ { 1 };
};

} // namespace engine
} // namespace samplar
