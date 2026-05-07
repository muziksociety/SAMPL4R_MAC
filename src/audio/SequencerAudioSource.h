#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
// SAMPLAR: DrumMapping.h deleted — StepData/RollStyle/VelocityShape inlined here
enum class RollStyle : uint8_t {
    None = 0,
    Regular,
    Triplet,
    Flam,
    Drag,
    Buzz
};

enum class VelocityShape : uint8_t {
    Flat = 0,
    Decay,
    Rise,
    Accent
};

struct StepData {
    bool active = false;
    float velocity = 1.0f;
    int pitchOffset = 0;
    float pan = 0.0f;
    RollStyle rollStyle = RollStyle::None;
    uint8_t rollCount = 1;
    VelocityShape velocityShape = VelocityShape::Flat;
    float rollIntensity = 1.0f;
    uint8_t sourceStepCount = 16;

    bool isSingleHit() const { return rollStyle == RollStyle::None || rollCount <= 1; }
    std::vector<std::pair<float, float>> getRollTriggers() const { return {}; }
    juce::String getDisplayString() const { return {}; }
    static char getRollStyleCode(RollStyle) { return ' '; }
    static RollStyle parseRollStyleCode(char) { return RollStyle::None; }
};
#include <vector>
#include <atomic>
#include <mutex>
#include <memory>

namespace samplar {

/**
 * Audio-thread accurate step sequencer engine.
 *
 * CRITICAL: All timing happens on the audio thread for sample-accurate playback.
 * The UI only reads atomic variables for display - it never controls timing.
 *
 * Usage:
 * 1. Call setPattern() with grid data and sample buffers
 * 2. Call setTempo() to set BPM
 * 3. Call start()/stop() to control playback
 * 4. Add as input to MixerAudioSource or use directly
 * 5. Read getPlayheadStep() from UI for display (atomic, safe)
 */
class SequencerAudioSource : public juce::AudioSource
{
public:
    //==========================================================================
    // Configuration
    //==========================================================================

    struct Config {
        int stepsPerBar = 16;
        int beatsPerBar = 4;
        int numBars = 1;
        double swingPercent = 0.0;  // 0-75%
    };

    //==========================================================================
    // Sample buffer reference (shared, no copy)
    //==========================================================================

    struct SampleBuffer {
        std::shared_ptr<const juce::AudioBuffer<float>> buffer;
        double sampleRate = 44100.0;

        bool isValid() const { return buffer != nullptr && buffer->getNumSamples() > 0; }
    };

    //==========================================================================
    // Row data (one per instrument track)
    //==========================================================================

    struct RowData {
        std::vector<bool> steps;           // Step on/off pattern
        std::vector<StepData> rollData;    // Roll/ratchet data per step
        SampleBuffer sample;               // Pre-loaded sample buffer
        bool muted = false;
        bool soloed = false;
        float volume = 1.0f;               // Row volume (linear gain, 0.0 to 2.0+)
        float pan = 0.5f;                  // Row pan (0.0=left, 0.5=center, 1.0=right)
    };

    //==========================================================================
    // Lifecycle
    //==========================================================================

    SequencerAudioSource();
    ~SequencerAudioSource() override;

    //==========================================================================
    // AudioSource interface
    //==========================================================================

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;

    //==========================================================================
    // Pattern Control (call from any thread - internally synchronized)
    //==========================================================================

    /**
     * Set the full pattern data. Thread-safe.
     * @param rows Vector of row data with step patterns and sample buffers
     * @param config Timing configuration (steps, bars, swing)
     */
    void setPattern(const std::vector<RowData>& rows, const Config& config);

    /**
     * Update a single row's sample buffer. Thread-safe.
     * Use this when a sample is loaded/changed without rebuilding the whole pattern.
     */
    void setRowSample(int rowIndex, const SampleBuffer& sample);

