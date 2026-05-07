#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

namespace samplar {

class EngineColorManager {
public:
    enum class EngineType {
        Simple,
        Creative,
        Dynamic,
        Impact,          // NEW: BIG REVERBS, BIG DELAYS, MASSIVE SPATIAL
        DirectMorphing,
        SpectralClone,
        PowerSynth,
        DDSPNoise
    };

    EngineColorManager()
    {
        engineColors_[EngineType::Simple]         = juce::Colour::fromString("ff00FF88");
        engineColors_[EngineType::Creative]       = juce::Colour::fromString("ffff69b4");
        engineColors_[EngineType::Dynamic]        = juce::Colour::fromString("ffffd700");
        engineColors_[EngineType::Impact]         = juce::Colour::fromString("ff00FFFF");  // Cyan - for BIG REVERBS
        engineColors_[EngineType::DirectMorphing] = juce::Colour::fromString("ff9370db");
        engineColors_[EngineType::SpectralClone]  = juce::Colour::fromString("ff32cd32");
        engineColors_[EngineType::PowerSynth]     = juce::Colour::fromString("ffff4500");
        engineColors_[EngineType::DDSPNoise]      = juce::Colour::fromString("ff8a2be2");

        // CRITICAL FIX: Default to Dynamic (FastSimple SM4 port) instead of Simple
        defaultEngine_ = EngineType::Dynamic;
    }

    void assignEngineToSample(int sampleIndex, EngineType engine)
    {
        sampleEngineAssignments_[sampleIndex] = engine;
    }

    void setDefaultEngine(EngineType engine)
    {
        defaultEngine_ = engine;
        juce::Logger::writeToLog("[TARGET] [EngineColorManager] Default engine set to: " + juce::String((int)engine));
    }

    EngineType getDefaultEngine() const
    {
        return defaultEngine_;
    }

    EngineType getAssignedEngine(int sampleIndex)
    {
        auto it = sampleEngineAssignments_.find(sampleIndex);
        if (it != sampleEngineAssignments_.end()) return it->second;

        // CRITICAL FIX: Use default engine instead of random modulo assignment
        // This ensures first V-button click uses the user's preferred default engine
        sampleEngineAssignments_[sampleIndex] = defaultEngine_;
        juce::Logger::writeToLog("[TARGET] [EngineColorManager] Sample " + juce::String(sampleIndex) + " assigned default engine: " + juce::String((int)defaultEngine_));
        return defaultEngine_;
    }

    juce::Colour getEngineColor(EngineType engine) const
    {
        auto it = engineColors_.find(engine);
        if (it != engineColors_.end()) return it->second;
        return juce::Colours::cyan;
    }

    juce::Colour getSampleColor(int sampleIndex)
    {
        return getEngineColor(getAssignedEngine(sampleIndex));
    }

private:
    std::map<EngineType, juce::Colour> engineColors_;
    std::map<int, EngineType> sampleEngineAssignments_;
    EngineType defaultEngine_ { EngineType::Dynamic };  // CRITICAL FIX: Default to Dynamic (FastSimple SM4 port)
};

} // namespace samplar

