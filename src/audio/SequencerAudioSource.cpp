#include "SequencerAudioSource.h"
#include "MicroFade.h"
#include <algorithm>

namespace samplar {

//==============================================================================
// Constructor / Destructor
//==============================================================================

SequencerAudioSource::SequencerAudioSource()
{
    juce::Logger::writeToLog("[SequencerAudioSource] === CREATED ===");

    // Initialize voice pool
    for (auto& voice : voices_)
    {
        voice.active = false;
        voice.rowIndex = -1;
        voice.samplePosition = 0;
        voice.buffer = nullptr;
    }
}

SequencerAudioSource::~SequencerAudioSource()
{
    juce::Logger::writeToLog("[SequencerAudioSource] === DESTROYED ===");
}

//==============================================================================
// AudioSource Interface
//==============================================================================

void SequencerAudioSource::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    juce::Logger::writeToLog("[SequencerAudioSource] prepareToPlay: " +
        juce::String(sampleRate) + " Hz, block=" + juce::String(samplesPerBlockExpected));

    currentSampleRate_ = sampleRate;
    samplesPerBlock_ = samplesPerBlockExpected;

    // Rebuild schedule with new sample rate
    rebuildSchedule();
}

void SequencerAudioSource::releaseResources()
{
    juce::Logger::writeToLog("[SequencerAudioSource] releaseResources");

    // Stop all voices
    for (auto& voice : voices_)
    {
        voice.active = false;
        voice.buffer = nullptr;
    }
}

void SequencerAudioSource::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    // Clear output buffer first
    bufferToFill.clearActiveBufferRegion();

    if (!playing_.load(std::memory_order_acquire))
        return;

    // Check if pattern update is pending
    if (patternUpdatePending_.load(std::memory_order_acquire))
    {
        std::lock_guard<std::mutex> lock(patternMutex_);
        rebuildSchedule();
        patternUpdatePending_.store(false, std::memory_order_release);
    }

    if (patternLengthSamples_ <= 0)
        return;

    const int numSamples = bufferToFill.numSamples;
    const int startSample = bufferToFill.startSample;
    auto* outputBuffer = bufferToFill.buffer;

    // Check for any solo'd rows
    bool anySoloed = false;
    {
        std::lock_guard<std::mutex> lock(patternMutex_);
        for (const auto& row : rows_)
        {
            if (row.soloed)
            {
                anySoloed = true;
                break;
            }
        }
    }

    // Process each sample in the block
    const int totalSteps = totalSteps_.load(std::memory_order_acquire);
    const int samplesPerStep = (totalSteps > 0) ? (patternLengthSamples_ / totalSteps) : patternLengthSamples_;

    for (int i = 0; i < numSamples; ++i)
    {
        const int64_t currentPos = globalSamplePosition_ % patternLengthSamples_;

        // Check if any triggers should fire at this position
        while (nextTriggerIndex_ < schedule_.size())
        {
            const auto& trigger = schedule_[nextTriggerIndex_];

            if (trigger.samplePosition <= currentPos)
            {
                // Check mute/solo state
                bool shouldPlay = true;
                {
                    std::lock_guard<std::mutex> lock(patternMutex_);
                    if (trigger.rowIndex < (int)rows_.size())
                    {
                        const auto& row = rows_[trigger.rowIndex];
                        if (row.muted)
                            shouldPlay = false;
                        else if (anySoloed && !row.soloed)
                            shouldPlay = false;
                    }
                }

                if (shouldPlay)
                {
                    triggerVoice(trigger.rowIndex);
                }

                ++nextTriggerIndex_;
            }
            else
            {
                break;  // No more triggers at this position
            }
        }

        // Check for loop wrap
        if (currentPos == 0 && globalSamplePosition_ > 0)
        {
            nextTriggerIndex_ = 0;
        }

        // Advance global position
        ++globalSamplePosition_;

        // Update playhead step and phase from position (works even with no triggers)
        if (totalSteps > 0 && samplesPerStep > 0)
        {
            int currentStep = static_cast<int>(currentPos / samplesPerStep) % totalSteps;
            playheadStep_.store(currentStep, std::memory_order_release);

            int posInStep = static_cast<int>(currentPos) % samplesPerStep;
            float phase = static_cast<float>(posInStep) / static_cast<float>(samplesPerStep);
            playheadPhase_.store(phase, std::memory_order_release);
        }
    }

    // Mix active voices into output with resampling and volume/pan
    for (auto& voice : voices_)
    {
        if (!voice.active || !voice.buffer)
            continue;

        const auto& srcBuffer = *voice.buffer;
        const int srcChannels = srcBuffer.getNumChannels();
        const int srcSamples = srcBuffer.getNumSamples();
        const int outChannels = outputBuffer->getNumChannels();

        // Calculate pan gains (constant-power panning)
        const float leftGain = voice.volume * std::cos(voice.pan * juce::MathConstants<float>::halfPi);
        const float rightGain = voice.volume * std::sin(voice.pan * juce::MathConstants<float>::halfPi);

        // Mix with linear interpolation for sample rate conversion
        for (int s = 0; s < numSamples; ++s)
        {
            // Check if we've reached end of source
            if (voice.samplePosition >= srcSamples - 1)
            {
                voice.active = false;
                voice.buffer = nullptr;
                break;
            }

            // Get interpolation indices and fraction
            int idx0 = static_cast<int>(voice.samplePosition);
            int idx1 = idx0 + 1;
            float frac = static_cast<float>(voice.samplePosition - idx0);

            // Ensure we don't read past buffer
            if (idx1 >= srcSamples)
            {
                idx1 = idx0;
                frac = 0.0f;
            }

            // Anti-click micro-fade: 2 ms ramp-in at start, 2 ms ramp-out at end.
            // Globally gated by samplar::microFadeEnabledFlag(); returns 1.0 if off.
            const float fadeGain = samplar::computeVoiceFadeGain(
                voice.samplePosition, srcSamples, currentSampleRate_);

            // Mix interpolated sample into each output channel with volume/pan
            for (int ch = 0; ch < outChannels; ++ch)
            {
                const int srcCh = juce::jmin(ch, srcChannels - 1);
                const float* src = srcBuffer.getReadPointer(srcCh);
                float* dst = outputBuffer->getWritePointer(ch, startSample);

                // Linear interpolation: sample = src[idx0] + frac * (src[idx1] - src[idx0])
                float sample = src[idx0] + frac * (src[idx1] - src[idx0]);

                // Apply volume and pan based on channel
                float gain = (ch == 0) ? leftGain : rightGain;
                if (outChannels == 1) gain = voice.volume;  // Mono output - just volume

                dst[s] += sample * gain * fadeGain;
            }

            // Advance position by resample ratio
            // ratio > 1.0 = source faster than device (pitch up, play faster through source)
            // ratio < 1.0 = source slower than device (pitch down, play slower through source)
            voice.samplePosition += voice.resampleRatio;
        }
    }
}

