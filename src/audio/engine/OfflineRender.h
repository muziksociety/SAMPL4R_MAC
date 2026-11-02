#pragma once

// Offline helpers built on the SAME PlaybackEngine that plays in realtime:
//   - renderSequence: sequencer lanes -> WAV buffer (drag WAV / export)
//   - renderSegment:  one chop incl. stem pitch + fades + pitched tail
//   - resampleStem:   one-off high-quality resample of a stem to the host rate (A23)
// All functions allocate and may take a while: call them on a worker thread,
// never on the audio thread.

#include "EngineTypes.h"
#include "EngineSnapshot.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <functional>
#include <memory>

namespace samplar {
namespace engine {

/** Lanes in `laneMask`, free-running from beat 0 for `numCells` beats at `bpm`.
 *  `snap` must outlive the call. Returns nullptr if there is nothing to render. */
std::unique_ptr<juce::AudioBuffer<float>> renderSequence(const EngineParams& params,
                                                         const Snapshot& snap,
                                                         double sampleRate,
                                                         uint32_t laneMask,
                                                         int numCells,
                                                         double bpm,
                                                         const std::function<void(float)>& progress = {},
                                                         int blockSize = 512);

/** One chop, rendered until the voice has fully decayed (cap: maxSeconds). */
std::unique_ptr<juce::AudioBuffer<float>> renderSegment(const EngineParams& params,
                                                        const Snapshot& snap,
                                                        double sampleRate,
                                                        int stem,
                                                        int segIndex,
                                                        double maxSeconds = 120.0,
                                                        int blockSize = 512);

/** Windowed-sinc resample of a whole buffer (srcRate -> dstRate). Returns
 *  nullptr if `shouldAbort` returned true. */
std::shared_ptr<juce::AudioBuffer<float>> resampleStem(const juce::AudioBuffer<float>& src,
                                                       double srcRate,
                                                       double dstRate,
                                                       const std::function<bool()>& shouldAbort = {});

} // namespace engine
} // namespace samplar