    /**
     * Update a single cell. Thread-safe.
     */
    void setCell(int row, int step, bool on);

    /**
     * Set tempo. Thread-safe.
     */
    void setTempo(double bpm);

    /**
     * Set swing amount (0-75%). Thread-safe.
     */
    void setSwing(double percent);

    /**
     * Set mute state for a row. Thread-safe.
     */
    void setRowMuted(int row, bool muted);

    /**
     * Set solo state for a row. Thread-safe.
     */
    void setRowSoloed(int row, bool soloed);

    /**
     * Set volume for a row. Thread-safe.
     * @param volume Linear gain (0.0 = silent, 1.0 = unity, >1.0 = boost)
     */
    void setRowVolume(int row, float volume);

    /**
     * Set pan for a row. Thread-safe.
     * @param pan 0.0 = full left, 0.5 = center, 1.0 = full right
     */
    void setRowPan(int row, float pan);

    //==========================================================================
    // Transport Control
    //==========================================================================

    void start();
    void stop();
    bool isPlaying() const { return playing_.load(std::memory_order_acquire); }

    /**
     * Get current playhead step (for UI display).
     * Safe to call from any thread.
     */
    int getPlayheadStep() const { return playheadStep_.load(std::memory_order_acquire); }

    /**
     * Get playhead phase within current step (0.0 to 1.0).
     * For smooth playhead animation.
     */
    float getPlayheadPhase() const { return playheadPhase_.load(std::memory_order_acquire); }

    /**
     * Get total number of steps in pattern.
     */
    int getTotalSteps() const { return totalSteps_.load(std::memory_order_acquire); }

private:
    //==========================================================================
    // Internal Types
    //==========================================================================

    // Active voice playing a sample with resampling support
    struct Voice {
        int rowIndex = -1;
        double samplePosition = 0.0;  // Floating-point for resampling interpolation
        double resampleRatio = 1.0;   // sampleRate / deviceRate
        std::shared_ptr<const juce::AudioBuffer<float>> buffer;
        bool active = false;
        float volume = 1.0f;          // Voice volume (copied from row on trigger)
        float pan = 0.5f;             // Voice pan (copied from row on trigger)
    };

    // Scheduled trigger (pre-calculated)
    struct ScheduledTrigger {
        int samplePosition;     // Global sample position in pattern
        int rowIndex;
        int stepIndex;
        float velocity = 1.0f;  // Velocity for this trigger (0.0-1.0)
    };

    //==========================================================================
    // Internal Methods
    //==========================================================================

    void rebuildSchedule();
    int calculateStepSamplePosition(int step) const;
    void triggerVoice(int rowIndex);

    //==========================================================================
    // State (audio thread only - no locks needed)
    //==========================================================================

    double currentSampleRate_ = 44100.0;
    int samplesPerBlock_ = 512;

    // Pattern data (protected by mutex for updates from other threads)
    mutable std::mutex patternMutex_;
    std::vector<RowData> rows_;
    Config config_;
    double tempoBpm_ = 120.0;

    // Pre-calculated schedule (rebuilt when pattern changes)
    std::vector<ScheduledTrigger> schedule_;
    int patternLengthSamples_ = 0;

    // Playback state (audio thread updates, UI reads atomically)
    std::atomic<bool> playing_{false};
    std::atomic<int> playheadStep_{0};
    std::atomic<float> playheadPhase_{0.0f};
    std::atomic<int> totalSteps_{16};

    // Audio thread state (only accessed from audio thread)
    int64_t globalSamplePosition_ = 0;  // Position within current loop
    size_t nextTriggerIndex_ = 0;       // Index into schedule_

    // Voice pool for polyphonic playback
    static constexpr int MAX_VOICES = 32;
    std::array<Voice, MAX_VOICES> voices_;

    // Flag to signal pattern update pending
    std::atomic<bool> patternUpdatePending_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SequencerAudioSource)
};

} // namespace samplar
