#include "ChopInspectorStrip.h"

namespace samplar {

namespace
{
    juce::String signedInt(int v) { return v > 0 ? "+" + juce::String(v) : juce::String(v); }

    juce::String panText(double p)
    {
        const int pct = juce::roundToInt(p * 100.0);
        if (pct == 0) return "PAN C";
        return pct < 0 ? "PAN L" + juce::String(-pct) : "PAN R" + juce::String(pct);
    }

    juce::String msText(const char* prefix, double ms)
    {
        return juce::String(prefix) + (ms >= 1000.0 ? juce::String(ms / 1000.0, 2) + " s"
                                                    : juce::String(juce::roundToInt(ms)) + " ms");
    }
}

ChopInspectorStrip::ChopInspectorStrip()
{
    addAndMakeVisible(title_);
    title_.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    title_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.85f));
    title_.setJustificationType(juce::Justification::centredLeft);
    title_.setMinimumHorizontalScale(0.6f);
    title_.setInterceptsMouseClicks(false, false);

    setupSlider(pitch_, -24.0, 24.0, 1.0, 0.0,
                [](double v) { return "PITCH " + signedInt(juce::roundToInt(v)) + " st"; },
                "Chop pitch in semitones (time is preserved). Double-click: 0");
    setupSlider(gain_, kMinGainDb, kMaxGainDb, 0.1, -6.0,
                [](double v) { return v <= kMinGainDb + 0.01 ? juce::String("GAIN -inf")
                                                              : "GAIN " + juce::String(v, 1) + " dB"; },
                "Chop gain in dB. Double-click: 0 dB");
    setupSlider(pan_, -1.0, 1.0, 0.01, 0.0, [](double v) { return panText(v); },
                "Chop pan. Double-click: centre");
    setupSlider(attack_, 0.0, 2000.0, 1.0, 100.0, [](double v) { return msText("ATK ", v); },
                "Fade-in at the start of the chop. Double-click: 0");
    setupSlider(release_, 0.0, 5000.0, 1.0, 300.0, [](double v) { return msText("REL ", v); },
                "Fade-out at the end of the chop, and the fade after note-off in GATE mode. Double-click: 0");
    pitch_.setDoubleClickReturnValue(true, 0.0);
    gain_.setDoubleClickReturnValue(true, 0.0);
    pan_.setDoubleClickReturnValue(true, 0.0);
    attack_.setDoubleClickReturnValue(true, 0.0);
    release_.setDoubleClickReturnValue(true, 0.0);

    for (auto* b : { &reverseBtn_, &triggerBtn_ })
    {
        addAndMakeVisible(b);
        b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d2d3a));
        b->setMouseClickGrabsKeyboardFocus(false);
    }
    reverseBtn_.setBaseTooltip("Play this chop backwards");
    triggerBtn_.setBaseTooltip("MIDI trigger for this chop: GLOBAL follows the 1SH/GATE switch, "
                           "1SHOT plays to the end, GATE stops on note-off");
    reverseBtn_.onClick = [this]()
    {
        if (! hasTarget_) return;
        if (onEditBegin) onEditBegin();
        values_.reverse = ! values_.reverse;
        updateButtons();
        pushValues();
    };
    triggerBtn_.onClick = [this]()
    {
        if (! hasTarget_ || ! triggerOk_) return;
        if (onEditBegin) onEditBegin();
        values_.trigger = (values_.trigger + 1) % 3;
        updateButtons();
        pushValues();
    };

    // Right-click on any control -> the page's MIDI Learn / Clear MIDI menu.
    for (const auto& param : midiParams())
    {
        auto* c = getControl(param);
        auto hook = [this, param, c]() { if (onMidiMenu != nullptr && c != nullptr) onMidiMenu(param, *c); };
        if (auto* sl = dynamic_cast<MidiLearnSlider*>(c)) sl->onContextMenu = hook;
        else if (auto* bt = dynamic_cast<MidiLearnButton*>(c)) bt->onContextMenu = hook;
    }

    clearTarget();
}

juce::Component* ChopInspectorStrip::getControl(const juce::String& param)
{
    if (param == "pitch")   return &pitch_;
    if (param == "gain")    return &gain_;
    if (param == "pan")     return &pan_;
    if (param == "attack")  return &attack_;
    if (param == "release") return &release_;
    if (param == "reverse") return &reverseBtn_;
    if (param == "trigger") return &triggerBtn_;
    return nullptr;
}

void ChopInspectorStrip::setMidiInfo(const juce::String& param, const juce::String& info)
{
    auto* c = getControl(param);
    if (auto* sl = dynamic_cast<MidiLearnSlider*>(c)) sl->setMidiInfo(info);
    else if (auto* bt = dynamic_cast<MidiLearnButton*>(c)) bt->setMidiInfo(info);
}

