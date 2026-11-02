#pragma once

// SAMPLAR realtime playback engine - shared value types.
//
// Threading contract (see PlaybackEngine.h for the full picture):
//   - EngineParams: atomics written by the UI / message thread, read by the
//     audio thread. Never locked.
//   - Command:      UI -> audio, through a lock-free FIFO (POD only).
//   - Notification: audio -> UI, through a lock-free FIFO (POD only), drained
//     by a message-thread timer.
//   - HostInfo:     filled by the processor from the host playhead each block.

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace samplar {
namespace engine {

constexpr int kMaxStems       = 8;
constexpr int kMaxLanes       = 8;     // UI allows 4; headroom for future lanes
constexpr int kMidiPolyphony  = 32;    // logical voices for MIDI + audition
constexpr int kMidiVoiceSlots = 40;    // + reserve so a stolen voice can fade out
constexpr int kSeqVoiceSlots  = 24;    // sequencer voices (choke per lane + release reserve)
constexpr int kTransportVoices = 2 * kMaxStems;   // one per stem + fade reserve for restart / scrub

enum class ModDest : uint8_t { None = 0, Pitch, FilterCutoff, Volume, Pan };
enum class VelCurve : uint8_t { Linear = 0, Exponential, Logarithmic, SoftKnee };

inline float applyVelCurve(float v, VelCurve c) noexcept
{
    v = juce::jlimit(0.0f, 1.0f, v);
    switch (c)
    {
        case VelCurve::Linear:      return v;
        case VelCurve::Exponential: return v * v;
        case VelCurve::Logarithmic: return std::sqrt(v);
        case VelCurve::SoftKnee:    return v < 0.7f ? v : 0.7f + (v - 0.7f) * 0.5f;
    }
    return v;
}

/** Per-stem realtime settings (UI writes, audio reads). */
struct StemParams
{
    std::atomic<float>   pitchSemis { 0.0f };   // stem pitch knob, -12..+12
    std::atomic<uint8_t> bendDest   { (uint8_t) ModDest::Pitch };
    std::atomic<uint8_t> wheelDest  { (uint8_t) ModDest::None };
    std::atomic<uint8_t> atDest     { (uint8_t) ModDest::None };
    std::atomic<float>   bendRange  { 2.0f };
    std::atomic<float>   wheelRange { 12.0f };
    std::atomic<float>   atRange    { 12.0f };
    std::atomic<uint8_t> velCurve   { (uint8_t) VelCurve::Linear };
};

/** Engine-wide realtime settings. One instance is shared by the live engine
 *  and every offline (export) engine, so exports use the same knob values. */
struct EngineParams
{
    std::array<StemParams, kMaxStems> stems;
    std::atomic<bool>   mono            { false };
    std::atomic<bool>   gate            { false };   // one-shots release on note-off
    std::atomic<bool>   transportLoop   { false };
    std::atomic<bool>   midiLearn       { false };   // next note-on is consumed + reported
    std::atomic<bool>   forwardCcToUi   { false };   // CC learn armed or bindings exist
    std::atomic<double> seqBpm          { 120.0 };   // free-running sequencer tempo
    std::atomic<float>  globalSemis     { 0.0f };    // grid-LOCK transpose, added to every voice (B17)
};

/** What the host playhead said for this block. */
struct HostInfo
{
    bool   valid     { false };   // a playhead exists and answered
    bool   playing   { false };
    bool   ppqValid  { false };
    double ppq       { 0.0 };     // quarter notes at the first sample of the block
    double bpm       { 0.0 };
    bool   offline   { false };   // host is bouncing (isNonRealtime)
};

// N-4: host playhead values are untrusted (bridged plugins, tempo-map edits,
// transport start): a non-finite or absurd BPM / PPQ must never reach the
// sequencer clock, whose tick loop and float -> int casts depend on them.
constexpr double kMinHostBpm = 20.0;
constexpr double kMaxHostBpm = 999.0;
constexpr double kMaxHostPpq = 1.0e7;   // ~7 days at 999 BPM; beyond = garbage

/** Sanitises `h` in place: BPM non-finite or <= 0 -> 0 (no tempo), else
 *  clamped to [kMinHostBpm, kMaxHostBpm]; PPQ non-finite or |ppq| >
 *  kMaxHostPpq -> no position. Returns true if anything was rejected or
 *  clamped. Realtime-safe (no allocation, no logging). */
inline bool sanitizeHostInfo(HostInfo& h) noexcept
{
    bool changed = false;
    if (! (std::isfinite(h.bpm) && h.bpm > 0.0))
    {
        changed = changed || h.bpm != 0.0;
        h.bpm = 0.0;
    }
    else if (h.bpm < kMinHostBpm || h.bpm > kMaxHostBpm)
    {
        h.bpm = juce::jlimit(kMinHostBpm, kMaxHostBpm, h.bpm);
        changed = true;
    }
    if (! std::isfinite(h.ppq) || std::abs(h.ppq) > kMaxHostPpq)
    {
        changed = changed || h.ppqValid;
        h.ppq = 0.0;
        h.ppqValid = false;
    }
    return changed;
}

/** Licence gate on the output (N-18): a flip ramps over `rampMs` instead of a
 *  hard cut / hard start. Audio thread; no allocation. */
struct GateRamp
{
    float gain { 1.0f };
    float step { 1.0f / 220.0f };

    void prepare(double sampleRate, bool open, double rampMs = 5.0) noexcept
    {
        step = 1.0f / (float) juce::jmax(1.0, rampMs * 0.001 * (sampleRate > 0.0 ? sampleRate : 44100.0));
        gain = open ? 1.0f : 0.0f;
    }
    /** Returns true if this block ramped (for diagnostics). */
    bool apply(juce::AudioBuffer<float>& buffer, int start, int num, bool open) noexcept
    {
        if (num <= 0) return false;
        const float target = open ? 1.0f : 0.0f;
        if (gain == target)
        {
            if (! open)
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    buffer.clear(ch, start, num);
            return false;
        }
        const float g0 = gain, s = step;
        auto next = [target, s](float g) noexcept
        {
            return target > g ? juce::jmin(target, g + s) : juce::jmax(target, g - s);
        };
        float gEnd = g0;
        for (int i = 0; i < num; ++i) gEnd = next(gEnd);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            float* d = buffer.getWritePointer(ch, start);
            float g = g0;
            for (int i = 0; i < num; ++i) { g = next(g); d[i] *= g; }
        }
        gain = gEnd;
        return true;
    }
};

enum class CmdType : uint8_t
{
    NoteOn,          // i0 note, f0 velocity
    NoteOff,         // i0 note
    StopVoices,      // release MIDI + audition + sequencer voices (not the transport)
    AllSoundOff,     // release everything incl. transport + sequencer
    PlaySegment,     // i0 stem, i1 segIndex, b0 loop, f0 velocity
    PlaySlot,        // i0 stem, i1 segIndex, f0 trimStart, f1 trimEnd, f2 speed, f3 pitch, f4 velocity, b0 reverse
    TransportStart,  // d0 start position in seconds, i0 generation (echoed in TransportStopped)
    TransportStop,
    SeqStart,        // i0 lane mask, i1 generation, d0 bpm override (<=0: params.seqBpm)
    SeqStop,
};

struct Command
{
    CmdType  type { CmdType::StopVoices };
    int      i0 { 0 }, i1 { 0 };
    float    f0 { 0 }, f1 { 0 }, f2 { 0 }, f3 { 0 }, f4 { 0 };
    double   d0 { 0 };
    bool     b0 { false };
    uint64_t snapSeq { 0 };     // snapshot the command was issued against
    uint32_t stampMs { 0 };     // juce::Time::getMillisecondCounter() when queued; 0 = never stale
};

/** Trigger commands that go stale while the host is not processing (A3 in
 *  review_round1): dropped when older than kStaleCommandMs. Stops, note-offs,
 *  transport / sequencer commands are always applied. */
inline bool isDroppableTrigger(CmdType t) noexcept
{
    return t == CmdType::NoteOn || t == CmdType::PlaySegment || t == CmdType::PlaySlot;
}
constexpr uint32_t kStaleCommandMs = 150;   // floor; the engine uses max(this, 3 x block duration) (N-13)

enum class NoteType : uint8_t
{
    SegmentTriggered,   // i0 stem, i1 segIndex, i2 note, d0 duration seconds
    SegmentStopped,     // i0 stem, i1 segIndex, i2 note
    TransportStopped,   // i0 generation
    SequencerStopped,   // i0 reason: 0 = host transport stopped, 1 = bypass / all sound off; i1 generation
    LearnNote,          // i0 note
    Controller,         // i0 channel, i1 cc, f0 value 0..1
};

struct Notification
{
    NoteType type { NoteType::TransportStopped };
    int      i0 { 0 }, i1 { 0 }, i2 { 0 };
    float    f0 { 0 };
    double   d0 { 0 };
};

/** Raw short MIDI message (standalone MIDI thread -> audio thread). */
struct RawMidi
{
    uint8_t b0 { 0 }, b1 { 0 }, b2 { 0 };
};

} // namespace engine
} // namespace samplar
