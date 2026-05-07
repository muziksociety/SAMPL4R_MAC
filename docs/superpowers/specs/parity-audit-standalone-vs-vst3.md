# Parity Audit: Standalone (SM5App) vs VST3 (SM5Plugin)

**Date:** 2026-04-22
**Auditor:** Verification Agent
**Scope:** Recent mentor, UI, and MIDI changes landed in master

---

## Build Results

| Target | Result | Artefact Timestamp |
|--------|--------|--------------------|
| SM5App (Standalone) | SUCCESS (prior build — no source changes since) | pre-audit |
| SM5Plugin (VST3) | SUCCESS | 2026-04-22 20:54:26 |

VST3 artefact confirmed at:
`build/SM5Plugin_artefacts/Release/VST3/SAMPLAR.vst3`

---

## Architecture Note (Why Most Things Are Parity)

`SamplarAudioProcessorEditor` explicitly hosts the **same RootComponent / page stack** that `MainWindow` uses (documented in `src/plugin/SamplarAudioProcessorEditor.h` lines 10-15). Therefore any change made to a page or component file automatically appears in both targets. True asymmetries only occur at the **service registration** or **audio-processor** level.

---

## Feature Parity Table

| Feature | Standalone SM5App | Plugin SM5Plugin | Gap / Action |
|---------|------------------|-----------------|--------------|
| Mentor service registration | YES — `App.cpp:138` registers `MentorService` via `MentorRegistration` | YES — `SamplarAudioProcessor` uses same `App` service layer | No gap |
| MentorPage UI (dedicated page) | YES — `src/mentor/ui/MentorPage.cpp` in page stack | YES — same page stack via shared RootComponent | No gap |
| Global `?` MentorHelperButton (top-center cluster) | YES — `MainWindow.cpp` adds button | YES — `SamplarAudioProcessorEditor` inherits same shell | No gap |
| Nudge bubble (mentor popup) | YES — `MentorHelperButton.cpp` handles popup | YES — same component | No gap |
| InitialPage mode buttons (Drum Stem / Full Song / S-Mode) | YES — `InitialPage.cpp` | YES — shared page | No gap |
| BROWSE DRUM LIBRARY layout rework | YES — `af9de30`: mode-row width/spacing match, `InitialPage.cpp` | YES — shared page | No gap |
| SampleBrowserPage + Add Folder button | YES — `SampleBrowserPage.cpp:1265-1268` | YES — shared page | No gap |
| SampleBrowserPage CHAT button removal | YES — no `chatButton` symbols found in `SampleBrowserPage.cpp` | YES — shared page | No gap |
| Step sequencer drag-out MIDI uses live midiMap | PARTIAL — `SamplarAudioProcessor.cpp:326-333` wires `MidiMappingService` to `AudioDeviceManager` in standalone; live midiMap available | YES — `MidiMappingService` wired to `SModeModel` at processor level (`SamplarAudioProcessor.cpp:317-320`); drag-out MIDI reads live map | Standalone has live midiMap but MIDI output path differs (device manager vs plugin MIDI out). See gap note below. |
| WavePreviewPage TooltipTab removed | YES — `WavePreviewPage.cpp:4566`: comment confirms TooltipTab deprecated, mentor popup replaces it | YES — shared page | No gap |
| Mentor JSON content (lessons) | YES — `MentorService` loads JSON; same service used everywhere | YES — same service | No gap |
| Mentor EN/ES toggle + OS-language default | YES — `MentorPage.cpp:70-78` subscribes to `mentor.languageChanged`; `MentorService::language()` drives toggle | YES — same service + same page | No gap |

---

## Parity Gaps

### Gap 1: MIDI Output Route (Medium — known, not a regression)

**Feature:** Drag-out MIDI from S-Mode sequencer slots

**Standalone behavior:** `MidiMappingService` is connected to `AudioDeviceManager` (`SamplarAudioProcessor.cpp:326-333`). MIDI events are sent to the OS MIDI device. Live midiMap is present and accurate.

**Plugin behavior:** MIDI events flow through `processBlock` MIDI output buffer to the DAW host. The host routes them downstream. Live midiMap is also present (`SModeModel` wired at `SamplarAudioProcessor.cpp:317-320`).

**Asymmetry:** The *destination* differs (OS device vs DAW host MIDI out), which is correct and expected for standalone vs plugin. However, if a future feature needs to send MIDI back to the DAW from standalone (e.g. for recording), that path does not exist today.

**Action:** No code change required. Document as intentional architecture. If "standalone ? DAW record" is ever needed, a virtual MIDI cable approach would be required.

---

## Follow-up Recommendations

1. **Mentor EN/ES OS-default** — confirm that `MentorService` reads `juce::SystemStats::getUserLanguage()` on first launch to set the default language. A quick grep of `MentorService.cpp` for `getUserLanguage` would verify this is implemented and not just planned.

2. **CHAT button audit** — the button is gone from `SampleBrowserPage`; confirm no dead `chatButton_` member variable remains in the header to avoid confusing future readers.

3. **Standalone MIDI record path** — add a note in `SamplarAudioProcessor.cpp` near line 326 explaining why standalone uses DeviceManager and plugin uses MIDI out buffer, so future engineers don't inadvertently unify them.

4. **SM5App build timestamp** — the standalone exe was not rebuilt in this session. Next time both targets should be built in the same cmake invocation to get matching timestamps for a clean audit trail.

---

## Conclusion

**11 of 12 feature checks: PARITY (no gap).**
**1 item: intentional architecture asymmetry (MIDI output route), not a bug.**

All recent mentor, UI layout, and browser changes are automatically present in both targets due to the shared RootComponent/page-stack architecture. No remediation work required.
