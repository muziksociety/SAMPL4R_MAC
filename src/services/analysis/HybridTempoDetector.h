#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "TempoAnalyzer.h"
#include "DWTTempoDetector.h"
#include "BeatThisDetector.h"

namespace samplar {

/**
 * Hybrid Tempo Detector
 *
 * Combines FFT Spectral Flux and DWT-based detection methods.
 * Uses confidence-weighted merge for robust BPM estimation.
 *
 * Merge Strategy:
 * 1. Run both detectors independently
 * 2. Check if results are harmonically related (half/double time)
 * 3. If results agree (within 3%), weighted average
 * 4. If results disagree, use higher confidence or resolve harmonics
 */
class HybridTempoDetector
{
public:
    struct Result
    {
        float bpm = 0.0f;
        float confidence = 0.0f;

        // Individual detector results (for debugging/analysis)
        float fftBPM = 0.0f;
        float fftConfidence = 0.0f;
        float dwtBPM = 0.0f;
        float dwtConfidence = 0.0f;

        // Beat This! neural detector (ISMIR 2024) — optional 4th detector,
        // enabled when WITH_BEAT_THIS is compiled in and Config::useBeatThis=true.
        float beatThisBPM         = 0.0f;
        float beatThisConfidence  = 0.0f;

        // Which detector was primary
        enum class Source { None, FFT, DWT, Merged, BeatThis } source = Source::None;

        // Octave correction (Schreiber & Müller 2014) applied as a post-step.
        // When wasCorrected==true, bpm above is already the corrected value;
        // preCorrectionBPM holds the fused candidate before correction.
        bool   octaveCorrected   = false;
        float  correctedBPM      = 0.0f;
        float  preCorrectionBPM  = 0.0f;
        float  onsetDensityHz    = 0.0f;

        juce::String getSourceName() const
        {
            switch (source)
            {
                case Source::FFT: return "FFT";
                case Source::DWT: return "DWT";
                case Source::Merged: return "Merged";
                case Source::BeatThis: return "BeatThis";
                default: return "None";
            }
        }
    };

    struct Config
    {
        // FFT detector config
        int fftSize = 2048;
        int hopSize = 512;
        float onsetThreshold = 0.3f;

        // DWT detector config
        int dwtLevels = 4;
        float windowSeconds = 3.0f;
        float windowHopSeconds = 1.5f;

        // Common config
        float minBPM = 40.0f;
        float maxBPM = 220.0f;
        float preferredBPM = 120.0f;

        // Merge config
        float agreementThreshold = 0.03f;  // 3% tolerance for "agreement"
        float harmonicWeight = 0.8f;       // Weight for harmonic resolution

        // Octave correction (Schreiber & Müller 2014) — disable-able for regression testing
        bool  enableOctaveCorrection = true;

        // Beat This! neural detector (overrides DSP result when confident enough).
        // No effect unless WITH_BEAT_THIS is compiled in.
        bool  useBeatThis              = true;
        float beatThisConfidenceFloor  = 0.5f; // accept neural result if conf >= this
    };

    HybridTempoDetector() = default;

    /**
     * Detect BPM using hybrid approach.
     */
    // Apple Clang rejects `= Config()` as a class-body default arg; callers
    // pass Config explicitly.
    Result detect(const juce::AudioBuffer<float>& buffer,
                  double sampleRate,
                  const Config& config);

    /**
     * Detect BPM from file.
     */
    Result detect(const juce::File& audioFile,
                  const Config& config);

private:
    TempoAnalyzer fftDetector_;
    DWTTempoDetector dwtDetector_;
    BeatThisDetector beatThisDetector_;
    juce::AudioFormatManager formatManager_;
    bool formatManagerInitialized_ = false;

    /**
     * Check if two BPMs are harmonically related (half, double, triple time).
     * Returns the relationship factor (0.5, 1.0, 2.0, 3.0) or 0 if unrelated.
     */
    float getHarmonicRelation(float bpm1, float bpm2);

    /**
     * Resolve harmonic ambiguity.
     * Prefers tempos in the 80-160 BPM "sweet spot".
     */
    float resolveHarmonic(float bpm1, float conf1, float bpm2, float conf2,
                          float harmonicFactor, const Config& config);

    /**
     * Confidence-weighted merge of two BPM estimates.
     */
    Result mergeResults(float fftBPM, float fftConf,
                        float dwtBPM, float dwtConf,
                        const Config& config);
};

} // namespace samplar
