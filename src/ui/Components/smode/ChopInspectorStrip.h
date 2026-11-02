#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "MidiLearnControls.h"
#include <cmath>
#include <functional>
#include <iterator>

namespace samplar {

/**
 * ChopInspectorStrip - one-row editor for the selected chop (UX gap 4).
 *
 *   [Vocals #3  C1] [PITCH +0 st] [GAIN 0.0 dB] [PAN C] [REV] [GLOBAL] [ATK 0 ms] [REL 0 ms]
 *
 * Pure view: SModePage feeds it the selected chop's SegmentEffects and writes
 * the edited values back to the model (the realtime engine reads them from the
 * playback snapshot, so playback, sequencer and exports all follow).
 * onEditBegin fires once per gesture (drag start, typed value, button click) so
 * the page can push exactly one undo entry per edit.
 */
class ChopInspectorStrip : public juce::Component
{
public:
    struct Values
    {
        int   pitchSemis { 0 };     // -24..+24
        float gainDb     { 0.0f };  // -36..+6 (model stores linear volume)
        float pan        { 0.0f };  // -1..+1
        bool  reverse    { false };
        int   trigger    { 0 };     // 0 = follow global 1SH/GATE, 1 = one-shot, 2 = gate
        float attackMs   { 0.0f };
        float releaseMs  { 0.0f };
    };

    ChopInspectorStrip();
    ~ChopInspectorStrip() override = default;

    /** Show a chop. `envelopeSupported` / `triggerSupported` = the model can
     *  store attack/release and the per-chop trigger mode. */
    void setTarget(const juce::String& title, const Values& v, bool envelopeSupported, bool triggerSupported);
    /** No chop selected: every control disabled, hint text shown. */
    void clearTarget();
    bool hasTarget() const { return hasTarget_; }
    const Values& getValues() const { return values_; }

    std::function<void()>              onEditBegin;
    std::function<void(const Values&)> onValuesChanged;
    /** Right-click on a control: `param` is one of kMidiParams. */
    std::function<void(const juce::String& param, juce::Component& anchor)> onMidiMenu;

    /** MIDI CC targets of the inspector ("selected chop" controls). */
    static const juce::StringArray& midiParams()
    {
        static const juce::StringArray p { "pitch", "gain", "pan", "reverse", "trigger", "attack", "release" };
        return p;
    }
    /** Control behind a param (nullptr for an unknown name). */
    juce::Component* getControl(const juce::String& param);
    /** Learned CC shown in the control tooltip ("" = none). */
    void setMidiInfo(const juce::String& param, const juce::String& info);
    juce::String getMidiInfo(const juce::String& param);
    /** A bound MIDI CC moved (value 0..1): sets the control like a user edit
     *  (onEditBegin fires first when beginGesture, then onValuesChanged). False = ignored
     *  (no chop selected, control disabled, value unchanged). */
    bool setFromController(const juce::String& param, float value01, bool beginGesture);

    void paint(juce::Graphics& g) override;
    void resized() override;

    static float gainDbToLinear(float db)   { return db <= kMinGainDb + 0.01f ? 0.0f : std::pow(10.0f, db / 20.0f); }
    static float linearToGainDb(float lin)  { return lin <= 0.0f ? kMinGainDb : juce::jlimit(kMinGainDb, kMaxGainDb, 20.0f * std::log10(lin)); }
    static juce::String triggerName(int t)  { return t == 1 ? "1SHOT" : (t == 2 ? "GATE" : "GLOBAL"); }

    static constexpr float kMinGainDb = -36.0f;
    static constexpr float kMaxGainDb = 6.0f;

private:
    void setupSlider(MidiLearnSlider& s, double lo, double hi, double step, double skewMid,
                     std::function<juce::String(double)> text, const juce::String& tip);
    void pushValues();
    void pushValuesSilently();   // read the sliders into values_ (no callback)
    void beginGestureIfIdle();
    void updateButtons();
    void setControlsEnabled(bool on);

    juce::Label      title_;
    MidiLearnSlider  pitch_, gain_, pan_, attack_, release_;
    MidiLearnButton  reverseBtn_ { "REV" };
    MidiLearnButton  triggerBtn_ { "GLOBAL" };

    Values values_;
    bool hasTarget_ { false };
    bool envelopeOk_ { false };
    bool triggerOk_ { false };
    bool dragging_ { false };
    bool updating_ { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChopInspectorStrip)
};

} // namespace samplar
