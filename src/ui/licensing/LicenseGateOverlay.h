#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

#include "LicenseActivationDialog.h"

namespace samplar {

/**
 * Full-window, non-modal sign-in panel: dims the SAMPLAR UI, blocks clicks to
 * it and shows LicenseActivationDialog centred. Replaces the blocking
 * runModalLoop() that 1.0.0 ran inside the plugin editor constructor
 * (ported from SM5 b0526074, without its InWindowDialog dependency).
 *
 *   +--------------------- plugin window ---------------------+
 *   |  (SAMPLAR UI, dimmed, not clickable)                     |
 *   |            +------- sign-in panel -------+               |
 *   |            |  SAMPLAR                     |              |
 *   |            |  [ Sign in with browser ]    |              |
 *   |            |  [ Try again ]   status      |              |
 *   |            +------------------------------+              |
 *   +---------------------------------------------------------+
 *
 * Two ownership modes:
 *  - owned by the caller (the plugin editor keeps it in a unique_ptr and sets
 *    its bounds);
 *  - self-owned via showInWindow(): it covers the window that contains the
 *    given component, follows its size, and deletes itself when finished or
 *    when that window is destroyed.
 * Message thread only.
 */
class LicenseGateOverlay : public juce::Component,
                           private juce::ComponentListener
{
public:
    /** dismissText: "Quit" / "Close" button text; empty = no way out but signing in. */
    LicenseGateOverlay(LicenseSystemService& service, EventBus& bus, const juce::String& dismissText);
    ~LicenseGateOverlay() override;

    /** 1 = licence validated, 0 = dismissed. Called once, asynchronously (never
        from inside a dialog callback), so the handler may delete this overlay. */
    std::function<void(int result)> onFinished;

    static void showInWindow(juce::Component* from, LicenseSystemService& service, EventBus& bus,
                             bool allowClose, std::function<void(int result)> onComplete);

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void finish(int result);

    void componentMovedOrResized(juce::Component& component, bool wasMoved, bool wasResized) override;
    void componentBeingDeleted(juce::Component& component) override;

    LicenseActivationDialog dialog_;
    juce::Component* host_ = nullptr;   // self-owned mode only; cleared when the host dies
    bool allowDismiss_ = false;
    bool finished_ = false;
    bool selfOwned_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LicenseGateOverlay)
};

} // namespace samplar
