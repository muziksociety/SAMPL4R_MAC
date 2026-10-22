# CLAUDE.md

This file provides guidance to Claude Code when working with code in this repository.

## Project Overview

SAMPLAR is a **C++/JUCE VST3 instrument** for sampling songs **by stem**. A song is
split locally into 4 stems (Vocals / Bass / Drums / Melody) with ONNX Runtime
(htdemucs). Each stem is shown as a waveform lane; chops (segments) on a lane act as
MIDI pads, and per-stem sequencer lanes play them back inside the DAW. It follows the
BIBLE architecture (services, event bus, clear ownership; see `docs/BIBLE_CPP_CONVERSION.md`).

Forked from Sample Monsta 5 (`b:/SANTI/SAMPLE_MONSTA/sample_monsta4/Sample_monsta5`) in
May 2026; SM5 keeps fixing the same separation/plugin files, so check it for ports.
Status, audit and roadmap: `docs/revival/REVIVAL_REPORT.md` (+ `audit_A..F_*.md`).

**Tech Stack:**
- **Language**: C++17
- **Framework**: JUCE (out-of-repo at `../JUCE`, i.e. `b:/SANTI/VST PROJECT/JUCE`)
- **Build System**: CMake + Visual Studio 2022
- **ML**: ONNX Runtime (stem separation `htdemucs`, tempo `beat_this`)
- **Platform**: Windows x64 (VST3). `SamplarApp` standalone is a dev-only harness.

## Skills and Documentation

### JUCE Framework Skill
For comprehensive JUCE knowledge (API reference, patterns, examples):
- **Primary:** `docs/JUCE_SKILL.md` - Complete JUCE framework skill generated from SAMPLAR's JUCE installation
- **API Reference:** Detailed class/method documentation with function signatures
- **Design Patterns:** 291 detected patterns from JUCE codebase analysis  
- **Code Examples:** Real usage patterns extracted from JUCE source
- **Audio Processing:** Threading, buffer handling, realtime constraints
- **Windows-Specific:** Platform-specific implementations and optimizations

Use this skill for JUCE-related questions, debugging audio issues, understanding framework patterns, and implementing new audio features.

## Development Commands

### Building
```bash
# Configure (first time or after CMakeLists.txt changes)
cmake -B build -G "Visual Studio 17 2022" -A x64

# Build Release
cmake --build build --config Release

# Build Debug
cmake --build build --config Debug

# Clean build
cmake --build build --config Release --clean-first
```

### Running / testing without a DAW
```bash
# Build the smoke host once (EXCLUDE_FROM_ALL)
cmake --build build --config Release --target VST3SmokeHost --parallel

# Load the built VST3 like a DAW: instantiate, process audio+MIDI, open/close/reopen
# the editor, save state, unload. Exit 0 = all steps passed.
./build/VST3SmokeHost_artefacts/Release/VST3SmokeHost.exe --show-ms 3000
./build/VST3SmokeHost_artefacts/Release/VST3SmokeHost.exe --two-instances
```
- Plugin log (dev build): `build/SamplarPlugin_artefacts/Release/VST3/SAMPLAR.vst3/Contents/x86_64-win/logs/SAMPLAR_console.log`
  (installed under Program Files: `%APPDATA%/Samplar/logs/SAMPLAR_console.log`).
- Git-bash: use `cmake --build ... --parallel`, NOT `-- /m` (the `/m` gets path-mangled).
- Installer: `release_assets/build_installer.ps1` (version comes from `project(samplar VERSION x.y.z)` in CMakeLists.txt - the ONLY place to bump it).

## Architecture Overview (BIBLE Framework)

