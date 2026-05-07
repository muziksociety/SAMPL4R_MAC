# SAMPLAR - Wave Editor UI Customization Guide

## 🎯 **Executive Summary**

SAMPLAR's wave editor UI is **highly customizable** through a sophisticated modular architecture that reuses SAMPLAR 4's JUCE-based wave editing components. You have **extensive control** over layout, effects, parameters, theming, and workflow without needing to modify core audio processing logic.

## 🏗️ **Architecture Overview**

### **Integration Pattern: SM4 Component Reuse**
```cpp
// SAMPLAR wraps SM4 JUCE components via composition
namespace samplar {
    class WaveEditorView : public juce::Component {
        std::unique_ptr<sm4juce::MainComponent> editor_;  // ← SM4 engine
        sm4juce::BufferSource bufferSource_;
        // SAMPLAR-specific navigation and callbacks
    };
}
```

### **Component Hierarchy**
```
WaveEditorView (SAMPLAR Host)
├── Title Label
├── Navigation Buttons (Back, Open)
├── MainComponent (SM4 Engine) ← **Main Customization Target**
│   ├── WaveformSection (Top 50%)
│   │   └── WaveformViewer ← Visual waveform display
│   ├── EffectsSection (Middle 12.5%)
│   │   ├── Effects Pipeline UI
│   │   └── Preset Management
│   └── ParameterPanel (Bottom 37.5%)
│       └── Dynamic Parameter Controls
└── Audio Integration (BufferSource + AudioSourcePlayer)
```

## 🎨 **UI Customization Possibilities**

### **1. Layout Modification**
**Target**: `MainComponent::resized()` method (lines 184-211)

```cpp
// Current layout percentages - EASILY CUSTOMIZABLE
auto waveformArea = area.removeFromTop(area.getHeight() * 0.5);    // 50%
auto effectsArea = area.removeFromTop(area.getHeight() * 0.25);    // 12.5%
// Parameters get remaining 37.5%
```

**Customization Options**:
- **Resize sections**: Change 50/12.5/37.5 ratios to your preference
- **Horizontal layouts**: Split screen left/right instead of top/bottom
- **Collapsible panels**: Add show/hide functionality for each section
- **Tabbed interface**: Convert to tabs instead of stacked layout
- **Resizable splitters**: Add drag handles between sections

### **2. Waveform Display Customization**
**Target**: `WaveformViewer` component (WaveformViewer.h/cpp)

**Extensive Customization Available**:
- **Visual styling**: Colors, gradients, zoom controls
- **Editing tools**: Trim handles, fade curves, selection regions
- **Overlay graphics**: Grid lines, time markers, frequency analysis
- **Interaction modes**: Different mouse/keyboard behaviors
- **Multi-channel display**: Stereo waveform visualization options

### **3. Effects Section Customization**
**Target**: `EffectsSection` component (Sections.h/cpp)

**Effects Pipeline Modifications**:
- **Add new effect types**: Extend beyond compressor/EQ/limiter/chorus
- **Drag-and-drop reordering**: Visual effects chain management
- **Effect thumbnails**: Visual effect representations
- **Bypass indicators**: Enhanced visual feedback
- **Parallel routing**: Split/merge signal paths

**Current Effects Available**:
```cpp
// Easily extendable effect types
"compressor" → DynamicEffectsProcessor::Compressor
"eq"         → DynamicEffectsProcessor::EQ  
"limiter"    → DynamicEffectsProcessor::Limiter
"chorus"     → DynamicEffectsProcessor::Chorus
// Add your own: "reverb", "delay", "distortion", etc.
```

### **4. Parameter Panel Customization**
**Target**: `ParameterPanel` component (ParameterFramework.h/cpp)

**Highly Flexible Parameter System**:
```cpp
// Parameter types easily customizable
enum class ParameterType {
    Knob,     // Rotary control
    Slider,   // Linear fader  
    // ADD: Button, Dropdown, XYPad, Envelope, etc.
};

// Range and styling fully customizable
ParameterSpec("threshold", "Threshold", ParameterType::Knob)
    .range(-60.0f, 0.0f)      // Min/max values
    .defaultVal(-12.0f)       // Default
    .unit("dB")               // Display unit
    // ADD: .color(), .size(), .style(), etc.
```

### **5. Theming and Styling**
**Integration Point**: JUCE LookAndFeel system

**Complete Visual Control**:
- **Colors**: Background, foreground, accent colors throughout
- **Fonts**: Typography, sizes, weights
- **Component styling**: Button shapes, slider styles, panel borders
- **Animation**: Smooth transitions, hover effects
- **Icons**: Custom graphics for controls and effects

## 🔧 **Implementation Strategies**

### **Strategy 1: Fork SM4 Components (Maximum Control)**
```cpp
// Copy SM4 components to SAMPLAR and modify directly
src/ui/WaveEditor/
├── CustomMainComponent.h     ← Forked from SM4
├── CustomWaveformViewer.h    ← Enhanced waveform
├── CustomEffectsSection.h    ← New effects pipeline  
└── CustomParameterPanel.h    ← Extended parameters
```

