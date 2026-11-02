# Graph Report - SAMPLAR  (2026-10-21)

## Corpus Check
- 199 files · ~245,314 words
- Verdict: corpus is large enough that graph structure adds value.
- Unclassified: 14 file(s) not represented in the graph (top: .cmake 6, (none) 4, .ico 2)

## Summary
- 5096 nodes · 9540 edges · 242 communities (227 shown, 11 thin omitted)
- Extraction: 88% EXTRACTED · 12% INFERRED · 0% AMBIGUOUS · INFERRED: 1105 edges (avg confidence: 0.84)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `cf8066ba`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- SModePage
- StemWaveformComponent
- PlaybackEngine
- MidiMappingService
- SequencerSlotComponent
- FullSongService.cpp
- linter_common.py
- SModeSegmentModel
- SModeUiSelfTest
- Voice
- BeatThisDetector.cpp
- LoadAudioOverlay
- LicenseSystemService
- LicenseCache
- SModePage.cpp
- SequencerLaneComponent
- vector
- MidiMappingService.cpp
- AnalysisTests.cpp
- WaveDragArea
- RootComponent
- Fft
- DebugConsole
- TempoAnalyzer
- AudioDeviceService
- PersistDummyProcessor
- SModeService
- EngineTests.cpp
- SignedLicense
- applyCommand
- SamplarAudioProcessor
- StemData
- OnnxSeparationProvider
- SModeSeparationService
- Snapshot
- EngineParams
- SepHardeningTests.cpp
- TestSigner
- IService
- LicenseInfoPanel
- SecureSessionManager.cpp
- String
- Context
- SpectralFluxDetector
- DWTTempoDetector
- SettingsDialog.cpp
- LicenseActivationDialog
- LicenseStatusWidget
- SlotDesc
- enforceRunningSession
- SequencerLaneComponent.cpp
- SamplarLookAndFeel
- LicenseGateOverlay
- ChopInspectorStrip
- AppState
- StemWaveformComponent.cpp
- ClockGuard
- SamplarAudioProcessorEditor
- process
- TopBarSpec
- Session
- revalidationWorker_
- AsyncFileLogger
- SModeMidiLearnOverlay
- FeatureManager
- wave_editor_ui_customization_guide.md
- Result
- PatternGenerator.cpp
- DummyProcessor
- TestHelpers.h
- AnalysisDsp.cpp
- GpuAdapterProbe.cpp
- OAuthBrowserAuthenticator
- PatternSlot
- SegmentFx.h
- HardwareFingerprintGenerator.cpp
- HybridTempoDetector
- OctaveCorrection.cpp
- StackSampler
- AudioDriver
- DragButton
- mouseDown
- RevalResult
- PlaybackEngine.cpp
- CreationJob
- UpdateDialog
- LicenseStatusWidget.cpp
- SpscFifo
- OnnxSeparationProvider.cpp
- AppState.cpp
- EventBus
- CancellableHttpPost
- LaneInput
- Fixture
- SModeSession.cpp
- UpdateCheckService
- TestLogger
- runSessionRoundtrip
- log
- MidiLearnable
- MainWindow
- App
- String
- .RootComponent
- SModeService.cpp
- mouseDrag
- Command
- SAMPLAR - Task Completion Checklist
- Config
- NotificationButton
- ChopInspectorStrip.cpp
- CrossProcessFileLock
- Args
- OnnxDelayLoad.cpp
- startBrowserAuthentication
- UIState
- SegSnap
- Result
- RelinkCheck
- SamplarSettings
- LicenseSystemTests
- SeparationProbe.cpp
- StateBinding
- DataPaths
- LicenseSystemService.cpp
- File
- SessionState
- MidiDeviceRow
- paint
- StatusSpy
- AudioBuffer
- StemSnap
- AppStateTests.cpp
- Result
- detect
- LicenseInfo
- handleKeyPress
- GpuTeardownOffload.cpp
- VST3SmokeHost.cpp
- RootListener
- SlotSnap
- SegmentEffects
- RecentSongs
- runAutoChop
- SettingsButton
- AnalysisCancel
- SModeService::DecodedStems
- log
- PlaySettings
- Individual Game Analysis
- setPlayheadFromMouse
- run_all.py
- OAuthBrowserAuthenticator.cpp
- StemPlayParams
- operator delete
- Notification
- ModState
- Result
- GpuAdapterInfo
- setupSlider
- HtdemucsIstftTests.cpp
- Watchdog
- createRootComponent
- LicensingHardeningTests
- SessionSource
- UpdateInfo
- Values
- LaneRescaleReport
- .~StemWaveformComponent
- AudioDeviceService.cpp
- OrtRuntime.cpp
- SModeNoticeBanner
- GateRamp
- AuthResult
- CommandResult
- Job
- SModeSessionTests
- split_htdemucs.py
- ApplyReport
- SeqRenderTarget
- ScopedStateBinding
- SeqEvent
- StdoutLogger
- uint64_t
- suggested_commands.md
- RecordingService
- connectToDeviceManager
- handleSegmentMenuResult
- requestLoadFile
- UpdateCheckService.cpp
- crashHandler
- handleIncomingMidiMessage
- SettingsDialog::showDialog
- String
- Result
- .getSessionStatus
- paint
- paint
- Result
- SAMPLAR - Third-Party Notices
- code_style_and_conventions.md
- resized
- HostWindow
- generate_icon.py
- CLAUDE.md
- Config
- NavModel
- context-mode — MANDATORY routing rules
- mouseUp
- Architecture Overview (BIBLE Framework)
- SOUL.md
- SAMPLAR Licensing
- StatusSpy
- 1.3.0 — 2026-04-24
- MEMORY.md - MonstaForge's Long-Term Memory
- SAMPLAR - Project Overview
- SAMPLAR - Technical Stack & Architecture
- Config
- MouseEvent
- Agent Workflow
- JUCE/C++ Coding Standards
- RawMidi
- Result
- fetch_onnxruntime_directml.py
- Agent Instructions
- BLOCKED commands — do NOT attempt these
- GenState
- Adding New Features
- Development Commands
- Dependencies
- paint
- IDENTITY.md
- modifierKeysChanged
- SModeSegmentModelTests
- addAudioCallback
- keyPressed
- getTooltip
- getAvailableBufferSizes

## God Nodes (most connected - your core abstractions)
1. `SModePage` - 264 edges
2. `MidiMappingService` - 207 edges
3. `PlaybackEngine` - 178 edges
4. `StemWaveformComponent` - 178 edges
5. `LicenseSystemService` - 104 edges
6. `SModeSegmentModel` - 97 edges
7. `SequencerSlotComponent` - 89 edges
8. `SequencerLaneComponent` - 87 edges
9. `RootComponent` - 85 edges
10. `SModeService` - 71 edges

## Surprising Connections (you probably didn't know these)
- `runBpmUnload()` --calls--> `cmd()`  [INFERRED]
  tools/vst3_smoke_host/VST3SmokeHost.cpp → src/audio/engine/tests/EngineTests.cpp
- `runUiSweep()` --calls--> `cmd()`  [INFERRED]
  tools/vst3_smoke_host/VST3SmokeHost.cpp → src/audio/engine/tests/EngineTests.cpp
- `testOfflineEqualsRealtime()` --calls--> `compare()`  [INFERRED]
  src/audio/engine/tests/EngineTests.cpp → tools/separation_probe/SeparationProbe.cpp
