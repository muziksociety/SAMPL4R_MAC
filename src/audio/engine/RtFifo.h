#pragma once

// Lock-free FIFOs for the realtime engine.
//
//   SpscFifo<T, N>: one producer thread, one consumer thread, wait-free.
//   MpscFifo<T, N>: several NON-audio producers serialise on a SpinLock among
//                   themselves; the consumer (audio thread) never touches the
//                   lock, so the audio side stays wait-free.
// T must be trivially copyable. Storage is fixed at construction: push/pop
// never allocate.

#include <juce_core/juce_core.h>
#include <array>
#include <type_traits>

namespace samplar {
namespace engine {

template <typename T, int Capacity>
class SpscFifo
{
    static_assert(std::is_trivially_copyable<T>::value, "FIFO items must be POD");
public:
    bool push(const T& item) noexcept
    {
        int s1, n1, s2, n2;
        fifo_.prepareToWrite(1, s1, n1, s2, n2);
        if (n1 + n2 < 1) return false;
        data_[(size_t) (n1 > 0 ? s1 : s2)] = item;
        fifo_.finishedWrite(1);
        return true;
    }

    bool pop(T& out) noexcept
    {
        int s1, n1, s2, n2;
        fifo_.prepareToRead(1, s1, n1, s2, n2);
        if (n1 + n2 < 1) return false;
        out = data_[(size_t) (n1 > 0 ? s1 : s2)];
        fifo_.finishedRead(1);
        return true;
    }

    int getNumReady() const noexcept { return fifo_.getNumReady(); }
    void reset() noexcept            { fifo_.reset(); }

private:
    juce::AbstractFifo fifo_ { Capacity };
    std::array<T, (size_t) Capacity> data_ {};
};

template <typename T, int Capacity>
class MpscFifo
{
public:
    bool push(const T& item) noexcept
    {
        const juce::SpinLock::ScopedLockType sl(producerLock_);
        return fifo_.push(item);
    }
    bool pop(T& out) noexcept               { return fifo_.pop(out); }
    int getNumReady() const noexcept        { return fifo_.getNumReady(); }

private:
    juce::SpinLock producerLock_;
    SpscFifo<T, Capacity> fifo_;
};

} // namespace engine
} // namespace samplar