**Pros**: Complete freedom, no SM4 dependencies
**Cons**: Maintenance overhead, need to merge SM4 updates manually

### **Strategy 2: Subclass SM4 Components (Balanced Approach)**
```cpp
namespace samplar {
    class EnhancedMainComponent : public sm4juce::MainComponent {
        // Override specific methods while inheriting core functionality
        void resized() override;
        void addCustomEffects();
        void setupCustomLayout();
    };
}
```

**Pros**: Inherit SM4 updates, focused customization
**Cons**: Limited by SM4 virtual interface

### **Strategy 3: Configuration-Based Customization (Minimal Risk)**
```cpp
// Modify SM4 components through configuration/styling
class ConfigurableWaveEditor {
    struct LayoutConfig {
        float waveformRatio = 0.5f;
        float effectsRatio = 0.125f;  
        bool useHorizontalLayout = false;
        // ... other options
    };
    
    void applyConfiguration(const LayoutConfig& config);
};
```

**Pros**: Safe, maintainable, easy to revert
**Cons**: Limited to designed configuration points

## 🎛️ **Specific Customization Targets**

### **Navigation Customization**
**Target**: `WaveEditorView` constructor (lines 41-49)

```cpp
// Current: Simple back/open buttons
// CUSTOMIZE: Add file browser, recent files, save/load presets
addAndMakeVisible(backButton_);
addAndMakeVisible(openButton_);
// ADD: saveButton_, exportButton_, presetButton_, etc.
```

### **Round-Trip Workflow Customization**
**Target**: `onApplyEditsAndReturn` callback system

```cpp
// Current: ESC key triggers save-and-return
// CUSTOMIZE: Add confirmation dialogs, auto-save, undo/redo
std::function<void(const juce::AudioBuffer<float>&, const juce::String&, int)> onApplyEditsAndReturn;
```

### **Session Management Customization**
**Target**: `WaveEditorState` and session persistence

**Enhanced Session Features**:
- **Auto-save intervals**: Save state every N seconds
- **Session versioning**: Keep history of edits
- **Collaborative editing**: Multi-user session support
- **Cloud sync**: Save sessions to cloud storage

### **Audio Processing Customization**
**Target**: `DynamicEffectsProcessor` and `BufferSource`

**Advanced Audio Features**:
- **Real-time processing**: Live preview during parameter changes
- **High-quality algorithms**: Replace basic effects with professional versions
- **VST plugin support**: Load external VST effects
- **Multi-threading**: Parallel audio processing

## 🚀 **Recommended Customization Roadmap**

### **Phase 1: Visual Polish (Low Risk)**
1. **Custom color scheme**: Update LookAndFeel colors
2. **Layout tweaking**: Adjust section ratios in `resized()`
3. **Better fonts**: Enhance typography throughout UI
4. **Smooth animations**: Add transitions between states

### **Phase 2: Enhanced Controls (Medium Risk)**  
1. **Advanced parameters**: Add XY pads, envelope editors
2. **Waveform enhancements**: Better zoom, selection tools
3. **Effects expansion**: Add reverb, delay, distortion
4. **Preset system**: Enhanced save/load with categories

### **Phase 3: Workflow Innovation (Higher Risk)**
1. **Non-destructive editing**: Layer-based effect system
2. **AI assistance**: Smart parameter suggestions
3. **Collaborative features**: Real-time multi-user editing
4. **Advanced analysis**: Spectral analysis overlay

## 📁 **Key Files for Customization**

### **Primary Targets**
- `MainComponent.h` (lines 15-608): Main layout and orchestration
- `WaveformViewer.h/cpp`: Waveform display and editing
- `Sections.h/cpp`: Effects pipeline UI
- `ParameterFramework.h/cpp`: Dynamic parameter system
- `WaveEditorView.h/cpp`: SAMPLAR integration layer

### **Supporting Systems**  
- `BufferSource.h`: Audio playback and effects processing
- `State.h/cpp`: Session state management and persistence
- `PresetManager.h/cpp`: Effect preset system
- `DynamicEffectsProcessor.h/cpp`: Audio effect implementations

## ⚠️ **Important Considerations**

### **Maintain SM4 Compatibility**
- Test changes don't break SM4 Python integration
- Keep the `sm4juce` namespace interface stable
- Document any breaking changes for future updates

### **Performance Impact**
- Wave editor runs alongside intensive audio processing
- Keep UI updates efficient (avoid unnecessary repaints)
- Consider GPU acceleration for complex waveform rendering

### **User Experience**
- Maintain muscle memory for existing SM4 users
- Provide migration path for existing edit sessions
- Add help/tutorial system for new features

## 🎯 **Conclusion**

The SAMPLAR wave editor UI offers **exceptional customization potential** through its modular JUCE-based architecture. Whether you want simple visual tweaks or complete workflow overhauls, the component system provides clean extension points without requiring modifications to core audio processing logic.

**Recommended approach**: Start with low-risk visual customization, then gradually enhance functionality based on user feedback and specific workflow requirements.