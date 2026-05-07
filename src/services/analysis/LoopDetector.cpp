#include "LoopDetector.h"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace samplar {

//==============================================================================
// MAIN ANALYSIS
//==============================================================================

SliceResult LoopDetector::analyze(const float* samples, int64_t numSamples,
                                   double sampleRate, double bpm) {
    SliceResult result;
    result.bpm = bpm;

    juce::Logger::writeToLog("🔄 [LoopDetector] === AUTOCORRELATION LOOP DETECTION ===");
    juce::Logger::writeToLog("🔄 [LoopDetector] BPM: " + juce::String(bpm, 1));
    juce::Logger::writeToLog("🔄 [LoopDetector] Target: " + juce::String(targetLoopBars_) + "-bar loops");
    juce::Logger::writeToLog("🔄 [LoopDetector] Audio: " + juce::String(numSamples / sampleRate, 2) + "s @ " +
                             juce::String(sampleRate) + " Hz");

    // Step 1: Find best loop length using autocorrelation
    loopInfo_ = findBestLoopLength(samples, numSamples, sampleRate, bpm);
    result.loopLengthBars = loopInfo_.loopLengthBars;

    juce::Logger::writeToLog("✅ [LoopDetector] Best loop: " + juce::String(loopInfo_.loopLengthBars) +
                             " bars (" + juce::String(loopInfo_.loopLengthSeconds, 2) + "s), " +
                             "correlation: " + juce::String(loopInfo_.correlation, 3));

    // Step 2: Generate loop boundaries at detected/target loop length
    // Use 4-bar loops as primary output
    const double beatDuration = 60.0 / bpm;
    const double barDuration = beatDuration * beatsPerBar_;
    const double loopDuration = barDuration * targetLoopBars_; // 4 bars
    const int64_t loopSamples = static_cast<int64_t>(loopDuration * sampleRate);

    juce::Logger::writeToLog("🔄 [LoopDetector] Generating " + juce::String(targetLoopBars_) + "-bar loop boundaries:");
    juce::Logger::writeToLog("🔄 [LoopDetector]   Beat: " + juce::String(beatDuration, 4) + "s");
    juce::Logger::writeToLog("🔄 [LoopDetector]   Bar: " + juce::String(barDuration, 4) + "s");
    juce::Logger::writeToLog("🔄 [LoopDetector]   Loop: " + juce::String(loopDuration, 4) + "s (" +
                             juce::String(loopSamples) + " samples)");

    // Step 2b: Find the first downbeat (beat 1) position
    // This ensures loops are cut at the actual musical beat 1, not at arbitrary position 0
    int64_t firstDownbeat = findFirstDownbeat(samples, numSamples, sampleRate, bpm);

    juce::Logger::writeToLog("🎯 [LoopDetector] First downbeat at: " + juce::String(firstDownbeat) +
                             " samples (" + juce::String(firstDownbeat / sampleRate * 1000.0, 1) + "ms)");

    // Generate loop boundaries every 4 bars, starting from the detected downbeat
    int64_t pos = firstDownbeat;
    int loopCount = 0;
    while (pos < numSamples) {
        result.loopBoundaries.push_back(pos);
        loopCount++;
        pos += loopSamples;
    }

    // Also include initial segment before downbeat as a potential loop if it's long enough
    // (but only if downbeat is significantly after the start)
    if (firstDownbeat > loopSamples / 4) {
        juce::Logger::writeToLog("⚠️ [LoopDetector] Audio starts " +
                                 juce::String(firstDownbeat / sampleRate * 1000.0, 1) +
                                 "ms before beat 1 - this portion will be trimmed");
    }

    juce::Logger::writeToLog("✅ [LoopDetector] Found " + juce::String(loopCount) + " loop boundaries");

    // Step 3: Detect individual sample onsets (transients)
    juce::Logger::writeToLog("🔄 [LoopDetector] Detecting individual sample onsets...");
    result.sampleBoundaries = detectOnsets(samples, numSamples, sampleRate);

    juce::Logger::writeToLog("✅ [LoopDetector] Found " + juce::String(result.sampleBoundaries.size()) +
                             " sample onsets");

    // Log summary
    juce::Logger::writeToLog("✅ [LoopDetector] === ANALYSIS COMPLETE ===");
    juce::Logger::writeToLog("✅ [LoopDetector] LOOPS: " + juce::String(result.loopBoundaries.size()) +
                             " x " + juce::String(targetLoopBars_) + "-bar sections");
    juce::Logger::writeToLog("✅ [LoopDetector] SAMPLES: " + juce::String(result.sampleBoundaries.size()) +
                             " individual hits");

    return result;
}