- `StemAudio` --references--> `map_`  [EXTRACTED]
  tools/separation_probe/SeparationProbe.cpp → src/app/EventBus.h
- `RecordingService` --inherits--> `IService`  [EXTRACTED]
  tests/app/ServiceRegistryTests.cpp → src/app/Service.h

## Import Cycles
- None detected.

## Communities (242 total, 11 thin omitted)

### Community 0 - "SModePage"
Cohesion: 0.01
Nodes (137): KeyListener, AppState, AutoMode, ComboBox, EventBus, HandlerId, Rectangle, set (+129 more)

### Community 1 - "StemWaveformComponent"
Cohesion: 0.02
Nodes (91): DrawableButton, Colour, Component, DragMode, Drawable, function, HeaderMode, Image (+83 more)

### Community 2 - "PlaybackEngine"
Cohesion: 0.02
Nodes (85): kMaxLanes, array, atomic, AudioBuffer, JUCE_DECLARE_NON_COPYABLE, kMaxStems, unique_ptr, PlaybackEngine (+77 more)

### Community 3 - "MidiMappingService"
Cohesion: 0.02
Nodes (78): AudioSource, AudioSourcePlayer, MidiInputCallback, array, atomic, AudioDeviceManager, EventBus, function (+70 more)

### Community 4 - "SequencerSlotComponent"
Cohesion: 0.03
Nodes (51): Colour, Component, DragMode, function, JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR, Path, Point, SequencerSlotComponent (+43 more)

### Community 5 - "FullSongService.cpp"
Cohesion: 0.15
Nodes (41): LockOutcome, acquireLock(), Config, EventBus, File, function, int64, Result (+33 more)

### Community 6 - "linter_common.py"
Cohesion: 0.06
Nodes (80): analyze(), main(), print_report(), SAMPLAR Release Linter - Check #4: EventBus topic sync. A topic that is…, scan(), analyze(), extract_strings(), load_scrub_list() (+72 more)

### Community 7 - "SModeSegmentModel"
Cohesion: 0.06
Nodes (58): AudioBuffer, ExtraStateProvider, ExtraStateRestorer, pair, shared_ptr, String, vector, ExtraStateProvider (+50 more)

### Community 8 - "SModeUiSelfTest"
Cohesion: 0.05
Nodes (54): File, String, vector, File, String, StringArray, LicenseCleanerUtility, getAllTargets (+46 more)

### Community 9 - "Voice"
Cohesion: 0.04
Nodes (48): Voice, active, attacking, cursor, edgeFade, ended, env, envStep (+40 more)

### Community 10 - "BeatThisDetector.cpp"
Cohesion: 0.08
Nodes (46): Impl, acquireEngine(), beatCountsFor(), BeatThisDetector, clearCache, detect, estimateBeatsPerBar, estimateTempo (+38 more)

### Community 11 - "LoadAudioOverlay"
Cohesion: 0.11
Nodes (16): File, FileDragAndDropTarget, ListBox, ListBoxModel, MouseEvent, StringArray, vector, LoadAudioOverlay (+8 more)

### Community 12 - "LicenseSystemService"
Cohesion: 0.04
Nodes (57): RevalidationWorker, FeatureManager, atomic, EventBus, HandlerId, int64, LicenseStatus, mutex (+49 more)

### Community 13 - "LicenseCache"
Cohesion: 0.09
Nodes (56): int64_t, once_flag, buildLockoutKey(), CacheEntry, cachedAt, hardwareId, integrityHash, lastValidated (+48 more)

### Community 14 - "SModePage.cpp"
Cohesion: 0.08
Nodes (74): MidiMappingService, Colour, Component, EventBus, function, KeyPress, SModeService, var (+66 more)

### Community 15 - "SequencerLaneComponent"
Cohesion: 0.04
Nodes (49): ActionMode, Colour, ComboBox, Component, function, JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR, PatternType, Rectangle (+41 more)

### Community 16 - "vector"
Cohesion: 0.10
Nodes (12): String, Array, atomic, vector, map_, mutex, unordered_map, PlaybackEngineTestAccess (+4 more)

### Community 17 - "MidiMappingService.cpp"
Cohesion: 0.07
Nodes (50): ModDestination, atomic, EventBus, applyVelocityCurve, auditionSlot, beginMidiLearnForSMode, checkEngineLifecycle, collectResampleResults (+42 more)

### Community 18 - "AnalysisTests.cpp"
Cohesion: 0.10
Nodes (40): mt19937, Pattern, addNote(), benchStem(), check(), AudioBuffer, File, string (+32 more)

### Community 19 - "WaveDragArea"
Cohesion: 0.08
Nodes (46): Graphics, MouseEvent, Rectangle, String, atomic, AudioBuffer, Component, function (+38 more)

### Community 20 - "RootComponent"
Cohesion: 0.04
Nodes (46): DragAndDropContainer, EventBus, File, FileDragAndDropTarget, HandlerId, ListBox, shared_ptr, Timer (+38 more)

### Community 21 - "Fft"
Cohesion: 0.05
Nodes (35): PFFFT_Setup, Config, channels, cropStart, hop, nFft, padStart, scale (+27 more)

### Community 22 - "DebugConsole"
Cohesion: 0.06
Nodes (41): AsyncUpdater, CodeDocument, CodeEditorComponent, File, FileLogger, Logger, String, unique_ptr (+33 more)

### Community 23 - "TempoAnalyzer"
Cohesion: 0.16
Nodes (25): AudioBuffer, Config, File, Result, vector, AudioFormatManager, FFT, JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (+17 more)

### Community 24 - "AudioDeviceService"
Cohesion: 0.10
Nodes (21): ChangeListener, AudioDeviceService, addDeviceChangeListener, deviceChangeCallbacks_, deviceManager_, deviceSwitchInProgress_, getCurrentBufferSize, lastActiveDevice_ (+13 more)

### Community 25 - "PersistDummyProcessor"
Cohesion: 0.05
Nodes (23): AudioProcessorListener, ChangeDetails, AppStatePersistenceTests, atomic, AudioBuffer, AudioProcessor, AudioProcessorEditor, Identifier (+15 more)

### Community 26 - "SModeService"
Cohesion: 0.05
Nodes (36): atomic, EventBus, File, HandlerId, int64, shared_ptr, String, ThreadPool (+28 more)

### Community 27 - "EngineTests.cpp"
Cohesion: 0.17
Nodes (33): allZeroFrom(), cmd(), AudioBuffer, CmdType, vector, fillModel(), firstNonZero(), main() (+25 more)

### Community 28 - "SignedLicense"
Cohesion: 0.10
Nodes (35): activePublicKey(), MemoryBlock, String, StringArray, Time, String, Time, isIssuedInFuture() (+27 more)

### Community 29 - "applyCommand"
Cohesion: 0.27
Nodes (17): applyVelCurve(), allNotesOff, allSoundOff, applyCommand, applySegFx, handleRawMidi, noteOff, noteOn (+9 more)

### Community 30 - "SamplarAudioProcessor"
Cohesion: 0.03
Nodes (75): AsyncFileLogger, BusesLayout, shutdown, acquireSharedLogger(), AsyncFileLogger, AudioBuffer, AudioProcessor, AudioProcessorEditor (+67 more)

### Community 31 - "StemData"
Cohesion: 0.07
Nodes (29): AudioBuffer, Colour, shared_ptr, String, vector, SegmentSnapshot, detectedBpm, extra (+21 more)

