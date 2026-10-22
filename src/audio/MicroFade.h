#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

namespace samplar {

/**
 * Global toggle for anti-click micro-fades on sample playback and export.
 *
 * Read by the realtime engine (src/audio/engine/PlaybackEngine) to apply a
 * short (default 2 ms) linear fade-in at a chop's start and fade-out at its
 * end. The same engine renders exports, so exports match playback.
 * Written by the Fade checkbox in SModePage.
 *
 * Thread-safe via std::atomic. Default: ON.
 *
 * When OFF, sample playback hits raw buffer edges (original behavior) and
 * clicks may be audible at segment boundaries.
 */
inline std::atomic<bool>& microFadeEnabledFlag()
{
    static std::atomic<bool> enabled{true};
    return enabled;
}

/** Fade length in milliseconds. Short enough to be inaudible (<= attack
    perception threshold) but long enough to neutralize discontinuity clicks. */
inline constexpr double kMicroFadeMs = 2.0;

/** Release fade used whenever a voice is stopped early (note-off in GATE,
    voice steal, sequencer step end, stop / all-notes-off). Always applied -
    a hard cut always clicks. */
inline constexpr double kReleaseFadeMs = 3.0;

/** Loop wrap crossfade, and the crossfade used when a voice switches from the
    direct path to the pitch shifter. */
inline constexpr double kLoopCrossfadeMs = 5.0;

/** Compute the per-sample fade gain for a voice currently at samplePosition
    within a source buffer of length srcSamples. Returns 1.0 in the interior
    of the buffer, 0..1 during the fade-in window, and 1..0 during the fade-out
    window. Returns 1.0 unconditionally if fading is globally disabled. */
inline float computeVoiceFadeGain(double samplePosition, int srcSamples, double sampleRate)
{
    if (!microFadeEnabledFlag().load(std::memory_order_relaxed)) return 1.0f;

    const int fadeSamples = static_cast<int>(kMicroFadeMs * 0.001 * sampleRate);
    if (fadeSamples <= 0 || srcSamples <= fadeSamples * 2) return 1.0f;

    if (samplePosition < static_cast<double>(fadeSamples))
        return static_cast<float>(samplePosition / fadeSamples);

    const double tailStart = static_cast<double>(srcSamples - fadeSamples);
    if (samplePosition > tailStart)
    {
        double remaining = static_cast<double>(srcSamples) - samplePosition;
        return juce::jlimit(0.0f, 1.0f, static_cast<float>(remaining / fadeSamples));
    }

    return 1.0f;
}

/** Apply a 2 ms linear fade-in/out in place to an audio buffer. Used by export
    paths so the exported WAV matches what the user heard during playback.
    No-op if the global fade flag is off. */
inline void applyMicroFade(juce::AudioBuffer<float>& buffer, double sampleRate,
                           double fadeMs = kMicroFadeMs)
{
    if (!microFadeEnabledFlag().load(std::memory_order_relaxed)) return;
    if (sampleRate <= 0.0 || buffer.getNumSamples() == 0) return;

    int fadeSamples = static_cast<int>(fadeMs * 0.001 * sampleRate);
    const int half = buffer.getNumSamples() / 2;
    if (fadeSamples > half) fadeSamples = half;
    if (fadeSamples <= 0) return;

    const int numSamples = buffer.getNumSamples();
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        buffer.applyGainRamp(ch, 0, fadeSamples, 0.0f, 1.0f);
        buffer.applyGainRamp(ch, numSamples - fadeSamples, fadeSamples, 1.0f, 0.0f);
    }
}

/** Same as applyMicroFade(), but targets a specific [startSample, startSample+numSamples)
    region of an existing buffer. Used by sequencer export to fade each slot's
    contribution independently (matching per-voice playback behavior). */
inline void applyMicroFadeRange(juce::AudioBuffer<float>& buffer,
                                 int startSample,
                                 int numSamples,
                                 double sampleRate,
                                 double fadeMs = kMicroFadeMs)
{
    if (!microFadeEnabledFlag().load(std::memory_order_relaxed)) return;
    if (sampleRate <= 0.0 || numSamples <= 0) return;
    if (startSample < 0 || startSample + numSamples > buffer.getNumSamples()) return;

    int fadeSamples = static_cast<int>(fadeMs * 0.001 * sampleRate);
    const int half = numSamples / 2;
    if (fadeSamples > half) fadeSamples = half;
    if (fadeSamples <= 0) return;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        buffer.applyGainRamp(ch, startSample, fadeSamples, 0.0f, 1.0f);
        buffer.applyGainRamp(ch, startSample + numSamples - fadeSamples, fadeSamples, 1.0f, 0.0f);
    }
}

} // namespace samplar
