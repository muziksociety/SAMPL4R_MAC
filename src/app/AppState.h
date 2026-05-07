#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_data_structures/juce_data_structures.h>
#include <functional>
#include <vector>

namespace samplar {

/**
 * AppState — centralized plugin state store.
 *
 * See docs/superpowers/specs/2026-04-21-appstate-full-plan.md for the full design.
 *
 * One juce::ValueTree root ("SM5AppState") owned by SamplarAudioProcessor.
 * Features register a StateBinding { nodeId, version, save, restore } that saves to
 * and restores from a named child tree. Late-registering features auto-drain: if
 * deserialize already ran and the feature's child tree exists in the buffered root,
 * restore() fires immediately on registerBinding.
 *
 * On-disk format:
 *   magic    4 bytes   "SMAS"
 *   version  2 bytes   little-endian 0x0001
 *   payload  ...       juce::ValueTree::writeToStream(...)
 *
 * Legacy JSON ({ "ui": ..., "smode": ..., "midi": ... }) is detected and imported
 * into the new tree before any binding restore() fires, so back-compat is free.
 *
 * Ownership rule: every feature that registers MUST unregister in its destructor.
 * Prefer ScopedStateBinding (RAII) over raw registerBinding/unregisterBinding.
 */

struct StateBinding
{
    juce::Identifier nodeId;
    int version { 1 };
    std::function<void(juce::ValueTree&)>       save;
    std::function<void(const juce::ValueTree&)> restore;
};

class AppState
{
public:
    explicit AppState(juce::AudioProcessor& processor);
    ~AppState();

    // Returns a token used to unregister. Or use ScopedStateBinding RAII.
    // If state has already been deserialized (late editor open), restore() fires
    // immediately with the buffered child tree (if one exists for this nodeId).
    int  registerBinding(StateBinding binding);
    void unregisterBinding(int token);

    // Called from SamplarAudioProcessor::getStateInformation / setStateInformation.
    void serialize  (juce::MemoryBlock& out);
    void deserialize(const void* data, int size);

    // Called when the editor is about to be destroyed (e.g. user closes the plugin
    // window within FL). Walks every registered binding's save() so the current
    // UI state is captured into root_ BEFORE the page components die. Next time
    // the editor opens, each binding's restore() can pull from root_.
    void snapshotAllBindings();

    // Force-invoke restore() on every registered binding against its current
    // sub-tree. Called from the editor after it finishes constructing, so state
    // is pulled back in sync rather than waiting for async late-drain.
    void restoreAllBindings();

    // Direct access (e.g. a feature wants to add a ValueTree::Listener on its sub-tree).
    juce::ValueTree getOrCreateChild(const juce::Identifier& nodeId);

    // Fire updateHostDisplay() on the processor — tells DAW the project is dirty.
    // Automatically called by the root listener on any tree property/child change.
    void markDirty();

    // Test / diagnostic: true after deserialize has been called at least once
    // (or the empty initial state has been considered ready).
    bool isRestoreReady() const { return restoreReady_; }

private:
    static constexpr const char* kMagic       = "SMAS";
    static constexpr int          kMagicLen    = 4;
    static constexpr juce::uint16 kFormatVersion = 1;
    static const juce::Identifier kRootId;

    juce::AudioProcessor& processor_;
    juce::ValueTree root_;

    struct Entry
    {
        int token;
        StateBinding binding;
    };
    std::vector<Entry> bindings_;
    int  nextToken_   { 1 };
    bool restoreReady_ { false };

    // Forwards any tree change to markDirty().
    struct RootListener : juce::ValueTree::Listener
    {
        AppState* owner;
        explicit RootListener(AppState* o) : owner(o) {}
        void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override { owner->markDirty(); }
        void valueTreeChildAdded     (juce::ValueTree&, juce::ValueTree&)        override { owner->markDirty(); }
        void valueTreeChildRemoved   (juce::ValueTree&, juce::ValueTree&, int)   override { owner->markDirty(); }
        void valueTreeChildOrderChanged(juce::ValueTree&, int, int)              override { owner->markDirty(); }
        void valueTreeParentChanged  (juce::ValueTree&)                          override {}
    };
    RootListener listener_ { this };

    // Legacy JSON importer — populates root_ from old-format { "ui", "smode", "midi" } blob.
    void importLegacyJson(const juce::String& jsonText);

    // Writes version attribute on root_ if missing.
    void stampRootVersion();

    JUCE_DECLARE_NON_COPYABLE(AppState)
};

/**
 * ScopedStateBinding — RAII helper. Registers on construction, unregisters on destruction.
 * Use this as a member of any class that needs to persist state.
 *
 * Example:
 *   class SampleBrowserPage {
 *       samplar::ScopedStateBinding stateBinding_ {
 *           processor_.getAppState(),
 *           { "sampleBrowser", 1,
 *             [this](juce::ValueTree& t){ saveMyStateTo(t); },
 *             [this](const juce::ValueTree& t){ restoreMyStateFrom(t); } }
 *       };
 *   };
 */
class ScopedStateBinding
{
public:
    ScopedStateBinding(AppState& s, StateBinding b)
        : state_(s), token_(s.registerBinding(std::move(b))) {}

    ~ScopedStateBinding() { state_.unregisterBinding(token_); }

    ScopedStateBinding(const ScopedStateBinding&) = delete;
    ScopedStateBinding& operator=(const ScopedStateBinding&) = delete;

private:
    AppState& state_;
    int token_;
};

} // namespace samplar
