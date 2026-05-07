---
name: integration-checklists
description: >
  Comprehensive integration checklists for VARIA VST development.
  Invoke when adding new engines, parameters, or UI components to ensure
  all integration points are covered and features reach the user interface.
allowed-tools:
  - Read
  - Grep
  - Glob
---

# Integration Checklists Skill

## Purpose
Prevent incomplete integrations by systematically verifying ALL connection points between code and UI.

## When to Use
- Adding new variation engine
- Adding new parameter/control
- Adding new UI component
- Modifying engine settings system
- Any change that affects user-facing functionality

## Primary Problem Solved
**"New engine doesn't appear in combobox after compilation"**

Root cause: Missing one or more integration points in the chain from code to UI.

## Checklist Templates

### 1. New Engine Integration

Use this checklist when adding any new variation engine (e.g., GranularEngine, ModalEngine, etc.)

#### Implementation Phase
- [ ] **IVariationEngine Interface**
  - File: `Source/Engine/[Name]Engine.h`
  - Inherits from `IVariationEngine`
  - Declares all virtual methods

- [ ] **Engine Implementation**
  - File: `Source/Engine/[Name]Engine.cpp`
  - Implements `generateVariation()`
  - Implements `setEffectSettings()`
  - Implements any other interface methods

#### Registration Phase
- [ ] **PluginProcessor Constructor**
  - File: `Source/PluginProcessor.cpp` (lines ~50-100)
  - Pattern: `engines_[N] = std::make_unique<[Name]Engine>();`
  - Engine index matches UI index

- [ ] **Engine Type Parameter**
  - File: `Source/PluginProcessor.cpp` (createParameterLayout)
  - Engine name added to StringArray
  - Pattern: `juce::StringArray {"Simple", "Creative", ..., "NewName"}`
  - Order matches engine indices

#### Settings Phase
- [ ] **Default Settings Mapping**
  - File: `Source/PluginProcessor.cpp` (getDefaultSettingsForEngine)
  - Case statement for engine index
  - Returns appropriate EffectSettingsMap
  - Pattern:
    ```cpp
    case ENGINE_INDEX:
        return {
            {"param1", 0.5f},
            {"param2", 1.0f}
        };
    ```

- [ ] **Settings Dialog (Option A: Dedicated Dialog)**
  - File: `Source/UI/[Name]EngineSettingsDialog.h`
  - Custom dialog for engine-specific settings
  - Implements populateEffects() or equivalent

- [ ] **Settings Dialog (Option B: Shared Dialog)**
  - File: `Source/UI/EngineSettingsDialog.h` (populateEffects)
  - Add sections for new engine parameters
  - Pattern: Check current engine index, add appropriate sliders/toggles

#### UI Phase
- [ ] **Combobox Population**
  - File: `Source/PluginEditor.cpp` (constructor, ~line 256)
  - Pattern: `engineComboBox.addItem("[Name]", ENGINE_INDEX);`
  - Index matches registration index
  - Name matches parameter StringArray

- [ ] **Combobox Handler**
  - File: `Source/PluginEditor.cpp` (engineComboBoxChanged or similar)
  - Handles selection of new engine
  - Updates processor state
  - (Usually automatic, verify existing handler covers all engines)

#### Persistence Phase
- [ ] **State Save**
  - File: `Source/PluginProcessor.cpp` (getStateInformation)
  - Engine settings saved to ValueTree
  - (Often automatic via APVTS, verify if custom state needed)

- [ ] **State Load**
  - File: `Source/PluginProcessor.cpp` (setStateInformation)
  - Engine settings restored from ValueTree
  - (Often automatic via APVTS, verify if custom state needed)

#### Testing Phase
- [ ] **UI Visibility Test**
  - Start VST: `start VariaVST.exe`
  - Open engine selection dropdown
  - Verify new engine name appears in list
  - Verify name is spelled correctly

- [ ] **Selection Test**
  - Select new engine from dropdown
  - Verify engine activates (check logs if available)
  - No crash or error

