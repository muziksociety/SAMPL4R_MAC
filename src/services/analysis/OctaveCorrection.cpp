#include "OctaveCorrection.h"
#include "AnalysisDsp.h"

#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace samplar {

//==============================================================================
// Internal helpers
//==============================================================================

namespace {

// Robust median (in-place modifies a copy).
static float medianOf(std::vector<float> v)
{
    if (v.empty()) return 0.0f;
    const size_t n = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(n), v.end());
    return v[n];
}

// Median Absolute Deviation (MAD).
static float madOf(const std::vector<float>& v, float med)
{
    if (v.empty()) return 0.0f;
    std::vector<float> dev;
    dev.reserve(v.size());
    for (float x : v) dev.push_back(std::abs(x - med));
    return medianOf(std::move(dev));
}

// Peak-pick onsets with an adaptive threshold:
//   threshold(t) = median(window) + 1.5 * MAD(window)
// using a 1-second sliding window centered at t.
// Returns indices of peaks.
static std::vector<int> pickOnsetPeaks(const std::vector<float>& env,
                                       double envSR)
{
    std::vector<int> peaks;
    if (env.size() < 3 || envSR <= 0.0) return peaks;

    const int halfWin = std::max(1, static_cast<int>(envSR * 0.5)); // 1s window
    const int N = static_cast<int>(env.size());

    // Half-wave rectified + normalized envelope.
    float maxV = 0.0f;
    for (float v : env) maxV = std::max(maxV, std::max(0.0f, v));
    if (maxV <= 0.0f) return peaks;
    const float inv = 1.0f / maxV;

    std::vector<float> rect;
    rect.reserve(env.size());
    for (float v : env) rect.push_back(std::max(0.0f, v) * inv);

    for (int i = 1; i < N - 1; ++i)
    {
        // Local maximum check
        if (rect[static_cast<size_t>(i)] <= rect[static_cast<size_t>(i - 1)] ||
            rect[static_cast<size_t>(i)] <= rect[static_cast<size_t>(i + 1)])
            continue;

        const int lo = std::max(0, i - halfWin);
        const int hi = std::min(N, i + halfWin + 1);
        std::vector<float> window(rect.begin() + lo, rect.begin() + hi);

        const float med = medianOf(window);
        const float mad = madOf(window, med);
        const float thr = med + 1.5f * mad;

        if (rect[static_cast<size_t>(i)] > std::max(thr, 0.05f))
            peaks.push_back(i);
    }
    return peaks;
}

struct CandidateScore
{
    double bpm   = 0.0;
    double score = 0.0;
    double onsetsPerBeat = 0.0;
};

// Score a candidate by closeness of onsets/beat to a wide acceptable range
// ([1.5, 6.0] — covers everything from quarter-note pulse to 16th-note hat
// patterns common in hip-hop / dnb / electronic). Soft Gaussian falloff
// outside. Plus a loose sweet-spot bonus for 80-160 BPM (tactus preference).
static double scoreCandidate(double bpm, double density)
{
    if (bpm < 60.0 || bpm > 220.0) return 0.0;

    const double beatsPerSec = bpm / 60.0;
    if (beatsPerSec <= 0.0) return 0.0;

    const double opb = density / beatsPerSec;

    // Range score: 1.0 inside [1.5, 6.0] (widened from [1.5, 4.0] — 16th-note
    // hat pattern at slow BPM produces ~5-6 onsets/beat, which was previously
    // penalized and pushed the system to misdetect at higher BPMs).
    double rangeScore;
    if (opb >= 1.5 && opb <= 6.0)
    {
        rangeScore = 1.0;
    }
    else
    {
        const double d = (opb < 1.5) ? (1.5 - opb) : (opb - 6.0);
        rangeScore = std::exp(-(d * d) / (2.0 * 1.0 * 1.0));
    }

    // Sweet-spot bonus (tactus): full bonus in 80-160 BPM, small bonus 60-180.
    double sweetSpot;
    if (bpm >= 80.0 && bpm <= 160.0)       sweetSpot = 1.0;
    else if (bpm >= 60.0 && bpm <= 180.0)  sweetSpot = 0.85;
    else                                   sweetSpot = 0.65;

    return rangeScore * sweetSpot;
}

} // anonymous namespace

//==============================================================================
// Public API
//==============================================================================

