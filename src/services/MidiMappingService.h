#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../app/Service.h"
#include "../app/EventBus.h"
#include "../audio/SamplePreviewCache.h"
#include "smode/SModeSegmentModel.h"
#include <array>
#include <mutex>
#include <atomic>
#include <memory>
#include <vector>

// Forward-declare signalsmith-stretch so we don't pull its header into our public API.
// Must match the upstream declaration exactly:
//   template<typename Sample=float, class RandomEngine=void> struct SignalsmithStretch;
namespace signalsmith { namespace stretch {
    template <typename Sample = float, class RandomEngine = void> struct SignalsmithStretch;
} }

namespace samplar {

/**
 * MidiMappingService — Phase 2 VST3 MIDI input.
 *
 * Maps MIDI notes to sample files and plays them back via an AudioSource
 * interface so processBlock() can pull audio directly.
 *
 * Mapping:
 *   - Browser page auto-map: visible tiles → notes 36-51  (16 pads)
 *   - Sequencer rows: row 0 → note 36, row 1 → note 37, etc.
 *   - Manual: mapSampleToNote() for arbitrary assignments
 *
 * Thread Safety:
 *   - handleNoteOn/handleNoteOff are called from the AUDIO THREAD.
 *   - Map updates (mapSampleToNote etc.) are called from the UI/MESSAGE THREAD.
 *   - Internally uses try_lock so audio thread never blocks.
 */
class MidiMappingService : public IService,
                           public juce::AudioSource,
                           public juce::MidiInputCallback,
                           public SamplePreviewCache::Listener
{
public:
    explicit MidiMappingService(EventBus& bus);
    ~MidiMappingService() override;

    // ── IService ────────────────────────────────────────────────────────────
    void initialise() override;
    void shutdown()   override;

    // ── AudioSource ─────────────────────────────────────────────────────────
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;

    // ── MIDI Input (called from AUDIO THREAD via processBlock) ───────────────
    void handleNoteOn (int channel, int noteNumber, float velocity);
    void handleNoteOff(int channel, int noteNumber);

    // ── Mapping control (call from MESSAGE/UI THREAD) ────────────────────────

    /** Map a sample file to a MIDI note (0–127). */
    void mapSampleToNote(int noteNumber, const juce::File& sampleFile);

    /** Remove mapping for a note. */
    void clearMapping(int noteNumber);

    /** Clear all 128 slots. */
    void clearAllMappings();

    /**
     * Auto-map up to 16 files to notes 36-51 (GM drum range start).
     * Called when the visible browser page changes.
     */
    void autoMapBrowserPage(const std::vector<juce::File>& visibleSamples);

    /**
     * Map sequencer row 0..N-1 to MIDI notes 36..36+N-1.
     * This mirrors the GM drum map: 36=kick, 37=rimshot, 38=snare, etc.
     */
    void mapSequencerChannels(int numChannels);

    /** Inject cache reference after services are all initialised. */
    void setSampleCache(SamplePreviewCache* cache);

    /** Inject S-Mode segment model for MIDI-triggered stem playback.
     *  Passing nullptr performs a hard reset: kills all voices, releases
     *  copied audio buffers, and stops S-Mode transport. Thread-safe
     *  because active is checked before buffer in getNextAudioBlock(). */
    void setSModeModel(SModeSegmentModel* model);
    SModeSegmentModel* getSModeModel() const { return smodeModel_; }

    // ── SamplePreviewCache::Listener ─────────────────────────────────────
    void sampleLoaded(const juce::File& file) override;

    // ── Locked Assignments ─────────────────────────────────────────────────

    /** Lock a sample file to a specific MIDI note (survives page changes). */
    void lockSampleToNote(int noteNumber, const juce::File& sampleFile);

    /** Unlock a MIDI note (returns to auto-map behavior). */
    void unlockNote(int noteNumber);

    /** Check if a note is locked. */
    bool isNoteLocked(int noteNumber) const;

    /** Get the locked MIDI note for a file, or -1 if not locked. */
    int getLockedNoteForFile(const juce::File& file) const;

    /** Return the MIDI note currently assigned to this sample file, or -1 if none.
     *  Considers ALL valid slots (locked + auto-mapped). Thread-safe (takes mapMutex_). */
    int noteForSample(const juce::File& file) const;

    /** Register the given sample at the next free MIDI slot for drag-out use.
     *  Scans 68..127 first (to avoid clobbering Browser auto-map at 48-67),
     *  then 0..47 as a fallback. Returns the chosen note, or -1 if all 128
     *  slots are full/locked. Idempotent: if the sample is already mapped,
     *  returns that note without changing the map. Thread-safe (takes mapMutex_). */
    int registerSampleAtNextFreeNote(const juce::File& file);

    /** Get MIDI note name string (e.g. "C3", "F#4"). */
    static juce::String noteNumberToName(int noteNumber);

    // ── MIDI Learn ──────────────────────────────────────────────────────────
    /** Enter MIDI learn mode for a specific tile/file. User presses a key to assign. */
    void beginMidiLearn(int tileIndex);
    void beginMidiLearnForFile(const juce::File& file);
    /** Enter MIDI learn mode for S-Mode segment assignment.
     *  On note press, publishes "smode.midi.learn.assigned" with noteNumber. */
    void beginMidiLearnForSMode();
    /** Exit MIDI learn mode. */
    void endMidiLearn();
    bool isMidiLearning() const { return midiLearnActive_.load(); }

    // ── Polyphony control ───────────────────────────────────────────────────
    /** Set monophonic mode (true = kill previous voices on new note). Default: false (poly). */
    void setMonophonic(bool mono) { monophonic_.store(mono, std::memory_order_relaxed); }
    bool isMonophonic() const     { return monophonic_.load(std::memory_order_relaxed); }

    // ── Trigger mode ─────────────────────────────────────────────────────
    /** Gate mode for samples: note-off stops playback immediately. Default: false (one-shot). */
    void setSampleGateMode(bool gate) { sampleGateMode_.store(gate, std::memory_order_relaxed); }
    bool isSampleGateMode() const     { return sampleGateMode_.load(std::memory_order_relaxed); }

    /** Gate mode for loops: note-off stops loop playback. Default: false (one-shot, plays once). */
    void setLoopGateMode(bool gate)   { loopGateMode_.store(gate, std::memory_order_relaxed); }
    bool isLoopGateMode() const       { return loopGateMode_.load(std::memory_order_relaxed); }

    // ── Standalone MIDI device input ────────────────────────────────────────
    /** Connect to a device manager so MIDI input goes directly to handleNoteOn/Off.
     *  Used in standalone mode where processBlock is not called. */
    void connectToDeviceManager(juce::AudioDeviceManager* dm);
    void disconnectFromDeviceManager();

    /** Set MIDI channel filter for standalone input. 0 = all channels, 1-16 = specific channel. */
    void setMidiChannelFilter(int channel);
    int  getMidiChannelFilter() const { return channelFilter_.load(std::memory_order_relaxed); }

    // ── MidiInputCallback (standalone direct MIDI) ──────────────────────────
    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& message) override;