//==============================================================================
// AUTOCORRELATION FOR LOOP DETECTION
//==============================================================================

float LoopDetector::computeAutocorrelation(const float* samples, int64_t numSamples,
                                            int64_t lag, int64_t windowSize) {
    if (lag >= numSamples || windowSize <= 0) return 0.0f;

    // Limit analysis window to available samples
    const int64_t effectiveWindow = std::min(windowSize, numSamples - lag);
    if (effectiveWindow <= 0) return 0.0f;

    // Compute normalized cross-correlation at given lag
    double sum = 0.0;
    double sumSq1 = 0.0;
    double sumSq2 = 0.0;

    for (int64_t i = 0; i < effectiveWindow; ++i) {
        const double v1 = samples[i];
        const double v2 = samples[i + lag];
        sum += v1 * v2;
        sumSq1 += v1 * v1;
        sumSq2 += v2 * v2;
    }

    // Normalized correlation coefficient
    const double denom = std::sqrt(sumSq1 * sumSq2);
    if (denom < 1e-10) return 0.0f;

    return static_cast<float>(sum / denom);
}

LoopInfo LoopDetector::findBestLoopLength(const float* samples, int64_t numSamples,
                                           double sampleRate, double bpm) {
    LoopInfo best;
    best.loopLengthBars = targetLoopBars_;
    best.correlation = 0.0f;

    const double beatDuration = 60.0 / bpm;
    const double barDuration = beatDuration * beatsPerBar_;

    // Check correlation at musical loop lengths: 1, 2, 4, 8 bars
    const int barCounts[] = { 1, 2, 4, 8 };
    const int numCounts = 4;

    juce::Logger::writeToLog("🔄 [LoopDetector] Checking autocorrelation at musical lags:");

    // Use a large analysis window (several seconds)
    const int64_t analysisWindow = std::min(numSamples / 2,
                                            static_cast<int64_t>(30.0 * sampleRate)); // 30 seconds max

    float bestCorr = -1.0f;
    int bestBars = targetLoopBars_;

    for (int i = 0; i < numCounts; ++i) {
        const int bars = barCounts[i];
        const double loopSeconds = barDuration * bars;
        const int64_t lagSamples = static_cast<int64_t>(loopSeconds * sampleRate);

        if (lagSamples >= numSamples) continue;

        const float corr = computeAutocorrelation(samples, numSamples, lagSamples, analysisWindow);

        juce::Logger::writeToLog("🔄 [LoopDetector]   " + juce::String(bars) + " bar(s) = " +
                                 juce::String(loopSeconds, 3) + "s: correlation = " +
                                 juce::String(corr, 4));

        // Prefer 4-bar loops if correlation is good enough
        // Give 4-bar loops a slight boost in the comparison
        float adjustedCorr = corr;
        if (bars == 4) adjustedCorr *= 1.1f; // 10% boost for 4-bar
        if (bars == 2) adjustedCorr *= 1.05f; // 5% boost for 2-bar

        if (adjustedCorr > bestCorr) {
            bestCorr = adjustedCorr;
            bestBars = bars;
            best.correlation = corr; // Store actual correlation
        }
    }

    best.loopLengthBars = bestBars;
    best.loopLengthSeconds = barDuration * bestBars;
    best.loopLengthSamples = static_cast<int>(best.loopLengthSeconds * sampleRate);

    // If no good correlation found, default to target
    if (best.correlation < 0.1f) {
        juce::Logger::writeToLog("⚠️ [LoopDetector] Low correlation, using default " +
                                 juce::String(targetLoopBars_) + "-bar loops");
        best.loopLengthBars = targetLoopBars_;
        best.loopLengthSeconds = barDuration * targetLoopBars_;
        best.loopLengthSamples = static_cast<int>(best.loopLengthSeconds * sampleRate);
    }

    return best;
}