### Core Structure
```
SAMPLAR/
+-- CMakeLists.txt              <- build; project(VERSION) is the single version source
+-- src/
|   +-- plugin/                 <- SamplarAudioProcessor (owns EventBus + ServiceRegistry), Editor
|   +-- app/                    <- EventBus, ServiceRegistry, ServiceLocator, AppState (DAW state)
|   +-- services/
|   |   +-- smode/              <- SModeService (model + worker pool), SModeSeparationService,
|   |   |                          SModeSegmentModel (stems/segments/undo), PatternGenerator
|   |   +-- separation/         <- OnnxSeparationProvider (htdemucs)
|   |   +-- analysis/           <- HybridTempoDetector (BeatThis + DSP), DownbeatDetector, ...
|   |   +-- FullSongService.*   <- separation engine wrapper (ONNX), despite the SM5 name
|   |   +-- MidiMappingService.*<- MIDI -> voices (audio thread), CC learn, pitch (signalsmith)
|   |   +-- LicenseSystemService.*, UpdateCheckService.*
|   +-- ui/
|   |   +-- MainWindow.cpp      <- RootComponent (plugin editor content) + dev standalone window
|   |   +-- Pages/SModePage.*   <- the whole S-Mode UI (top bar, stem lanes, sequencer)
|   |   +-- Components/smode/   <- StemWaveformComponent, SequencerLane/Slot, MIDI learn overlay
|   |   +-- licensing/          <- sign-in overlay / dialog, status widget
|   |   +-- Style/              <- SamplarLookAndFeel
|   +-- security/, networking/  <- RSA-signed licences, OAuth (PKCE), hardware id
|   +-- utils/                  <- DataPaths (resource/user dirs), OnnxDelayLoad, Tracing, ...
+-- third_party/                <- beat_this, signalsmith-stretch/-linear (onnxruntime via FetchContent)
+-- models/                     <- htdemucs*.onnx (git-ignored, staged into the bundle)
+-- tools/vst3_smoke_host/      <- DAW-less load/unload harness
+-- release_assets/             <- Inno installer + build script
+-- docs/revival/               <- 2026-10 audit + roadmap
```

### Core Components

#### EventBus (`src/app/EventBus.h`)
Thread-aware pub/sub messaging system:
```cpp
namespace samplar {
class EventBus {
    HandlerId subscribe(const std::string& topic, Handler h);
    void unsubscribe(const std::string& topic, HandlerId id);
    void publish(const std::string& topic, const juce::var& payload = {});
};
}
```

#### ServiceRegistry (`src/app/ServiceRegistry.h`)
Service lifecycle management:
```cpp
namespace samplar {
class ServiceRegistry {
    void registerService(const std::string& name, std::unique_ptr<IService> svc);
    IService* get(const std::string& name) const;
    void initialiseAll();
    void shutdownAll();
};
}
```

#### IService Interface (`src/app/Service.h`)
All services implement:
```cpp
namespace samplar {
struct IService {
    virtual ~IService() = default;
    virtual void initialise() = 0;
    virtual void shutdown() = 0;
};
}
```

### S-Mode data flow
```
Load song (SModePage / drag-drop)
  -> SModeService::startWorkflow -> SModeSeparationService (joinable worker thread)
     -> FullSongService -> OnnxSeparationProvider (htdemucs, 4 stems)
  -> "smode.separation.complete" -> SModeService loads stems into SModeSegmentModel
  -> "smode.workflow.ready" -> SModePage builds stem lanes, runs BPM/downbeat on the
     SModeService worker pool
MIDI note (audio thread) -> MidiMappingService -> segment voices (signalsmith pitch)
```

### Threading rules learned the hard way (see docs/revival/audit_A_realtime.md)
- No detached threads. Heavy UI-triggered jobs go through `SModeService::runBackground()`
  (pool drained on shutdown); copy audio on the message thread first - never hold raw
  pointers into `SModeSegmentModel` (a new song clears it).
- Every `MessageManager::callAsync` / `Timer::callAfterDelay` lambda that touches `this`
  needs a `Component::SafePointer` or a `shared_ptr<atomic<bool>>` alive token.
- Results of async analysis must check `model.getContentGeneration()` before applying.
- MSVC: do not write `[safe = SafePointer<X>(this)]` init-captures inside a nested lambda;
  hoist the SafePointer to a local first.
- Services are initialised in registration order and shut down in reverse.

## JUCE/C++ Coding Standards

### Thread Safety Rules
```cpp
// Audio thread (processBlock) - REALTIME SAFE ONLY
void processBlock(juce::AudioBuffer<float>& buffer) {
    // SAFE: Read atomic parameters
    float value = paramValue.load();

    // SAFE: Process audio
    buffer.applyGain(value);

    // FORBIDDEN: Allocations, locks, UI updates, file I/O
}

// UI thread (message thread) - All UI work here
void buttonClicked() {
    // SAFE: Update UI, change parameters
    label.setText("Clicked");
}

// Background threads - Heavy processing
void generateVariationsAsync() {
    // SAFE: Heavy computation, file I/O
    // Use AsyncUpdater to notify UI when done
}
```

