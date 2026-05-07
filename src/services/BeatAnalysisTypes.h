#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>

namespace samplar {

/**
 * Stores BPM detection result with confidence metric
 */
struct BPMResult {
    float bpm = 120.0f;            // Detected BPM (default 120)
    float confidence = 0.0f;       // 0.0 - 1.0 confidence score
    bool isManualOverride = false; // True if user manually set BPM

    // Hybrid detector details (for UI display)
    float fftBPM = 0.0f;           // FFT spectral flux detector result
    float fftConfidence = 0.0f;    // FFT detector confidence
    float dwtBPM = 0.0f;           // DWT wavelet detector result
    float dwtConfidence = 0.0f;    // DWT detector confidence
    juce::String detectorSource;   // "FFT", "DWT", "LTE", "Merged", or "None"

    // Loop Tempo Estimator (LTE) results - STFT + ODF based
    // From Audacity's loop-tempo-estimator library
    float lteBPM = 0.0f;           // LTE detected BPM (0 if not classified as loop)
    float lteScore = 0.0f;         // LTE loop classification score (0-1)
    bool lteDetectedAsLoop = false; // True if LTE classified audio as a loop
    bool lteTooLong = false;       // True if audio was too long for LTE analysis (>60s)

    // ODF (Onset Detection Function) data from LTE - used for auto loop detection
    std::vector<float> odf;        // Onset Detection Function values
    double odfSampleRate = 0.0;    // Sample rate of ODF (frames per second)
    std::vector<int> odfPeakIndices; // Indices of detected onset peaks in ODF

    // Octave-corrected BPM (consensus between DWT and LTE)
    float correctedBPM = 0.0f;     // Final BPM after octave correction
    bool octaveCorrected = false;  // True if LTE was halved to match DWT

    // IOI (Inter-Onset Interval) detector results - direct transient measurement
    float ioiBPM = 0.0f;           // IOI detected BPM (0 if detection failed)
    float ioiConfidence = 0.0f;    // IOI confidence (0-1)
    int ioiOnsetCount = 0;         // Number of onsets detected
    std::vector<double> ioiOnsetTimes; // Onset times in seconds (for phase alignment)

    // Phase optimization result - optimal downbeat position
    double optimalPhase = 0.0;     // Best phase offset in seconds for grid alignment
    float phaseConfidence = 0.0f;  // How well the grid aligns with onsets (0-1)
};

/**
 * Represents a single beat position in the audio
 */
struct BeatPosition {
    double timeSeconds = 0.0;      // Position in seconds
    int beatNumber = 0;            // Beat number (1-based within bar)
    int barNumber = 0;             // Bar number (0-based)
    bool isDownbeat = false;       // True if first beat of bar
};

/**
 * Represents a loop boundary marker
 */
struct LoopBoundary {
    double startTimeSeconds = 0.0; // Loop start position
    double endTimeSeconds = 0.0;   // Loop end position
    int startBar = 0;              // Starting bar number
    int lengthBars = 4;            // Length in bars (typically 4, 8, 16)
    float confidence = 0.0f;       // Detection confidence
};

/**
 * Bar classification type based on correlation patterns
 */
enum class BarType {
    Unknown,        // Not yet classified
    Anchor,         // Part of high-correlation continuous region (verse/chorus)
    Transition,     // Low correlation, transitional section
    Fill,           // Single bar break within anchor region
    Intro,          // Low correlation at start of track
    Outro,          // Low correlation at end of track
    Breakdown       // Extended low-correlation section mid-track
};

/**
 * Analysis data for a single bar
 */
struct BarAnalysis {
    int barIndex = 0;              // Bar number (0-based)
    double startTime = 0.0;        // Start time in seconds
    double endTime = 0.0;          // End time in seconds
    float correlationToNext = 0.0f; // Correlation with next bar (0-1)
    float correlationToPrev = 0.0f; // Correlation with previous bar (0-1)
    float avgCorrelation = 0.0f;   // Average of prev and next correlation
    bool isAnchor = false;         // Part of high-correlation region
    BarType type = BarType::Unknown; // Classification
    float energy = 0.0f;           // RMS energy of this bar (for future use)
};

/**
 * A continuous high-correlation region (anchor)
 * These are sections like verse/chorus where patterns repeat consistently
 */
struct AnchorRegion {
    int startBar = 0;              // First bar of anchor region
    int endBar = 0;                // Last bar of anchor region (inclusive)
    int lengthBars = 0;            // Number of bars in this region
    float avgCorrelation = 0.0f;   // Average correlation within region
    float minCorrelation = 0.0f;   // Minimum correlation (weakest link)
    double startTime = 0.0;        // Start time in seconds
    double endTime = 0.0;          // End time in seconds
    bool usedForAlignment = false; // Was this anchor used to lock grid?
    float alignmentConfidence = 0.0f; // How confident is alignment from this anchor
};

/**
 * Statistics about correlation distribution across all bars
 * Used for adaptive anchor threshold calculation
 */
struct CorrelationStats {
    float mean = 0.0f;             // Average correlation across all bars
    float stdDev = 0.0f;           // Standard deviation of correlations
    float median = 0.0f;           // 50th percentile
    float p25 = 0.0f;              // 25th percentile (low correlations)
    float p50 = 0.0f;              // 50th percentile (same as median)
    float p60 = 0.0f;              // 60th percentile (adaptive threshold candidate)
    float p75 = 0.0f;              // 75th percentile (high correlations)
    float min = 0.0f;              // Minimum correlation
    float max = 0.0f;              // Maximum correlation
    int patternResolution = 1;     // Detected pattern resolution (1 or 2 bars)
    float twoBarCorrelation = 0.0f; // Average 2-bar correlation (for AABB detection)

