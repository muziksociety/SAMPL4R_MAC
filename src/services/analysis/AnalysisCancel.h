#pragma once

#include <atomic>

namespace samplar {

/**
 * Cancel token for the analysis detectors (BeatThis / HybridTempo / Downbeat).
 *
 * Wraps a caller-owned std::atomic<bool> that must outlive the detect() call (capture the
 * owning shared_ptr in the job lambda). Either polarity is accepted:
 *   AnalysisCancel (&cancelFlag)                - cancel when *flag == true (implicit)
 *   AnalysisCancel::whenFalse (alive.get())     - cancel when an "alive" flag turns false
 * A default-constructed token never cancels.
 *
 * Detectors poll it between stages and a watcher thread aborts a running ONNX inference
 * (Ort::RunOptions::SetTerminate), so a cancel returns within ~200 ms.
 */
struct AnalysisCancel
{
    const std::atomic<bool>* flag = nullptr;
    bool cancelValue = true;

    AnalysisCancel() noexcept = default;
    AnalysisCancel (const std::atomic<bool>* cancelWhenTrue) noexcept : flag (cancelWhenTrue) {}

    static AnalysisCancel whenFalse (const std::atomic<bool>* aliveFlag) noexcept
    {
        AnalysisCancel c (aliveFlag);
        c.cancelValue = false;
        return c;
    }

    bool canCancel() const noexcept { return flag != nullptr; }
    bool requested() const noexcept { return flag != nullptr && flag->load (std::memory_order_acquire) == cancelValue; }
};

} // namespace samplar
