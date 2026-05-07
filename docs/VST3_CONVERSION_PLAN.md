# SAMPLAR — VST3 Conversion Architecture Plan

**Date:** 2026-03-14
**Version:** 1.1 (Post-Audit)
**Status:** AUDITED — All critical findings incorporated

---

## 1. Executive Summary

Convert SAMPLAR from a standalone-only JUCE application to a **hybrid Standalone + VST3 plugin** using a single `AudioProcessor` codebase. The standalone retains ALL current functionality (audio device selection, file browsing, sequencer, processing pipeline) while the VST3 format adds DAW integration, MIDI input, audio input, and multi-output routing.

**Key Principle:** Zero functionality loss. The standalone must work exactly as it does today, plus gain MIDI input capabilities. The VST3 is an additive layer on top.

---

## 2. Current Architecture (What We Have)

### 2.1 Build System
```
CMakeLists.txt:
  juce_add_gui_app(SM5App)          ? Standalone only
  Modules: gui_basics, gui_extra, audio_devices, audio_utils,
           audio_basics, dsp, cryptography, opengl, core, events
  Missing: juce_audio_processors    ? Required for VST3
```

### 2.2 Application Boot Sequence
```
App : JUCEApplication
  ? initialise()
    ? ServiceRegistry created
    ? AudioDeviceService registered (owns AudioDeviceManager)
    ? All services registered & initialized
    ? MainWindow created (DocumentWindow)
      ? MainContentComponent created
        ? SampleBrowserPage, WavePreviewPage, etc.
        ? AudioSourcePlayer registered with AudioDeviceManager
```

### 2.3 Audio Chain (Current)
```
[Sample Files / Cache]
       ?
[AudioFormatReaderSource] or [BufferSource (cached)]
       ?
[SoundTouchSource] (pitch/speed)
       ?
[ProcessingAudioSource] (gate/comp/limiter)
       ?
[AudioTransportSource] (transport control)
       ?
[MixerAudioSource] (preview + sequencer)
  +-- Preview chain (above)
  +-- SequencerAudioSource (32 voices, audio-thread timing)
       ?
[AudioSourcePlayer] (implements AudioIODeviceCallback)
       ?
[AudioDeviceManager] ? [Hardware Audio Device]
```

### 2.4 Services (19 Total)
| Service | VST3 Impact |
|---------|------------|
| AudioDeviceService | REPLACED by host in VST3, kept for standalone |
| GeneratorService | No change |
| VariationService | No change |
| DrumSliceService | No change |
| FullSongService | No change |
| ProcessingCoordinatorService | No change |
| PreSliceProcessingService | No change |
| DrumLibraryService | No change |
| ClassificationService | No change |
| SampleAnalysisService | No change |
| BeatAnalysisService | No change |
| DrumSeparatorService | No change |
| LicenseSystemService | No change (validates in both modes) |
| ChatService | No change |
| UpdateCheckService | No change |
| SecureSessionManager | No change |
| FeatureManager | No change |
| OAuthBrowserAuthenticator | No change |
| **NEW: MidiMappingService** | **NEW** |

**17 of 19 services are completely untouched.** AudioDeviceService becomes conditional (standalone-only). One new service added.

---

## 3. Target Architecture (What We Build)

### 3.1 Build System Change
```cmake
# BEFORE:
juce_add_gui_app(SM5App ...)

# AFTER:
juce_add_plugin(SM5Plugin
    COMPANY_NAME "Muzik Society"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    PLUGIN_MANUFACTURER_CODE MzSo
    PLUGIN_CODE SM5p
    FORMATS Standalone VST3        # Both from one codebase
    VST3_CATEGORIES "Instrument" "Sampler" "Drum"
    PRODUCT_NAME "SAMPLAR"
)

# ADD module:
target_link_libraries(SM5Plugin PRIVATE juce::juce_audio_processors)
```

This single `juce_add_plugin()` call generates:
- `SAMPLAR.exe` (Standalone — identical to current app)
- `SAMPLAR.vst3` (VST3 plugin for DAWs)

### 3.2 New Core Class: SamplarAudioProcessor