//==============================================================================
// ONSET DETECTION FOR INDIVIDUAL SAMPLES
//==============================================================================

std::vector<int64_t> LoopDetector::detectOnsets(const float* samples, int64_t numSamples,
                                                 double sampleRate) {
    std::vector<int64_t> onsets;

    // Compute spectral flux (energy derivative)
    auto flux = computeSpectralFlux(samples, numSamples, sampleRate);

    if (flux.empty()) return onsets;

    // Find peaks in spectral flux
    // Adaptive threshold based on mean + std
    const float mean = std::accumulate(flux.begin(), flux.end(), 0.0f) / flux.size();

    float variance = 0.0f;
    for (float f : flux) {
        variance += (f - mean) * (f - mean);
    }
    variance /= flux.size();
    const float stdDev = std::sqrt(variance);

    const float threshold = mean + 1.5f * stdDev; // Adaptive threshold

    // Hop size for flux calculation (in samples)
    const int hopSize = static_cast<int>(sampleRate * 0.01); // 10ms hop

    // Minimum distance between onsets (50ms)
    const int minDistance = static_cast<int>(sampleRate * 0.05);

    int64_t lastOnset = -minDistance;

    for (size_t i = 1; i < flux.size() - 1; ++i) {
        // Peak detection: local maximum above threshold
        if (flux[i] > threshold &&
            flux[i] > flux[i-1] &&
            flux[i] >= flux[i+1]) {

            const int64_t samplePos = static_cast<int64_t>(i) * hopSize;

            // Enforce minimum distance
            if (samplePos - lastOnset >= minDistance) {
                onsets.push_back(samplePos);
                lastOnset = samplePos;
            }
        }
    }

    // Always include start if not already
    if (onsets.empty() || onsets[0] > hopSize) {
        onsets.insert(onsets.begin(), 0);
    }

    return onsets;
}

std::vector<float> LoopDetector::computeSpectralFlux(const float* samples, int64_t numSamples,
                                                      double sampleRate) {
    std::vector<float> flux;

    // Simple energy-based onset detection
    // Frame size: 20ms, hop: 10ms
    const int frameSize = static_cast<int>(sampleRate * 0.02);
    const int hopSize = static_cast<int>(sampleRate * 0.01);

    if (frameSize <= 0 || hopSize <= 0 || numSamples < frameSize) {
        return flux;
    }

    // Compute RMS energy per frame
    std::vector<float> energy;
    for (int64_t start = 0; start + frameSize <= numSamples; start += hopSize) {
        float sumSq = 0.0f;
        for (int i = 0; i < frameSize; ++i) {
            sumSq += samples[start + i] * samples[start + i];
        }
        energy.push_back(std::sqrt(sumSq / frameSize));
    }

    // Compute spectral flux (positive difference in energy)
    flux.push_back(0.0f); // First frame
    for (size_t i = 1; i < energy.size(); ++i) {
        const float diff = energy[i] - energy[i-1];
        flux.push_back(std::max(0.0f, diff)); // Half-wave rectification
    }

    return flux;
}

//==============================================================================
// DOWNBEAT DETECTION - Phase Correlation Algorithm
//==============================================================================
//
// Key insight: Fills and breaks are ONE-OFF events. The actual beat 1 produces
// a REPEATING pattern across bars. We find the phase offset that maximizes
// pattern consistency across all bars.
//
//==============================================================================