### Memory Management
```cpp
// ALWAYS use std::unique_ptr for ownership
std::unique_ptr<juce::Slider> slider = std::make_unique<juce::Slider>();

// NEVER use raw pointers for owned objects
// juce::Slider* slider = new juce::Slider();  // BAD
```

### Audio Buffer Initialization
```cpp
// ALWAYS clear buffers before use
juce::AudioBuffer<float> buffer(numChannels, numSamples);
buffer.clear();  // CRITICAL: Initialize to zero

// ALWAYS check buffer dimensions
if (buffer.getNumSamples() == 0 || buffer.getNumChannels() == 0)
    return;
```

### MANDATORY Console Logging
All code must include comprehensive logging (see `docs/BIBLE_CPP_CONVERSION.md`):

```cpp
// Function entry
juce::Logger::writeToLog("🎵 [ServiceName] === STARTING FUNCTION_NAME ===");

// Step logging
juce::Logger::writeToLog("🎵 [ServiceName] Step 1: Description");

// Success
juce::Logger::writeToLog("✅ [ServiceName] Success message");

// Error
juce::Logger::writeToLog("❌ [ServiceName] Error message with context");

// File operations
juce::Logger::writeToLog("📁 [ServiceName] File: " + file.getFullPathName());
```

**Emoji Conventions:**
- 🎵 General operations
- 🐍 Python bridge operations
- ⚡ C++ operations
- 🔧 External tools/processes
- 📁 File operations
- ✅ Success states
- ❌ Error states
- 🔄 Loading/processing states
- 💾 Save operations
- 🎯 Target/goal states

## Adding New Features

### Adding a New Service
1. Create class inheriting from `samplar::IService`
2. Implement `initialise()` and `shutdown()`
3. Register with `ServiceRegistry`
4. Use `EventBus` for inter-service communication

### Adding UI Components
1. Create Component class in `src/ui/Components/`
2. Implement `resized()` and `paint()`
3. Use `addAndMakeVisible()` in parent
4. Follow existing LookAndFeel patterns in `src/ui/Style/`

## Key Integration Points

### Event Bus Topics (live)
- `"smode.separation.progress" / ".complete" / ".error"` - separation worker
- `"smode.workflow.ready" / ".error"` - stems loaded into the model
- `"smode.segment.triggered" / ".stopped"`, `"smode.transport.stopped"` - playback -> UI
- `"smode.downbeat.detected"`, `"host.tempo"` / `"host.tempo.request"` - tempo
- `"state.restored"` - DAW project reload
- `"update.available"`, `"license.status.reminder"`, `"license.status"` - account/updates

## Dependencies

### Required
- **JUCE 7.x** - UI, audio, threading
- **CMake 3.20+** - Build system
- **C++17 compiler** - MSVC 2022 recommended

### Bundled
- **ONNX Runtime** - stem separation + Beat This! tempo (delay-loaded, see `src/utils/OnnxDelayLoad.cpp`)
- **signalsmith-stretch** - realtime per-stem / per-voice pitch shifting

## Build Configuration Options

```cmake
WITH_BEAT_THIS=ON               # Beat This! neural tempo (needs third_party/beat_this/models/beat_this.onnx)
WITH_SIGNALSMITH_STRETCH=ON     # realtime pitch shifter
SAMPLAR_BUILD_STANDALONE=OFF    # dev-only SamplarApp
SAMPLAR_BUILD_TESTS=OFF         # SamplarTests console runner
SAMPLAR_ENABLE_TRACING=OFF      # Chrome-trace profiler
SAMPLAR_ENABLE_DEV_FALLBACK=OFF # never ON for release builds
```

## Important Notes

### Anti-Patterns to Avoid
1. **Blocking audio thread** - No locks, allocations, or I/O
2. **Direct service imports** - Use EventBus or ServiceRegistry.get()
3. **Raw pointers for ownership** - Always use smart pointers
4. **Uninitialized buffers** - Always call buffer.clear()
5. **Missing CMakeLists.txt entries** - New .cpp files must be added

### Code Quality
- Keep changes minimal and surgical
- Follow existing code patterns
- After any lifecycle/threading change run VST3SmokeHost (single + `--two-instances`)
- Verify CMakeLists.txt includes new source files before building

## Agent Workflow