```cpp
class SamplarAudioProcessor : public juce::AudioProcessor
{
public:
    SamplarAudioProcessor();
    ~SamplarAudioProcessor() override;

    // === AudioProcessor Interface ===
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // === Plugin Info ===
    const juce::String getName() const override { return "SAMPLAR"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // === Programs (presets) ===
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    // === State Save/Restore ===
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // === Editor ===
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    // === Bus Layout ===
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    // === SAMPLAR Internal Access ===
    samplar::ServiceRegistry& getServiceRegistry() { return *serviceRegistry_; }
    samplar::EventBus& getEventBus() { return eventBus_; }
    samplar::SequencerAudioSource& getSequencer() { return *sequencerSource_; }
    samplar::SamplePreviewCache& getSampleCache() { return *sampleCache_; }

    // === Runtime Mode Detection ===
    bool isRunningAsPlugin() const;
    bool isRunningAsStandalone() const;

private:
    // Core framework (same as current App)
    samplar::EventBus eventBus_;
    std::unique_ptr<samplar::ServiceRegistry> serviceRegistry_;

    // Audio sources (existing, unchanged)
    std::unique_ptr<samplar::SequencerAudioSource> sequencerSource_;
    std::unique_ptr<samplar::SamplePreviewCache> sampleCache_;
    std::unique_ptr<samplar::ProcessingAudioSource> processingSource_;

    // MIDI mapping (NEW)
    std::unique_ptr<samplar::MidiMappingService> midiMapping_;

    // Preview mixer for sample browser playback
    juce::MixerAudioSource masterMixer_;

    // Parameters
    juce::AudioProcessorValueTreeState apvts_;
};
```

### 3.3 The processBlock() Bridge

This is the critical adapter — wrapping the existing AudioSource chain inside processBlock():

```cpp
void SamplarAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                      juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // 1. Process MIDI input ? trigger samples
    for (const auto metadata : midiMessages)
    {
        auto msg = metadata.getMessage();
        if (msg.isNoteOn())
        {
            midiMapping_->handleNoteOn(msg.getChannel(),
                                        msg.getNoteNumber(),
                                        msg.getVelocity());
        }
        else if (msg.isNoteOff())
        {
            midiMapping_->handleNoteOff(msg.getChannel(),
                                         msg.getNoteNumber());
        }
    }

    // 2. Pull audio from existing AudioSource chain
    juce::AudioSourceChannelInfo info(&buffer, 0, buffer.getNumSamples());
    masterMixer_.getNextAudioBlock(info);
}
```

**Why this works:** JUCE's `AudioSource::getNextAudioBlock()` is a pull-based model. We simply call it from within the push-based `processBlock()`. The entire existing chain (sequencer, preview, effects) produces audio exactly as before.

### 3.4 Audio Bus Configuration

```cpp
SamplarAudioProcessor::SamplarAudioProcessor()
    : AudioProcessor(BusesProperties()
        // No audio input by default (can be enabled for live sampling)
        .withInput("Input", juce::AudioChannelSet::stereo(), false)
        // Main stereo output (always active)
        .withOutput("Main Out", juce::AudioChannelSet::stereo(), true)
        // Optional individual drum outputs (host activates as needed)
        .withOutput("Kick", juce::AudioChannelSet::stereo(), false)
        .withOutput("Snare", juce::AudioChannelSet::stereo(), false)
        .withOutput("HiHat", juce::AudioChannelSet::stereo(), false)
        .withOutput("Toms", juce::AudioChannelSet::stereo(), false)
        .withOutput("Perc", juce::AudioChannelSet::stereo(), false)
        .withOutput("FX", juce::AudioChannelSet::stereo(), false)
        .withOutput("Aux 1", juce::AudioChannelSet::stereo(), false)
        .withOutput("Aux 2", juce::AudioChannelSet::stereo(), false))
{
    // ...
}
```

Users get a main stereo out by default. DAW users can activate individual outs for mixing.

### 3.5 UI: AudioProcessorEditor