    // ── Computer Keyboard MIDI ────────────────────────────────────────────
    /** Enable/disable computer keyboard as MIDI input (QWERTY → note mapping). */
    void setComputerKeyboardEnabled(bool enabled) { computerKeyboardEnabled_.store(enabled, std::memory_order_relaxed); }
    bool isComputerKeyboardEnabled() const { return computerKeyboardEnabled_.load(std::memory_order_relaxed); }

    /** Convert a JUCE KeyPress to a MIDI note number. Returns -1 if unmapped.
     *  Standard layout: Z-M = C3-B3 (lower octave), Q-U = C4-B4 (upper octave). */
    static int keyToMidiNote(const juce::KeyPress& key);

    /** Handle computer keyboard key press/release as MIDI events. Returns true if handled.
     *  handleKeyPress dedupes OS autorepeat: if the note is already flagged as held
     *  (from a prior un-released press), the call is swallowed and NO new noteOn is fired.
     *  This ensures POLY/MONO/1SH/Gate voice logic in handleNoteOn/Off is honored
     *  (one noteOn per physical key-down, one noteOff per physical key-up). */
    bool handleKeyPress(const juce::KeyPress& key);
    bool handleKeyRelease(const juce::KeyPress& key);

    /** Poll-style release driver for pages that override keyStateChanged.
     *  Walks internal held-keys set; for any flagged note whose underlying key
     *  is no longer physically down, fires handleNoteOff and clears the flag.
     *  Returns true if at least one note was released. Safe to call from
     *  any Component::keyStateChanged override on the message thread. */
    bool releaseUnheldComputerKeys();