juce::String ChopInspectorStrip::getMidiInfo(const juce::String& param)
{
    auto* c = getControl(param);
    if (auto* sl = dynamic_cast<MidiLearnSlider*>(c)) return sl->getMidiInfo();
    if (auto* bt = dynamic_cast<MidiLearnButton*>(c)) return bt->getMidiInfo();
    return {};
}

bool ChopInspectorStrip::setFromController(const juce::String& param, float value01, bool beginGesture)
{
    if (! hasTarget_ || ! std::isfinite(value01)) return false;
    const double v = juce::jlimit(0.0, 1.0, (double) value01);
    Values next = values_;
    if (param == "reverse")
        next.reverse = v >= 0.5;
    else if (param == "trigger")
    {
        if (! triggerOk_) return false;
        next.trigger = juce::jlimit(0, 2, (int) (v * 3.0));
    }
    else
    {
        auto* sl = dynamic_cast<MidiLearnSlider*>(getControl(param));
        // The strip's own enable state (not isEnabled(), which also reflects a
        // disabled parent such as the licence-locked root).
        if (sl == nullptr) return false;
        if ((param == "attack" || param == "release") && ! envelopeOk_) return false;
        const juce::ScopedValueSetter<bool> guard(updating_, true);
        // proportionOfLengthToValue follows the slider skew, so a CC sweeps the
        // control exactly like dragging it.
        sl->setValue(sl->proportionOfLengthToValue(v), juce::dontSendNotification);
        const auto before = values_;
        pushValuesSilently();
        const bool changed = before.pitchSemis != values_.pitchSemis || before.gainDb != values_.gainDb
                          || before.pan != values_.pan || before.attackMs != values_.attackMs
                          || before.releaseMs != values_.releaseMs;
        if (changed)
        {
            if (beginGesture && onEditBegin) onEditBegin();
            if (onValuesChanged) onValuesChanged(values_);
        }
        return changed;
    }
    if (next.reverse == values_.reverse && next.trigger == values_.trigger) return false;
    values_.reverse = next.reverse;
    values_.trigger = next.trigger;
    updateButtons();
    if (beginGesture && onEditBegin) onEditBegin();
    if (onValuesChanged) onValuesChanged(values_);
    return true;
}

void ChopInspectorStrip::setupSlider(MidiLearnSlider& s, double lo, double hi, double step, double skewMid,
                                     std::function<juce::String(double)> text, const juce::String& tip)
{
    addAndMakeVisible(s);
    s.setSliderStyle(juce::Slider::LinearBar);
    s.setRange(lo, hi, step);
    if (skewMid > lo && skewMid < hi && lo >= 0.0)
        s.setSkewFactorFromMidPoint(skewMid);
    // Bar sliders draw their value text inside the bar (read-only: a
    // double-click resets to the default instead of opening an editor).
    s.setTextBoxStyle(juce::Slider::TextBoxLeft, true, 0, 0);
    s.textFromValueFunction = std::move(text);
    s.valueFromTextFunction = [](const juce::String& t)
    {
        auto digits = t.retainCharacters("-+.0123456789");
        if (t.containsIgnoreCase("L")) return -std::abs(digits.getDoubleValue()) / 100.0;
        if (t.containsIgnoreCase("R") && ! t.containsIgnoreCase("REL")) return std::abs(digits.getDoubleValue()) / 100.0;
        if (t.containsIgnoreCase(" s") && ! t.containsIgnoreCase("ms")) return digits.getDoubleValue() * 1000.0;
        return digits.getDoubleValue();
    };
    s.updateText();   // the text box was built before the text function existed
    s.setBaseTooltip(tip);
    s.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setColour(juce::Slider::trackColourId, juce::Colour(0xff00b8d4).withAlpha(0.45f));
    s.setMouseClickGrabsKeyboardFocus(false);
    s.onDragStart = [this]()
    {
        dragging_ = true;
        if (hasTarget_ && onEditBegin) onEditBegin();
    };
    s.onDragEnd = [this]() { dragging_ = false; };
    s.onValueChange = [this]()
    {
        if (updating_ || ! hasTarget_) return;
        beginGestureIfIdle();
        pushValues();
    };
}

void ChopInspectorStrip::beginGestureIfIdle()
{
    // Typed values / double-click resets arrive without a drag gesture.
    if (! dragging_ && onEditBegin) onEditBegin();
}

void ChopInspectorStrip::pushValuesSilently()
{
    values_.pitchSemis = juce::roundToInt(pitch_.getValue());
    values_.gainDb     = (float) gain_.getValue();
    values_.pan        = (float) pan_.getValue();
    values_.attackMs   = (float) attack_.getValue();
    values_.releaseMs  = (float) release_.getValue();
}

