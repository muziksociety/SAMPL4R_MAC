#include "DownbeatDetector.h"

#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <vector>
#include <algorithm>

namespace samplar {

namespace {

constexpr int    kFftOrder     = 11;          // 2^11 = 2048
constexpr int    kFftSize      = 1 << kFftOrder;
constexpr int    kHopSize      = kFftSize / 2; // 50% overlap -> ~46ms frame, ~23ms hop @44.1k
// Kick-only band: kicks dominate the downbeat (beat 1) in pop/rock/hip-hop/
// electronic. Snare lives on backbeats (2,4 in 4/4) and hi-hat on off-beats —
// neither should bias the downbeat phase estimate. Kick fundamentals sit at
// 30-80 Hz; the punch / body extends to ~150 Hz. Restricting the spectral
// flux to this band gives a much cleaner downbeat phase signal than the
// previous 50–4000 Hz broadband window.
constexpr float  kBandLowHz    = 30.0f;
constexpr float  kBandHighHz   = 150.0f;
// Fallback (broadband) used only when kick-band signal is too weak — e.g.,
// a drum stem with mostly percussion / no kicks (jazz brush kits, bossa).
constexpr float  kFallbackLowHz  = 50.0f;
constexpr float  kFallbackHighHz = 4000.0f;
constexpr float  kKickConfidenceFloor = 0.08f; // retry broadband below this
constexpr int    kAnalysisBars = 8;            // first 8 bars of 4/4 -> 32 beats
constexpr float  kSilenceDbfs  = -60.0f;       // RMS gate

inline float dbToGain(float db) { return std::pow(10.0f, db * 0.05f); }

} // namespace

DownbeatDetector::Result DownbeatDetector::detect(
    const juce::AudioBuffer<float>& drumStem,
    double sampleRate,
    double bpm)
{
    juce::Logger::writeToLog("🎵 [DownbeatDetector] === detect === bpm=" + juce::String(bpm, 2)
                             + " sr=" + juce::String(sampleRate, 1)
                             + " samples=" + juce::String(drumStem.getNumSamples())
                             + " ch=" + juce::String(drumStem.getNumChannels()));

    Result r;

    // ── Validate inputs ──────────────────────────────────────────────────
    if (drumStem.getNumSamples() <= 0 || drumStem.getNumChannels() <= 0)
    {
        juce::Logger::writeToLog("❌ [DownbeatDetector] empty buffer");
        return r;
    }
    if (sampleRate <= 0.0)
    {
        juce::Logger::writeToLog("❌ [DownbeatDetector] bad sampleRate");
        return r;
    }
    if (bpm < 60.0 || bpm > 220.0)
    {
        juce::Logger::writeToLog("❌ [DownbeatDetector] bpm out of range [60,220]");
        return r;
    }

    const int    numSamples    = drumStem.getNumSamples();
    const int    numChannels   = drumStem.getNumChannels();
    const double tauSamples    = (60.0 / bpm) * sampleRate;
    const double tauFrames     = tauSamples / static_cast<double>(kHopSize);

    if (tauFrames < 4.0)
    {
        juce::Logger::writeToLog("❌ [DownbeatDetector] tauFrames too small: " + juce::String(tauFrames, 3));
        return r;
    }

    // ── Downmix to mono ──────────────────────────────────────────────────
    std::vector<float> mono(static_cast<size_t>(numSamples), 0.0f);
    {
        const float inv = 1.0f / static_cast<float>(numChannels);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* rd = drumStem.getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i)
                mono[static_cast<size_t>(i)] += rd[i] * inv;
        }
    }

    // ── Silence gate ──────────────────────────────────────────────────────
    // Use the ENTIRE track for downbeat phase analysis (per user request).
    // More samples averages out local drum variations (fills, breaks) so the
    // comb-filter phase locks to the consistent kick-pattern phase across
    // the song rather than getting biased by the intro.
    const int analysisSamples = numSamples;
    {
        double sumSq = 0.0;
        for (int i = 0; i < analysisSamples; ++i)
            sumSq += static_cast<double>(mono[static_cast<size_t>(i)]) * mono[static_cast<size_t>(i)];
        const double rms = std::sqrt(sumSq / std::max(1, analysisSamples));
        const float gate = dbToGain(kSilenceDbfs);
        if (rms < gate)
        {
            juce::Logger::writeToLog("❌ [DownbeatDetector] silent stem (rms=" + juce::String(rms, 6) + ")");
            return r;
        }
    }

    // ── STFT + spectral flux envelope ────────────────────────────────────
    const int numFrames = (analysisSamples - kFftSize) / kHopSize + 1;
    if (numFrames < static_cast<int>(std::ceil(tauFrames * 4.0)))
    {
        juce::Logger::writeToLog("❌ [DownbeatDetector] too few frames (" + juce::String(numFrames)
                                 + ") for at least 1 bar at tau=" + juce::String(tauFrames, 2));
        return r;
    }

    juce::dsp::FFT fft(kFftOrder);

    // Hann window
    std::vector<float> hann(static_cast<size_t>(kFftSize));
    {
        const double twoPiN = juce::MathConstants<double>::twoPi / (kFftSize - 1);
        for (int i = 0; i < kFftSize; ++i)
            hann[static_cast<size_t>(i)] = 0.5f * (1.0f - static_cast<float>(std::cos(twoPiN * i)));
    }

    std::vector<float> prevMag(static_cast<size_t>(kFftSize / 2 + 1), 0.0f);
    std::vector<float> curMag (static_cast<size_t>(kFftSize / 2 + 1), 0.0f);
    std::vector<float> flux   (static_cast<size_t>(numFrames),        0.0f);

    // FFT scratch: real-input FFT needs 2 * size floats
    std::vector<float> fftBuf(static_cast<size_t>(2 * kFftSize), 0.0f);

    auto computeFluxInBand = [&](float lowHz, float highHz)
    {
        std::fill(prevMag.begin(), prevMag.end(), 0.0f);
        std::fill(curMag.begin(),  curMag.end(),  0.0f);
        std::fill(flux.begin(),    flux.end(),    0.0f);

        const int bandLowBin  = std::max(1,
            static_cast<int>(std::floor(lowHz  * kFftSize / sampleRate)));
        const int bandHighBin = std::min(kFftSize / 2,
            static_cast<int>(std::ceil(highHz * kFftSize / sampleRate)));

        for (int f = 0; f < numFrames; ++f)
        {
            const int start = f * kHopSize;
            std::fill(fftBuf.begin(), fftBuf.end(), 0.0f);
            for (int i = 0; i < kFftSize; ++i)
                fftBuf[static_cast<size_t>(i)] = mono[static_cast<size_t>(start + i)] * hann[static_cast<size_t>(i)];

            fft.performFrequencyOnlyForwardTransform(fftBuf.data());

            float frameFlux = 0.0f;
            for (int b = bandLowBin; b <= bandHighBin; ++b)
            {
                const float mag  = fftBuf[static_cast<size_t>(b)];
                const float diff = mag - prevMag[static_cast<size_t>(b)];
                if (diff > 0.0f) frameFlux += diff;
                curMag[static_cast<size_t>(b)] = mag;
            }
            flux[static_cast<size_t>(f)] = frameFlux;
            std::swap(prevMag, curMag);
        }
    };

    // Primary pass: KICK-ONLY band (kicks fall on the downbeat).
    computeFluxInBand(kBandLowHz, kBandHighHz);
    juce::String bandUsed = "kick(30-150Hz)";

    // Zero out frame 0 — the FFT diff against an all-zero prevMag produces
    // a spurious huge flux value that wrecks the backward walk in the
    // attack-snap step (otherwise the walk-back terminates at frame 0
    // every time, regardless of actual onset position).
    if (!flux.empty()) flux[0] = 0.0f;

    auto projectPhase = [&]() -> std::pair<double /*conf*/, double /*phaseRad*/>
    {
        float fluxMax = 0.0f;
        for (float v : flux) fluxMax = std::max(fluxMax, v);
        if (fluxMax <= 0.0f) return { 0.0, 0.0 };
        const float invMax = 1.0f / fluxMax;
        double fluxSum = 0.0;
        for (float& v : flux) { v *= invMax; fluxSum += v; }

        // Use ALL frames of the buffer for phase projection — averages
        // across the entire track for stability.
        const int analysisFrames = numFrames;

        double re = 0.0, im = 0.0;
        const double w = juce::MathConstants<double>::twoPi / tauFrames;
        for (int n = 0; n < analysisFrames; ++n)
        {
            const double a = w * static_cast<double>(n);
            re += static_cast<double>(flux[static_cast<size_t>(n)]) * std::cos(a);
            im += static_cast<double>(flux[static_cast<size_t>(n)]) * std::sin(a);
        }
        const double m = std::sqrt(re * re + im * im);
        double c = (fluxSum > 0.0) ? (m / fluxSum) : 0.0;
        if (c > 1.0) c = 1.0;
        return { c, std::atan2(im, re) };
    };

    auto [confidence, phaseRadKick] = projectPhase();
    double phaseRad = phaseRadKick;

    // Fallback: if kick-band confidence is too weak (no kicks audible — jazz,
    // brushes, sparse percussion), recompute with the broadband window so we
    // still get *some* phase signal from snare/hat onsets.
    if (confidence < kKickConfidenceFloor)
    {
        juce::Logger::writeToLog("🎵 [DownbeatDetector] kick-band conf=" + juce::String(confidence, 3)
            + " < floor — falling back to broadband");
        computeFluxInBand(kFallbackLowHz, kFallbackHighHz);
        if (!flux.empty()) flux[0] = 0.0f;  // same frame-0 zero-init fix
        auto [c2, p2] = projectPhase();
        if (c2 > confidence) { confidence = c2; phaseRad = p2; bandUsed = "broadband(50-4000Hz)"; }
    }
    // wrap into [0, 2pi)
    if (phaseRad < 0.0) phaseRad += juce::MathConstants<double>::twoPi;
    double phaseFrames = phaseRad / juce::MathConstants<double>::twoPi * tauFrames;

    int64_t beat1Samples = static_cast<int64_t>(std::llround(phaseFrames * kHopSize));
    const int64_t beatPeriodSamples = static_cast<int64_t>(std::llround(tauSamples));

    // Clamp into [0, tau) range
    while (beat1Samples >= beatPeriodSamples) beat1Samples -= beatPeriodSamples;
    while (beat1Samples < 0)                  beat1Samples += beatPeriodSamples;

    // ── Attack-onset snap ────────────────────────────────────────────────
    // The phase estimate locks the comb filter to where the kick PATTERN
    // repeats — but the comb response peaks ~mid-transient, NOT at the
    // attack onset. Drum-track downbeat grids need to land where the
    // transient STARTS, not where it peaks. Two-step refinement:
    //   1) Find the strongest flux peak within ±tau/4 of the phase estimate
    //      (locks to the actual nearest kick).
    //   2) Walk BACKWARD from that flux peak while flux > 20% of peak —
    //      stops at the moment energy STARTS rising (attack onset).
    //   3) Snap to the nearest zero-crossing in the audio buffer for
    //      sample-level visual precision (no half-sample-into-kick artifact).
    {
        // (1) locate the nearest kick to the phase estimate
        const int beat1Frame    = std::max(0,
            std::min(numFrames - 1, static_cast<int>(beat1Samples / kHopSize)));
        const int searchRadius  = std::max(2, static_cast<int>(tauFrames * 0.25));
        const int searchLo      = std::max(0, beat1Frame - searchRadius);
        const int searchHi      = std::min(numFrames - 1, beat1Frame + searchRadius);

        int   peakFrame = beat1Frame;
        float peakVal   = flux[static_cast<size_t>(beat1Frame)];
        for (int f = searchLo; f <= searchHi; ++f)
        {
            const float v = flux[static_cast<size_t>(f)];
            if (v > peakVal) { peakVal = v; peakFrame = f; }
        }

        // (2) walk backward to find where flux first rose above 20% of peak.
        //     This stops at the FIRST frame where flux is already above
        //     threshold (i.e. the rise has begun). The actual sample-level
        //     attack onset sits within that frame's audio window — DO NOT
        //     step further back, that overshoots into the prior silent gap.
        const float attackThresh = peakVal * 0.20f;
        int attackFrame = peakFrame;
        while (attackFrame > 0
               && flux[static_cast<size_t>(attackFrame - 1)] > attackThresh)
        {
            --attackFrame;
        }

        // Anchor sample = start of the attack frame.
        const int64_t frameStart = static_cast<int64_t>(attackFrame) * kHopSize;
        int64_t attackSamples = frameStart;

        // (3) Time-domain sample-level refinement using a KICK-BAND envelope.
        //     The FFT framing alone can't resolve sub-frame (~23 ms) onset
        //     position — kick rises often complete within one frame. We
        //     filter the audio to the kick band (one-pole low-pass at ~200
        //     Hz drops hi-hat / snare noise), build a rectified envelope
        //     smoothed at ~50 Hz, then locate the rising edge where the
        //     envelope first crosses 12% of local peak. That's where the
        //     kick body STARTS — sample-accurate.
        //
        //     Search window centered on frameStart: small, just ±1 FFT
        //     frame either side. The flux walk-back has already located
        //     the attack frame to ±1-frame precision; we only need sub-
        //     frame sample resolution here, NOT a half-beat re-search
        //     that would catch the wrong kick or pre-track noise.
        const int halfWin = kHopSize;
        const int64_t winLo = std::max<int64_t>(0, frameStart - halfWin);
        const int64_t winHi = std::min<int64_t>(numSamples, frameStart + halfWin);
        const int winLen   = static_cast<int>(winHi - winLo);

        if (winLen > 64)
        {
            // One-pole low-pass: y[n] = a * x[n] + (1-a) * y[n-1].
            // Cutoff ~200 Hz: a = 2π·200 / sr  ≈ 0.0285 at 44.1k.
            const float lpA = static_cast<float>(2.0 * juce::MathConstants<double>::pi
                                                 * 200.0 / sampleRate);
            // Envelope smoother ~50 Hz:
            const float envA = static_cast<float>(2.0 * juce::MathConstants<double>::pi
                                                  * 50.0 / sampleRate);

            std::vector<float> env(static_cast<size_t>(winLen), 0.0f);
            float lp = 0.0f, sm = 0.0f;
            // Prime the filters with a small lead-in for stability
            const int64_t primeLen = std::min<int64_t>(2048, winLo);
            for (int64_t i = 0; i < primeLen; ++i)
            {
                const float x = mono[static_cast<size_t>(winLo - primeLen + i)];
                lp += lpA * (x - lp);
                const float r = std::abs(lp);
                sm += envA * (r - sm);
            }
            float envPeak = 0.0f;
            for (int i = 0; i < winLen; ++i)
            {
                const float x = mono[static_cast<size_t>(winLo + i)];
                lp += lpA * (x - lp);
                const float r = std::abs(lp);
                sm += envA * (r - sm);
                env[static_cast<size_t>(i)] = sm;
                envPeak = std::max(envPeak, sm);
            }

            if (envPeak > 0.0f)
            {
                // Find the FIRST index where envelope crosses 8% of envPeak
                // going UP. The threshold is a balance: too low (~4%) picks
                // up the previous kick's low-frequency tail and lands the
                // grid in the silent gap before the kick; too high (~12%)
                // lands the grid inside the kick body on the spectral
                // colour-shift point. 8% sits at the kick's true front edge.
                const float thresh = envPeak * 0.08f;
                int riseIdx = -1;
                for (int i = 1; i < winLen; ++i)
                {
                    if (env[static_cast<size_t>(i - 1)] < thresh
                        && env[static_cast<size_t>(i)] >= thresh)
                    {
                        riseIdx = i;
                        break;
                    }
                }
                if (riseIdx >= 0)
                {
                    int64_t hit = winLo + riseIdx;

                    // No explicit smoother-delay compensation — the 4%
                    // threshold (vs 12% prior) already grabs the rise ~3-4
                    // ms earlier, which approximately cancels the combined
                    // 200 Hz LP + 50 Hz envelope group delay. Adding more
                    // pushes the grid into silence BEFORE the kick.

                    // Walk backward to nearest zero-crossing on the FULL
                    // audio for a visually clean grid line. Up to ~3 ms.
                    const int zcLimit = std::min<int>(128, static_cast<int>(hit));
                    int64_t zc = hit;
                    for (int back = 1; back <= zcLimit; ++back)
                    {
                        const float a = mono[static_cast<size_t>(hit - back)];
                        const float b = mono[static_cast<size_t>(hit - back + 1)];
                        if (a * b <= 0.0f) { zc = hit - back; break; }
                    }
                    attackSamples = zc;
                }
            }
        }

        // Clamp into [0, beatPeriod)
        while (attackSamples >= beatPeriodSamples) attackSamples -= beatPeriodSamples;
        while (attackSamples < 0)                  attackSamples += beatPeriodSamples;

        juce::Logger::writeToLog("🎵 [DownbeatDetector] attack-snap: phase="
            + juce::String(beat1Samples) + " -> peak@frame=" + juce::String(peakFrame)
            + " -> attack@" + juce::String(attackSamples) + " samples");

        beat1Samples = attackSamples;
    }

    r.beat1Sample = beat1Samples;
    r.confidence  = confidence;
    r.valid       = true;

    juce::Logger::writeToLog("✅ [DownbeatDetector] beat1=" + juce::String(r.beat1Sample)
                             + " samples (" + juce::String(static_cast<double>(r.beat1Sample) / sampleRate, 4) + "s)"
                             + " conf=" + juce::String(r.confidence, 3)
                             + " tauSamp=" + juce::String(beatPeriodSamples)
                             + " band=" + bandUsed);
    return r;
}

} // namespace samplar