    /** Force-clear all computer-keyboard held flags and send noteOff for each.
     *  Call on focus loss / page switch to avoid stuck notes. */
    void resetComputerKeyboardHeld();

    // ── S-Mode direct playback (click-to-play without MIDI note) ────────────
    /** Play a buffer directly through the voice mixer.
     *  @param stemIdx  optional stem index (0..MAX_STEMS-1). When >= 0 the
     *  realtime SoundTouch path is engaged for the voice and the active
     *  stem-pitch knob is honoured. Pass -1 to skip pitch (raw playback). */
    void playBufferDirect(std::shared_ptr<const juce::AudioBuffer<float>> buffer,
                          double bufferSampleRate, float velocity = 1.0f, bool looping = false,
                          int stemIdx = -1);

    /** Stop all active voices immediately (for sequencer stop, etc.). */
    void stopAllVoices();

    // ── State serialization (for VST3 state persistence) ────────────────────
    /** Get slot info for serialization. Returns file path, sampleRate, locked flag. */
    struct SlotInfo {
        int noteNumber;
        juce::File file;
        std::shared_ptr<const juce::AudioBuffer<float>> buffer;
        double sampleRate;
        bool locked;
    };
    /** Get all occupied (valid) slots for state save. */
    std::vector<SlotInfo> getOccupiedSlots() const;

    /** Lightweight metadata-only snapshot for state save (no audio loading, no I/O).
     *  Safe to call from any thread including the audio thread. */
    struct SlotMeta {
        int noteNumber;
        juce::String filePath;
        double sampleRate;
        bool locked;
    };
    std::vector<SlotMeta> getOccupiedSlotsMeta() const;

    /** Restore a slot from saved state (with pre-loaded buffer). */
    void restoreSlot(int noteNumber, const juce::File& file,
                     std::shared_ptr<juce::AudioBuffer<float>> buffer,
                     double sampleRate, bool locked);

    // ── S-Mode Transport (play all stems mixed) ─────────────────────────────
    void startSModeTransport(double startTimeSeconds);
    void stopSModeTransport();
    bool isSModeTransportActive() const { return transportActive_.load(std::memory_order_relaxed); }
    double getSModeTransportPositionSeconds() const;
    void setSModeTransportLooping(bool loop) { transportLooping_.store(loop, std::memory_order_relaxed); }
    bool isSModeTransportLooping() const { return transportLooping_.load(std::memory_order_relaxed); }

    static constexpr int BROWSER_MAP_START_NOTE = 48;   // C3 — first pad note
    static constexpr int BROWSER_MAP_NUM_PADS   = 20;

    // ── Per-stem pitch shift (realtime SoundTouch in audio path) ────────────
    /** Set per-stem pitch in semitones. Range -12..+12. Lock-free atomic
     *  store — the audio thread picks up the new value on the next block
     *  via setPitchSemiTones(). Knob → audible delay is one audio block
     *  (~10 ms at 512 frames @ 48 kHz). */
    void  setStemPitchSemitones(int stemIdx, float semitones);
    float getStemPitchSemitones(int stemIdx) const;
    static constexpr int MAX_STEMS = 8;

private:
    // ── MIDI Slot ───────────────────────────────────────────────────────────

