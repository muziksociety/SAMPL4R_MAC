#pragma once

// Bridges the per-chop settings of the S-Mode model (SegmentEffects) into an
// engine Snapshot (SegSnap), plus the per-lane sequencer resolution (B18).
//
// SModeSegmentModel::buildPlaybackSnapshot() (owned by the persistence
// workstream) copies only positions / note / loop. fillSegmentFx() completes
// the snapshot on the message thread right after it is built, before it is
// published, so the realtime engine and every offline export see the same
// values (one renderer, B26).
//
// attackMs / releaseMs / triggerMode are read only if SegmentEffects has those
// members (detected at compile time), so this header keeps compiling before
// and after the model gains them.

#include "EngineSnapshot.h"
#include "../../services/smode/SModeSegmentModel.h"
#include <cmath>
#include <cstring>
#include <type_traits>
#include <utility>

namespace samplar {
namespace engine {

namespace segfx_detail
{
    template <class T, class = void> struct HasAttack  : std::false_type {};
    template <class T> struct HasAttack<T, std::void_t<decltype(std::declval<T&>().attackMs)>>  : std::true_type {};
    template <class T, class = void> struct HasRelease : std::false_type {};
    template <class T> struct HasRelease<T, std::void_t<decltype(std::declval<T&>().releaseMs)>> : std::true_type {};
    template <class T, class = void> struct HasTrigger : std::false_type {};
    template <class T> struct HasTrigger<T, std::void_t<decltype(std::declval<T&>().triggerMode)>> : std::true_type {};
}

/** True when SegmentEffects carries attack / release / trigger mode. */
constexpr bool segmentFxHasEnvelope() noexcept
{
    return segfx_detail::HasAttack<SegmentEffects>::value
        && segfx_detail::HasRelease<SegmentEffects>::value;
}
constexpr bool segmentFxHasTriggerMode() noexcept
{
    return segfx_detail::HasTrigger<SegmentEffects>::value;
}

template <class Fx> float getAttackMs(const Fx& fx) noexcept
{
    if constexpr (segfx_detail::HasAttack<Fx>::value) return (float) fx.attackMs;
    else { (void) fx; return 0.0f; }
}
template <class Fx> float getReleaseMs(const Fx& fx) noexcept
{
    if constexpr (segfx_detail::HasRelease<Fx>::value) return (float) fx.releaseMs;
    else { (void) fx; return 0.0f; }
}
/** 0 = follow the global 1SH/GATE switch, 1 = one-shot, 2 = gate. */
template <class Fx> int getTriggerMode(const Fx& fx) noexcept
{
    if constexpr (segfx_detail::HasTrigger<Fx>::value) return (int) fx.triggerMode;
    else { (void) fx; return 0; }
}
template <class Fx> void setAttackMs(Fx& fx, float v) noexcept
{
    if constexpr (segfx_detail::HasAttack<Fx>::value) fx.attackMs = v; else { (void) fx; (void) v; }
}
template <class Fx> void setReleaseMs(Fx& fx, float v) noexcept
{
    if constexpr (segfx_detail::HasRelease<Fx>::value) fx.releaseMs = v; else { (void) fx; (void) v; }
}
template <class Fx> void setTriggerMode(Fx& fx, int v) noexcept
{
    if constexpr (segfx_detail::HasTrigger<Fx>::value) fx.triggerMode = v; else { (void) fx; (void) v; }
}

/** Fingerprint of every per-chop setting the engine reads (the model's
 *  computePlaybackHash() does not cover them). Message thread. */
inline uint64_t computeSegmentFxHash(const SModeSegmentModel& model) noexcept
{
    uint64_t h = 1469598103934665603ULL;
    auto mix  = [&h](uint64_t v) { for (int i = 0; i < 8; ++i) { h ^= (v >> (i * 8)) & 0xFFu; h *= 1099511628211ULL; } };
    auto mixF = [&mix](float f) { uint32_t u; std::memcpy(&u, &f, sizeof(u)); mix(u); };
    for (const auto& st : model.getStems())
    {
        mix((uint64_t) st.segments.size());
        for (const auto& seg : st.segments)
        {
            const auto& fx = seg.effects;
            mix((uint64_t) (int64_t) fx.pitchSemitones);
            mixF(fx.volume);
            mixF(fx.pan);
            mix(fx.reverse ? 1u : 0u);
            mixF(getAttackMs(fx));
            mixF(getReleaseMs(fx));
            mix((uint64_t) getTriggerMode(fx));
        }
    }
    return h;
}

/** Copies the per-chop settings of `model` into `snap.segs` and the lane
 *  resolution of `lanes` into `snap.lanes` (same order as the builder used). */
inline void fillSegmentFx(Snapshot& snap, const SModeSegmentModel& model,
                          const std::vector<LaneDesc>& lanes) noexcept
{
    const auto& stems = model.getStems();
    for (auto& ss : snap.segs)
    {
        if (ss.stem < 0 || ss.stem >= (int) stems.size()) continue;
        const auto& segs = stems[(size_t) ss.stem].segments;
        if (ss.segIndex < 0 || ss.segIndex >= (int) segs.size()) continue;
        const auto& fx = segs[(size_t) ss.segIndex].effects;
        ss.pitchSemis = (float) juce::jlimit(-24, 24, fx.pitchSemitones);
        ss.gain       = std::isfinite(fx.volume) ? juce::jlimit(0.0f, 4.0f, fx.volume) : 1.0f;
        ss.pan        = std::isfinite(fx.pan) ? juce::jlimit(-1.0f, 1.0f, fx.pan) : 0.0f;
        ss.reverse    = fx.reverse;
        ss.trigger    = (uint8_t) juce::jlimit(0, 2, getTriggerMode(fx));
        ss.attackMs   = juce::jlimit(0.0f, 2000.0f, getAttackMs(fx));
        ss.releaseMs  = juce::jlimit(0.0f, 5000.0f, getReleaseMs(fx));
    }
    for (size_t l = 0; l < snap.lanes.size() && l < lanes.size(); ++l)
        snap.lanes[l].cellsPerBeat = juce::jlimit(1, 4, lanes[l].cellsPerBeat);
}

} // namespace engine
} // namespace samplar
