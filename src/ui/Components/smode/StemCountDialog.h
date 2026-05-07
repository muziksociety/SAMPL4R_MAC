#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace samplar {

/**
 * StemCountDialog — modal dialog letting the user choose between
 * 4-stem or 6-stem separation before S-Mode processing begins.
 */
class StemCountDialog : public juce::Component
{
public:
    StemCountDialog();

    void paint(juce::Graphics& g) override;
    void resized() override;

    /** Callback when user picks a stem count: 4 or 6. */
    std::function<void(int stemCount)> onStemCountSelected;

    /** Show as a modal dialog centered in the given parent. */
    static void show(juce::Component* parent, std::function<void(int stemCount)> callback);

private:
    juce::Label titleLabel_ { {}, "How many stems?" };
    juce::TextButton fourStemBtn_ { "4 Stems" };
    juce::TextButton sixStemBtn_ { "6 Stems" };

    juce::Label fourDesc_ { {}, "Vocals / Bass / Drums / Other\nFaster separation" };
    juce::Label sixDesc_ { {}, "Vocals / Bass / Drums\nPiano / Guitar / Other\nMore detail" };
};

} // namespace samplar
