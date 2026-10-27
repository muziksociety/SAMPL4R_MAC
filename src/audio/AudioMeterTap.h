#pragma once

#include <atomic>

namespace samplar {

// Lock-free audio-meter tap. The audio thread writes the current per-channel
// peak via publish(); the UI thread reads via consumeLeft() / consumeRight().
// Header-only — no CMakeLists change. RT-safe (atomic loads/stores only).
//
// Each consume() returns the max peak seen since the last consume() call and
// resets the accumulator, so the UI naturally gets a peak-hold display when
// it polls at e.g. 30 Hz.
class AudioMeterTap {
public:
    static AudioMeterTap& instance() {
        static AudioMeterTap inst;
        return inst;
    }

    // Audio thread: call once per processBlock with the peak magnitude of
    // each channel (clamp to 0..1 range upstream if needed).
    void publish(float peakLeft, float peakRight) noexcept {
        atomicMax(peakL_, peakLeft);
        atomicMax(peakR_, peakRight);
    }

    // UI thread: read-and-reset the peak hold for one channel.
    float consumeLeft()  noexcept { return peakL_.exchange(0.0f, std::memory_order_relaxed); }
    float consumeRight() noexcept { return peakR_.exchange(0.0f, std::memory_order_relaxed); }

private:
    AudioMeterTap() = default;

    static void atomicMax(std::atomic<float>& slot, float v) noexcept {
        float prev = slot.load(std::memory_order_relaxed);
        while (v > prev
               && !slot.compare_exchange_weak(prev, v, std::memory_order_relaxed)) {}
    }

    std::atomic<float> peakL_{0.0f};
    std::atomic<float> peakR_{0.0f};
};

} // namespace samplar