void ChopInspectorStrip::pushValues()
{
    pushValuesSilently();
    juce::Logger::writeToLog("[ChopInspector] values: pitch=" + juce::String(values_.pitchSemis)
        + " gainDb=" + juce::String(values_.gainDb, 1) + " pan=" + juce::String(values_.pan, 2)
        + " rev=" + juce::String(values_.reverse ? 1 : 0) + " trig=" + juce::String(values_.trigger)
        + " atk=" + juce::String(values_.attackMs, 0) + " rel=" + juce::String(values_.releaseMs, 0));
    if (onValuesChanged) onValuesChanged(values_);
}

void ChopInspectorStrip::updateButtons()
{
    reverseBtn_.setColour(juce::TextButton::buttonColourId,
                          values_.reverse ? juce::Colour(0xff8a5a00) : juce::Colour(0xff2d2d3a));
    triggerBtn_.setButtonText(triggerName(values_.trigger));
    triggerBtn_.setColour(juce::TextButton::buttonColourId,
                          values_.trigger == 0 ? juce::Colour(0xff2d2d3a) : juce::Colour(0xff1a3d4a));
}

void ChopInspectorStrip::setControlsEnabled(bool on)
{
    pitch_.setEnabled(on);
    gain_.setEnabled(on);
    pan_.setEnabled(on);
    reverseBtn_.setEnabled(on);
    triggerBtn_.setEnabled(on && triggerOk_);
    attack_.setEnabled(on && envelopeOk_);
    release_.setEnabled(on && envelopeOk_);
    if (! envelopeOk_)
    {
        attack_.setBaseTooltip("Attack fade - needs the model's per-chop attack field (not stored yet)");
        release_.setBaseTooltip("Release fade - needs the model's per-chop release field (not stored yet)");
    }
}

void ChopInspectorStrip::setTarget(const juce::String& title, const Values& v, bool envelopeSupported, bool triggerSupported)
{
    const juce::ScopedValueSetter<bool> guard(updating_, true);
    hasTarget_  = true;
    envelopeOk_ = envelopeSupported;
    triggerOk_  = triggerSupported;
    values_     = v;
    title_.setText(title, juce::dontSendNotification);
    pitch_.setValue(v.pitchSemis, juce::dontSendNotification);
    gain_.setValue(v.gainDb, juce::dontSendNotification);
    pan_.setValue(v.pan, juce::dontSendNotification);
    attack_.setValue(v.attackMs, juce::dontSendNotification);
    release_.setValue(v.releaseMs, juce::dontSendNotification);
    updateButtons();
    setControlsEnabled(true);
    repaint();
}

void ChopInspectorStrip::clearTarget()
{
    const juce::ScopedValueSetter<bool> guard(updating_, true);
    hasTarget_ = false;
    dragging_  = false;
    values_    = {};
    title_.setText("No chop selected", juce::dontSendNotification);
    pitch_.setValue(0.0, juce::dontSendNotification);
    gain_.setValue(0.0, juce::dontSendNotification);
    pan_.setValue(0.0, juce::dontSendNotification);
    attack_.setValue(0.0, juce::dontSendNotification);
    release_.setValue(0.0, juce::dontSendNotification);
    updateButtons();
    setControlsEnabled(false);
    repaint();
}

void ChopInspectorStrip::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff14142a));
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(juce::Colour(hasTarget_ ? 0xff2f6d8a : 0xff2a2a4a));
    g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.0f);
}

void ChopInspectorStrip::resized()
{
    // Preferred widths; everything scales down together when the row is narrow
    // (850 px minimum window = 834 px row) and grows up to 1.4x when wide.
    struct Item { juce::Component* c; int w; };
    const Item items[] = { { &title_, 124 }, { &pitch_, 92 }, { &gain_, 100 }, { &pan_, 76 },
                           { &reverseBtn_, 44 }, { &triggerBtn_, 60 }, { &attack_, 88 }, { &release_, 92 } };
    constexpr int kGap = 4;
    auto area = getLocalBounds().reduced(3, 2);
    int pref = 0;
    for (auto& it : items) pref += it.w;
    const int gaps  = kGap * (int) (std::size(items) - 1);
    const float scale = juce::jlimit(0.5f, 1.4f, (float) (area.getWidth() - gaps) / (float) pref);
    for (auto& it : items)
    {
        it.c->setBounds(area.removeFromLeft(juce::roundToInt((float) it.w * scale)));
        area.removeFromLeft(kGap);
    }
}

} // namespace samplar
