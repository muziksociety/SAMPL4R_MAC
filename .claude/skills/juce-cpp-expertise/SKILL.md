---
name: juce-cpp-expertise
description: >
  Expert knowledge of JUCE framework and C++ for audio plugin development.
  Invoke when implementing VST features, understanding JUCE patterns, or
  debugging C++/JUCE code.
allowed-tools:
  - Read
  - Grep
  - Glob
---

# JUCE/C++ Expertise Skill

## Purpose
Provide deep knowledge of JUCE framework patterns and C++ best practices for VST plugin development.

## JUCE Framework Architecture

### Core JUCE Modules Used in VARIA VST

**juce_audio_processors**
- `AudioProcessor`: Main plugin class (VariaVSTAudioProcessor)
- `AudioProcessorEditor`: UI class (VariaVSTAudioProcessor)
- `AudioProcessorValueTreeState` (APVTS): Parameter management
- Pattern: All parameters must be in APVTS for DAW automation and persistence

**juce_gui_basics**
- `Component`: Base class for all UI elements
- `Slider`, `ComboBox`, `Button`: Standard controls
- `LookAndFeel`: Custom styling
- Pattern: Components must be `addAndMakeVisible()` in parent

**juce_audio_formats**
- `AudioFormatManager`: Handles file loading (WAV, MP3, etc.)
- `AudioFormatReader`: Reads audio files
- Pattern: Always register formats before use

**juce_dsp**
- FFT, filters, convolution (if used)
- Pattern: Prepare with sampleRate and samplesPerBlock

## Common JUCE Patterns in VARIA VST

### Pattern 1: AudioProcessorValueTreeState (APVTS)

**Parameter Creation:**
```cpp
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Float parameter
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "paramID",                    // Unique identifier (used in code)
        "Display Name",               // Shown in DAW
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),  // min, max, step
        0.5f                          // default value
    ));

    // Choice parameter (ComboBox)
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "engineType",
        "Engine Type",
        juce::StringArray {"Simple", "Creative", "Impact", "Dembow"},
        0  // default index
    ));

    return layout;
}

// In constructor:
: apvts(*this, nullptr, "Parameters", createParameterLayout())
```

**Accessing Parameter Values:**
```cpp
// Method 1: Raw parameter value (most common in engines)
float value = *apvts.getRawParameterValue("paramID");

// Method 2: Via AudioParameterFloat pointer
if (auto* param = apvts.getParameter("paramID"))
    float value = param->getValue();  // Returns 0-1 normalized

// Method 3: Direct cast (if stored as member variable)
float value = mutationRate->get();
```

**CRITICAL: APVTS automatically handles:**
- DAW automation
- State save/load (via getStateInformation/setStateInformation)
- Thread-safe parameter access
- Undo/redo

### Pattern 2: UI Parameter Binding

**Slider Attachment:**
```cpp
// In PluginEditor.h
class PluginEditor : public juce::AudioProcessorEditor
{
    std::unique_ptr<juce::Slider> mutationSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mutationAttachment;
};

// In PluginEditor.cpp constructor
mutationSlider = std::make_unique<juce::Slider>();
mutationSlider->setSliderStyle(juce::Slider::LinearHorizontal);
addAndMakeVisible(*mutationSlider);

// Attachment binds slider to APVTS parameter (automatic sync)
mutationAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
    audioProcessor.apvts,
    "mutationRate",
    *mutationSlider
);
```

**ComboBox Attachment:**
```cpp
// In PluginEditor.h
std::unique_ptr<juce::ComboBox> engineComboBox;
std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> engineAttachment;

// In PluginEditor.cpp
engineComboBox = std::make_unique<juce::ComboBox>();
engineComboBox->addItem("Simple", 1);    // Item ID starts at 1
engineComboBox->addItem("Creative", 2);
addAndMakeVisible(*engineComboBox);

engineAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
    audioProcessor.apvts,
    "engineType",
    *engineComboBox
);
```

**CRITICAL: Attachment Pattern Benefits:**
- Automatic bidirectional sync (UI ↔ parameter)
- DAW automation updates UI automatically
- No manual listener code needed
- Thread-safe

### Pattern 3: Engine Interface (VARIA-Specific)

**IVariationEngine Interface:**
```cpp
class IVariationEngine {
public:
    virtual ~IVariationEngine() = default;

    // Generate variation from source audio
    virtual juce::AudioBuffer<float> generateVariation(
        const juce::AudioBuffer<float>& source,
        double sampleRate,
        int variationIndex
    ) = 0;

    // Apply effect settings to engine
    virtual void setEffectSettings(const samplar::EffectSettingsMap& settings) = 0;
};
```