    // Adaptive thresholds calculated from distribution
    float adaptiveThreshold = 0.65f;  // Calculated anchor threshold
    int adaptiveMinBars = 4;          // Calculated minimum anchor length
};

/**
 * Complete beat grid information for an audio file
 */
struct BeatGridInfo {
    BPMResult bpm;
    double downbeatTimeSeconds = 0.0;      // User-adjustable first beat position
    int beatsPerBar = 4;                   // Time signature numerator
    double audioLengthSeconds = 0.0;       // Total audio length
    std::vector<BeatPosition> beats;       // All beat positions
    std::vector<LoopBoundary> detectedLoops; // Detected loop regions
    double selectionOffset = 0.0;          // Selection start offset for coordinate conversion

    /**
     * Calculate beat positions based on BPM and downbeat
     */
    void recalculateBeats() {
        beats.clear();
        if (bpm.bpm <= 0 || audioLengthSeconds <= 0) return;

        double beatIntervalSeconds = 60.0 / bpm.bpm;
        double currentTime = downbeatTimeSeconds;
        int beatNum = 1;
        int barNum = 0;

        // Generate beats before downbeat (negative bars conceptually)
        double preTime = downbeatTimeSeconds - beatIntervalSeconds;
        while (preTime >= 0) {
            BeatPosition bp;
            bp.timeSeconds = preTime;
            int beatsFromDownbeat = static_cast<int>((downbeatTimeSeconds - preTime) / beatIntervalSeconds);
            bp.beatNumber = beatsPerBar - (beatsFromDownbeat % beatsPerBar);
            if (bp.beatNumber == 0) bp.beatNumber = beatsPerBar;
            bp.barNumber = -1 - static_cast<int>((downbeatTimeSeconds - preTime) / (beatIntervalSeconds * beatsPerBar));
            bp.isDownbeat = (bp.beatNumber == 1);
            beats.insert(beats.begin(), bp);
            preTime -= beatIntervalSeconds;
        }

        // Generate beats from downbeat forward
        while (currentTime < audioLengthSeconds) {
            BeatPosition bp;
            bp.timeSeconds = currentTime;
            bp.beatNumber = beatNum;
            bp.barNumber = barNum;
            bp.isDownbeat = (beatNum == 1);
            beats.push_back(bp);

            beatNum++;
            if (beatNum > beatsPerBar) {
                beatNum = 1;
                barNum++;
            }
            currentTime += beatIntervalSeconds;
        }
    }

    /**
     * Snap a time position to nearest beat
     */
    double snapToGrid(double timeSeconds) const {
        if (beats.empty()) return timeSeconds;

        double nearestTime = beats[0].timeSeconds;
        double minDist = std::abs(timeSeconds - nearestTime);

        for (const auto& beat : beats) {
            double dist = std::abs(timeSeconds - beat.timeSeconds);
            if (dist < minDist) {
                minDist = dist;
                nearestTime = beat.timeSeconds;
            }
        }
        return nearestTime;
    }

    /**
     * Get beat interval in seconds
     */
    double getBeatIntervalSeconds() const {
        if (bpm.bpm <= 0) return 0.5; // Default 120 BPM
        return 60.0 / bpm.bpm;
    }

    /**
     * Get bar duration in seconds
     */
    double getBarDurationSeconds() const {
        return getBeatIntervalSeconds() * beatsPerBar;
    }
};

} // namespace samplar