```cpp
class SM5PluginEditor : public juce::AudioProcessorEditor
{
public:
    SM5PluginEditor(SamplarAudioProcessor& p)
        : AudioProcessorEditor(p), processor_(p)
    {
        // Wrap the EXISTING MainContentComponent
        // All current UI pages work unchanged
        mainContent_ = std::make_unique<samplar::MainContentComponent>(
            processor_.getServiceRegistry(),
            processor_.getEventBus()
        );
        addAndMakeVisible(mainContent_.get());

        // Set plugin window size
        setSize(1200, 800);
        setResizable(true, true);
        setResizeLimits(900, 600, 1920, 1200);
    }

    void resized() override
    {
        mainContent_->setBounds(getLocalBounds());
    }

private:
    SamplarAudioProcessor& processor_;
    std::unique_ptr<samplar::MainContentComponent> mainContent_;
};
```

**Key insight:** The entire existing UI (SampleBrowserPage, WavePreviewPage, StepSequencer, etc.) becomes a child component inside the editor. Zero UI rewrite needed.

---

## 4. MIDI Implementation

### 4.1 Sample Browser MIDI Mapping

Map visible sample browser tiles to MIDI notes:

```
Page of 16 tiles:
  Tile 0  ? MIDI Note 36 (C1 - Kick)
  Tile 1  ? MIDI Note 37 (C#1)
  Tile 2  ? MIDI Note 38 (D1 - Snare)
  ...
  Tile 15 ? MIDI Note 51 (D#2)

When user navigates to a new page, mapping shifts to new tiles.
```

```cpp
class MidiMappingService : public samplar::IService
{
public:
    void handleNoteOn(int channel, int noteNumber, float velocity);
    void handleNoteOff(int channel, int noteNumber);

    // Map sample file to MIDI note
    void mapSampleToNote(int noteNumber, const juce::File& sampleFile);

    // Auto-map visible browser page
    void autoMapBrowserPage(const std::vector<juce::File>& visibleSamples);

    // Sequencer channel mapping (fixed)
    // Row 0 ? Note 36, Row 1 ? Note 38, etc.
    void mapSequencerChannels(int numChannels);

private:
    struct MidiSlot {
        juce::File sampleFile;
        std::shared_ptr<const juce::AudioBuffer<float>> cachedBuffer;
        float currentVelocity { 0.0f };
        bool isPlaying { false };
    };

    std::array<MidiSlot, 128> midiMap_;  // Full MIDI range
    samplar::SamplePreviewCache* cache_ { nullptr };
};
```

### 4.2 Standalone MIDI Input

The standalone already gets MIDI for free via `StandaloneFilterWindow`:

```cpp
// JUCE's StandalonePluginHolder automatically:
// 1. Creates AudioDeviceManager with MIDI input enabled
// 2. Routes MIDI input through processBlock's MidiBuffer
// 3. Provides MIDI device selection in Options menu

// We just need these plugin properties:
NEEDS_MIDI_INPUT TRUE    // ? Already set in juce_add_plugin()
```

No additional MIDI code needed for standalone. The same `processBlock()` MIDI handling works in both modes.

---

## 5. New Feature: Sample Mode

### 5.1 Concept

Load a full song, grid it to tempo, and trigger grid slices via MIDI like a classic sampler (MPC/SP-404 style).

```
WORKFLOW:
1. Load song ? Auto-detect tempo ? Display beat grid
2. User adjusts grid (existing Step 2 functionality!)
3. "Sample Mode" button ? Switches to pad layout
4. Each grid slice becomes a triggerable pad
5. MIDI notes trigger slices in real-time
6. Slices can be:
   - Pitched up/down (SoundTouch — already have this)
   - Time-stretched (SoundTouch — already have this)
   - Reversed
   - Looped
   - Assigned to sequencer channels
```

### 5.2 Integration with Existing Code

| Existing Component | Sample Mode Usage |
|---|---|
| WavePreviewPage Step 2 | Grid the song (already works) |
| BeatAnalysisService | Auto-detect tempo (already works) |
| SoundTouchSource | Pitch/speed per slice (already works) |
| SequencerAudioSource | Sequence the slices (extend with file-per-step) |
| SamplePreviewCache | Cache all slices for instant triggering |
| MidiMappingService | Map slices to MIDI notes (new) |