**Engine Registration Pattern:**
```cpp
// In PluginProcessor constructor
engines_.resize(NUM_ENGINES);
engines_[0] = std::make_unique<SimpleVariationEngine>();
engines_[1] = std::make_unique<CreativeVariationEngine>();
engines_[2] = std::make_unique<ImpactVariationEngine>();
engines_[3] = std::make_unique<DembowEngine>();
// ... etc

// Usage in generateVariations()
int engineIndex = engineType->getIndex();
if (engineIndex >= 0 && engineIndex < engines_.size() && engines_[engineIndex]) {
    variation = engines_[engineIndex]->generateVariation(sourceAudio_, sourceSampleRate_, i);
}
```

### Pattern 4: State Save/Load

**APVTS Automatic State:**
```cpp
void getStateInformation(juce::MemoryBlock& destData) override
{
    // APVTS state (parameters)
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);

    // Custom state (if needed)
    // xml->setAttribute("customValue", someValue);
}

void setStateInformation(const void* data, int sizeInBytes) override
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState != nullptr) {
        if (xmlState->hasTagName(apvts.state.getType())) {
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
        }

        // Restore custom state (if needed)
        // someValue = xmlState->getIntAttribute("customValue");
    }
}
```

### Pattern 5: Audio Buffer Handling

**Creating Buffers:**
```cpp
juce::AudioBuffer<float> buffer(numChannels, numSamples);
buffer.clear();  // Zero all samples

// Copy from another buffer
buffer.makeCopyOf(sourceBuffer);

// Get write pointers
float* leftChannel = buffer.getWritePointer(0);
float* rightChannel = buffer.getWritePointer(1);

// Get read pointers (const)
const float* leftChannel = buffer.getReadPointer(0);
```

**Safe Buffer Operations:**
```cpp
// Always check channel count
if (buffer.getNumChannels() > 0) {
    // Process channel 0
}

// Always check sample count
if (buffer.getNumSamples() > 0) {
    // Process samples
}

// NEVER assume stereo - could be mono or multichannel
for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
    float* channelData = buffer.getWritePointer(ch);
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        channelData[i] *= gain;
    }
}
```

## VARIA VST Specific Patterns

### Engine Settings Map Pattern

```cpp
// EffectSettingsMap is defined in sm5 namespace
namespace samplar {
    using EffectSettingsMap = std::map<std::string, float>;
}

// Default settings for each engine
samplar::EffectSettingsMap getDefaultSettingsForEngine(int engineIndex) {
    switch (engineIndex) {
        case 0:  // Simple Engine
            return {
                {"pitchShift", 0.0f},
                {"timeStretch", 1.0f},
                {"filtering", 0.5f}
            };
        case 1:  // Creative Engine
            return {
                {"distortion", 0.3f},
                {"reverb", 0.4f},
                {"delay", 0.2f}
            };
        // ... etc
    }
}

// Apply to engine
engines_[engineIndex]->setEffectSettings(settings);
```

### Variation Generation Pattern

```cpp
void generateVariations(int count = 20) {
    variations_.clear();
    variations_.reserve(count);

    int engineIndex = engineType->getIndex();

    for (int i = 0; i < count; ++i) {
        if (engines_[engineIndex]) {
            auto variation = engines_[engineIndex]->generateVariation(
                sourceAudio_,
                sourceSampleRate_,
                i
            );
            variations_.push_back(std::move(variation));
        }

        generationProgress_ = static_cast<float>(i + 1) / count;
    }

    isGenerating_ = false;
}
```

## C++ Best Practices for JUCE

### Memory Management

**Use std::unique_ptr for owned objects:**
```cpp
std::unique_ptr<juce::Slider> slider;
slider = std::make_unique<juce::Slider>();

// NOT:
juce::Slider* slider = new juce::Slider();  // DON'T DO THIS
```

**Use std::vector for collections:**
```cpp
std::vector<std::unique_ptr<IVariationEngine>> engines_;
std::vector<juce::AudioBuffer<float>> variations_;
```

**JUCE handles deletion automatically for:**
- Components added with `addAndMakeVisible()`
- Child components (deleted when parent deleted)

### Thread Safety

**Audio Thread vs UI Thread:**
```cpp
// Audio thread (processBlock)
void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    // NO UI OPERATIONS HERE
    // NO FILE I/O HERE
    // NO ALLOCATIONS IF POSSIBLE
    // Read parameters from APVTS (thread-safe)
    float value = *apvts.getRawParameterValue("param");
}

// UI thread (timer callbacks, button clicks)
void buttonClicked() {
    // Safe to update UI
    // Safe to start background tasks
    // Change parameters via APVTS (thread-safe)
}

// Background thread (for heavy processing)
void generateVariationsOnBackgroundThread() {
    // Use juce::Thread or std::thread
    // Notify UI when done via AsyncUpdater or MessageManager::callAsync
}
```

**AsyncUpdater Pattern for Thread-Safe UI Updates:**
```cpp
class MyProcessor : public juce::AudioProcessor, private juce::AsyncUpdater {
    void handleAsyncUpdate() override {
        // Called on message thread (UI thread)
        // Safe to update UI here
    }

    void someBackgroundTask() {
        // Do heavy work
        triggerAsyncUpdate();  // Request UI update
    }
};
```

### String Handling