//==============================================================================
// Pattern Control
//==============================================================================

void SequencerAudioSource::setPattern(const std::vector<RowData>& rows, const Config& config)
{
    juce::Logger::writeToLog("[SequencerAudioSource] setPattern: " +
        juce::String(rows.size()) + " rows, " +
        juce::String(config.stepsPerBar * config.numBars) + " steps");

    {
        std::lock_guard<std::mutex> lock(patternMutex_);
        rows_ = rows;
        config_ = config;
    }

    totalSteps_.store(config.stepsPerBar * config.numBars, std::memory_order_release);
    patternUpdatePending_.store(true, std::memory_order_release);
}

void SequencerAudioSource::setRowSample(int rowIndex, const SampleBuffer& sample)
{
    std::lock_guard<std::mutex> lock(patternMutex_);

    if (rowIndex >= 0 && rowIndex < (int)rows_.size())
    {
        rows_[rowIndex].sample = sample;
        juce::Logger::writeToLog("[SequencerAudioSource] setRowSample: row " +
            juce::String(rowIndex) + " updated");
    }
}

void SequencerAudioSource::setCell(int row, int step, bool on)
{
    std::lock_guard<std::mutex> lock(patternMutex_);

    if (row >= 0 && row < (int)rows_.size())
    {
        if (step >= 0 && step < (int)rows_[row].steps.size())
        {
            rows_[row].steps[step] = on;
            patternUpdatePending_.store(true, std::memory_order_release);
        }
    }
}

void SequencerAudioSource::setTempo(double bpm)
{
    {
        std::lock_guard<std::mutex> lock(patternMutex_);
        tempoBpm_ = juce::jlimit(20.0, 400.0, bpm);
    }
    patternUpdatePending_.store(true, std::memory_order_release);

    juce::Logger::writeToLog("[SequencerAudioSource] setTempo: " + juce::String(bpm) + " BPM");
}

void SequencerAudioSource::setSwing(double percent)
{
    {
        std::lock_guard<std::mutex> lock(patternMutex_);
        config_.swingPercent = juce::jlimit(0.0, 75.0, percent);
    }
    patternUpdatePending_.store(true, std::memory_order_release);
}