**This feature is 70% already built.** The remaining work is a new UI page and slice-to-MIDI mapping.

---

## 6. Implementation Phases

### Phase 0: Preparation (Non-Breaking) — EXPANDED BY AUDIT
**Goal:** Decouple audio from UI without changing any behavior. **This is the largest phase.**

| Task | Risk | Audit Ref | Details |
|------|------|-----------|---------|
| **Audio ownership refactor** | HIGH | A1 | Extract `masterMixer_`, `sequencerEngine_`, `previewMixer_`, `transport_`, `player_` from SampleBrowserPage into a new `AudioEngine` class owned by the processor. SampleBrowserPage gets references instead. Same for WavePreviewPage and EngineLabPage audio chains. **This is the single largest task.** |
| **JUCEApplication accessor replacement** | HIGH | A2 | Replace all ~50 `JUCEApplication::getInstance()` calls across 12 files with `samplar::ServiceLocator::getRegistry()` or pass processor reference through constructor chain. |
| **Modal loop elimination** | MEDIUM | A3 | Convert `LicenseActivationDialog::runModalLoop()` to async `enterModalState()` with callback. |
| **Path resolution audit** | MEDIUM | A4, A7 | Create `samplar::DataPaths` helper that resolves `data/`, Python, demucs paths correctly in both standalone and VST3 modes. Replace all `getCurrentWorkingDirectory()` fallbacks and `currentExecutableFile` assumptions. Affects ~15 files. |
| **AudioDeviceManager guards** | MEDIUM | A6 | Add `isRunningAsStandalone()` checks around all 4 UI components that call `addAudioCallback()`. |
| Extract MainContentComponent | LOW | — | Move all UI from MainWindow into a standalone Component class |
| Add `juce_audio_processors` module | NONE | — | CMake change only, no code impact |
| Create SamplarAudioProcessor skeleton | NONE | — | New file, no existing code touched |

**Phase 0 is preparation-only — the standalone app must behave identically after all Phase 0 changes.**

### Phase 1: Dual Build (Standalone + VST3)
**Goal:** Both targets compile and run. VST3 shows UI but audio may not work yet.

| Task | Risk | Details |
|------|------|---------|
| Switch `juce_add_gui_app` ? `juce_add_plugin` | MEDIUM | Standalone behavior must remain identical |
| Implement processBlock() bridge | MEDIUM | Adapter wrapping existing AudioSource chain |
| Create SM5PluginEditor | LOW | Wraps MainContentComponent |
| State save/restore stub | LOW | Empty for now, prevents crashes |
| Test standalone still works identically | CRITICAL | Full regression test |

### Phase 2: MIDI Input
**Goal:** MIDI triggers samples in both standalone and VST3.

| Task | Risk | Details |
|------|------|---------|
| MidiMappingService | LOW | New service, no existing code changed |
| Browser page auto-mapping | LOW | Maps visible tiles to notes 36-51 |
| Sequencer channel mapping | LOW | Maps rows to fixed MIDI notes |
| MIDI learn UI | LOW | Click pad + play note = mapped |
| Standalone MIDI device selection | NONE | Free via StandaloneFilterWindow |

### Phase 3: Audio Input & Sample Mode
**Goal:** Live audio input for recording/sampling. New Sample Mode page.

| Task | Risk | Details |
|------|------|---------|
| Enable input bus | LOW | Already declared in BusesProperties |
| Live recording to buffer | MEDIUM | New code, audio thread safety critical |
| Sample Mode page UI | MEDIUM | New page, reuses existing grid/slice code |
| Slice-to-pad mapping | LOW | Extends MidiMappingService |
| Slice manipulation (reverse, loop) | LOW | New AudioSource wrappers |

### Phase 4: Multi-Output & Polish
**Goal:** Individual drum outputs, automation, CLAP support.

