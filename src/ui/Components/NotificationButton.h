#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace samplar {

class NotificationButton : public juce::Button, private juce::Timer {
public:
    enum class Mode {
        UpToDate,
        UpdateAvailable,
        TrialExpiring
    };

    NotificationButton();

    void setMode(Mode mode);
    Mode getMode() const { return mode_; }

    void paintButton(juce::Graphics& g, bool over, bool down) override;

private:
    void timerCallback() override;

    Mode mode_{Mode::UpToDate};
    float pulsePhase_{0.0f};
    static constexpr float pulseSpeed_ = 0.05f;
};

} // namespace samplar