### Community 32 - "OnnxSeparationProvider"
Cohesion: 0.05
Nodes (35): Env, File, function, shared_ptr, String, unique_ptr, vector, HtdemucsIstft (+27 more)

### Community 33 - "SModeSeparationService"
Cohesion: 0.08
Nodes (31): EventBus, File, String, StringArray, vector, atomic, EventBus, File (+23 more)

### Community 34 - "Snapshot"
Cohesion: 0.06
Nodes (36): array, kMaxStems, vector, LaneSnap, cellCover, cellsPerBeat, cellStart, muted (+28 more)

### Community 35 - "EngineParams"
Cohesion: 0.07
Nodes (35): EngineParams, forwardCcToUi, gate, globalSemis, midiLearn, mono, seqBpm, stems (+27 more)

### Community 36 - "SepHardeningTests.cpp"
Cohesion: 0.14
Nodes (37): RunOnce, isAvailable, allFinite(), cacheWorkerMain(), CancelRun, cancelled, latencyMs, valid (+29 more)

### Community 37 - "TestSigner"
Cohesion: 0.12
Nodes (32): BCRYPT_ALG_HANDLE, BCRYPT_KEY_HANDLE, int64, LicenseSystemService, O, String, unique_ptr, var (+24 more)

### Community 38 - "IService"
Cohesion: 0.15
Nodes (16): IService, initialise, shutdown, string, unique_ptr, string, unique_ptr, unordered_map (+8 more)

### Community 39 - "LicenseInfoPanel"
Cohesion: 0.06
Nodes (35): Colour, JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR, Label, TextButton, LicenseInfoPanel, activateBtn_, activeColor_, appSectionLabel_ (+27 more)

### Community 40 - "SecureSessionManager.cpp"
Cohesion: 0.13
Nodes (32): File, MemoryBlock, optional, String, deriveKeyMaterial(), fromBase64(), mutex, string (+24 more)

### Community 41 - "String"
Cohesion: 0.30
Nodes (10): bankRangeName(), String, String, clearMidiLearn, debugMidiSelfTest, flashUndoStatus, refreshMidiLearnInfo, showMissingAudioNotice (+2 more)

### Community 42 - "Context"
Cohesion: 0.12
Nodes (16): initialise, AppState, Context, appState, bus, registry, uiState, EventBus (+8 more)

### Community 43 - "SpectralFluxDetector"
Cohesion: 0.10
Nodes (28): applyMicroFade(), applyMicroFadeRange(), AudioBuffer, vector, vector, Settings, attackWindowMs, backtrackSamples (+20 more)

### Community 44 - "DWTTempoDetector"
Cohesion: 0.18
Nodes (20): AudioBuffer, Config, File, Result, vector, DWTTempoDetector, autocorrelate, db4Hi (+12 more)

### Community 45 - "SettingsDialog.cpp"
Cohesion: 0.10
Nodes (22): AudioSettingsPanel::onBufferChanged(), AudioSettingsPanel::onDeviceChanged(), AudioSettingsPanel::paint(), AudioSettingsPanel::refreshDeviceList(), AudioSettingsPanel::updateSampleRateDisplay(), AudioDeviceManager, Graphics, LicenseSystemService (+14 more)

### Community 46 - "LicenseActivationDialog"
Cohesion: 0.05
Nodes (56): HyperlinkButton, Colour, Component, DialogWindow, EventBus, function, Graphics, LicenseStatus (+48 more)

### Community 47 - "LicenseStatusWidget"
Cohesion: 0.07
Nodes (30): Button::Listener, ProgressBar, Button, Component, EventBus, HandlerId, Label, LicenseStatus (+22 more)

### Community 48 - "SlotDesc"
Cohesion: 0.08
Nodes (29): LaneDesc, cellsPerBeat, muted, numCells, slots, SlotDesc, cellSpan, empty (+21 more)

### Community 49 - "enforceRunningSession"
Cohesion: 0.19
Nodes (16): int64, minutes, time_point, buildStatusPayload, enforceRunningSession, getRemainingGraceTime, hasStoredTokenLocked, nowMs (+8 more)

### Community 50 - "SequencerLaneComponent.cpp"
Cohesion: 0.14
Nodes (26): vector, rescaleLaneSlots(), Colour, function, pair, var, vector, buildSlotComponents (+18 more)

### Community 51 - "SamplarLookAndFeel"
Cohesion: 0.15
Nodes (26): Font, LookAndFeel_V4, Slider, SliderStyle, Button, Colour, ComboBox, Drawable (+18 more)

### Community 52 - "LicenseGateOverlay"
Cohesion: 0.11
Nodes (27): ComponentListener, Component, EventBus, function, Graphics, KeyPress, LicenseSystemService, String (+19 more)

### Community 53 - "ChopInspectorStrip"
Cohesion: 0.07
Nodes (23): MidiLearnButton, ChopInspectorStrip, attack_, dragging_, envelopeOk_, gain_, kMaxGainDb, kMinGainDb (+15 more)

### Community 54 - "AppState"
Cohesion: 0.09
Nodes (24): AppState, aliveToken_, bindings_, dispatchPending_, dispatchSeq_, kFormatVersion, kMagic, kMagicLen (+16 more)

### Community 55 - "StemWaveformComponent.cpp"
Cohesion: 0.13
Nodes (26): atomic, function, Graphics, HeaderMode, Rectangle, StemKind, applyHeaderMode, computeSpectralColors (+18 more)

### Community 56 - "ClockGuard"
Cohesion: 0.15
Nodes (13): ClockGuard, kMask, kMaxPlausibleMs, kMaxYearsAfterBuild, kMinPlausibleMs, kToleranceMs, File, int64 (+5 more)

### Community 57 - "SamplarAudioProcessorEditor"
Cohesion: 0.10
Nodes (22): SamplarAudioProcessor, Graphics, AudioProcessorEditor, Component, HandlerId, JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR, unique_ptr, LicenseGateOverlay (+14 more)

### Community 58 - "process"
Cohesion: 0.15
Nodes (18): HostInfo, bpm, offline, playing, ppq, ppqValid, valid, sanitizeHostInfo() (+10 more)

### Community 59 - "TopBarSpec"
Cohesion: 0.11
Nodes (18): topBarMinWidth(), TopBarSpec, bpm, dbl, eject, g, grid, half (+10 more)

### Community 60 - "Session"
Cohesion: 0.13
Nodes (22): ofstream, atomic, mutex, ofstream, ScopedEvent, category_, name_, startUs_ (+14 more)

### Community 61 - "revalidationWorker_"
Cohesion: 0.11
Nodes (18): Done, CancellableHttpPost, EventBus, shared_ptr, Thread, Job, accessToken, machineId (+10 more)

### Community 62 - "AsyncFileLogger"
Cohesion: 0.10
Nodes (26): AsyncFileLogger, AsyncFileLogger::AsyncFileLogger(), dropped_, file_, kMaxQueuedLines, logMessage, mutex_, out_ (+18 more)

### Community 63 - "SModeMidiLearnOverlay"
Cohesion: 0.10
Nodes (24): EventBus, Graphics, MouseEvent, ServiceRegistry, String, Component, EventBus, function (+16 more)

### Community 64 - "FeatureManager"
Cohesion: 0.13
Nodes (21): FT, initializer_list, FeatureType, string, FeatureManager, canPerformAction, featureMutex_, getFeaturesForLicense (+13 more)