void SequencerAudioSource::setRowMuted(int row, bool muted)
{
    std::lock_guard<std::mutex> lock(patternMutex_);

    if (row >= 0 && row < (int)rows_.size())
    {
        rows_[row].muted = muted;
    }
}

void SequencerAudioSource::setRowSoloed(int row, bool soloed)
{
    std::lock_guard<std::mutex> lock(patternMutex_);

    if (row >= 0 && row < (int)rows_.size())
    {
        rows_[row].soloed = soloed;
    }
}

void SequencerAudioSource::setRowVolume(int row, float volume)
{
    std::lock_guard<std::mutex> lock(patternMutex_);

    if (row >= 0 && row < (int)rows_.size())
    {
        rows_[row].volume = volume;
        juce::Logger::writeToLog("[SequencerAudioSource] setRowVolume: row " +
            juce::String(row) + " = " + juce::String(volume, 2));
    }
}

void SequencerAudioSource::setRowPan(int row, float pan)
{
    std::lock_guard<std::mutex> lock(patternMutex_);

    if (row >= 0 && row < (int)rows_.size())
    {
        rows_[row].pan = juce::jlimit(0.0f, 1.0f, pan);
    }
}

//==============================================================================
// Transport Control
//==============================================================================

void SequencerAudioSource::start()
{
    juce::Logger::writeToLog("[SequencerAudioSource] START");

    // Reset playback position
    globalSamplePosition_ = 0;
    nextTriggerIndex_ = 0;
    playheadStep_.store(0, std::memory_order_release);
    playheadPhase_.store(0.0f, std::memory_order_release);

    // Stop all active voices
    for (auto& voice : voices_)
    {
        voice.active = false;
        voice.buffer = nullptr;
    }

    // Ensure schedule is up to date
    {
        std::lock_guard<std::mutex> lock(patternMutex_);
        rebuildSchedule();
    }

    playing_.store(true, std::memory_order_release);
}

void SequencerAudioSource::stop()
{
    juce::Logger::writeToLog("[SequencerAudioSource] STOP");

    playing_.store(false, std::memory_order_release);

    // Stop all active voices
    for (auto& voice : voices_)
    {
        voice.active = false;
        voice.buffer = nullptr;
    }

    playheadPhase_.store(0.0f, std::memory_order_release);
}

//==============================================================================
// Internal Methods
//==============================================================================

void SequencerAudioSource::rebuildSchedule()
{
    schedule_.clear();

    const int totalSteps = config_.stepsPerBar * config_.numBars;
    if (totalSteps <= 0 || currentSampleRate_ <= 0 || tempoBpm_ <= 0)
    {
        patternLengthSamples_ = 0;
        return;
    }

    // Calculate pattern length in samples
    // Duration = (beats * 60) / BPM
    // Beats = totalSteps / stepsPerBeat
    const double stepsPerBeat = static_cast<double>(config_.stepsPerBar) / static_cast<double>(config_.beatsPerBar);
    const double beatsInPattern = static_cast<double>(totalSteps) / stepsPerBeat;
    const double patternDurationSec = (beatsInPattern * 60.0) / tempoBpm_;
    patternLengthSamples_ = static_cast<int>(patternDurationSec * currentSampleRate_);

    juce::Logger::writeToLog("[SequencerAudioSource] rebuildSchedule: " +
        juce::String(totalSteps) + " steps, " +
        juce::String(patternLengthSamples_) + " samples (" +
        juce::String(patternDurationSec, 3) + "s)");

    // Calculate step duration in samples for roll timing
    const int samplesPerStep = patternLengthSamples_ / totalSteps;

    // Build schedule of all triggers (including rolls/ratchets)
    for (int step = 0; step < totalSteps; ++step)
    {
        int stepStartPos = calculateStepSamplePosition(step);

        for (int row = 0; row < (int)rows_.size(); ++row)
        {
            if (step < (int)rows_[row].steps.size() && rows_[row].steps[step])
            {
                // Schedule triggers even without samples (triggerVoice handles null buffers)
                // This ensures playback/playhead works without loaded samples

                // Check for roll data
                bool hasRoll = false;
                if (step < (int)rows_[row].rollData.size())
                {
                    const auto& rollData = rows_[row].rollData[step];
                    if (rollData.rollCount > 1 || rollData.rollStyle != RollStyle::None)
                    {
                        hasRoll = true;

                        // Get roll trigger positions and velocities
                        auto rollTriggers = rollData.getRollTriggers();
                        for (const auto& [relPos, velocity] : rollTriggers)
                        {
                            ScheduledTrigger trigger;
                            // relPos is 0.0-1.0 within step
                            trigger.samplePosition = stepStartPos + static_cast<int>(relPos * samplesPerStep);
                            trigger.rowIndex = row;
                            trigger.stepIndex = step;
                            trigger.velocity = velocity;
                            schedule_.push_back(trigger);
                        }
                    }
                }

                // If no roll, add single trigger
                if (!hasRoll)
                {
                    ScheduledTrigger trigger;
                    trigger.samplePosition = stepStartPos;
                    trigger.rowIndex = row;
                    trigger.stepIndex = step;
                    trigger.velocity = 1.0f;
                    schedule_.push_back(trigger);
                }
            }
        }
    }

    // Sort by sample position (should already be sorted, but ensure)
    std::sort(schedule_.begin(), schedule_.end(),
        [](const ScheduledTrigger& a, const ScheduledTrigger& b) {
            return a.samplePosition < b.samplePosition;
        });

    juce::Logger::writeToLog("[SequencerAudioSource] Schedule: " +
        juce::String(schedule_.size()) + " triggers");
}