| Task | Risk | Details |
|------|------|---------|
| Multi-out bus routing | MEDIUM | Route sequencer channels to individual outs |
| APVTS parameters | MEDIUM | Expose key controls for DAW automation |
| CLAP support | LOW | 2 CMake lines via clap-juce-extensions |
| State save/restore (full) | MEDIUM | Sample paths, settings, sequencer state |
| Preset system | LOW | VST3 preset standard + user presets |

---

## 7. Risk Analysis

### 7.1 HIGH Risk Items

| Risk | Impact | Mitigation |
|------|--------|------------|
| `JUCEApplication::getInstance()` returns null in VST3 | CRASH | Audit all calls, replace with processor reference |
| Editor destroyed/recreated on each open | STATE LOSS | Store ALL state in AudioProcessor, never in Editor |
| File path resolution differs in plugin sandbox | BROKEN FEATURES | Use absolute paths, resolve against plugin binary location |
| `JUCE_MODAL_LOOPS_PERMITTED` may conflict with hosts | UI FREEZE | Replace modal dialogs with async alternatives |
| Host provides different sample rate/buffer size | AUDIO GLITCH | Ensure all sources handle dynamic prepareToPlay() |

### 7.2 MEDIUM Risk Items

| Risk | Impact | Mitigation |
|------|--------|------------|
| Python bridge subprocess paths | BROKEN AI FEATURES | Resolve paths relative to plugin binary, not CWD |
| Demucs CLI subprocess execution | BROKEN SEPARATION | Same path resolution, test in sandboxed hosts |
| Thread model changes (host audio thread vs our thread) | RACE CONDITIONS | Audit all mutex usage in audio chain |
| Large UI in small plugin window | UX ISSUES | Resizable editor with minimum size constraints |
| Multiple plugin instances | RESOURCE CONFLICT | Each instance gets own ServiceRegistry + cache |

### 7.3 LOW Risk Items

| Risk | Impact | Mitigation |
|------|--------|------------|
| License system in plugin mode | LICENSE BYPASS | Same hardware ID check works in both modes |
| SoundTouch in processBlock | LATENCY | Already handles variable buffer sizes |
| Sequencer timing in host | TEMPO DRIFT | Use host transport info when available |

---

## 8. What Does NOT Change

**Critical guarantee: These components are untouched.**

- All 8 variation engines (Simple, Creative, Impact, PowerSynth, DDSP, Spectral, DirectMorph, FastSimple)
- GeneratorService, VariationService, ProcessingCoordinatorService
- BeatAnalysisService, SampleAnalysisService, ClassificationService
- DrumSliceService, FullSongService, DrumSeparatorService
- LicenseSystemService, SecureSessionManager, FeatureManager
- PreSliceProcessingService (Gate, Comp, Limiter chain)
- SamplePreviewCache (LRU cache)
- SequencerAudioSource (32-voice polyphonic engine)
- SoundTouchSource (pitch/speed)
- ProcessingAudioSource (effect wrapper)
- EventBus (pub/sub messaging)
- All UI pages (SampleBrowserPage, WavePreviewPage, etc.)
- All UI components (StepSequencer, WaveEditor, BeatGridOverlay, etc.)
- DrumLibrary system
- Pattern library (Latin, with rolls, etc.)
- File drag-and-drop
- Context menus
- Trim handles
- All keyboard shortcuts

---

## 9. Files to Create (New)

| File | Purpose |
|------|---------|
| `src/plugin/SamplarAudioProcessor.h` | AudioProcessor header |
| `src/plugin/SamplarAudioProcessor.cpp` | processBlock, state, buses |
| `src/plugin/SM5PluginEditor.h` | AudioProcessorEditor header |
| `src/plugin/SM5PluginEditor.cpp` | Wraps MainContentComponent |
| `src/services/MidiMappingService.h` | MIDI note ? sample mapping |
| `src/services/MidiMappingService.cpp` | MIDI handling implementation |

## 10. Files to Modify (Existing)