- [ ] **Settings Test**
  - Click settings button with new engine selected
  - Verify correct settings dialog appears
  - Verify settings are for the correct engine (not another engine's settings)

- [ ] **Functionality Test**
  - Load audio sample
  - Generate variations with new engine
  - Verify variations are created
  - Verify variations are different from original

- [ ] **Persistence Test**
  - Select new engine
  - Change some settings
  - Save project
  - Close and reopen VST
  - Load project
  - Verify engine still selected
  - Verify settings persisted

- [ ] **All Engines Test**
  - Test EVERY engine still works:
    - Simple
    - Creative
    - Impact
    - Dembow
    - SpectralClone
    - DDSPNoise
    - PowerSynth
    - DirectMorph
    - MultiBandEffects
    - [Your new engine]
  - Verify no regression

---

### 2. New Parameter Integration

Use this checklist when adding a new user-controllable parameter to any engine.

#### Parameter Definition Phase
- [ ] **APVTS Parameter Creation**
  - File: `Source/PluginProcessor.cpp` (createParameterLayout)
  - Pattern:
    ```cpp
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "paramID",           // Unique ID
        "Parameter Name",    // Display name
        juce::NormalisableRange<float>(min, max, step),
        defaultValue
    ));
    ```
  - ID is unique across all parameters
  - Range is appropriate for parameter type

- [ ] **Parameter Member Variable (if needed)**
  - File: `Source/PluginProcessor.h`
  - Pattern: `juce::AudioParameterFloat* paramName { nullptr };`
  - (Only if direct access needed, APVTS provides automatic access)

- [ ] **Parameter Initialization (if member variable)**
  - File: `Source/PluginProcessor.cpp` (constructor)
  - Pattern: `paramName = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("paramID"));`

#### UI Creation Phase
- [ ] **Widget Declaration**
  - File: `Source/PluginEditor.h`
  - Pattern: `std::unique_ptr<juce::Slider> paramSlider;`
  - Or: ComboBox, Button, ToggleButton, etc.

- [ ] **Widget Initialization**
  - File: `Source/PluginEditor.cpp` (constructor)
  - Pattern:
    ```cpp
    paramSlider = std::make_unique<juce::Slider>();
    paramSlider->setSliderStyle(juce::Slider::LinearHorizontal);
    addAndMakeVisible(*paramSlider);
    ```

- [ ] **Attachment Declaration**
  - File: `Source/PluginEditor.h`
  - Pattern: `std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> paramAttachment;`
  - Or: ButtonAttachment, ComboBoxAttachment, etc.

- [ ] **Attachment Initialization**
  - File: `Source/PluginEditor.cpp` (constructor)
  - Pattern:
    ```cpp
    paramAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts,
        "paramID",
        *paramSlider
    );
    ```
  - Binds widget to parameter

- [ ] **Layout/Positioning**
  - File: `Source/PluginEditor.cpp` (resized)
  - Widget positioned in UI layout
  - Pattern: `paramSlider->setBounds(x, y, width, height);`

- [ ] **Label (optional but recommended)**
  - File: `Source/PluginEditor.cpp`
  - Label created and positioned
  - Describes parameter clearly

#### Engine Consumption Phase
- [ ] **Engine Reads Parameter**
  - File: `Source/Engine/[Engine].cpp` (generateVariation)
  - Pattern: `float value = processor.getParameterValue("paramID");`
  - Or via APVTS: `float value = *apvts.getRawParameterValue("paramID");`
  - Parameter used in algorithm

- [ ] **Parameter Affects Output**
  - Verify parameter actually changes engine behavior
  - Different values produce different results

#### Testing Phase
- [ ] **Widget Visibility Test**
  - Start VST
  - Verify widget appears in UI
  - Verify label is readable

- [ ] **Interaction Test**
  - Move slider / change value
  - Verify widget responds smoothly
  - No crashes

- [ ] **Engine Receives Value Test**
  - Add logging in engine: `LOG("Param value: " << value);`
  - Change widget value
  - Generate variation
  - Check log shows correct value

- [ ] **Behavior Change Test**
  - Set parameter to minimum
  - Generate variation
  - Set parameter to maximum
  - Generate variation
  - Verify audible/visible difference

- [ ] **Persistence Test**
  - Change parameter value
  - Save project
  - Close VST
  - Reopen VST
  - Load project
  - Verify parameter value persisted

---

### 3. New UI Component Integration

Use this checklist when adding a new UI component (button, panel, visualizer, etc.)

#### Component Class Phase
- [ ] **Component Header**
  - File: `Source/UI/[Component].h`
  - Inherits from juce::Component
  - Declares all necessary methods

- [ ] **Component Implementation**
  - File: `Source/UI/[Component].cpp`
  - Implements paint(), resized()
  - Implements event handlers

#### Integration Phase
- [ ] **Parent Ownership**
  - File: `Source/PluginEditor.h` (or parent component)
  - Pattern: `std::unique_ptr<ComponentClass> componentName;`

- [ ] **Component Initialization**
  - File: `Source/PluginEditor.cpp` (constructor)
  - Pattern:
    ```cpp
    componentName = std::make_unique<ComponentClass>();
    addAndMakeVisible(*componentName);
    ```

- [ ] **Layout**
  - File: `Source/PluginEditor.cpp` (resized)
  - Component positioned
  - Pattern: `componentName->setBounds(x, y, width, height);`

#### Event Handling Phase
- [ ] **Event Handlers Declared**
  - File: `Source/UI/[Component].h`
  - Pattern: `void buttonClicked()`, `void mouseDown()`, etc.

- [ ] **Event Handlers Implemented**
  - File: `Source/UI/[Component].cpp`
  - Handlers update state correctly

- [ ] **State Communication**
  - Component updates processor state when needed
  - Component receives state updates and refreshes display

#### Testing Phase
- [ ] **Visibility Test**
  - Component appears in UI
  - Positioned correctly
  - Renders without errors

- [ ] **Interaction Test**
  - User interactions trigger handlers
  - Handlers execute correctly

- [ ] **State Test**
  - Component displays current state
  - Component updates when state changes externally

---

## Integration Failure Patterns (from mistakes.md)

### Pattern 1: Engine Invisible in UI
**Symptom:** Engine compiles but doesn't appear in dropdown

**Missing Integration Point:** Usually one of:
- Not added to engineType StringArray
- Not added to UI combobox via addItem()
- Index mismatch between registration and UI

**Prevention:** Verify both parameter creation AND UI population

### Pattern 2: Settings Show Wrong Engine
**Symptom:** Click settings button, see different engine's settings

**Missing Integration Point:**
- getDefaultSettingsForEngine() case statement missing or wrong index
- Settings dialog hardcoded to specific engine

**Prevention:** Verify settings mapping includes new engine with correct index

### Pattern 3: Parameters Don't Affect Output
**Symptom:** Move slider, no change in generated variations

**Missing Integration Point:**
- Engine doesn't read parameter value
- Parameter ID mismatch between APVTS and engine

**Prevention:** Add logging in engine to verify parameter received

### Pattern 4: Settings Don't Persist
**Symptom:** Change settings, save/reload, settings reset

**Missing Integration Point:**
- Parameter not in APVTS (custom state needs manual save/load)
- getStateInformation/setStateInformation missing custom state handling

**Prevention:** Test save/reload cycle explicitly

---

## How to Use This Skill

1. **Select Appropriate Checklist**
   - New Engine -> Use checklist #1
   - New Parameter -> Use checklist #2
   - New UI Component -> Use checklist #3

2. **Work Through Each Item Systematically**
   - Read the code at specified location
   - Verify pattern exists
   - Note file:line where found
   - Mark [X] if present, [ ] if missing

3. **Report Findings**
   - List all verified items with file:line
   - List all missing items with where they should be added
   - Block implementation if any items missing

4. **Create Test Plan**
   - Include all test phase items
   - Test ALL engines, not just new one
   - Test edge cases

5. **Verify Tests Pass**
   - Execute all tests
   - Document results
   - Only declare complete when ALL tests pass

---

## Success Metrics

- **100% Checklist Completion**: Every item verified before implementation
- **Zero Missing Integration Points**: All connections from code to UI verified
- **All Engines Tested**: No regression in existing engines
- **First-Time Success**: Feature works immediately after implementation

---

## Integration Supervisor Agent Role

This skill is primarily used by the **Integration Supervisor Agent**, who:
- Receives tasks from Orchestrator
- Selects appropriate checklist
- Verifies each item
- Reports complete status or blocks implementation
- Creates test plans based on checklist items
- Validates test results

The Integration Supervisor is the GATEKEEPER who prevents incomplete integrations.