### Community 65 - "wave_editor_ui_customization_guide.md"
Cohesion: 0.06
Nodes (31): **1. Layout Modification**, **2. Waveform Display Customization**, **3. Effects Section Customization**, **4. Parameter Panel Customization**, **5. Theming and Styling**, 🏗️ **Architecture Overview**, **Audio Processing Customization**, **Component Hierarchy** (+23 more)

### Community 66 - "Result"
Cohesion: 0.09
Nodes (22): String, vector, Result, beatsPerBar, beatThisBPM, beatThisConfidence, beatTimesSec, bpm (+14 more)

### Community 67 - "PatternGenerator.cpp"
Cohesion: 0.24
Nodes (22): pair, PatternType, String, vector, PatternGenerator, applySubSlicing, applyVelocityCurve, bjorklund (+14 more)

### Community 68 - "DummyProcessor"
Cohesion: 0.09
Nodes (7): AudioBuffer, AudioProcessor, AudioProcessorEditor, MemoryBlock, MidiBuffer, String, DummyProcessor

### Community 69 - "TestHelpers.h"
Cohesion: 0.14
Nodes (12): File, JUCE_DECLARE_NON_COPYABLE, String, ScopedEnv, name_, previous_, ScopedLicenseSandbox, env_ (+4 more)

### Community 70 - "AnalysisDsp.cpp"
Cohesion: 0.27
Nodes (9): deque, autocorrelationFFT(), AudioBuffer, vector, downmixToMono(), downmixToMonoBuffer(), energyStepScores(), findAttackSample() (+1 more)

### Community 71 - "GpuAdapterProbe.cpp"
Cohesion: 0.12
Nodes (20): IDXGIAdapter1, OrtCustomThreadHandle, OrtThreadWorkerFn, belowNormalThreadPriorityLevel(), uint64_t, wstring, getCurrentThreadPriorityLevel(), isUnifiedMemoryAdapter() (+12 more)

### Community 72 - "OAuthBrowserAuthenticator"
Cohesion: 0.09
Nodes (20): CancellableHttpPost, atomic, CompletionCallback, JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR, shared_ptr, unique_ptr, OAuthBrowserAuthenticator, callbackPort_ (+12 more)

### Community 73 - "PatternSlot"
Cohesion: 0.09
Nodes (19): PatternSlot, cellSpan, empty, gridCell, pitchSemitones, probability, reverse, role (+11 more)

### Community 74 - "SegmentFx.h"
Cohesion: 0.17
Nodes (18): false_type, Fx, computeSegmentFxHash(), fillSegmentFx(), getAttackMs(), getReleaseMs(), getTriggerMode(), vector (+10 more)

### Community 75 - "HardwareFingerprintGenerator.cpp"
Cohesion: 0.28
Nodes (13): containsDisallowedShellChars(), atomic, File, String, HardwareFingerprintGenerator, executeSystemCommand, generateHardwareIdV2, getOsMachineId (+5 more)

### Community 76 - "HybridTempoDetector"
Cohesion: 0.10
Nodes (24): AudioBuffer, Config, File, Result, AudioFormatManager, HybridTempoDetector, beatThisDetector_, detect (+16 more)

### Community 77 - "OctaveCorrection.cpp"
Cohesion: 0.15
Nodes (19): CandidateScore, bpm, onsetsPerBeat, score, correctOctave(), correctOctaveFromBuffer(), AudioBuffer, vector (+11 more)

### Community 78 - "StackSampler"
Cohesion: 0.08
Nodes (29): DWORD64, HANDLE, array, atomic, DWORD, mutex, string, thread (+21 more)

### Community 79 - "AudioDriver"
Cohesion: 0.12
Nodes (18): AudioPluginInstance, AudioDriver, blockSize_, buffer_, exceptions_, paced_, peak_, phase_ (+10 more)

### Community 80 - "DragButton"
Cohesion: 0.12
Nodes (8): ComponentDragger, Button, Graphics, MouseEvent, DragButton, dragger_, ExitButton, PinButton

### Community 81 - "mouseDown"
Cohesion: 0.13
Nodes (19): EdgeHit, Point, String, findSegmentAtX, findSegmentEdge, isNearGridLine, midiNoteToName, mouseDoubleClick (+11 more)

### Community 82 - "RevalResult"
Cohesion: 0.11
Nodes (25): RevalOutcome, post, CancellableHttpPost, String, LicenseServerClient, interpretRevalidate, isValidMachineIdV2, kMaxRetryAfterSeconds (+17 more)

### Community 83 - "PlaybackEngine.cpp"
Cohesion: 0.14
Nodes (25): SignalsmithStretch, hermite(), allocVoice, ensurePoolHeadroom, enterStretch, fastReleaseVoice, findSeg, forceCutVoice (+17 more)

### Community 84 - "CreationJob"
Cohesion: 0.06
Nodes (38): cancel, isReinstallError, condition_variable, Env, File, function, mutex, Session (+30 more)

### Community 85 - "UpdateDialog"
Cohesion: 0.13
Nodes (18): Graphics, Component, function, JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR, TextButton, UpdateDialog, laterBtn_, licenseOnlyMode_ (+10 more)

### Community 86 - "LicenseStatusWidget.cpp"
Cohesion: 0.17
Nodes (18): Colour, EventBus, LicenseStatus, LicenseSystemService, String, var, getStatusColour, getStatusText (+10 more)

### Community 87 - "SpscFifo"
Cohesion: 0.15
Nodes (11): AbstractFifo, Capacity, SpinLock, array, MpscFifo, fifo_, producerLock_, SpscFifo (+3 more)

### Community 88 - "OnnxSeparationProvider.cpp"
Cohesion: 0.16
Nodes (22): ProgressFn, CancelWatcher, stop_, thread_, atomic, File, function, String (+14 more)

### Community 89 - "AppState.cpp"
Cohesion: 0.21
Nodes (17): addSupersededNode, AppState::AppState(), deserialize, dispatchRestores, getChildCopy, getOrCreateChild, importLegacyJson, serialize (+9 more)

### Community 90 - "EventBus"
Cohesion: 0.08
Nodes (27): Handler, HandlerId, string, var, EventBus, mu_, nextId_, publish (+19 more)

### Community 91 - "CancellableHttpPost"
Cohesion: 0.14
Nodes (13): CancellableHttpPost, active_, cancelled_, mutex_, atomic, mutex, String, WebInputStream (+5 more)

### Community 92 - "LaneInput"
Cohesion: 0.11
Nodes (18): AudioBuffer, mutex, shared_ptr, vector, LaneInput, cellsPerBeat, muted, numBars (+10 more)

### Community 93 - "Fixture"
Cohesion: 0.15
Nodes (20): int64, LicenseSystemService, O, String, unique_ptr, UnitTest, Fixture, bus (+12 more)

### Community 94 - "SModeSession.cpp"
Cohesion: 0.33
Nodes (17): applySessionToModel(), clampF(), countSegments(), Identifier, int64, ValueTree, getB(), getD() (+9 more)

### Community 95 - "UpdateCheckService"
Cohesion: 0.11
Nodes (18): atomic, EventBus, mutex, PropertiesFile, shared_ptr, thread, Timer, unique_ptr (+10 more)

### Community 96 - "TestLogger"
Cohesion: 0.14
Nodes (14): ConsoleRunner, CriticalSection, File, FileOutputStream, Logger, String, unique_ptr, main() (+6 more)

### Community 97 - "runSessionRoundtrip"
Cohesion: 0.24
Nodes (12): clearAcks(), File, vector, NoteProbe, muted, note, stem, notesOf() (+4 more)

