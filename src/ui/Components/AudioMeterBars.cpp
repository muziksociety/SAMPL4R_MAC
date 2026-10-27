#include "AudioMeterBars.h"
#include "../../audio/AudioMeterTap.h"

namespace samplar {

AudioMeterBars::AudioMeterBars() {
    setOpaque(false);
    setInterceptsMouseClicks(false, false);
    startTimerHz(kTimerHz);
}

AudioMeterBars::~AudioMeterBars() {
    stopTimer();
}

void AudioMeterBars::timerCallback() {
    auto& tap = AudioMeterTap::instance();
    const float peak = juce::jmax(tap.consumeLeft(), tap.consumeRight());
    level_ = juce::jmax(juce::jlimit(0.0f, 1.0f, peak), level_ * kDecay);
    repaint();
}

void AudioMeterBars::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat().reduced(2.0f);
    if (bounds.isEmpty()) return;

    const juce::Colour cyan{0xff00e7ff};

    // Dim full-height background
    g.setColour(cyan.withAlpha(0.18f));
    g.fillRect(bounds);

    // Bright active portion from the bottom up
    const float h = level_ * bounds.getHeight();
    g.setColour(cyan);
    g.fillRect(bounds.withTop(bounds.getBottom() - h));
}

} // namespace samplar