| File | Change | Risk |
|------|--------|------|
| `CMakeLists.txt` | `juce_add_gui_app` ? `juce_add_plugin` + add module | LOW |
| `src/app/App.h` | Conditional: only used in standalone mode | LOW |
| `src/app/AudioDeviceService.h/.cpp` | Add `isHostManaged()` guard | LOW |
| `src/ui/MainWindow.h/.cpp` | Extract content into MainContentComponent | MEDIUM |
| `src/ui/Pages/SampleBrowserPage.cpp` | MIDI auto-mapping on page change | LOW |

**Total: 6 new files, 5 modified files. Zero deleted files.**

---

## 10. Architecture Diagram

```
+-----------------------------------------------------------------+
¦                    SamplarAudioProcessor                             ¦
¦                                                                  ¦
¦  +--------------+  +--------------+  +-----------------------+ ¦
¦  ¦ EventBus     ¦  ¦ ServiceReg   ¦  ¦ APVTS (Parameters)    ¦ ¦
¦  ¦ (pub/sub)    ¦  ¦ (17 services)¦  ¦ - Volume, Pan         ¦ ¦
¦  +--------------+  +--------------+  ¦ - Effect params       ¦ ¦
¦                                       ¦ - Tempo, Swing        ¦ ¦
¦  +----------------------------------+ +-----------------------+ ¦
¦  ¦         masterMixer_             ¦                            ¦
¦  ¦  +------------+ +-------------+ ¦                            ¦
¦  ¦  ¦ Preview    ¦ ¦ Sequencer   ¦ ¦  +-----------------------+ ¦
¦  ¦  ¦ Chain      ¦ ¦ AudioSource ¦ ¦  ¦ MidiMappingService    ¦ ¦
¦  ¦  ¦ (browser)  ¦ ¦ (32 voices) ¦ ¦  ¦ - Note?Sample map    ¦ ¦
¦  ¦  +------------+ +-------------+ ¦  ¦ - Velocity layers    ¦ ¦
¦  +----------------------------------+  ¦ - Auto-map browser   ¦ ¦
¦                                        +-----------------------+ ¦
¦  processBlock(buffer, midi):                                     ¦
¦    1. Parse MIDI ? MidiMappingService                            ¦
¦    2. masterMixer_.getNextAudioBlock(buffer)                     ¦
¦    3. Route to output buses                                      ¦
¦                                                                  ¦
+------------------------------------------------------------------¦
¦                                                                  ¦
¦  STANDALONE MODE          ¦  VST3 MODE                           ¦
¦  -------------            ¦  ---------                           ¦
¦  StandaloneFilterWindow   ¦  DAW Host                            ¦
¦  AudioDeviceManager       ¦  Host audio engine                   ¦
¦  MIDI device selection    ¦  Host MIDI routing                   ¦
¦  Audio settings dialog    ¦  Host manages I/O                    ¦
¦  DocumentWindow frame     ¦  Embedded in DAW                     ¦
¦                                                                  ¦
+------------------------------------------------------------------+
```

---

## 11. Success Criteria

### Standalone Mode
- [ ] Boots and runs identically to current v1.0.9
- [ ] All sample browsing, processing, sequencer features work
- [ ] Audio device selection works (same as today)
- [ ] MIDI input works (new — select MIDI device in settings)
- [ ] MIDI notes trigger samples from browser
- [ ] No performance regression

### VST3 Mode
- [ ] Loads in Ableton Live, FL Studio, Cubase, Reaper
- [ ] UI renders correctly (resizable, 1200x800 default)
- [ ] Audio output works (stereo main bus)
- [ ] MIDI input triggers samples
- [ ] Sequencer plays in sync with DAW transport
- [ ] State saves/restores correctly (close and reopen session)
- [ ] Multiple instances can run simultaneously
- [ ] No crashes on editor open/close cycles

---

## 12. References