int64_t LoopDetector::findFirstDownbeat(const float* samples, int64_t numSamples,
                                          double sampleRate, double bpm) {
    juce::Logger::writeToLog("🎯 [LoopDetector] === PHASE CORRELATION DOWNBEAT DETECTION ===");
    juce::Logger::writeToLog("🎯 [LoopDetector] BPM: " + juce::String(bpm, 1) +
                             ", SampleRate: " + juce::String(sampleRate));

    if (numSamples == 0 || sampleRate <= 0 || bpm <= 0) {
        juce::Logger::writeToLog("⚠️ [LoopDetector] Invalid parameters, returning 0");
        return 0;
    }

    // Calculate timing constants
    const double beatDuration = 60.0 / bpm;
    const double barDuration = beatDuration * beatsPerBar_;
    const int64_t beatSamples = static_cast<int64_t>(beatDuration * sampleRate);
    const int64_t barSamples = static_cast<int64_t>(barDuration * sampleRate);

    juce::Logger::writeToLog("🎯 [LoopDetector] Bar: " + juce::String(barDuration, 3) + "s (" +
                             juce::String(barSamples) + " samples)");

    // How many complete bars do we have?
    const int numBars = static_cast<int>(numSamples / barSamples);
    if (numBars < 2) {
        juce::Logger::writeToLog("⚠️ [LoopDetector] Less than 2 bars, using position 0");
        return 0;
    }

    juce::Logger::writeToLog("🎯 [LoopDetector] Audio contains " + juce::String(numBars) + " complete bars");

    //==========================================================================
    // STEP 1: Compute energy envelope (RMS in 16th-note segments)
    //==========================================================================
    const int segmentsPerBar = 16;  // 16th note resolution
    const int64_t segmentSamples = barSamples / segmentsPerBar;
    const int totalSegments = static_cast<int>(numSamples / segmentSamples);

    std::vector<float> energyEnvelope(totalSegments, 0.0f);
    std::vector<float> lowFreqEnvelope(totalSegments, 0.0f);  // For kick detection

    // Simple low-pass filter coefficients (approximate 200Hz cutoff)
    const float lpAlpha = std::exp(-2.0f * 3.14159f * 200.0f / static_cast<float>(sampleRate));

    for (int seg = 0; seg < totalSegments; ++seg) {
        int64_t start = seg * segmentSamples;
        int64_t end = std::min(start + segmentSamples, numSamples);

        float sumSq = 0.0f;
        float lowFreqSumSq = 0.0f;
        float lpState = 0.0f;

        for (int64_t i = start; i < end; ++i) {
            float s = samples[i];
            sumSq += s * s;

            // Simple IIR low-pass for kick detection
            lpState = lpAlpha * lpState + (1.0f - lpAlpha) * s;
            lowFreqSumSq += lpState * lpState;
        }

        energyEnvelope[seg] = std::sqrt(sumSq / (end - start));
        lowFreqEnvelope[seg] = std::sqrt(lowFreqSumSq / (end - start));
    }

    juce::Logger::writeToLog("🎯 [LoopDetector] Computed energy envelope (" +
                             juce::String(totalSegments) + " segments)");

    //==========================================================================
    // STEP 2: For each phase offset, compute pattern correlation score
    //==========================================================================
    // Phase offset = where we think beat 1 is (in segments from start)
    // We test offsets from 0 to segmentsPerBar-1 (one full bar of phases)

    int64_t bestPhaseOffset = 0;
    float bestScore = -1.0f;

    // Step through phase offsets (16th note resolution)
    for (int phase = 0; phase < segmentsPerBar; ++phase) {
        // How many complete bars can we analyze at this phase?
        int barsToAnalyze = (totalSegments - phase) / segmentsPerBar;
        if (barsToAnalyze < 2) continue;

        // Limit to reasonable number of bars for analysis
        barsToAnalyze = std::min(barsToAnalyze, 8);

        // Extract pattern for each bar at this phase
        std::vector<std::vector<float>> barPatterns(barsToAnalyze);
        std::vector<float> avgPattern(segmentsPerBar, 0.0f);

        for (int bar = 0; bar < barsToAnalyze; ++bar) {
            barPatterns[bar].resize(segmentsPerBar);
            for (int s = 0; s < segmentsPerBar; ++s) {
                int idx = phase + bar * segmentsPerBar + s;
                if (idx < totalSegments) {
                    barPatterns[bar][s] = energyEnvelope[idx];
                    avgPattern[s] += energyEnvelope[idx];
                }
            }
        }

        // Normalize average pattern
        for (int s = 0; s < segmentsPerBar; ++s) {
            avgPattern[s] /= barsToAnalyze;
        }

        // Compute correlation score: how well does each bar match the average?
        float totalCorrelation = 0.0f;
        std::vector<float> barCorrelations(barsToAnalyze);

        for (int bar = 0; bar < barsToAnalyze; ++bar) {
            // Pearson correlation between this bar and average
            float sumXY = 0.0f, sumX = 0.0f, sumY = 0.0f;
            float sumX2 = 0.0f, sumY2 = 0.0f;

            for (int s = 0; s < segmentsPerBar; ++s) {
                float x = barPatterns[bar][s];
                float y = avgPattern[s];
                sumXY += x * y;
                sumX += x;
                sumY += y;
                sumX2 += x * x;
                sumY2 += y * y;
            }

            float n = static_cast<float>(segmentsPerBar);
            float denom = std::sqrt((n * sumX2 - sumX * sumX) * (n * sumY2 - sumY * sumY));

            float corr = 0.0f;
            if (denom > 1e-10f) {
                corr = (n * sumXY - sumX * sumY) / denom;
            }

            barCorrelations[bar] = corr;
            totalCorrelation += corr;
        }

        // Find lowest correlation bar (likely the fill/break bar)
        float minCorr = 1.0f;
        int minCorrBar = 0;
        for (int bar = 0; bar < barsToAnalyze; ++bar) {
            if (barCorrelations[bar] < minCorr) {
                minCorr = barCorrelations[bar];
                minCorrBar = bar;
            }
        }

        // Score = average correlation, excluding the outlier bar (fill)
        float score = 0.0f;
        int countedBars = 0;
        for (int bar = 0; bar < barsToAnalyze; ++bar) {
            if (bar != minCorrBar) {  // Exclude likely fill bar
                score += barCorrelations[bar];
                countedBars++;
            }
        }
        if (countedBars > 0) {
            score /= countedBars;
        }

        // BONUS: Add low-frequency weight for segment 0 (beat 1 should have kick)
        int beat1Segment = phase;
        if (beat1Segment < totalSegments) {
            float kickBonus = lowFreqEnvelope[beat1Segment] * 0.5f;
            score += kickBonus;
        }

        if (score > bestScore) {
            bestScore = score;
            bestPhaseOffset = phase;
        }
    }

    // Convert segment offset to sample offset
    int64_t bestDownbeat = bestPhaseOffset * segmentSamples;

    juce::Logger::writeToLog("✅ [LoopDetector] Best phase: segment " + juce::String(bestPhaseOffset) +
                             " (correlation score: " + juce::String(bestScore, 3) + ")");
    juce::Logger::writeToLog("✅ [LoopDetector] First downbeat at sample " + juce::String(bestDownbeat) +
                             " (" + juce::String(bestDownbeat / sampleRate * 1000.0, 1) + "ms)");

    //==========================================================================
    // STEP 3: Refine to nearest onset (snap to actual transient)
    //==========================================================================
    auto onsets = detectOnsets(samples, numSamples, sampleRate);
    if (!onsets.empty()) {
        // Find nearest onset to our detected phase
        int64_t tolerance = segmentSamples / 2;  // Half a 16th note
        int64_t nearestOnset = bestDownbeat;
        int64_t minDist = tolerance;

        for (int64_t onset : onsets) {
            int64_t dist = std::abs(onset - bestDownbeat);
            if (dist < minDist) {
                minDist = dist;
                nearestOnset = onset;
            }
        }

        if (nearestOnset != bestDownbeat) {
            juce::Logger::writeToLog("🎯 [LoopDetector] Snapped to nearest onset: " +
                                     juce::String(nearestOnset) + " (was " +
                                     juce::String(bestDownbeat) + ")");
            bestDownbeat = nearestOnset;
        }
    }

    juce::Logger::writeToLog("✅ [LoopDetector] Final downbeat: " + juce::String(bestDownbeat) +
                             " samples (" + juce::String(bestDownbeat / sampleRate * 1000.0, 1) + "ms)");

    return bestDownbeat;
}

} // namespace samplar
