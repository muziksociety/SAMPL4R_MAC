#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include <string>

namespace samplar {

/**
 * A single slot in a generated sequencer pattern.
 * References a segment from a specific stem, with per-slot effects.
 */
struct PatternSlot
{
    int stemIndex { -1 };
    int segmentIndex { -1 };
    char role { 'A' };        // Pattern role letter (A, B, C, D)
    bool empty { true };      // True = unfilled slot

    // Per-slot effects
    float velocity { 1.0f };  // 0.0–1.0 volume multiplier
    float pitchSemitones { 0.0f }; // Pitch offset in semitones
    bool reverse { false };   // Play segment reversed
    float probability { 1.0f }; // 0.0–1.0 chance of triggering

    // Trim: normalized positions within the segment (0.0 to 1.0)
    float trimStart { 0.0f };  // Left trim (0.0 = no trim)
    float trimEnd { 1.0f };    // Right trim (1.0 = no trim)

    // Speed: playback rate (1.0 = normal, 0.5 = half speed, 2.0 = double)
    float speed { 1.0f };

    // Integer cell-based grid position (fixed grid of numBars * 4 cells).
    // gridCell = which cell index (0 to N-1) this slot starts at.
    // cellSpan = how many cells wide (1, 2, 3, or 4).
    int gridCell { 0 };
    int cellSpan { 1 };
};

/**
 * PatternGenerator — creates musically structured arrangements
 * from available segments using common song-form patterns.
 *
 * Pattern Types:
 *   AABB  — seg1 seg1 seg2 seg2        (simple repetition)
 *   ABAB  — seg1 seg2 seg1 seg2        (call & response)
 *   AAAB  — seg1 seg1 seg1 seg2        (build + variation)
 *   ABAC  — seg1 seg2 seg1 seg3        (verse-chorus)
 *   AABA  — seg1 seg1 seg2 seg1        (classic song form)
 *   ABCD  — seg1 seg2 seg3 seg4        (maximum variety)
 *   RAND  — pure random selection
 *   EUCLID — Euclidean rhythm distribution
 */
class PatternGenerator
{
public:
    enum class PatternType
    {
        AABB,
        ABAB,
        AAAB,
        ABAC,
        AABA,
        ABCD,
        Random,
        Euclidean
    };

    /** Segment reference — identifies a segment available for pattern use. */
    struct SegmentRef
    {
        int stemIndex;
        int segmentIndex;
        int64_t lengthSamples;
    };

    /**
     * Generate a pattern arrangement.
     *
     * @param availableSegments  Segments from the selected stems
     * @param pattern            Musical structure pattern
     * @param numBars            How many bars to fill (2, 4, 8, 16)
     * @param slotsPerBar        Density: segments per bar (1, 2, 4, 8, 16)
     * @return                   Ordered vector of slots filling the pattern
     */
    static std::vector<PatternSlot> generate(
        const std::vector<SegmentRef>& availableSegments,
        PatternType pattern,
        int numBars,
        int slotsPerBar = 1);

    /**
     * Generate Euclidean rhythm pattern.
     * Distributes 'hits' evenly across 'steps' using Bjorklund's algorithm.
     *
     * @param availableSegments  Pool of segments to pick from
     * @param totalSteps         Total number of steps (numBars * slotsPerBar)
     * @param numHits            How many active hits to place
     * @param rotation           Rotate the pattern by N steps
     * @return                   Pattern with hits distributed evenly
     */
    static std::vector<PatternSlot> generateEuclidean(
        const std::vector<SegmentRef>& availableSegments,
        int totalSteps,
        int numHits,
        int rotation = 0);

    /**
     * Mutate an existing pattern — randomly swap/remove/reverse ~20% of slots.
     * Creates variation while preserving overall feel.
     */
    static void mutate(std::vector<PatternSlot>& slots,
                       const std::vector<SegmentRef>& availableSegments,
                       float intensity = 0.2f);

    /**
     * Apply velocity curve to a pattern.
     * Types: "crescendo", "decrescendo", "accent2", "accent4", "random"
     */
    static void applyVelocityCurve(std::vector<PatternSlot>& slots,
                                    const juce::String& curveType);

    /**
     * Randomly reverse some slots in a pattern.
     * @param probability  0.0–1.0 chance per slot
     */
    static void randomReverse(std::vector<PatternSlot>& slots,
                               float probability = 0.15f);

    /**
     * Apply random pitch modulation to slots.
     * @param maxSemitones  Maximum pitch deviation (e.g. 2.0 = ±2 semitones)
     */
    static void randomPitch(std::vector<PatternSlot>& slots,
                             float maxSemitones = 2.0f);

    /**
     * Apply tempo-grid sub-slicing to pattern slots.
     * Each segment is divided into beat-quantized chunks.
     * Slots get random sub-slices instead of playing the full segment.
     *
     * @param slots          Pattern to modify in-place
     * @param model          Segment model (to get segment lengths)
     * @param bpm            Current BPM
     * @param sampleRate     Sample rate
     * @param subdivBeats    Sub-slice length in beats (e.g. 1.0 = quarter note)
     */
    static void applySubSlicing(std::vector<PatternSlot>& slots,
                                 const class SModeSegmentModel& model,
                                 double bpm,
                                 double sampleRate,
                                 float subdivBeats = 1.0f);

    /** Get the structure string for a pattern (e.g. "AABA"). */
    static juce::String getPatternString(PatternType type);

    /** Get all pattern type names for UI dropdown. */
    static std::vector<std::pair<PatternType, juce::String>> getAllPatterns();

    /** Get the number of unique roles needed for a pattern type. */
    static int getRequiredRoles(PatternType type);

    /**
     * Bjorklund's algorithm — distributes hits evenly across steps.
     * Returns a bool vector: true = hit, false = rest.
     */
    static std::vector<bool> bjorklund(int steps, int hits);

private:
    /**
     * Expand a pattern type into a bar-level role sequence.
     * e.g. AABA with 4 bars → {'A','A','B','A'}
     *      AABA with 8 bars → {'A','A','B','A','A','A','B','A'}
     */
    static std::vector<char> expandPattern(PatternType type, int numBars);

    /** Pick N unique segments randomly from the pool. */
    static std::vector<SegmentRef> pickUniqueSegments(
        const std::vector<SegmentRef>& pool, int count);
};

} // namespace samplar
