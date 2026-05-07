#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <optional>
#include "../BeatAnalysisTypes.h"

namespace samplar {

/**
 * TempoCache — disk-backed cache for BPM detection results.
 *
 * Keyed by SHA-256 of a stable hash over the audio buffer content:
 *   - first 64 KB of raw samples (channel 0)
 *   - last 64 KB of raw samples (channel 0)
 *   - numSamples
 *   - sampleRate
 *
 * Stored as JSON files under %APPDATA%/Samplar/tempo_cache/<hash>.json
 * (via DataPaths::getUserDataDirectory()).
 *
 * Numeric fields of BPMResult are persisted. The ODF vector is NOT cached
 * (bloats the file + downstream loop detection recomputes as needed). Any
 * consumer that strictly requires ODF should bypass the cache.
 *
 * Thread safety: read/write are independent file operations — safe to call
 * from any thread. No in-memory shared state.
 */
class TempoCache
{
public:
    /** Compute the cache key for a given audio buffer. */
    static juce::String computeKey(const juce::AudioBuffer<float>& buffer, double sampleRate);

    /** Look up a cached result. Returns nullopt on miss or parse error. */
    static std::optional<BPMResult> get(const juce::String& key);

    /** Store a result under the given key. Overwrites if present. */
    static void put(const juce::String& key, const BPMResult& result);

    /** Optional UX helper: delete the entry for this key (e.g. re-analyze). */
    static void invalidate(const juce::String& key);

    /** Path to the cache directory (created lazily). */
    static juce::File getCacheDirectory();

private:
    static juce::File keyToFile(const juce::String& key);
    static juce::var resultToVar(const BPMResult& r);
    static BPMResult varToResult(const juce::var& v);
};

} // namespace samplar