### Community 98 - "log"
Cohesion: 0.27
Nodes (12): String, StringArray, firstLineAfterHeader(), log(), main(), parseArgs(), Result, lines (+4 more)

### Community 99 - "MidiLearnable"
Cohesion: 0.17
Nodes (9): Base, function, MouseEvent, String, MidiLearnable, baseTip_, midiInfo_, onContextMenu (+1 more)

### Community 100 - "MainWindow"
Cohesion: 0.17
Nodes (11): BorderSize, SourceDetails, StringArray, DocumentWindow, FileDragAndDropTarget, MainWindow, closeButtonPressed, fileDragEnter (+3 more)

### Community 101 - "App"
Cohesion: 0.09
Nodes (14): JUCEApplication, App, earlyLogger_, eventBus_, getApplicationVersion, mainWindow, serviceRegistry, String (+6 more)

### Community 102 - "String"
Cohesion: 0.16
Nodes (16): function, String, applyBuiltInCcTarget, beginCcLearn, clearCcBindings, clearCcBindingsForTarget, describeCcBindings, endCcLearn (+8 more)

### Community 103 - ".RootComponent"
Cohesion: 0.21
Nodes (3): KeyPress, SModeService, var

### Community 104 - "SModeService.cpp"
Cohesion: 0.23
Nodes (15): DecodedStems, EventBus, function, var, computeSessionFingerprint, continueRestoreWithoutStems, initialise, onSeparationComplete (+7 more)

### Community 105 - "mouseDrag"
Cohesion: 0.18
Nodes (16): MouseWheelDetails, MouseEvent, mouseDrag, mouseEnter, mouseExit, mouseUp, mouseWheelMove, onGridOffsetChanged (+8 more)

### Community 106 - "Command"
Cohesion: 0.12
Nodes (15): Command, b0, d0, f0, f1, f2, f3, f4 (+7 more)

### Community 107 - "SAMPLAR - Task Completion Checklist"
Cohesion: 0.09
Nodes (21): 1. Code Documentation, 1. Game Implementation, 1. Pre-Build Verification, 1. Service Changes, 1. Windows (Primary Platform), 2. Asset Integration, 2. Build Process, 2. Cross-Platform Considerations (+13 more)

### Community 108 - "Config"
Cohesion: 0.12
Nodes (16): Config, agreementThreshold, alwaysRunDsp, beatThisConfidenceFloor, dwtLevels, enableOctaveCorrection, fftSize, harmonicWeight (+8 more)

### Community 109 - "NotificationButton"
Cohesion: 0.16
Nodes (13): Graphics, Mode, Button, Mode, Timer, NotificationButton, mode_, NotificationButton::NotificationButton() (+5 more)

### Community 110 - "ChopInspectorStrip.cpp"
Cohesion: 0.28
Nodes (14): clearTarget, getControl, getMidiInfo, onMidiMenu, resized, setControlsEnabled, setMidiInfo, setTarget (+6 more)

### Community 111 - "CrossProcessFileLock"
Cohesion: 0.17
Nodes (15): wstring, CrossProcessFileLock, handle_, locked_, path_, tryLock, unlock, wstring (+7 more)

### Community 112 - "Args"
Cohesion: 0.17
Nodes (12): Args, bpmUnloadDir, bpmUnloadSongSec, plugin, reopenBudgetMs, roundtripWav, showMs, stacks (+4 more)

### Community 113 - "OnnxDelayLoad.cpp"
Cohesion: 0.25
Nodes (13): FARPROC, PDelayLoadInfo, PVOID, DWORD, HMODULE, samplar_dliFailureHook(), samplar_dliNotifyHook(), samplar_loadCoLocatedOnnx() (+5 more)

### Community 114 - "startBrowserAuthentication"
Cohesion: 0.67
Nodes (3): OAuthCompletionCallback, reapRetiredAuthenticators, startBrowserAuthentication

### Community 115 - "UIState"
Cohesion: 0.11
Nodes (14): String, UIState, currentPage, hasSavedState, libraryFolder, libraryMode, loadedFiles, loadedFolder (+6 more)

### Community 116 - "SegSnap"
Cohesion: 0.14
Nodes (14): SegSnap, attackMs, end, gain, loop, midiNote, pan, pitchSemis (+6 more)

### Community 117 - "Result"
Cohesion: 0.11
Nodes (27): array, AudioBuffer, Result, String, array, Mode, KeyDetector, computeChroma (+19 more)

### Community 118 - "RelinkCheck"
Cohesion: 0.13
Nodes (14): CcBinding, cc, channel, target, String, RelinkCheck, accept, contentMismatch (+6 more)

### Community 119 - "SamplarSettings"
Cohesion: 0.14
Nodes (9): AudioSettingsPanel::AudioSettingsPanel(), AudioSettingsPanel::onAutoRoundBpmToggled(), AudioSettingsPanel::onUseGpuToggled(), mutex, PropertiesFile, unique_ptr, SamplarSettings, mutex_ (+1 more)

### Community 120 - "LicenseSystemTests"
Cohesion: 0.24
Nodes (6): buildLicenseVar(), String, UnitTest, var, LicenseSystemTests, dir_

### Community 121 - "SeparationProbe.cpp"
Cohesion: 0.25
Nodes (13): compare(), ConsoleLogger, quiet, t0, AudioBuffer, Logger, String, loadStems() (+5 more)

### Community 122 - "StateBinding"
Cohesion: 0.15
Nodes (13): registerBinding, restoreAllBindings, Entry, binding, token, function, Identifier, StateBinding (+5 more)

### Community 123 - "DataPaths"
Cohesion: 0.28
Nodes (7): File, resolveModelPath(), DataPaths, sOverrideAppDir, File, getLogsDirectory(), File

### Community 124 - "LicenseSystemService.cpp"
Cohesion: 0.16
Nodes (37): clockSkewTooLarge(), LicenseStatus, String, StringArray, uint64_t, var, extractEventString(), extractIntProperty() (+29 more)

### Community 125 - "File"
Cohesion: 0.25
Nodes (14): File, StemCount, String, ValueTree, acceptRelinkCandidate, beginPendingSession, createSessionTree, readAudioDurationSeconds (+6 more)

### Community 126 - "SessionState"
Cohesion: 0.16
Nodes (12): function, Status, Status, ValueTree, SessionState, clear, completeRestore, message_ (+4 more)

### Community 127 - "MidiDeviceRow"
Cohesion: 0.18
Nodes (11): ComboBox, String, ToggleButton, unique_ptr, MidiDeviceRow, channel, channelCombo, enableBtn (+3 more)

### Community 128 - "paint"
Cohesion: 0.27
Nodes (9): Graphics, MouseEvent, Rectangle, getDotArea, mouseDown, mouseExit, mouseMove, paint (+1 more)

### Community 129 - "StatusSpy"
Cohesion: 0.33
Nodes (6): EventBus, HandlerId, StatusSpy, count, id, last

### Community 130 - "AudioBuffer"
Cohesion: 0.21
Nodes (13): AudioSourceChannelInfo, AudioBuffer, MidiBuffer, shared_ptr, unique_ptr, applyAudioGate, getNextAudioBlock, isAudioAllowedForExport (+5 more)

### Community 131 - "StemSnap"
Cohesion: 0.17
Nodes (13): AudioBuffer, shared_ptr, StemOverride, audio, rate, StemSnap, audible, audio (+5 more)

