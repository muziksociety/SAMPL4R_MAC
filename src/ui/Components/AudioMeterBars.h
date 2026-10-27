#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace samplar {

// Single vertical audio level bar — drawn left of the SAMPLAR logo as a
// quick "is audio flowing" indicator (useful for RDP/cloud sessions with
// no local speakers). Reads peak from AudioMeterTap on a 30 Hz timer.
class AudioMeterBars : public juce::Component, private juce::Timer {
public:
    AudioMeterBars();
    ~AudioMeterBars() override;

    void paint(juce::Graphics& g) override;

private:
    void timerCallback() override;

    static constexpr int   kTimerHz = 30;
    static constexpr float kDecay   = 0.85f;  // per-tick envelope decay

    float level_ = 0.0f;  // smoothed 0..1
};

} // namespace samplar
