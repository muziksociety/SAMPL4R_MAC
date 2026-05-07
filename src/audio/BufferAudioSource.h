#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <mutex>
#include <cstring>

namespace samplar {

/**
 * Simple audio source that plays from a buffer.
 * Designed for instant playback of cached samples without transport overhead.
 *
 * Key features:
 * - No file I/O - plays directly from memory buffer
 * - Instant start/stop - no teardown delays needed
 * - Thread-safe play/stop control via atomic flags
 * - Supports immediate buffer replacement during playback
 */
class BufferAudioSource : public juce::AudioSource
{
public:
    BufferAudioSource() = default;
    ~BufferAudioSource() override = default;

    //==========================================================================
    // Buffer Management
    //==========================================================================

    /**
     * Set the buffer to play. Takes ownership via move.
     * Can be called while playing - will switch to new buffer immediately.
     * @param buffer The audio buffer to play
     * @param sampleRate Sample rate of the buffer
     */
    void setBuffer(juce::AudioBuffer<float>&& buffer, double sampleRate)
    {
        std::lock_guard<std::mutex> lock(bufferMutex_);
        buffer_ = std::move(buffer);
        bufferSampleRate_ = sampleRate;
        readPosition_.store(0);
        hasBuffer_.store(buffer_.getNumSamples() > 0);
    }

    /**
     * Set the buffer to play from a copy.
     * @param buffer The audio buffer to copy and play
     * @param sampleRate Sample rate of the buffer
     */
    void setBufferCopy(const juce::AudioBuffer<float>& buffer, double sampleRate)
    {
        std::lock_guard<std::mutex> lock(bufferMutex_);
        buffer_.makeCopyOf(buffer);
        bufferSampleRate_ = sampleRate;
        readPosition_.store(0);
        hasBuffer_.store(buffer_.getNumSamples() > 0);
    }

    /**
     * Clear the buffer.
     */
    void clearBuffer()
    {
        std::lock_guard<std::mutex> lock(bufferMutex_);
        buffer_.setSize(0, 0);
        bufferSampleRate_ = 0.0;
        readPosition_.store(0);
        hasBuffer_.store(false);
        isPlaying_.store(false);
    }

    //==========================================================================
    // Playback Control (Thread-Safe)
    //==========================================================================

    /**
     * Start playback from the beginning.
     */
    void play()
    {
        readPosition_.store(0);
        isPlaying_.store(true);
    }

    /**
     * Start playback from a specific position.
     * @param positionSamples Position in samples
     */
    void playFromPosition(int positionSamples)
    {
        readPosition_.store(juce::jmax(0, positionSamples));
        isPlaying_.store(true);
    }

    /**
     * Stop playback immediately.
     */
    void stop()
    {
        isPlaying_.store(false);
    }

    /**
     * Check if currently playing.
     */
    bool isPlaying() const { return isPlaying_.load(); }
    void setLooping(bool loop) { looping_.store(loop); }
    bool isLooping() const { return looping_.load(); }

    /**
     * Check if buffer is loaded and ready.
     */
    bool hasBuffer() const { return hasBuffer_.load(); }

    /**
     * Get current playback progress (0.0 to 1.0).
     */
    float getProgress() const
    {
        if (!hasBuffer_.load())
            return 0.0f;

        int total = buffer_.getNumSamples();
        if (total == 0)
            return 0.0f;

        return static_cast<float>(readPosition_.load()) / static_cast<float>(total);
    }

    /**
     * Get duration in seconds.
     */
    double getDurationSeconds() const
    {
        if (!hasBuffer_.load() || bufferSampleRate_ <= 0.0)
            return 0.0;

        return static_cast<double>(buffer_.getNumSamples()) / bufferSampleRate_;
    }

    //==========================================================================
    // AudioSource Implementation
    //==========================================================================

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override
    {
        outputSampleRate_ = sampleRate;
        // No preparation needed - we play directly from buffer
    }

    void releaseResources() override
    {
        // Nothing to release
    }

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override
    {
        // Always clear first to prevent garbage audio
        bufferToFill.clearActiveBufferRegion();

        if (!isPlaying_.load() || !hasBuffer_.load())
            return;

        // Lock buffer for reading
        std::lock_guard<std::mutex> lock(bufferMutex_);

        const int totalSamples = buffer_.getNumSamples();
        const int numChannels = buffer_.getNumChannels();

        if (totalSamples == 0 || numChannels == 0)
            return;

        int currentPos = readPosition_.load();

        if (currentPos >= totalSamples) {
            if (looping_.load()) {
                currentPos = 0;
                readPosition_.store(0);
            } else {
                isPlaying_.store(false);
                return;
            }
        }

        const int remaining = totalSamples - currentPos;
        const int numSamplesToRead = juce::jmin(bufferToFill.numSamples, remaining);

        // Copy audio data
        for (int ch = 0; ch < bufferToFill.buffer->getNumChannels(); ++ch) {
            const int srcChannel = juce::jmin(ch, numChannels - 1);
            const float* src = buffer_.getReadPointer(srcChannel, currentPos);
            float* dst = bufferToFill.buffer->getWritePointer(ch, bufferToFill.startSample);

            std::memcpy(dst, src, static_cast<size_t>(numSamplesToRead) * sizeof(float));
        }

        int newPos = currentPos + numSamplesToRead;

        // If looping and we hit the end, wrap around and fill the remaining output
        if (newPos >= totalSamples && looping_.load()) {
            newPos = 0;
            // Fill any remaining samples in this block from the start of the buffer
            int samplesLeft = bufferToFill.numSamples - numSamplesToRead;
            if (samplesLeft > 0) {
                int wrapRead = juce::jmin(samplesLeft, totalSamples);
                for (int ch = 0; ch < bufferToFill.buffer->getNumChannels(); ++ch) {
                    const int srcChannel = juce::jmin(ch, numChannels - 1);
                    const float* src = buffer_.getReadPointer(srcChannel, 0);
                    float* dst = bufferToFill.buffer->getWritePointer(ch, bufferToFill.startSample + numSamplesToRead);
                    std::memcpy(dst, src, static_cast<size_t>(wrapRead) * sizeof(float));
                }
                newPos = wrapRead;
            }
        } else if (newPos >= totalSamples) {
            isPlaying_.store(false);
        }

        readPosition_.store(newPos);
    }

private:
    // Buffer storage (protected by mutex for buffer replacement)
    std::mutex bufferMutex_;
    juce::AudioBuffer<float> buffer_;
    double bufferSampleRate_ = 0.0;
    double outputSampleRate_ = 44100.0;

    // Atomic state for lock-free audio thread access
    std::atomic<int> readPosition_{0};
    std::atomic<bool> isPlaying_{false};
    std::atomic<bool> looping_{false};
    std::atomic<bool> hasBuffer_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BufferAudioSource)
};

} // namespace samplar