This project uses specialized Claude agents for development (see `.claude/agents/`):
- **orchestrator** - Coordinates all agents, enforces workflow gates
- **evidence-gatherer** - Investigates codebase, finds patterns
- **frontend-architect** - UI/UX design with ASCII mockups
- **architect** - Backend architecture (services, engines, data flow)
- **risk-analyst** - Identifies risks for both frontend and backend
- **implementation** - Writes code
- **verification** - Tests all engines
- **documentation** - Creates devlogs
- **mistake-learning** - Documents lessons learned

Use `/orchestrate` command to start agent workflow for complex features.

### EVIDENCE GATE (MANDATORY - HARD BLOCK)

```
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
!!  NO PLANNING WITHOUT EVIDENCE. NO EXCEPTIONS. EVER.        !!
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
```

**What**: A hard blocker that prevents ANY design/planning work until verified evidence exists.

**Why**: Agents repeatedly made plans based on assumptions instead of reading actual code. This led to failed implementations.

**Required BEFORE design phase:**
1. `evidence-report.json` with verified findings (actual file:line, actual code)
2. `integration-checklist.json` with traced code paths

**Evidence Quality Standards:**
- GOOD: `"file": "src/ui/Pages/WavePreviewPage.cpp", "line": 234, "verified": true`
- BAD: `"file": "probably WavePreviewPage.cpp", "line": "unknown", "verified": false`

**Enforcement:**
- Orchestrator HARD BLOCKS design phase without verified evidence
- All planning agents self-enforce evidence checks
- Implementation agent verifies file:line before each edit

See `mistakes.md` Mistake 2 for full details.

---

### LOGGING GATE (MANDATORY - HARD BLOCK)

```
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
!!  NO IMPLEMENTATION WITHOUT LOGGING. LOGGING IS NOT OPTIONAL.!!
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
```

**What**: Every implementation MUST include comprehensive console logging.

**Why**: Code without logging cannot be debugged. Logging is the PRIMARY source of evidence for bug fixes.

**Required in every function:**
1. Function entry with all parameters
2. Decision points (which branch taken)
3. Loop progress (for non-trivial loops)
4. Error conditions with context
5. State changes (before/after)
6. Function exit with result

**Log Format:**
```cpp
juce::Logger::writeToLog("[ComponentName] === functionName START ===");
juce::Logger::writeToLog("[ComponentName]   param: " + juce::String(value));
juce::Logger::writeToLog("[ComponentName] === functionName END ===");
```

See `mistakes.md` Mistake 3 for full details.

---

### LOG EVIDENCE GATE (MANDATORY FOR BUG FIXES)

```
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
!!  NO BUG FIX WITHOUT LOG EVIDENCE. GUESSING IS FORBIDDEN.   !!
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
```

**What**: Bug fixes MUST be based on actual console log evidence, not guesses.

**Why**: Static code analysis shows what code DOES, not what it does AT RUNTIME. Guessing has ~20% success rate. Log-based debugging has ~90%+ success rate.

**The Debugging Workflow:**
```
BUG REPORT
    |
    v
CHECK FOR LOGS --> [No Logs?] --> Add Diagnostic Logging
    |                                     |
    v                                     v
[Logs Exist]                    User Reproduces Bug
    |                                     |
    +----------> ANALYZE LOGS <-----------+
                      |
                      v
              90%+ CERTAINTY?
                   /    \
                 NO      YES
                 |        |
                 v        v
         Get More    Design Fix
           Logs

```

**Required for bug fixes:**
1. Log evidence showing the bug occurring
2. Analysis of WHERE/WHAT/WHY from logs
3. 90%+ certainty of root cause before fix design
4. If no logs: `diagnostic-logging-plan.json` first

See `mistakes.md` Mistake 3 for full details.

---

### DUAL-TRACK ARCHITECTURE (CRITICAL)

For features with BOTH UI and backend components, we use dual-track design:

```
                    INVESTIGATION PHASE
                           |
                           v
        +------------------+------------------+
        |                  |                  |
        v                  v                  v
  evidence-gatherer  integration-supervisor  audio-research
        |                  |                  |
        +------------------+------------------+
                           |
                           v
                    DESIGN PHASE (PARALLEL)
        +------------------+------------------+
        |                  |                  |
        v                  v                  v
  frontend-architect    architect        risk-analyst
  (UI/UX design)     (backend design)   (risk analysis)
        |                  |                  |
        +------------------+------------------+
                           |
                           v
                  USER APPROVAL REQUIRED
                           |
                           v
                  IMPLEMENTATION PHASE
```