### Community 132 - "AppStateTests.cpp"
Cohesion: 0.18
Nodes (9): AppState, AppStateTests, StringArray, UnitTest, FakeFeature, bpm, restores, stems (+1 more)

### Community 133 - "Result"
Cohesion: 0.15
Nodes (13): vector, Result, beatCounts, beatsPerBar, beatTimesSec, bpm, cancelled, confidence (+5 more)

### Community 134 - "detect"
Cohesion: 0.24
Nodes (12): beatNumbers(), AudioBuffer, Result, vector, dbToGain(), DownbeatDetector, detect, detectCombFilter (+4 more)

### Community 135 - "LicenseInfo"
Cohesion: 0.15
Nodes (13): string, time_point, vector, LicenseInfo, currentActivations, customerEmail, expiresAt, features (+5 more)

### Community 136 - "handleKeyPress"
Cohesion: 0.27
Nodes (11): KeyPress, drainNotifications, endMidiLearn, handleKeyPress, handleKeyRelease, isOctaveKey, keyToMidiNote, keyToSemitoneOffset (+3 more)

### Community 137 - "GpuTeardownOffload.cpp"
Cohesion: 0.14
Nodes (12): DeletedAtShutdown, DWORD, HMODULE, Image, LPVOID, Holder, keep, ReleaseJob (+4 more)

### Community 138 - "VST3SmokeHost.cpp"
Cohesion: 0.18
Nodes (16): countSegs(), MemoryBlock, unique_ptr, ValueTree, parseState(), rewrap(), SampledStep, sessionOf() (+8 more)

### Community 139 - "RootListener"
Cohesion: 0.27
Nodes (6): isDirtySuppressedOnThisThread, markDirty, ValueTree, RootListener, owner, ValueTree::Listener

### Community 140 - "SlotSnap"
Cohesion: 0.17
Nodes (12): SlotSnap, cellSpan, end, gridCell, pitchSemis, reverse, segIndex, slotIndex (+4 more)

### Community 141 - "SegmentEffects"
Cohesion: 0.10
Nodes (18): Segment, effects, endSample, midiNote, selected, startSample, SegmentEffects, attackMs (+10 more)

### Community 142 - "RecentSongs"
Cohesion: 0.38
Nodes (5): File, String, vector, RecentSongs, kMaxRecents

### Community 143 - "runAutoChop"
Cohesion: 0.22
Nodes (15): Range, AudioBuffer, AutoMode, pair, set, vector, gridCandidates(), pickChops() (+7 more)

### Community 144 - "SettingsButton"
Cohesion: 0.18
Nodes (7): SafePointer, Button, Component, Graphics, SettingsButton, ctx_, dialog_

### Community 145 - "AnalysisCancel"
Cohesion: 0.11
Nodes (15): RunStatus, AnalysisCancel, cancelValue, flag, atomic, atomic, condition_variable, Session (+7 more)

### Community 146 - "SModeService::DecodedStems"
Cohesion: 0.20
Nodes (11): atomic, AudioBuffer, vector, SModeService::DecodedStems, buffers, error, files, ms (+3 more)

### Community 147 - "log"
Cohesion: 0.29
Nodes (11): checkRelinkCandidate(), computeSourceFingerprint(), File, String, vector, describeModel(), log(), selectPlaySettings() (+3 more)

### Community 148 - "PlaySettings"
Cohesion: 0.18
Nodes (11): array, kMaxStems, PlaySettings, ccBindings, dawSync, gate, loopGate, mono (+3 more)

### Community 149 - "Individual Game Analysis"
Cohesion: 0.10
Nodes (20): 1. SnakeGame (Most Advanced), 2. BlockPuzzleGame (Tetris-style), 3. BreakoutGame (Most Polished), 4. Game2048 (Sophisticated Animations), 5. FlappyGame (Asset-Heavy), 6. SpaceShooterGame (Classic Arcade), 7. DummyGame (Minimal/Testing), Asset Management (+12 more)

### Community 150 - "setPlayheadFromMouse"
Cohesion: 0.25
Nodes (11): Graphics, MouseEvent, firstVisibleLane, getWaveColumnInPage, mouseDown, mouseDrag, mouseUp, paint (+3 more)

### Community 151 - "run_all.py"
Cohesion: 0.33
Nodes (10): count_findings(), main(), Path, Emit an interactive, Obsidian-style force-directed graph using vis-network…, SAMPLAR Release Linter - Unified runner. Executes all 8 checks, collects their…, Run a single check and return {status, exit_code, report}., Emit an Obsidian-style vault map as Graphviz DOT., render_dot() (+2 more)

### Community 152 - "OAuthBrowserAuthenticator.cpp"
Cohesion: 0.23
Nodes (18): base64UrlNoPad(), CompletionCallback, string, maskEmail(), cancelAuthentication, exchangeCodeForToken, generateRandomToken, OAuthBrowserAuthenticator::OAuthBrowserAuthenticator() (+10 more)

### Community 153 - "StemPlayParams"
Cohesion: 0.20
Nodes (10): StemPlayParams, atDest, atRange, bendDest, bendRange, operator==, pitch, velCurve (+2 more)

### Community 154 - "operator delete"
Cohesion: 0.36
Nodes (9): align_val_t, nothrow_t, size_t, countedAlignedAlloc(), countedAlignedFree(), countedAlloc(), countedFree(), operator delete() (+1 more)

### Community 155 - "Notification"
Cohesion: 0.22
Nodes (8): NoteType, Notification, d0, f0, i0, i1, i2, type

### Community 156 - "ModState"
Cohesion: 0.29
Nodes (7): ModState, at, bend, sAt, sBend, sWheel, wheel

### Community 157 - "Result"
Cohesion: 0.11
Nodes (19): Config, deviceOverride, drumsOnly, onnxModelFileName, onProgress, outputDirOverride, useStemCache, File (+11 more)

### Community 158 - "GpuAdapterInfo"
Cohesion: 0.22
Nodes (9): GpuAdapterInfo, dedicatedVideoMemory, index, name, uint64_t, wstring, CachedGpuProbe, info (+1 more)

### Community 159 - "setupSlider"
Cohesion: 0.31
Nodes (9): beginGestureIfIdle, onEditBegin, onValuesChanged, pushValues, pushValuesSilently, setFromController, setupSlider, function (+1 more)

### Community 160 - "HtdemucsIstftTests.cpp"
Cohesion: 0.31
Nodes (6): Config, UnitTest, vector, HtdemucsIstftTests, naiveReference(), snrDb()

### Community 161 - "Watchdog"
Cohesion: 0.20
Nodes (7): atomic, thread, Watchdog, deadline_, step_, stop_, thread_

### Community 162 - "createRootComponent"
Cohesion: 0.25
Nodes (7): Page, Component, String, unique_ptr, createRootComponent(), MainWindow::MainWindow(), makeSModePage()

### Community 163 - "LicensingHardeningTests"
Cohesion: 0.17
Nodes (6): FileOutputStream, Logger, FileOnlyLogger, out, UnitTest, LicensingHardeningTests

### Community 164 - "SessionSource"
Cohesion: 0.25
Nodes (8): File, vector, SessionSource, sourceFile, sourceFingerprint, stemCount, stemFiles, uiExtraJson

### Community 165 - "UpdateInfo"
Cohesion: 0.14
Nodes (14): string, vector, LicenseReminder, daysRemaining, message, status, type, UpdateInfo (+6 more)