    struct MidiSlot {
        juce::File file;
        // Shared buffer cached by SamplePreviewCache (no copy, instant access)
        std::shared_ptr<const juce::AudioBuffer<float>> buffer;
        double sampleRate = 44100.0;
        bool valid = false;
        bool locked = false;   // True = user-assigned, survives page changes
    };

    // ── Voice ────────────────────────────────────────────────────────────────

    struct Voice {
        std::shared_ptr<const juce::AudioBuffer<float>> buffer;
        double position     = 0.0;
        double resampleRatio = 1.0;
        float  velocity     = 1.0f;
        bool   active       = false;
        bool   looping      = false;  // True = wrap to start instead of ending
        int    noteNumber   = -1;     // Which note triggered this voice
        int    stemIdx      = -1;     // Stem index for realtime pitch (or -1 = no pitch)
        float  lastSemi     = 0.0f;   // Last applied semitone value (dedup)
        int    startSample  = 0;      // For S-Mode segment: start within voice.buffer
        int    endSample    = 0;      // For S-Mode segment: end within voice.buffer (0 = use full buffer)

        // Native sample rate of voice.buffer. SignalsmithStretch is configured
        // to this rate on note-on. The block process() call uses the
        // inputRate / deviceRate ratio for time-rate (so output stays at
        // device rate while pitch is shifted by setTransposeSemitones).
        double bufferSampleRate = 0.0;
        juce::uint32 lastConfiguredSr = 0;  // Tracks SignalsmithStretch reconfig dedup

        // Realtime pitch shifter (signalsmith-stretch). Lazy-allocated in
        // prepareToPlay so we never allocate on the audio thread. The
        // instance lives forever inside this Voice — it is just reset()'d
        // between uses, never destroyed.
        std::unique_ptr<signalsmith::stretch::SignalsmithStretch<float>> stretch;

        // EOS tail tracking. Once the source is exhausted, we feed silence
        // for one extra block to flush the internal latency, then deactivate.
        bool eosFedSilence = false;
    };

    // ── Internal helpers ─────────────────────────────────────────────────────

    /** Resolve the shared buffer for a slot from cache (UI thread). */
    void resolveBuffer(MidiSlot& slot);

    /** Find a free voice, stealing the oldest active one if necessary. */
    Voice& allocateVoice();

    /** Apply current stem pitch atomic to a signalsmith-stretch instance.
     *  Cheap when unchanged (no-op). Called per audio block. RT-safe. */
    static void applyPitchIfChanged(signalsmith::stretch::SignalsmithStretch<float>& s,
                                    float& lastSemi, float targetSemi);

    // ── Per-stem pitch slot — realtime path ──────────────────────────────
    // Just an atomic semi value per stem. Audio thread reads it each block
    // and applies via SignalsmithStretch::setTransposeSemitones (RT-safe).
    struct StemPitchSlot
    {
        std::atomic<float> targetSemi { 0.0f };
    };
    std::array<StemPitchSlot, MAX_STEMS> stemPitch_;

    // Per-stem realtime signalsmith-stretch instances for the transport mix
    // path. Allocated in prepareToPlay; only touched on the audio thread.
    std::array<std::unique_ptr<signalsmith::stretch::SignalsmithStretch<float>>, MAX_STEMS> stemStretch_;
    std::array<float,        MAX_STEMS> stemLastSemi_ {};
    // Tracks the input sample rate each stem's stretch instance has been
    // configured for (0 = never configured / needs config). When modelSr
    // changes (e.g. user loads a new song), reconfigure on the audio
    // thread.
    std::array<juce::uint32, MAX_STEMS> stemLastConfiguredSr_ {};

