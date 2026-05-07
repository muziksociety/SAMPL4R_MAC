#include "NotificationButton.h"
#include <cmath>

namespace samplar {

NotificationButton::NotificationButton()
    : Button("Notification")
{
    juce::Logger::writeToLog("[NotificationButton] === constructor START ===");
    setTooltip("Version Info");
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setVisible(true);
    juce::Logger::writeToLog("[NotificationButton] === constructor END === visible: true, mode: UpToDate");
}

void NotificationButton::setMode(Mode mode)
{
    juce::Logger::writeToLog("[NotificationButton] === setMode START ===");
    juce::Logger::writeToLog("[NotificationButton]   mode: "
        + juce::String(mode == Mode::UpdateAvailable ? "UpdateAvailable"
                       : mode == Mode::TrialExpiring ? "TrialExpiring" : "UpToDate"));

    mode_ = mode;
    if (mode == Mode::UpToDate) {
        juce::Logger::writeToLog("[NotificationButton]   Decision: UpToDate mode, stopping timer, staying visible");
        stopTimer();
        setVisible(true);
    } else {
        juce::Logger::writeToLog("[NotificationButton]   Decision: Active mode, starting 30Hz timer and showing");
        setVisible(true);
        startTimerHz(30);
    }
    repaint();
    juce::Logger::writeToLog("[NotificationButton] === setMode END ===");
}

void NotificationButton::timerCallback()
{
    pulsePhase_ += pulseSpeed_;
    if (pulsePhase_ > juce::MathConstants<float>::twoPi)
        pulsePhase_ -= juce::MathConstants<float>::twoPi;
    repaint();
}

void NotificationButton::paintButton(juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced(2.0f);
    auto cx = r.getCentreX();
    auto cy = r.getCentreY();

    if (mode_ == Mode::UpToDate) {
        // Subtle steady circle - no pulse, dim until hovered
        auto baseColor = juce::Colour(0xff888899);
        auto color = down ? baseColor.brighter(0.3f)
                          : (over ? baseColor.brighter(0.2f) : baseColor);

        g.setColour(color.withAlpha(over ? 0.25f : 0.1f));
        g.fillEllipse(r);

        g.setColour(color.withAlpha(over ? 0.7f : 0.4f));
        g.drawEllipse(r, 1.5f);

        // "i" info icon
        g.setColour(color.withAlpha(over ? 0.9f : 0.5f));
        g.fillEllipse(cx - 1.5f, cy - r.getHeight() * 0.22f, 3.0f, 3.0f);
        g.fillRoundedRectangle(cx - 1.5f, cy - r.getHeight() * 0.05f,
                               3.0f, r.getHeight() * 0.28f, 1.0f);
        return;
    }

    float pulse = 0.75f + 0.25f * std::sin(pulsePhase_);

    juce::Colour baseColor;
    if (mode_ == Mode::UpdateAvailable) {
        baseColor = juce::Colour::fromString("ff00FF88"); // Green
    } else {
        baseColor = juce::Colour::fromString("ffFFAA00"); // Amber
    }

    auto color = down ? baseColor.brighter(0.2f)
                      : (over ? baseColor.brighter(0.15f) : baseColor);

    // Outer glow with pulse
    g.setColour(color.withAlpha(pulse * 0.3f));
    g.fillEllipse(r);

    // Circle outline (matches other title bar buttons style)
    g.setColour(color.withAlpha(pulse));
    g.drawEllipse(r, 1.5f);

    // Center icon
    g.setColour(color.withAlpha(down ? 1.0f : (over ? 0.95f : 0.85f)));

    if (mode_ == Mode::UpdateAvailable) {
        // Up arrow icon
        auto arrowSize = r.getWidth() * 0.25f;
        juce::Path arrow;
        arrow.addTriangle(cx, cy - arrowSize,
                          cx - arrowSize, cy + arrowSize * 0.3f,
                          cx + arrowSize, cy + arrowSize * 0.3f);
        g.fillPath(arrow);
        g.fillRoundedRectangle(cx - 1.5f, cy, 3.0f, arrowSize, 1.0f);
    } else {
        // Exclamation mark for trial warning
        g.fillRoundedRectangle(cx - 1.5f, cy - r.getHeight() * 0.2f,
                               3.0f, r.getHeight() * 0.22f, 1.0f);
        g.fillEllipse(cx - 2.0f, cy + r.getHeight() * 0.1f, 4.0f, 4.0f);
    }
}

} // namespace samplar