### Community 166 - "Values"
Cohesion: 0.25
Nodes (8): Values, attackMs, gainDb, pan, pitchSemis, releaseMs, reverse, trigger

### Community 167 - "LaneRescaleReport"
Cohesion: 0.25
Nodes (8): StringArray, LaneRescaleReport, details, emptyMerged, kept, merged, outOfRange, shortened

### Community 168 - ".~StemWaveformComponent"
Cohesion: 0.29
Nodes (7): Colour, onFocusRequested, onLinkToggled, onPitchChanged, onPitchMidiMenu, refreshLinkButton, refreshStemButtons

### Community 169 - "AudioDeviceService.cpp"
Cohesion: 0.28
Nodes (16): ChangeBroadcaster, changeListenerCallback, getAvailableOutputDevices, getCurrentDeviceName, gracefullySwitchDevice, handleDeviceDisconnection, handleDeviceListChange, handleDeviceReconnection (+8 more)

### Community 170 - "OrtRuntime.cpp"
Cohesion: 0.15
Nodes (10): OrtApi, Thread, condition_variable, DWORD, LPVOID, creatorThreadProc(), delayLoadFilter(), guardedGetApi() (+2 more)

### Community 171 - "SModeNoticeBanner"
Cohesion: 0.06
Nodes (24): Kind, SettableTooltipClient, BigPlayButton, playing_, Button, Component, DeletedAtShutdown, function (+16 more)

### Community 172 - "GateRamp"
Cohesion: 0.33
Nodes (4): GateRamp, gain, step, AudioBuffer

### Community 173 - "AuthResult"
Cohesion: 0.12
Nodes (16): AuthResult, accessToken, email, errorMessage, expiresAt, hardwareId, issuedAt, licenseInfo (+8 more)

### Community 174 - "CommandResult"
Cohesion: 0.33
Nodes (6): CommandResult, cancelled, output, started, timedOut, string

### Community 175 - "Job"
Cohesion: 0.18
Nodes (11): Fn, forEachStemForChannel(), DWORD, function, HMODULE, LPVOID, Job, fn (+3 more)

### Community 176 - "SModeSessionTests"
Cohesion: 0.33
Nodes (4): UnitTest, ValueTree, SModeSessionTests, streamRoundTrip()

### Community 177 - "split_htdemucs.py"
Cohesion: 0.47
Nodes (5): build_body(), host_tail(), main(), Build models/htdemucs_body.onnx from models/htdemucs.onnx. Why: the exported…, The C++ tail (OnnxSeparationProvider) in numpy, for verification.

### Community 178 - "ApplyReport"
Cohesion: 0.40
Nodes (5): ApplyReport, applied, clamped, dropped, skipped

### Community 179 - "SeqRenderTarget"
Cohesion: 0.40
Nodes (5): atomic, MidiMappingService, SeqRenderTarget, allMask, svc

### Community 180 - "ScopedStateBinding"
Cohesion: 0.50
Nodes (3): unregisterBinding, ScopedStateBinding, token_

### Community 181 - "SeqEvent"
Cohesion: 0.50
Nodes (4): SeqEvent, lane, offset, slot

### Community 182 - "StdoutLogger"
Cohesion: 0.50
Nodes (3): Logger, String, StdoutLogger

### Community 183 - "uint64_t"
Cohesion: 0.50
Nodes (3): uint64_t, testApplyRevalidation(), testEpoch()

### Community 184 - "suggested_commands.md"
Cohesion: 0.12
Nodes (15): Asset Management, Build Commands (Windows), Build Project, Configure Build, Dependency Verification, Execute Application, File Operations, Git Operations (Standard) (+7 more)

### Community 185 - "RecordingService"
Cohesion: 0.19
Nodes (8): string, UnitTest, vector, Journal, events, RecordingService, name_, ServiceRegistryTests

### Community 186 - "connectToDeviceManager"
Cohesion: 0.50
Nodes (4): AudioDeviceManager, connectToDeviceManager, disconnectFromDeviceManager, shutdown

### Community 187 - "handleSegmentMenuResult"
Cohesion: 0.50
Nodes (4): handleSegmentMenuResult, onMidiAssignRequested, onSegmentDeleted, onSegmentsChanged

### Community 188 - "requestLoadFile"
Cohesion: 0.25
Nodes (9): File, StringArray, fileDragEnter, fileDragExit, filesDropped, isInterestedInFileDrag, onRequestSeparation, requestLoadFile (+1 more)

### Community 189 - "UpdateCheckService.cpp"
Cohesion: 0.24
Nodes (11): EventBus, string, dismissVersion, getHardwareId, initialise, parseResponse, performCheck, shutdown (+3 more)

### Community 190 - "crashHandler"
Cohesion: 0.67
Nodes (3): EXCEPTION_POINTERS, LONG, crashHandler()

### Community 191 - "handleIncomingMidiMessage"
Cohesion: 0.67
Nodes (3): MidiInput, MidiMessage, handleIncomingMidiMessage

### Community 192 - "SettingsDialog::showDialog"
Cohesion: 0.67
Nodes (3): Component, DialogWindow, SettingsDialog::showDialog()

### Community 194 - "Result"
Cohesion: 0.18
Nodes (11): String, vector, Result, beatPositions, bpm, confidence, downbeatConfidence, downbeatIndex (+3 more)

### Community 198 - "Result"
Cohesion: 0.20
Nodes (10): Source, Result, beat1Sample, beatsPerBar, cancelled, confidence, firstDownbeatSample, periodSamples (+2 more)

### Community 199 - "SAMPLAR - Third-Party Notices"
Cohesion: 0.14
Nodes (13): Beat This! (beat tracker), Beat This! model weights (MIT), Demucs / HTDemucs (stem separation model), Demucs (MIT), DirectML, JUCE, Licence texts not present in the source tree, Not shipped (for clarity) (+5 more)

### Community 200 - "code_style_and_conventions.md"
Cohesion: 0.14
Nodes (13): Classes and Structs, Constants and Enums, Documentation Style, Error Handling, File Organization, Game-Specific Patterns, Header Files (.h), IMiniGame Interface Implementation (+5 more)

### Community 201 - "resized"
Cohesion: 0.33
Nodes (9): Rectangle, layoutInspectorRow, layoutSeqHeader, layoutSeqLanes, layoutToolRow, layoutTopBar, resized, showTempoOverflowMenu (+1 more)

### Community 203 - "generate_icon.py"
Cohesion: 0.27
Nodes (9): auto_crop(), main(), Image, Generate SAMPLAR icons from the master logo.png at the repo root. Source:…, Trim borders that are either transparent or near-white., Center the image on a transparent square canvas., Zoom into a squared image by `zoom` (e.g. 1.35) — scale up then center-crop…, square_pad() (+1 more)

### Community 204 - "CLAUDE.md"
Cohesion: 0.20
Nodes (9): Anti-Patterns to Avoid, Build Configuration Options, Code Quality, Event Bus Topics (live), Important Notes, JUCE Framework Skill, Key Integration Points, Project Overview (+1 more)

### Community 205 - "Config"
Cohesion: 0.25
Nodes (8): Config, detectDownbeat, fftSize, hopSize, maxBPM, minBPM, onsetThreshold, preferredBPM

### Community 206 - "NavModel"
Cohesion: 0.29
Nodes (3): ListBoxModel, NavModel, items