OctaveCorrectionResult correctOctave(double candidateBPM,
                                     const std::vector<float>& onsetEnvelope,
                                     double envelopeSampleRate)
{
    OctaveCorrectionResult r;
    r.correctedBPM = candidateBPM;

    if (candidateBPM <= 0.0 || onsetEnvelope.empty() || envelopeSampleRate <= 0.0)
    {
        r.reasoning = "no-op (invalid input)";
        return r;
    }

    // 1) Onset density
    const auto peaks = pickOnsetPeaks(onsetEnvelope, envelopeSampleRate);
    const double durationSec = static_cast<double>(onsetEnvelope.size()) / envelopeSampleRate;
    const double density = (durationSec > 0.0)
                         ? static_cast<double>(peaks.size()) / durationSec
                         : 0.0;
    r.onsetDensityHz = density;

    // 2) Candidate set — includes OCTAVE multiples (T/2, T*2, T/3, T*3) AND
    //    METRIC ratios (T*3/4, T*2/3, T*4/3, T*3/2) so we catch 3-against-4
    //    compound/simple-time confusion (e.g. detector says 113 BPM when the
    //    actual is 85 BPM — ratio 113/85 = 1.33 = 4/3, a metric error not
    //    an octave error). Without these the system can NEVER correct that.
    const double T = candidateBPM;
    const std::vector<double> candidates = {
        T / 3.0,
        T / 2.0,
        T * 2.0 / 3.0,    // metric (3:2)
        T * 3.0 / 4.0,    // metric (4:3)  ← catches the 113 ↔ 85 case
        T,
        T * 4.0 / 3.0,    // metric (3:4)
        T * 3.0 / 2.0,    // metric (2:3)
        T * 2.0,
        T * 3.0,
    };

    CandidateScore best;
    best.bpm   = T;
    best.score = scoreCandidate(T, density);
    best.onsetsPerBeat = density / (T / 60.0);

    CandidateScore scoreT = best;

    // Ratio "simplicity" rank: the detector's own T first, then octaves,
    // then 3:2 metric, then 4:3, then triples. Used to break score ties.
    auto rankOf = [T](double c) -> int
    {
        const double r = c / T;
        if (std::abs(r - 1.0) < 1e-6) return 0;
        if (std::abs(r - 2.0) < 1e-6 || std::abs(r - 0.5) < 1e-6) return 1;
        if (std::abs(r - 1.5) < 1e-6 || std::abs(r - 2.0 / 3.0) < 1e-6) return 2;
        if (std::abs(r - 4.0 / 3.0) < 1e-6 || std::abs(r - 0.75) < 1e-6) return 3;
        return 4;
    };
    CandidateScore scoreHalf, scoreDouble;
    scoreHalf.bpm   = T / 2.0;
    scoreHalf.score = scoreCandidate(scoreHalf.bpm, density);
    scoreDouble.bpm = T * 2.0;
    scoreDouble.score = scoreCandidate(scoreDouble.bpm, density);

    for (double c : candidates)
    {
        CandidateScore cs;
        cs.bpm = c;
        cs.score = scoreCandidate(c, density);
        cs.onsetsPerBeat = (c > 0.0) ? density / (c / 60.0) : 0.0;

        // Tie-breaker (within the tie band): prefer the SIMPLER ratio to T
        // (T itself, then octaves, then metric ratios); only among equally
        // simple ratios prefer the slower tempo. The old rule ("slower wins
        // any tie") replaced a correct, top-scoring T by T*2/3 or T*3/4
        // whenever that also scored 1.0 (120 -> 80, 129 -> 86 in tests).
        const double tieBand = std::max(0.05, best.score * 0.05);
        if (cs.score > best.score + tieBand)
            best = cs;
        else if (std::abs(cs.score - best.score) <= tieBand)
        {
            const int rc = rankOf(cs.bpm), rb = rankOf(best.bpm);
            if (rc < rb || (rc == rb && cs.bpm < best.bpm && cs.bpm >= 60.0))
                best = cs;
        }
    }

    // 3) Bimodal density gate (Fix 3): when T and T/2 score similarly,
    //    use the 3.5 Hz onset-density rule to break the tie.
    const double scoreDiff = std::abs(scoreT.score - scoreHalf.score);
    if (scoreDiff < 0.10) // similar scores
    {
        if (density >= 3.5)
        {
            // Prefer faster
            if (scoreT.bpm > scoreHalf.bpm) best = scoreT; else best = scoreHalf;
            r.reasoning << "bimodal-gate(density>=3.5): prefer faster; ";
        }
        else
        {
            // Prefer slower
            if (scoreT.bpm < scoreHalf.bpm) best = scoreT; else best = scoreHalf;
            r.reasoning << "bimodal-gate(density<3.5): prefer slower; ";
        }
    }

    // 4) Apply
    r.winnerScore = best.score;
    if (std::abs(best.bpm - candidateBPM) > 0.5)
    {
        r.correctedBPM = best.bpm;
        r.wasCorrected = true;
        r.reasoning << "T=" << juce::String(T, 2)
                    << " density=" << juce::String(density, 2) << " ops"
                    << " -> winner=" << juce::String(best.bpm, 2)
                    << " (score=" << juce::String(best.score, 2)
                    << ", opb=" << juce::String(best.onsetsPerBeat, 2) << ")";
    }
    else
    {
        r.correctedBPM = candidateBPM;
        r.wasCorrected = false;
        r.reasoning << "no change (T best) density=" << juce::String(density, 2);
    }

    // Always log candidate scoring so we can verify quality on user's corpus.
    juce::Logger::writeToLog(
        juce::String("[OctaveCorrection] T=") + juce::String(T, 2)
        + " density=" + juce::String(density, 2) + " ops"
        + " candidates: T/2=" + juce::String(scoreHalf.score, 2)
        + " T="   + juce::String(scoreT.score, 2)
        + " T*2=" + juce::String(scoreDouble.score, 2)
        + " -> " + juce::String(best.bpm, 2)
        + (r.wasCorrected ? " (CORRECTED)" : " (unchanged)"));

    return r;
}

OctaveCorrectionResult correctOctaveFromBuffer(double candidateBPM,
                                               const juce::AudioBuffer<float>& monoBuffer,
                                               double sampleRate)
{
    double envSR = 0.0;
    if (monoBuffer.getNumChannels() <= 0 || monoBuffer.getNumSamples() <= 0)
        return correctOctave(candidateBPM, {}, 0.0);
    // C11 fix: shared ODF with the correct real-only FFT layout (full band,
    // as before). Multi-channel input is downmixed (old code read ch 0 only).
    const std::vector<float> mono = analysis::downmixToMono(monoBuffer);
    auto env = analysis::spectralFluxEnvelope(mono.data(), static_cast<int>(mono.size()),
                                              sampleRate, /*fftSize*/ 2048, /*hopSize*/ 512,
                                              0.0, sampleRate * 0.5, envSR);
    return correctOctave(candidateBPM, env, envSR);
}

} // namespace samplar