int SequencerAudioSource::calculateStepSamplePosition(int step) const
{
    if (patternLengthSamples_ <= 0)
        return 0;

    const int totalSteps = config_.stepsPerBar * config_.numBars;
    if (totalSteps <= 0)
        return 0;

    const int samplesPerStep = patternLengthSamples_ / totalSteps;

    // Base position
    int position = step * samplesPerStep;

    // Apply swing to odd steps
    if (step % 2 == 1 && config_.swingPercent > 0.0)
    {
        double swingAmount = juce::jlimit(0.0, 0.75, config_.swingPercent / 100.0) * 0.5;
        int swingOffset = static_cast<int>(samplesPerStep * swingAmount);
        position += swingOffset;
    }

    return position;
}

void SequencerAudioSource::triggerVoice(int rowIndex)
{
    // Get sample buffer, sample rate, volume, and pan for this row
    std::shared_ptr<const juce::AudioBuffer<float>> buffer;
    double sampleRate = currentSampleRate_;  // Default to device rate
    float rowVolume = 1.0f;
    float rowPan = 0.5f;
    {
        std::lock_guard<std::mutex> lock(patternMutex_);
        if (rowIndex >= 0 && rowIndex < (int)rows_.size())
        {
            buffer = rows_[rowIndex].sample.buffer;
            if (rows_[rowIndex].sample.sampleRate > 0)
            {
                sampleRate = rows_[rowIndex].sample.sampleRate;
            }
            rowVolume = rows_[rowIndex].volume;
            rowPan = rows_[rowIndex].pan;
        }
    }

    if (!buffer)
        return;

    // Calculate resample ratio: sampleRate / deviceRate
    // If sample is 44100Hz and device is 48000Hz, ratio = 0.91875 (play slower through source to maintain pitch)
    // If sample is 48000Hz and device is 44100Hz, ratio = 1.0884 (play faster through source to maintain pitch)
    double resampleRatio = (currentSampleRate_ > 0) ? (sampleRate / currentSampleRate_) : 1.0;

    // Log sample rate info for debugging (only if ratio differs from 1.0)
    if (std::abs(resampleRatio - 1.0) > 0.001)
    {
        juce::Logger::writeToLog("[SequencerAudioSource] Row " + juce::String(rowIndex) +
            " resample: " + juce::String(sampleRate, 0) + "Hz -> " +
            juce::String(currentSampleRate_, 0) + "Hz (ratio=" +
            juce::String(resampleRatio, 4) + ") vol=" + juce::String(rowVolume, 2));
    }

    // Find a free voice
    for (auto& voice : voices_)
    {
        if (!voice.active)
        {
            voice.rowIndex = rowIndex;
            voice.samplePosition = 0.0;
            voice.resampleRatio = resampleRatio;
            voice.buffer = buffer;
            voice.volume = rowVolume;
            voice.pan = rowPan;
            voice.active = true;
            return;
        }
    }

    // No free voice - steal the oldest one (first in array that's active)
    for (auto& voice : voices_)
    {
        if (voice.active)
        {
            voice.rowIndex = rowIndex;
            voice.samplePosition = 0.0;
            voice.resampleRatio = resampleRatio;
            voice.buffer = buffer;
            voice.volume = rowVolume;
            voice.pan = rowPan;
            // voice.active already true
            return;
        }
    }
}

} // namespace samplar