**Frontend Track Delivers:**
- ASCII mockups (BEFORE and AFTER states)
- Component hierarchy diagram
- User interaction flow (step-by-step)
- UI files to modify/create
- Integration points with backend

**Backend Track Delivers:**
- Service architecture
- Engine implementations
- Data models and flow
- APVTS parameters
- Thread safety considerations
- Integration points for UI

**Why Dual-Track?**
Combined frontend/backend designs consistently fell short on one side. See `mistakes.md` for details.

### Mistakes Documentation

All agents MUST read `mistakes.md` before starting work. It contains:
- Documented failures and root causes
- Prevention strategies
- Checklist for new implementations
- Success patterns to follow

**Key Lesson**: NEVER implement features with UI components without frontend-design.json containing ASCII mockups.

# context-mode — MANDATORY routing rules

You have context-mode MCP tools available. These rules are NOT optional — they protect your context window from flooding. A single unrouted command can dump 56 KB into context and waste the entire session.

## BLOCKED commands — do NOT attempt these

### curl / wget — BLOCKED
Any Bash command containing `curl` or `wget` is intercepted and replaced with an error message. Do NOT retry.
Instead use:
- `ctx_fetch_and_index(url, source)` to fetch and index web pages
- `ctx_execute(language: "javascript", code: "const r = await fetch(...)")` to run HTTP calls in sandbox

### Inline HTTP — BLOCKED
Any Bash command containing `fetch('http`, `requests.get(`, `requests.post(`, `http.get(`, or `http.request(` is intercepted and replaced with an error message. Do NOT retry with Bash.
Instead use:
- `ctx_execute(language, code)` to run HTTP calls in sandbox — only stdout enters context

### WebFetch — BLOCKED
WebFetch calls are denied entirely. The URL is extracted and you are told to use `ctx_fetch_and_index` instead.
Instead use:
- `ctx_fetch_and_index(url, source)` then `ctx_search(queries)` to query the indexed content

## REDIRECTED tools — use sandbox equivalents

### Bash (>20 lines output)
Bash is ONLY for: `git`, `mkdir`, `rm`, `mv`, `cd`, `ls`, `npm install`, `pip install`, and other short-output commands.
For everything else, use:
- `ctx_batch_execute(commands, queries)` — run multiple commands + search in ONE call
- `ctx_execute(language: "shell", code: "...")` — run in sandbox, only stdout enters context

### Read (for analysis)
If you are reading a file to **Edit** it → Read is correct (Edit needs content in context).
If you are reading to **analyze, explore, or summarize** → use `ctx_execute_file(path, language, code)` instead. Only your printed summary enters context. The raw file content stays in the sandbox.

### Grep (large results)
Grep results can flood context. Use `ctx_execute(language: "shell", code: "grep ...")` to run searches in sandbox. Only your printed summary enters context.

## Tool selection hierarchy

1. **GATHER**: `ctx_batch_execute(commands, queries)` — Primary tool. Runs all commands, auto-indexes output, returns search results. ONE call replaces 30+ individual calls.
2. **FOLLOW-UP**: `ctx_search(queries: ["q1", "q2", ...])` — Query indexed content. Pass ALL questions as array in ONE call.
3. **PROCESSING**: `ctx_execute(language, code)` | `ctx_execute_file(path, language, code)` — Sandbox execution. Only stdout enters context.
4. **WEB**: `ctx_fetch_and_index(url, source)` then `ctx_search(queries)` — Fetch, chunk, index, query. Raw HTML never enters context.
5. **INDEX**: `ctx_index(content, source)` — Store content in FTS5 knowledge base for later search.

## Subagent routing

When spawning subagents (Agent/Task tool), the routing block is automatically injected into their prompt. Bash-type subagents are upgraded to general-purpose so they have access to MCP tools. You do NOT need to manually instruct subagents about context-mode.

## Output constraints

- Keep responses under 500 words.
- Write artifacts (code, configs, PRDs) to FILES — never return them as inline text. Return only: file path + 1-line description.
- When indexing content, use descriptive source labels so others can `ctx_search(source: "label")` later.

## ctx commands

| Command | Action |
|---------|--------|
| `ctx stats` | Call the `ctx_stats` MCP tool and display the full output verbatim |
| `ctx doctor` | Call the `ctx_doctor` MCP tool, run the returned shell command, display as checklist |
| `ctx upgrade` | Call the `ctx_upgrade` MCP tool, run the returned shell command, display as checklist |
