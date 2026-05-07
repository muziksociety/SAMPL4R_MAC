#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <vector>
#include <map>
#include <optional>
#include <functional>

namespace samplar {

/**
 * Per-segment effect settings.
 */
struct SegmentEffects
{
    int pitchSemitones { 0 };       // -24 to +24
    float volume { 1.0f };          // 0.0 to 1.0
    float pan { 0.0f };             // -1.0 (L) to +1.0 (R)
    float filterCutoff { 1.0f };    // 0.0 to 1.0 (normalized)
    bool filterIsHighPass { false }; // false = LP, true = HP
    bool reverse { false };
    bool loop { false };
};

/**
 * A single segment within a stem waveform.
 */
struct Segment
{
    int64_t startSample { 0 };
    int64_t endSample { 0 };
    int midiNote { -1 };            // -1 = unassigned
    SegmentEffects effects;
    bool selected { false };

    double getDurationSeconds(double sampleRate) const
    {
        if (sampleRate <= 0.0) return 0.0;
        return static_cast<double>(endSample - startSample) / sampleRate;
    }

    int64_t getLengthSamples() const { return endSample - startSample; }
};

/**
 * Data for one stem (e.g. Vocals, Bass, Drums...).
 */
struct StemData
{
    juce::String name;                          // "Vocals", "Bass", etc.
    juce::AudioBuffer<float> audioBuffer;       // The stem audio
    std::vector<Segment> segments;
    bool linked { false };                       // Link toggle for combined triggering
    bool solo { false };
    bool mute { false };
    float volume { 1.0f };                       // Stem mix volume (0-1)

    // Cached spectral colors — survive editor close/reopen so waveforms
    // don't need to be re-analyzed when the UI is recreated.
    std::vector<juce::Colour> spectralColors;
    double spectralDuration { 0.0 };
    bool spectralReady { false };
};

/**
 * SModeSegmentModel — owns all stem data, segments, and MIDI assignments.
 * Pure data model with no UI or EventBus dependency.
 * Provides undo/redo via snapshot stack.
 */
class SModeSegmentModel
{
public:
    SModeSegmentModel() = default;

    // ── Stem Management ──────────────────────────────────────────────
    void clear();
    void setStemCount(int count);
    int getStemCount() const { return static_cast<int>(stems_.size()); }
    StemData* getStem(int index);
    const StemData* getStem(int index) const;

    // ── Segment CRUD ─────────────────────────────────────────────────
    /** Add a segment to a stem. Returns the segment index. */
    int addSegment(int stemIndex, int64_t startSample, int64_t endSample);

    /** Remove a segment from a stem. */
    void removeSegment(int stemIndex, int segmentIndex);

    /** Resize a segment. */
    void resizeSegment(int stemIndex, int segmentIndex, int64_t newStart, int64_t newEnd);

    /** Clear all segments on a stem. */
    void clearSegments(int stemIndex);

    /** Get segment at a stem + index. */
    Segment* getSegment(int stemIndex, int segmentIndex);
    const Segment* getSegment(int stemIndex, int segmentIndex) const;

    // ── MIDI Assignment ──────────────────────────────────────────────
    /** Auto-assign MIDI notes sequentially across all stems (C1 = 36 upward). */
    void quickAssignMidiNotes(int startNote = 36);

    /** Assign a specific MIDI note to a segment. */
    void assignMidiNote(int stemIndex, int segmentIndex, int midiNote);

    /** Find which stem+segment owns a given MIDI note. Returns {stemIdx, segIdx} or {-1,-1}. */
    std::pair<int, int> findSegmentByMidiNote(int midiNote) const;

    /** Find ALL stem+segment pairs that own a given MIDI note (for LINK ALL multi-stem triggering). */
    std::vector<std::pair<int, int>> findAllSegmentsByMidiNote(int midiNote) const;

    /** Clear MIDI assignment from a segment. */
    void clearMidiNote(int stemIndex, int segmentIndex);

    // ── Effects ──────────────────────────────────────────────────────
    void setSegmentEffects(int stemIndex, int segmentIndex, const SegmentEffects& fx);
    SegmentEffects getSegmentEffects(int stemIndex, int segmentIndex) const;

