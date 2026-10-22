#pragma once

// Immutable playback snapshot (RCU payload).
//
// Built on the message thread from SModeSegmentModel + the sequencer lanes,
// published to the audio thread by an atomic pointer swap, and freed on the
// message thread once the audio thread has acknowledged a newer one AND no
// voice still plays from it (SnapshotManager). The audio thread only ever
// sees `const Snapshot*` - it never owns, copies or releases a shared_ptr.

#include "EngineTypes.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <memory>
#include <vector>

namespace samplar {
namespace engine {

/** One stem: a shared, immutable audio buffer + its sample rate. */
struct StemSnap
{
    std::shared_ptr<const juce::AudioBuffer<float>> audio;   // keep-alive (never touched by the audio thread)
    const float* ch[2] { nullptr, nullptr };                 // raw read pointers (R == L for mono)
    int      numSamples { 0 };
    double   rate       { 44100.0 };   // sample rate of `audio`
    float    volume     { 1.0f };
    bool     audible    { true };      // mute / solo resolved
    /** Stable identity of the stem's SOURCE audio (N-11): the same for the
     *  model-rate buffer and its host-rate resampled copy, different for a new
     *  song. 0 = unknown (the engine then compares the read pointers). */
    uint64_t sourceId   { 0 };
};

/** One chop, in stem-buffer samples. */
struct SegSnap
{
    int     stem     { -1 };
    int     segIndex { -1 };   // index in the model (UI notifications)
    int64_t start    { 0 };
    int64_t end      { 0 };
    bool    loop     { false };
    int     midiNote { -1 };

    // Per-chop settings (SegmentEffects, filled by fillSegmentFx(); UX gap 4).
    float   pitchSemis { 0.0f };   // time-preserving, added to the stem pitch
    float   gain       { 1.0f };   // linear
    float   pan        { 0.0f };   // -1 (L) .. +1 (R)
    bool    reverse    { false };
    uint8_t trigger    { 0 };      // 0 = follow the global 1SH/GATE switch, 1 = one-shot, 2 = gate
    float   attackMs   { 0.0f };   // fade-in at the chop start
    float   releaseMs  { 0.0f };   // fade-out at the chop end / after note-off
};

/** One sequencer slot, window already trimmed, in stem-buffer samples. */
struct SlotSnap
{
    int     stem      { -1 };
    int     segIndex  { -1 };
    int     slotIndex { -1 };   // index into the lane's slot vector (UI highlight)
    int64_t start     { 0 };
    int64_t end       { 0 };
    int     gridCell  { 0 };
    int     cellSpan  { 1 };
    float   velocity  { 1.0f };
    float   pitchSemis{ 0.0f };
    float   speed     { 1.0f };
    bool    reverse   { false };
};

struct LaneSnap
{
    int  numCells { 16 };     // cells; one cell = 1 / cellsPerBeat beats
    int  cellsPerBeat { 1 };  // 1, 2 (8ths) or 4 (16ths) - B18
    bool muted    { false };
    std::vector<SlotSnap> slots;
    std::vector<int> cellStart;   // [numCells] slot index that STARTS at this cell, or -1
    std::vector<int> cellCover;   // [numCells] slot index that COVERS this cell, or -1
};

struct Snapshot
{
    uint64_t seq        { 0 };
    double   modelRate  { 44100.0 };   // rate of the model's sample positions (UI / transport clock)
    int      stemCount  { 0 };
    std::array<StemSnap, kMaxStems> stems {};
    std::vector<SegSnap> segs;                   // ordered by stem, then model segment index
    std::array<int, kMaxStems + 1> stemSegBase {};   // segs[stemSegBase[s] + segIndex]
    std::array<std::vector<int>, 128> noteMap;   // note -> indices into segs
    int64_t  transportLengthModel { 0 };   // longest stem (ALL stems, muted too), model samples (A14)
    std::vector<LaneSnap> lanes;
    double   maxSegmentSeconds { 0.0 };
};

// ---------------------------------------------------------------------------
// Builder input for sequencer lanes (value types, no UI dependency).
// ---------------------------------------------------------------------------
struct SlotDesc
{
    bool  empty      { true };
    int   stem       { -1 };
    int   segIndex   { -1 };
    int   gridCell   { 0 };
    int   cellSpan   { 1 };
    float velocity   { 1.0f };
    float pitchSemis { 0.0f };
    float speed      { 1.0f };
    bool  reverse    { false };
    float trimStart  { 0.0f };
    float trimEnd    { 1.0f };
};

struct LaneDesc
{
    int  numCells { 16 };
    int  cellsPerBeat { 1 };  // 1, 2 or 4 (sequencer x8 / x16 subdivisions, B18)
    bool muted    { false };
    std::vector<SlotDesc> slots;
};

/** Per-stem buffer override used by the builder: a host-rate resampled copy. */
struct StemOverride
{
    std::shared_ptr<const juce::AudioBuffer<float>> audio;
    double rate { 0.0 };   // 0 = no override
};

} // namespace engine
} // namespace samplar