    // Scratch buffers for planar in/out passed to SignalsmithStretch::process.
    // signalsmith-stretch consumes/produces non-interleaved (planar) audio,
    // matching JUCE's AudioBuffer layout — no interleave step needed. We
    // still need these for stem mixing because we read from
    // stem->audioBuffer (planar) but need a writable scratch for the
    // stretcher's output before mixing into dest with per-stem volume.
    juce::AudioBuffer<float> stemOutScratch_;   // 2ch x maxBlockSize
    juce::AudioBuffer<float> voiceOutScratch_;  // 2ch x maxBlockSize
    // Input scratch for the stem path (reused sequentially across stems).
    // Holds pre-resampled deviceSr-rate input frames feeding the stretcher.
    juce::AudioBuffer<float> stretchInScratch_; // 2ch x (maxBlockSize + slack)
    // Separate input scratch for the voice realtime path — voices may run
    // before stems in the same getNextAudioBlock() invocation, so we cannot
    // share stretchInScratch_ across the two paths within a single block.
    juce::AudioBuffer<float> voiceInScratch_;   // 2ch x maxBlockSize

    // ── State ────────────────────────────────────────────────────────────────

    EventBus& bus_;
    EventBus::HandlerId browserPageHandler_{ 0 };

    SamplePreviewCache* cache_ { nullptr };
    SModeSegmentModel*  smodeModel_ { nullptr };  // S-Mode segment MIDI triggering

    // 128-slot MIDI map  (UI thread writes, audio thread reads via try_lock)
    mutable std::mutex mapMutex_;
    std::array<MidiSlot, 128> midiMap_;

    // Voice pool — audio thread only, no lock needed
    static constexpr int MAX_VOICES = 16;
    std::array<Voice, MAX_VOICES> voices_;

    double currentSampleRate_ = 44100.0;
    int    blockSize_          = 512;

    // MIDI learn state
    std::atomic<bool> midiLearnActive_{ false };
    std::atomic<int>  midiLearnTileIndex_{ -1 };
    std::atomic<bool> midiLearnSMode_{ false };    // S-Mode segment learn mode
    juce::File        midiLearnFile_;              // File to lock when learn completes

    // Polyphony mode: false = polyphonic (drum pad default), true = monophonic
    std::atomic<bool> monophonic_{ false };

    // Trigger modes
    std::atomic<bool> sampleGateMode_{ false };  // false=one-shot, true=gate (note-off stops)
    std::atomic<bool> loopGateMode_{ false };     // false=one-shot (play once), true=gate (loop while held)

    // Standalone audio output bridge (AudioSource → AudioIODeviceCallback)
    juce::AudioSourcePlayer audioPlayer_;

    // Standalone MIDI device input — raw pointer, not owned
    juce::AudioDeviceManager* connectedDeviceManager_{ nullptr };

    // MIDI channel filter: 0 = all channels, 1-16 = specific channel
    std::atomic<int> channelFilter_{ 0 };

    // S-Mode transport state (plays all stems mixed from a position)
    std::atomic<bool>   transportActive_{ false };
    std::atomic<bool>   transportLooping_{ false };
    std::atomic<bool>   computerKeyboardEnabled_{ true };  // Default ON (auto-disabled when hardware MIDI found)
    // Per-MIDI-note held flag for computer-keyboard input. Indexed by note [0..127].
    // Set true on accepted handleKeyPress (key-down edge), cleared on handleKeyRelease
    // or releaseUnheldComputerKeys(). Used to dedup OS autorepeat so POLY/MONO/1SH/Gate
    // voice logic in handleNoteOn sees ONE noteOn per physical press.
    std::array<std::atomic<bool>, 128> computerKeyHeld_{};
    std::atomic<double> transportPosSamples_{ 0.0 };  // Position in model sample rate

    // Destruction guard — set in shutdown() so in-flight callAsync lambdas
    // (which capture `this`) can bail out before dereferencing a freed object.
    std::atomic<bool> destroyed_{ false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiMappingService)
};

} // namespace samplar