    // ── Stem Linking ─────────────────────────────────────────────────
    void setLinked(int stemIndex, bool linked);
    bool isLinked(int stemIndex) const;
    std::vector<int> getLinkedStems() const;

    // ── Solo / Mute ──────────────────────────────────────────────────
    void setSolo(int stemIndex, bool solo);
    void setMute(int stemIndex, bool mute);
    void setStemVolume(int stemIndex, float volume);

    /** Returns true if this stem should produce audio (respects solo/mute). */
    bool isStemAudible(int stemIndex) const;

    // ── Undo / Redo ──────────────────────────────────────────────────
    /** Push current state onto the undo stack. `label` appears in tooltips/logs.
     *  No-op while a restore is in progress (re-entrancy guard). */
    void pushUndoState(const juce::String& label = {});

    bool undo();
    bool redo();

    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }

    /** Peek the label that will be used for the next undo/redo (UI tooltips). */
    juce::String peekUndoLabel() const { return undoStack_.empty() ? juce::String() : undoStack_.back().label; }
    juce::String peekRedoLabel() const { return redoStack_.empty() ? juce::String() : redoStack_.back().label; }

    /** Clear both stacks — call on stem load so undo can't walk into a different session. */
    void clearUndoHistory();

    /** Temporarily suppress pushUndoState. Use around post-undo UI refresh so that
     *  helpers (applyBpm, setGridDivision, …) don't push a fresh entry and wipe redo. */
    void beginSuppressPush() { restoringSnapshot_ = true; }
    void endSuppressPush()   { restoringSnapshot_ = false; }

    /** Fired after push / undo / redo / clear. UI uses it to enable/disable buttons. */
    std::function<void()> onUndoStackChanged;

    /** Hooks so SModePage can contribute extra state (lane patterns, grid lock, …)
     *  to each snapshot without the model knowing about UI types. */
    using ExtraStateProvider = std::function<juce::var(void)>;
    using ExtraStateRestorer = std::function<void(const juce::var&)>;
    void setExtraStateHooks(ExtraStateProvider get, ExtraStateRestorer set)
    {
        extraGet_ = std::move(get);
        extraSet_ = std::move(set);
    }

    // ── Tempo ────────────────────────────────────────────────────────
    void setDetectedTempo(double bpm) { detectedBpm_ = bpm; }
    double getDetectedTempo() const { return detectedBpm_; }

    void setSampleRate(double sr) { sampleRate_ = sr; }
    double getSampleRate() const { return sampleRate_; }

    // ── Raw access (for serialization) ───────────────────────────────
    const std::vector<StemData>& getStems() const { return stems_; }
    std::vector<StemData>& getStems() { return stems_; }

private:
    std::vector<StemData> stems_;
    double sampleRate_ { 44100.0 };
    double detectedBpm_ { 0.0 };

    // Undo snapshot — captures everything edit-undoable. Audio buffers and
    // spectral caches stay put (those don't change with edits).
    struct SegmentSnapshot
    {
        struct StemSnapshot
        {
            std::vector<Segment> segments;   // segments include per-segment FX (SegmentEffects lives inside Segment)
            bool  linked { false };
            bool  solo   { false };
            bool  mute   { false };
            float volume { 1.0f };
        };
        std::vector<StemSnapshot> stems;
        double       detectedBpm { 0.0 };
        double       sampleRate  { 44100.0 };
        juce::var    extra;             // opaque UI state supplied by SModePage
        juce::String label;             // shown in tooltips / logs
    };

    std::vector<SegmentSnapshot> undoStack_;
    std::vector<SegmentSnapshot> redoStack_;
    static constexpr int kMaxUndoLevels { 50 };

    // Re-entrancy guard — push is a no-op while we're restoring.
    bool restoringSnapshot_ { false };

    ExtraStateProvider extraGet_;
    ExtraStateRestorer extraSet_;

    SegmentSnapshot captureSnapshot(const juce::String& label = {}) const;
    void restoreSnapshot(const SegmentSnapshot& snap);
    void notifyUndoStackChanged();
};

} // namespace samplar
