#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../app/Service.h"
#include "../app/EventBus.h"
#include "../audio/engine/PlaybackEngine.h"
#include "smode/SModeSegmentModel.h"
#include "smode/PatternGenerator.h"
#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <vector>
#include <cstdint>

namespace samplar {

/**
 * MidiMappingService - message-thread front end of the realtime playback
 * engine (src/audio/engine/PlaybackEngine).
 *
 * Audio thread (plugin processBlock / standalone getNextAudioBlock):
 *   renderBlock() -> PlaybackEngine::process(). Nothing else of this class
 *   runs on the audio thread. No locks, allocations, logging or callAsync.
 *
 * Message thread (everything else):
 *   - every UI action (note from a click / QWERTY key, audition, stop,
 *     transport, sequencer) becomes a Command in a lock-free FIFO;
 *   - a 30 Hz timer drains the engine's notification FIFO (segment
 *     triggered / stopped, transport stopped, MIDI learn, CC learn), publishes
 *     host tempo (A9), rebuilds + publishes the immutable playback snapshot
 *     when the model or the sequencer lanes change, and frees old snapshots.
 *
 * Standalone MIDI devices call handleIncomingMidiMessage() on their own
 * thread; those messages go to the engine through a lock-free FIFO (A27).
 */
class MidiMappingService : public IService,
                           public juce::AudioSource,
                           public juce::MidiInputCallback,
                           private juce::Timer
{
public:
    explicit MidiMappingService(EventBus& bus);
    ~MidiMappingService() override;

    // -- IService --------------------------------------------------------------
    void initialise() override;
    void shutdown()   override;

    // -- AudioSource (standalone app; audio thread) ------------------------------
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;

    // -- Plugin audio thread -----------------------------------------------------
    /** Adds the engine output into `buffer` (caller clears it first). */
    void renderBlock(juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi,
                     const engine::HostInfo& host) noexcept;
    void renderBlockBypassed(juce::AudioBuffer<float>& buffer) noexcept;
    /** Licence audio gate (LicenseSystemService::audioAllowedFlag(), must
     *  outlive this service). Applied with a 5 ms ramp to every live render
     *  path (plugin, bypass, standalone) and checked by the offline exports.
     *  nullptr = no gate (tests). Set once, before audio starts. */
    void setAudioGate(const std::atomic<bool>* allowed) noexcept;
    /** Plugin audio thread: the processor rejected / clamped host BPM / PPQ
     *  this block (N-4). Lock-free; the timer logs it. */
    void noteRejectedHostInfo() noexcept { hostInfoRejected_.fetch_add(1, std::memory_order_relaxed); }
    /** Ask the timer to re-publish "host.tempo" (DAW sync toggled on). Lock-free. */
    void requestHostTempoRepublish() noexcept { tempoRequest_.store(true, std::memory_order_relaxed); }
    /** Longest chop + stretcher tail, capped (A26). Any thread. */
    double getTailLengthSeconds() const noexcept;
    /** Pitched voices are pre-rolled (outputSeek), so nothing to compensate (A12). */
    int    getLatencySamples() const noexcept { return 0; }

    // -- Note triggers from the UI (message thread, queued to the audio thread) --
    void handleNoteOn (int channel, int noteNumber, float velocity);
    void handleNoteOff(int channel, int noteNumber);

    /** Attach / detach the S-Mode model. nullptr: release every voice, stop the
     *  transport and the sequencer, publish an empty snapshot. Message thread. */
    void setSModeModel(SModeSegmentModel* model);
    SModeSegmentModel* getSModeModel() const { return model_; }

    /** Rebuild + publish the playback snapshot now if the model changed.
     *  Called before every UI-triggered playback command. Message thread. */
    void syncSnapshotNow();

    /** ONE note-naming convention for the whole UI (B22): JUCE's default,
     *  middle C = MIDI 60 = "C3" (as in Ableton Live / Cubase). 36 = "C1". */
    static juce::String noteNumberToName(int noteNumber);
    static constexpr int kOctaveForMiddleC = 3;

    /** Quick Assign note banks (B21): 16 consecutive notes per stem, stem 0
     *  (Vocals) from C1 = 36, stem 1 (Bass) from E2 = 52, stem 2 (Drums) from
     *  G#3 = 68, stem 3 (Melody) from C5 = 84. -1 if the bank leaves 0..127. */
    static constexpr int kBankBaseNote = 36;
    static constexpr int kBankSize     = 16;
    static int getStemBankFirstNote(int stemIdx) noexcept
    {
        const int n = kBankBaseNote + stemIdx * kBankSize;
        return (stemIdx < 0 || n + kBankSize - 1 > 127) ? -1 : n;
    }

    /** Grid-LOCK transpose (B17): added to every voice (MIDI, audition,
     *  sequencer, transport, exports). Realtime, -24..+24 semitones. */
    void  setGlobalTransposeSemitones(float semis);
    float getGlobalTransposeSemitones() const { return params_.globalSemis.load(std::memory_order_relaxed); }

    // -- MIDI learn (S-Mode segment assignment) -------------------------------------
    /** On the next note-on, publishes "smode.midi.learn.assigned" (noteNumber). */
    void beginMidiLearnForSMode();
    void endMidiLearn();
    bool isMidiLearning() const { return params_.midiLearn.load(std::memory_order_relaxed); }

    // -- Polyphony / trigger mode -----------------------------------------------
    void setMonophonic(bool mono)     { params_.mono.store(mono, std::memory_order_relaxed); }
    bool isMonophonic() const         { return params_.mono.load(std::memory_order_relaxed); }
    /** GATE: one-shot chops release on note-off. Looped chops always do (A11). */
    void setSampleGateMode(bool gate) { params_.gate.store(gate, std::memory_order_relaxed); }
    bool isSampleGateMode() const     { return params_.gate.load(std::memory_order_relaxed); }
    /** Kept for API compatibility: loops always release on note-off now. */
    void setLoopGateMode(bool gate)   { loopGateMode_.store(gate, std::memory_order_relaxed); }
    bool isLoopGateMode() const       { return loopGateMode_.load(std::memory_order_relaxed); }

    // -- Standalone MIDI device input ----------------------------------------------
    void connectToDeviceManager(juce::AudioDeviceManager* dm);
    void disconnectFromDeviceManager();
    /** 0 = all channels, 1-16 = one channel (standalone input only). */
    void setMidiChannelFilter(int channel);
    int  getMidiChannelFilter() const { return channelFilter_.load(std::memory_order_relaxed); }
    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;

    // -- Computer keyboard MIDI ------------------------------------------------------
    // Layout (Ableton Live style): A W S E D F T G Y H U J K O L P ; = 17
    // chromatic notes from the current base note; Z / X = octave down / up.
    void setComputerKeyboardEnabled(bool enabled) { computerKeyboardEnabled_.store(enabled, std::memory_order_relaxed); }
    bool isComputerKeyboardEnabled() const { return computerKeyboardEnabled_.load(std::memory_order_relaxed); }
    /** Semitone offset of a note key from the base note, or -1. */
    static int keyToSemitoneOffset(const juce::KeyPress& key);
    static bool isOctaveKey(const juce::KeyPress& key);
    /** Note the key would play now (base note + offset), or -1. */
    int  keyToMidiNote(const juce::KeyPress& key) const;
    /** Lowest note of the computer keyboard (C of the current octave). */
    int  getComputerKeyboardBaseNote() const { return kbBaseNote_.load(std::memory_order_relaxed); }
    int  getComputerKeyboardTopNote() const  { return juce::jmin(127, getComputerKeyboardBaseNote() + kNumNoteKeys - 1); }
    void shiftComputerKeyboardOctave(int delta);
    /** One note-on per physical key-down (OS autorepeat is swallowed). Z / X
     *  shift the octave. Returns true if the key was consumed. */
    bool handleKeyPress(const juce::KeyPress& key);
    bool handleKeyRelease(const juce::KeyPress& key);
    bool releaseUnheldComputerKeys();
    void resetComputerKeyboardHeld();
    static constexpr int kNumNoteKeys = 17;

    // -- Audition (message thread) ---------------------------------------------------
    /** Play one chop with the stem pitch knob, exactly like a MIDI note would. */
    void playSegment(int stemIdx, int segIdx, bool looping, float velocity = 1.0f);
    /** Play a sequencer slot (trim, speed, reverse, slot pitch, velocity). */
    void auditionSlot(const PatternSlot& slot);
    /** Release (short fade) every MIDI / audition / sequencer voice. */
    void stopAllVoices();

    // -- S-Mode transport (all stems mixed from a position) --------------------------
    void   startSModeTransport(double startTimeSeconds);
    void   stopSModeTransport();
    bool   isSModeTransportActive() const { return transportUiActive_; }
    double getSModeTransportPositionSeconds() const { return engine_->getTransportPositionSeconds(); }
    void   setSModeTransportLooping(bool loop) { params_.transportLoop.store(loop, std::memory_order_relaxed); }
    bool   isSModeTransportLooping() const     { return params_.transportLoop.load(std::memory_order_relaxed); }

    // -- DAW tempo sync toggle (message thread; persisted in the project) ------------
    /** The page's "DAW" toggle: when on, the page follows "host.tempo". Stored
     *  here (processor lifetime) so it survives editor close and project reload. */
    void setDawTempoSync(bool on) { dawTempoSync_ = on; }
    bool isDawTempoSync() const   { return dawTempoSync_; }

    // -- Sequencer (clock runs on the audio thread, B36) -----------------------------
    struct LaneInput
    {
        std::vector<PatternSlot> slots;
        int  numBars { 4 };
        int  cellsPerBeat { 1 }; // cells per bar = 4 * cellsPerBeat (1, 2 = 8ths, 4 = 16ths; B18)
        bool muted { false };
    };
    /** Replace the sequencer lanes (index = UI lane index). Message thread. */
    void setSequencerLanes(const std::vector<LaneInput>& lanes);
    /** Free-running tempo (used when the host is not playing). */
    void setSequencerBpm(double bpm);
    /** Start lanes in `laneMask`: follows the host transport while the DAW
     *  plays, free-running otherwise. */
    void startSequencer(uint32_t laneMask);
    void stopSequencer();
    bool isSequencerRunning() const { return seqUiRunning_; }
    void getSequencerLaneState(int lane, int& playingSlot, float& slotProgress, float& playhead) const;

    // -- Offline render (drag WAV / export) - same renderer as playback ---------------
    /** Any NON-audio thread. Lanes in the mask, one full pass of the longest lane. */
    std::unique_ptr<juce::AudioBuffer<float>> renderSequencerOffline(uint32_t laneMask, double& outSampleRate,
                                                                     std::function<void(float)> progress = {});
    /** Any NON-audio thread. One chop with stem pitch, fades and pitched tail. */
    std::unique_ptr<juce::AudioBuffer<float>> renderSegmentOffline(int stemIdx, int segIdx, double& outSampleRate);

    // -- Per-stem pitch (realtime) -------------------------------------------------------
    static constexpr int MAX_STEMS = engine::kMaxStems;
    void  setStemPitchSemitones(int stemIdx, float semitones);
    float getStemPitchSemitones(int stemIdx) const;

    // -- Per-stem modulation routing (pitch bend, mod wheel, aftertouch) ---------------
    enum class ModDestination : uint8_t { None = 0, Pitch, FilterCutoff, Volume, Pan };
    void setStemPitchBendDest  (int stemIdx, ModDestination d);
    void setStemModWheelDest   (int stemIdx, ModDestination d);
    void setStemAftertouchDest (int stemIdx, ModDestination d);
    ModDestination getStemPitchBendDest  (int stemIdx) const;
    ModDestination getStemModWheelDest   (int stemIdx) const;
    ModDestination getStemAftertouchDest (int stemIdx) const;
    void setStemPitchBendRange  (int stemIdx, float range);
    void setStemModWheelRange   (int stemIdx, float range);
    void setStemAftertouchRange (int stemIdx, float range);
    float getStemPitchBendRange  (int stemIdx) const;
    float getStemModWheelRange   (int stemIdx) const;
    float getStemAftertouchRange (int stemIdx) const;

    // -- Per-stem velocity curve -----------------------------------------------------------
    enum class VelocityCurve : uint8_t { Linear = 0, Exponential, Logarithmic, SoftKnee };
    void          setStemVelocityCurve(int stemIdx, VelocityCurve c);
    VelocityCurve getStemVelocityCurve(int stemIdx) const;
    static float  applyVelocityCurve(float v01, VelocityCurve c);

    // -- Generic CC learn / bindings (callbacks run on the message thread) ----------------
    // A binding maps (channel, CC) to a named TARGET (a stable control id such
    // as "stem0.pitch"). Controls register their target while they exist; a
    // binding whose target is not registered right now is kept (inert) so it
    // survives editor close and is saved with the project.
    struct CcBindingInfo
    {
        int channel { 0 };      // 0 = omni, 1..16
        int cc { 0 };           // 0..127
        juce::String target;
    };
    /** Register / replace the control behind `target` (value 0..1). */
    void registerCcTarget(const juce::String& target, std::function<void(float)> apply);
    void unregisterCcTarget(const juce::String& target);
    /** Arm learn for `target` (registers `apply` as its control): the next CC
     *  received is bound to it, replacing an older binding of that CC. */
    void beginCcLearn(const juce::String& target, std::function<void(float)> apply);
    void endCcLearn();
    bool isCcLearning() const { return ccLearnActive_; }
    void clearCcBindings();
    /** Persistable copy of every binding (project save). */
    std::vector<CcBindingInfo> getCcBindings() const;
    /** Replace every binding (project restore). Invalid entries are dropped. */
    void setCcBindings(const std::vector<CcBindingInfo>& bindings);
    /** Target armed by beginCcLearn ("" when not learning). */
    juce::String getCcLearnTarget() const { return ccLearnActive_ ? ccLearnPendingTarget_ : juce::String(); }
    /** Remove every binding of one target ("Clear MIDI"); returns the count removed. */
    int clearCcBindingsForTarget(const juce::String& target);
    /** Bindings of one target for tooltips / menus, e.g. "CC 74 ch 1" ("" = none). */
    juce::String describeCcBindings(const juce::String& target) const;
    /** Bumped on every binding change; the UI refreshes tooltips when it moves. */
    uint32_t getCcBindingsGeneration() const { return ccGeneration_; }
    /** Built-in targets owned by this service (work with the editor closed):
     *  "stem<N>.pitch" -> per-stem pitch, CC 0..1 = -12..+12 semitones. */
    static bool isBuiltInCcTarget(const juce::String& target);
    /** Test hook: what a CC from the engine does (message thread). */
    void debugInjectController(int channel, int cc, float value01) { onControllerFromEngine(channel, cc, value01); }

private:
    void timerCallback() override;
    void drainNotifications();
    void publishHostTempoIfNeeded();
    void rebuildSnapshotIfNeeded(bool force);
    void collectResampleResults();
    void scheduleResample(const std::shared_ptr<const juce::AudioBuffer<float>>& src, double srcRate, double dstRate);
    void onControllerFromEngine(int channel, int cc, float value01);
    void updateCcForwarding();
    bool pushCommand(engine::Command c);
    void checkEngineLifecycle();
    void logEngineDiagnostics();
    void applyAudioGate(juce::AudioBuffer<float>& buffer, int start, int num) noexcept;
    bool isAudioAllowedForExport(const char* what) const;

    EventBus& bus_;

    // -- realtime engine (declaration order matters: snapshots_ refers to engine_) --
    engine::EngineParams params_;
    std::unique_ptr<engine::PlaybackEngine> engine_;
    engine::SnapshotManager snapshots_;

    SModeSegmentModel* model_ { nullptr };
    std::vector<engine::LaneDesc> laneDescs_;
    uint64_t lastModelHash_ { 0 };
    uint64_t lastFxHash_ { 0 };
    double   lastBuiltHostRate_ { 0.0 };
    bool     snapshotDirty_ { true };
    juce::uint32 lastRebuildLogMs_ { 0 };
    std::atomic<double> hostRate_ { 0.0 };
    std::atomic<double> maxSegmentSeconds_ { 0.0 };

    // Export snapshot (stems at the model rate). Read by worker threads.
    mutable std::mutex exportMutex_;
    std::shared_ptr<const engine::Snapshot> exportSnap_;

    // Host-rate resampled stems (A23), message thread only.
    struct ResampledStem
    {
        std::weak_ptr<const juce::AudioBuffer<float>> src;
        const juce::AudioBuffer<float>* key { nullptr };
        std::shared_ptr<const juce::AudioBuffer<float>> out;
        double dstRate { 0.0 };
    };
    std::vector<ResampledStem> resampled_;
    std::set<std::pair<const juce::AudioBuffer<float>*, int>> resamplePending_;
    struct ResampleInbox
    {
        std::mutex lock;
        std::vector<ResampledStem> done;
    };
    std::shared_ptr<ResampleInbox> resampleInbox_ { std::make_shared<ResampleInbox>() };
    std::unique_ptr<juce::ThreadPool> resamplePool_;
    std::shared_ptr<std::atomic<bool>> resampleAbort_ { std::make_shared<std::atomic<bool>>(false) };

    // Licence gate (N-18): flag owned by LicenseSystemService; ramp state is audio-thread only.
    const std::atomic<bool>* audioGate_ { nullptr };
    engine::GateRamp gate_;
    std::atomic<int> gateFlips_ { 0 };
    std::atomic<int> hostInfoRejected_ { 0 };

    // Engine diagnostics already logged (message thread)
    uint32_t seenEngineGen_ { 0 };
    uint32_t transportStartEngineGen_ { 0 }, seqStartEngineGen_ { 0 };   // engine generation at Play / seq start
    int seenForcedKills_ { 0 }, seenHostRejected_ { 0 }, seenEngineHostRejected_ { 0 };
    int seenStaleDropped_ { 0 }, seenBypassDropped_ { 0 }, seenGateFlips_ { 0 }, seenDeferredStretch_ { 0 };
    juce::uint32 lastDeferredLogMs_ { 0 };

    // UI-side run state (message thread)
    bool transportUiActive_ { false };
    bool seqUiRunning_ { false };
    int  transportGen_ { 0 };
    int  seqGen_ { 0 };
    int  lastDroppedNotes_ { 0 };

    // Host tempo (A9): audio thread stores, timer publishes.
    std::atomic<double> hostBpm_ { 0.0 };
    std::atomic<bool>   tempoRequest_ { false };
    double lastPublishedBpm_ { 0.0 };

    // MIDI learn
    bool midiLearnSMode_ { false };
    std::atomic<bool> loopGateMode_ { false };

    // Standalone
    juce::AudioSourcePlayer audioPlayer_;
    juce::AudioDeviceManager* connectedDeviceManager_ { nullptr };
    std::atomic<int> channelFilter_ { 0 };

    // Computer keyboard (message thread). heldKeyNote_[k] = note started by
    // note key k (index into the layout), so an octave shift while a key is
    // held still releases the right note.
    std::atomic<bool> computerKeyboardEnabled_ { true };
    std::atomic<int>  kbBaseNote_ { kBankBaseNote };   // A = C1: the Vocals bank
    std::array<int, kNumNoteKeys> heldKeyNote_ { { -1, -1, -1, -1, -1, -1, -1, -1, -1,
                                                    -1, -1, -1, -1, -1, -1, -1, -1 } };

    // CC learn / bindings (message thread)
    std::vector<CcBindingInfo> ccBindings_;
    std::map<juce::String, std::function<void(float)>> ccTargets_;
    bool ccLearnActive_ { false };
    juce::String ccLearnPendingTarget_;
    uint32_t ccGeneration_ { 0 };
    bool applyBuiltInCcTarget(const juce::String& target, float value01);
    bool dawTempoSync_ { false };

    std::atomic<bool> destroyed_ { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiMappingService)
};

} // namespace samplar
