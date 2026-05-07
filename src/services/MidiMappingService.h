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
    void playBufferDirect(std::shared_ptr<const juce::AudioBuffer<float>> buffer,
                          double bufferSampleRate, float velocity = 1.0f, bool looping = false);

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
    };

    // ── Internal helpers ─────────────────────────────────────────────────────

    /** Resolve the shared buffer for a slot from cache (UI thread). */
    void resolveBuffer(MidiSlot& slot);

    /** Find a free voice, stealing the oldest active one if necessary. */
    Voice& allocateVoice();

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