### Community 207 - "context-mode — MANDATORY routing rules"
Cohesion: 0.22
Nodes (9): Bash (>20 lines output), context-mode — MANDATORY routing rules, ctx commands, Grep (large results), Output constraints, Read (for analysis), REDIRECTED tools — use sandbox equivalents, Subagent routing (+1 more)

### Community 208 - "mouseUp"
Cohesion: 0.22
Nodes (8): clearSlot, mouseDown, mouseUp, onSlotCopied, onSlotModified, onSlotMoved, onSlotResized, onSpeedChanged

### Community 209 - "Architecture Overview (BIBLE Framework)"
Cohesion: 0.25
Nodes (8): Architecture Overview (BIBLE Framework), Core Components, Core Structure, EventBus (`src/app/EventBus.h`), IService Interface (`src/app/Service.h`), S-Mode data flow, ServiceRegistry (`src/app/ServiceRegistry.h`), Threading rules learned the hard way (see docs/revival/audit_A_realtime.md)

### Community 210 - "SOUL.md"
Cohesion: 0.25
Nodes (7): Chain of Command, Communication, How I Work, Identity, My Purpose, My Vibe, The Monsta Way

### Community 211 - "SAMPLAR Licensing"
Cohesion: 0.22
Nodes (8): Files on disk (`%APPDATA%\Samplar`, or `SAMPLAR_CACHE_DIR_OVERRIDE`), Flow, Logging, Machine release, SAMPLAR Licensing, Signing key, Testing a release build, Update check (`services/UpdateCheckService`)

### Community 212 - "StatusSpy"
Cohesion: 0.33
Nodes (6): EventBus, HandlerId, var, StatusSpy, id, last

### Community 213 - "1.3.0 — 2026-04-24"
Cohesion: 0.29
Nodes (6): 1.3.0 — 2026-04-24, Fixes, Improvements, New, Previous releases, Under the hood

### Community 214 - "MEMORY.md - MonstaForge's Long-Term Memory"
Cohesion: 0.29
Nodes (7): Architecture, Important Patterns, Key Files, Lessons Learned, MEMORY.md - MonstaForge's Long-Term Memory, Project Status, Recent Context

### Community 215 - "SAMPLAR - Project Overview"
Cohesion: 0.29
Nodes (6): Business Model, Key Features, Project Purpose, SAMPLAR - Project Overview, Target Use Case, Technical Innovation

### Community 216 - "SAMPLAR - Technical Stack & Architecture"
Cohesion: 0.29
Nodes (6): Architecture Pattern, Core Technology Stack, Game Architecture, Key Dependencies, SAMPLAR - Technical Stack & Architecture, Service Communication

### Community 217 - "Config"
Cohesion: 0.33
Nodes (6): Config, hopSeconds, maxBPM, minBPM, numLevels, windowSeconds

### Community 218 - "MouseEvent"
Cohesion: 0.29
Nodes (6): MouseEvent, mouseDoubleClick, mouseDrag, mouseExit, mouseMove, onDragStarted

### Community 219 - "Agent Workflow"
Cohesion: 0.33
Nodes (6): Agent Workflow, DUAL-TRACK ARCHITECTURE (CRITICAL), EVIDENCE GATE (MANDATORY - HARD BLOCK), LOG EVIDENCE GATE (MANDATORY FOR BUG FIXES), LOGGING GATE (MANDATORY - HARD BLOCK), Mistakes Documentation

### Community 220 - "JUCE/C++ Coding Standards"
Cohesion: 0.40
Nodes (5): Audio Buffer Initialization, JUCE/C++ Coding Standards, MANDATORY Console Logging, Memory Management, Thread Safety Rules

### Community 222 - "RawMidi"
Cohesion: 0.40
Nodes (4): RawMidi, b0, b1, b2

### Community 223 - "Result"
Cohesion: 0.40
Nodes (5): vector, Result, bpm, confidence, windowBPMs

### Community 224 - "fetch_onnxruntime_directml.py"
Cohesion: 0.60
Nodes (4): copy(), main(), nupkg(), Fetch ONNX Runtime (DirectML build) + DirectML from NuGet into…

### Community 225 - "Agent Instructions"
Cohesion: 0.50
Nodes (3): Agent Instructions, Landing the Plane (Session Completion), Quick Reference

### Community 226 - "BLOCKED commands — do NOT attempt these"
Cohesion: 0.50
Nodes (4): BLOCKED commands — do NOT attempt these, curl / wget — BLOCKED, Inline HTTP — BLOCKED, WebFetch — BLOCKED

### Community 227 - "GenState"
Cohesion: 0.50
Nodes (4): GenState, cursor, ended, wrapped

### Community 229 - "Adding New Features"
Cohesion: 0.67
Nodes (3): Adding a New Service, Adding New Features, Adding UI Components

### Community 230 - "Development Commands"
Cohesion: 0.67
Nodes (3): Building, Development Commands, Running / testing without a DAW

### Community 231 - "Dependencies"
Cohesion: 0.67
Nodes (3): Bundled, Dependencies, Required

### Community 232 - "paint"
Cohesion: 0.67
Nodes (3): Graphics, paint, rebuildWaveformPath

### Community 236 - "addAudioCallback"
Cohesion: 0.67
Nodes (3): AudioIODeviceCallback, addAudioCallback, removeAudioCallback

## Knowledge Gaps
- **1740 isolated node(s):** `serviceRegistry`, `eventBus_`, `earlyLogger_`, `nodeId`, `version` (+1735 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 2610 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **11 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `SModePage` connect `SModePage` to `StemWaveformComponent`, `SModeSegmentModel`, `SModeUiSelfTest`, `LoadAudioOverlay`, `SModePage.cpp`, `SequencerLaneComponent`, `vector`, `runAutoChop`, `WaveDragArea`, `RootComponent`, `setPlayheadFromMouse`, `String`, `Context`, `SModeNoticeBanner`, `SeqRenderTarget`, `ChopInspectorStrip`, `requestLoadFile`, `SModeMidiLearnOverlay`, `resized`?**
  _High betweenness centrality (0.116) - this node is a cross-community bridge._
- **Why does `MidiMappingService` connect `MidiMappingService` to `Snapshot`, `EngineParams`, `PlaybackEngine`, `AudioBuffer`, `IService`, `String`, `handleKeyPress`, `SModeSegmentModel`, `GateRamp`, `vector`, `SlotDesc`, `MidiMappingService.cpp`, `connectToDeviceManager`, `LaneInput`, `handleIncomingMidiMessage`?**
  _High betweenness centrality (0.107) - this node is a cross-community bridge._
- **Why does `PlaybackEngine` connect `PlaybackEngine` to `Snapshot`, `EngineParams`, `GenState`, `MidiMappingService`, `EngineTests.cpp`, `Voice`, `Command`, `vector`, `PlaybackEngine.cpp`, `SeqEvent`, `SpscFifo`, `process`, `Notification`, `ModState`, `applyCommand`, `RawMidi`?**
  _High betweenness centrality (0.077) - this node is a cross-community bridge._
- **What connects `serviceRegistry`, `eventBus_`, `earlyLogger_` to the rest of the system?**
  _1740 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `SModePage` be split into smaller, more focused modules?**
  _Cohesion score 0.014285714285714285 - nodes in this community are weakly interconnected._
- **Should `StemWaveformComponent` be split into smaller, more focused modules?**
  _Cohesion score 0.01886465905157494 - nodes in this community are weakly interconnected._
- **Should `PlaybackEngine` be split into smaller, more focused modules?**
  _Cohesion score 0.01904761904761905 - nodes in this community are weakly interconnected._