**Use juce::String:**
```cpp
juce::String name = "Engine";
name << " " << index;  // Concatenation

// NOT std::string (unless interfacing with non-JUCE code)
```

### File Handling

**Use juce::File:**
```cpp
juce::File audioFile("C:\\path\\to\\file.wav");

if (audioFile.existsAsFile()) {
    auto* reader = formatManager.createReaderFor(audioFile);
    if (reader != nullptr) {
        // Read audio
        delete reader;  // OR use std::unique_ptr<AudioFormatReader>
    }
}
```

## Common JUCE Gotchas in VARIA VST

### Gotcha 1: ComboBox Item IDs Start at 1

```cpp
// WRONG:
engineComboBox->addItem("Simple", 0);  // ID 0 is invalid!

// CORRECT:
engineComboBox->addItem("Simple", 1);
engineComboBox->addItem("Creative", 2);

// If using with APVTS index (0-based), need conversion:
int comboBoxID = apvtsIndex + 1;
```

### Gotcha 2: addAndMakeVisible Required

```cpp
// WRONG:
slider = std::make_unique<juce::Slider>();
// Slider won't appear!

// CORRECT:
slider = std::make_unique<juce::Slider>();
addAndMakeVisible(*slider);  // Now visible
```

### Gotcha 3: Parameter IDs Must Be Unique

```cpp
// WRONG:
layout.add(std::make_unique<juce::AudioParameterFloat>("gain", "Gain", ...));
layout.add(std::make_unique<juce::AudioParameterFloat>("gain", "Output Gain", ...));
// Second parameter will fail!

// CORRECT:
layout.add(std::make_unique<juce::AudioParameterFloat>("inputGain", "Gain", ...));
layout.add(std::make_unique<juce::AudioParameterFloat>("outputGain", "Output Gain", ...));
```

### Gotcha 4: Audio Buffer Channel Count

```cpp
// WRONG:
float* left = buffer.getWritePointer(0);
float* right = buffer.getWritePointer(1);  // Could crash if mono!

// CORRECT:
if (buffer.getNumChannels() == 1) {
    // Mono processing
} else if (buffer.getNumChannels() >= 2) {
    // Stereo processing
}
```

### Gotcha 5: ValueTree vs XML State

```cpp
// MODERN (JUCE 6+):
auto state = apvts.copyState();  // Returns ValueTree
std::unique_ptr<juce::XmlElement> xml(state.createXml());

// When restoring:
if (xmlState->hasTagName(apvts.state.getType())) {  // Check tag name
    apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}
```

## VARIA VST File Structure

```
Source/
├── PluginProcessor.h/cpp      # Main AudioProcessor
├── PluginEditor.h/cpp         # Main UI
├── Engine/
│   ├── IVariationEngine.h     # Engine interface
│   ├── SimpleVariationEngine.h/cpp
│   ├── CreativeVariationEngine.h/cpp
│   ├── ImpactVariationEngine.h/cpp
│   ├── DembowEngine.h/cpp
│   └── [NewEngine].h/cpp      # New engines go here
├── UI/
│   ├── *EngineSettingsDialog.h    # Engine-specific settings
│   └── [Components].h             # Reusable UI components
└── Util/
    └── Log.h                  # Logging utilities
```

## JUCE CMake Integration

**Typical CMakeLists.txt pattern:**
```cmake
juce_add_plugin(VariaVST
    PLUGIN_NAME "VARIA"
    FORMATS VST3 Standalone
    PRODUCT_NAME "VARIA VST"
)

target_sources(VariaVST PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Engine/SimpleVariationEngine.cpp
    # Add new sources here
)

target_link_libraries(VariaVST PRIVATE
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_gui_basics
)
```

**When adding new files:**
1. Add to `target_sources`
2. Run CMake configure
3. Build: `cmake --build build --config Release`

## Debugging JUCE Applications

**Common debugging techniques:**

```cpp
// Use DBG macro for debug output
DBG("Engine index: " + juce::String(engineIndex));

// Use jassert for debug assertions
jassert(engineIndex >= 0 && engineIndex < engines_.size());

// Use Logger for file logging
juce::Logger::writeToLog("Processing variation " + juce::String(i));

// VARIA uses custom LOG macro (from Util/Log.h)
LOG("SimpleEngine: generateVariation called with index " << variationIndex);
```

## Key Principles

1. **APVTS for all parameters** - Automatic persistence and automation
2. **Attachments for UI binding** - No manual listeners
3. **std::unique_ptr for ownership** - No manual delete
4. **Thread safety** - Know which thread you're on
5. **Check buffer dimensions** - Never assume stereo
6. **ASCII only** - No Unicode in VARIA VST
7. **Engine interface** - All engines implement IVariationEngine
8. **Settings map** - Engine parameters via std::map<string, float>

## When to Use This Skill

- Implementing new engine features
- Adding UI parameters
- Debugging parameter binding issues
- Understanding JUCE patterns in existing code
- Designing new components
- Optimizing audio processing
- Fixing thread safety issues