- [KrumSampler](https://github.com/krismakesstuff/KrumSampler) — Open-source JUCE drum sampler
- [DrumCraker](https://github.com/Wamphyre/DrumCraker) — VST3 drum sampler
- [JUCE SamplerPluginDemo](https://github.com/juce-framework/JUCE/blob/master/examples/Plugins/SamplerPluginDemo.h)
- [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) — CLAP support for JUCE
- [JUCE CMake AudioPlugin Example](https://github.com/juce-framework/JUCE/blob/master/examples/CMake/AudioPlugin/CMakeLists.txt)
- [JUCE Bus Layouts Tutorial](https://juce.com/tutorials/tutorial_audio_bus_layouts/)
- [JUCE AudioProcessor Docs](https://docs.juce.com/master/classAudioProcessor.html)
- [Steinberg VST3 Processing FAQ](https://steinbergmedia.github.io/vst3_dev_portal/pages/FAQ/Processing.html)

---

## 13. Post-Audit Findings (Risk Audit Appendix)

**Auditor:** Risk Analyst Agent
**Based on:** Systematic search of all .cpp/.h files under `src/`

### Audit Summary Table

| ID | Severity | File(s) | Issue | Phase |
|----|----------|---------|-------|-------|
| A1 | CRITICAL | SampleBrowserPage.h:485,:162; WavePreviewPage.h:123; EngineLabPage.h:45 | Audio chain (masterMixer_, AudioSourcePlayer) lives inside UI Components, not processor. processBlock() bridge cannot function without refactor. | Phase 0 |
| A2 | CRITICAL | 12 files, ~50 call sites | `JUCEApplication::getInstance()` returns null in VST3. Crashes at MainWindow.cpp:48,:1815 (`systemRequestedQuit()`). | Phase 0 |
| A3 | CRITICAL | LicenseActivationDialog.cpp:142 | `runModalLoop()` blocks host message thread. DAW freeze in Ableton/FL Studio. | Phase 0 |
| A4 | HIGH | StepSequencerComponent.cpp:3668+, PatternLibraryDialog.cpp:252, samplarAnimation.cpp:487 | `getCurrentWorkingDirectory()` fallbacks resolve to wrong location in VST3 (e.g. `C:\Windows\System32`). | Phase 0 |
| A5 | HIGH | SequencerAudioSource.cpp:68,83,112; SamplePreviewCache.cpp:45 | `std::mutex` acquired on audio thread. Priority inversion risk with ASIO at low buffer sizes. | Phase 2 |
| A6 | HIGH | SampleBrowserPage.h:163, WavePreviewPage.h:125, EngineLabPage.h:46, SpectralSeparateDialog.h:221 | `AudioDeviceManager*` pointer in 4 UI components, all call `addAudioCallback()`. Null in VST3. | Phase 0 |
| A7 | HIGH | FullSongService.cpp (6 sites), DrumSeparatorService.cpp:59 + 10 other files | `currentExecutableFile` resolves to VST3 bundle path, not the data directory. Python/demucs paths break. | Phase 0 |
| A8 | MEDIUM | No existing getStateInformation | State save/restore complexity underestimated. Sequencer, samples, WaveEditor, MIDI mappings all need persistence. Must start in Phase 1, not Phase 4. | Phase 1-3 |
| A9 | MEDIUM | FullSongService.cpp:788 | Process CWD mutation is a race condition with multiple plugin instances. CWD is per-process, not per-instance. | Phase 2 |
| A10 | MEDIUM | CMakeLists.txt, MainWindow.cpp | `juce_add_plugin` changes standalone wrapper behavior: adds Options menu, changes window management, audio settings file location. | Phase 1 |

### A1: Audio Ownership Must Be Inverted (CRITICAL)

**The Problem:** The plan's `processBlock()` bridge calls `masterMixer_.getNextAudioBlock()` but `masterMixer_` is a member of `SampleBrowserPage` (a UI Component). In VST3, the editor is created/destroyed on every open/close cycle. Audio must outlive the UI.

Three separate `AudioSourcePlayer` instances exist:
- `SampleBrowserPage.h:162` — main sequencer + preview chain
- `WavePreviewPage.h:123` — WaveEditor preview
- `EngineLabPage.h:45` — EngineLab preview

**The Fix:** Create a new `AudioEngine` class owned by `SamplarAudioProcessor`:
```
SamplarAudioProcessor
  +-- AudioEngine (NEW - owns all audio sources)
        +-- masterMixer_
        +-- sequencerEngine_ (SequencerAudioSource)
        +-- previewMixer_ + transport_ + resampler_
        +-- processingSource_
```
UI pages receive `AudioEngine&` references. Audio survives editor destruction.

### A2: JUCEApplication::getInstance() Scope (CRITICAL)

**Affected Files (12 total, ~50 sites):**

| File | Count | Pattern |
|------|-------|---------|
| SampleBrowserPage.cpp | 14 | `dynamic_cast<samplar::App*>(JUCEApplication::getInstance())` for ServiceRegistry |
| MainWindow.cpp | 12 | ServiceRegistry access + `systemRequestedQuit()` |
| WavePreviewPage.cpp | 8 | ServiceRegistry access |
| SettingsDialog.cpp | 4 | ServiceRegistry access |
| ChatService.cpp | 4 | ServiceRegistry access |
| EngineLabPage.cpp | 3 | ServiceRegistry access |
| PatternLibraryDialog.cpp | 2 | ServiceRegistry access |
| ChatPage.cpp | 2 | ServiceRegistry access |
| VariationManager.cpp | 2 | ServiceRegistry access |
| SpectralSeparateDialog.cpp | 2 | ServiceRegistry access |
| WaveEditorView.cpp | 2 | ServiceRegistry access |
| UpdateCheckService.cpp | 1 | ServiceRegistry access |

**The Fix:** Create `samplar::ServiceLocator` with a static accessor, or thread `SamplarAudioProcessor*` through the component constructor chain.

### A3: Modal Loop Blocks Host (CRITICAL)

`LicenseActivationDialog.cpp:142` uses synchronous `runModalLoop()`. In VST3, this freezes the DAW's message thread. Must convert to `enterModalState()` + `ModalCallbackFunction`.

All other modal uses (8 sites across SampleBrowserPage, PatternLibraryDialog, WavePreviewPage) already use async callbacks and are safe.

### A5: Mutex on Audio Thread (HIGH — Deferred)

`SequencerAudioSource::getNextAudioBlock()` acquires `patternMutex_` on every call. Works in standalone but causes priority inversion under ASIO with low buffers in DAW hosts.

**Fix (Phase 2):** Replace with lock-free double-buffer swap for pattern data. Use `juce::AbstractFifo` for commands, `std::atomic` flags for play/stop.

### A8: State Save Complexity (MEDIUM — Revised Timeline)

State that must be persisted for DAW session recall:
- Sequencer: per-row sample paths, mute/solo/volume/pan, per-step on/off/velocity/ratchet, tempo, swing, pattern length
- Sample browser: library root, category selection, MIDI mapping table
- WaveEditor: trim points, effect parameters, processing preset
- Active files: currently loaded song, preview sample

**Revised:** State save stub in Phase 1, sequencer state in Phase 2, full state in Phase 3 (not Phase 4).

### A10: Standalone Wrapper Behavioral Changes (MEDIUM)

When switching to `juce_add_plugin`, the standalone becomes `StandaloneFilterWindow` which:
1. Adds its own "Options" button (conflicts with existing SettingsDialog)
2. Controls window title/frame (conflicts with custom MainWindow)
3. Saves audio settings in a different location (device config may reset)

**Fix:** Customize `StandaloneFilterWindow` or use `JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1` to keep existing App class.

---

## 14. Final Assessment

**Conversion is feasible and the rewards are substantial.** The core audio processing (variation engines, sequencer, SoundTouch, effects chain) is completely unaffected. The work is concentrated in:

1. **Audio ownership refactor** (Phase 0) — largest single task, ~2 weeks
2. **JUCEApplication replacement** (Phase 0) — mechanical but widespread, ~1 week
3. **Path resolution** (Phase 0) — systematic, ~3 days
4. **processBlock bridge** (Phase 1) — straightforward once A1 is done, ~2 days
5. **MIDI implementation** (Phase 2) — new feature, clean addition, ~1 week
6. **Sample Mode** (Phase 3) — 70% already exists, ~1-2 weeks

**No existing feature is lost. The standalone gains MIDI input. The VST3 opens entirely new markets.**
