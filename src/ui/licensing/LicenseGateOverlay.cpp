#include "LicenseGateOverlay.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace samplar {

namespace {

constexpr int kPanelWidth  = 480;
constexpr int kPanelHeight = 440;

// The panel belongs inside the plugin editor (or the app window's content),
// never on top of a host / title bar.
juce::Component* findGateHost(juce::Component* from)
{
    if (from == nullptr)
        return nullptr;
    if (auto* editor = dynamic_cast<juce::AudioProcessorEditor*>(from))
        return editor;
    if (auto* editor = from->findParentComponentOfClass<juce::AudioProcessorEditor>())
        return editor;
    auto* top = from->getTopLevelComponent();
    if (auto* window = dynamic_cast<juce::ResizableWindow*>(top))
        if (auto* content = window->getContentComponent())
            return content;
    return top;
}

} // namespace

LicenseGateOverlay::LicenseGateOverlay(LicenseSystemService& service, EventBus& bus, const juce::String& dismissText)
    : dialog_(service, bus),
      allowDismiss_(dismissText.isNotEmpty())
{
    juce::Logger::writeToLog("[LicenseGate] created, dismiss: " + (dismissText.isEmpty() ? juce::String("(none)") : dismissText));
    setInterceptsMouseClicks(true, true);   // blocks the UI underneath
    setWantsKeyboardFocus(true);

    dialog_.setDismissButtonText(dismissText);
    dialog_.onValidated = [this] { finish(1); };
    dialog_.onQuit      = [this] { finish(0); };
    addAndMakeVisible(dialog_);
}

LicenseGateOverlay::~LicenseGateOverlay()
{
    if (host_ != nullptr)
        host_->removeComponentListener(this);
    juce::Logger::writeToLog("[LicenseGate] destroyed");
}

void LicenseGateOverlay::showInWindow(juce::Component* from, LicenseSystemService& service, EventBus& bus,
                                      bool allowClose, std::function<void(int result)> onComplete)
{
    juce::Logger::writeToLog("[LicenseGate] === showInWindow START === allowClose " + juce::String(allowClose ? 1 : 0));

    auto* host = findGateHost(from);
    if (host == nullptr)
    {
        juce::Logger::writeToLog("[LicenseGate] showInWindow: no window to show in - cancelled");
        if (onComplete)
            juce::MessageManager::callAsync([onComplete]() { onComplete(0); });
        return;
    }

    // Self-owned: deleted in finish() or when the host is destroyed.
    auto* overlay = new LicenseGateOverlay(service, bus, allowClose ? "Close" : "Quit");
    overlay->selfOwned_ = true;
    overlay->onFinished = std::move(onComplete);
    overlay->host_ = host;
    host->addComponentListener(overlay);
    host->addAndMakeVisible(overlay);
    overlay->setBounds(host->getLocalBounds());
    overlay->toFront(true);
    juce::Logger::writeToLog("[LicenseGate] === showInWindow END (non-blocking) ===");
}

void LicenseGateOverlay::finish(int result)
{
    if (finished_)
        return;
    finished_ = true;
    juce::Logger::writeToLog("[LicenseGate] finish(" + juce::String(result) + ")");

    // Deferred: we are inside a dialog callback here, and the handler may
    // delete this overlay (and with it the dialog).
    juce::Component::SafePointer<LicenseGateOverlay> safe(this);
    juce::MessageManager::callAsync([safe, result]() {
        if (safe == nullptr)
            return;
        const bool selfOwned = safe->selfOwned_;
        if (auto cb = safe->onFinished)   // copy: the handler may delete the overlay
            cb(result);
        if (selfOwned && safe != nullptr)
            delete safe.getComponent();
    });
}

void LicenseGateOverlay::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xe6101018));
}

void LicenseGateOverlay::resized()
{
    const int w = juce::jmin(kPanelWidth, getWidth() - 16);
    const int h = juce::jmin(kPanelHeight, getHeight() - 16);
    dialog_.setBounds(getLocalBounds().withSizeKeepingCentre(juce::jmax(0, w), juce::jmax(0, h)));
}

bool LicenseGateOverlay::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && allowDismiss_ && !dialog_.isOAuthInProgress())
    {
        juce::Logger::writeToLog("[LicenseGate] Escape -> dismiss");
        finish(0);
        return true;
    }
    // Not ours: let it travel up to the editor / host (DAW shortcuts such as the
    // space bar keep working). The SAMPLAR UI is a sibling, not a parent, so it
    // never receives these keys.
    return false;
}

void LicenseGateOverlay::componentMovedOrResized(juce::Component& component, bool, bool wasResized)
{
    if (wasResized && &component == host_)
        setBounds(component.getLocalBounds());
}

void LicenseGateOverlay::componentBeingDeleted(juce::Component& component)
{
    if (&component != host_)
        return;
    component.removeComponentListener(this);
    host_ = nullptr;
    if (selfOwned_)
    {
        // Delete now, while the licence service / event bus this panel uses are
        // certainly still alive (they outlive the window).
        juce::Logger::writeToLog("[LicenseGate] host window destroyed -> deleting self-owned panel");
        delete this;
    }
}

} // namespace samplar
