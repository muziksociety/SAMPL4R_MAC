# CLAUDE.md

This file provides guidance to Claude Code when working with code in this repository.

## Project Overview

SAMPLAR is a **C++/JUCE audio application** for drum sample processing and variation generation. It follows the BIBLE architecture framework (see `docs/BIBLE_CPP_CONVERSION.md`) with services, event bus, and clear ownership patterns.

**Tech Stack:**
- **Language**: C++17
- **Framework**: JUCE 7.x
- **Build System**: CMake
- **Platform**: Windows (with potential for cross-platform)

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

### Running
```bash
# Run the standalone application
./build/SM5App_artefacts/Release/Sample\ Monsta\ 5.exe

# Or from build directory
start "build/SM5App_artefacts/Release/SAMPLAR.exe"
```

### Testing
```bash
# Build and run tests (if available)
cmake --build build --target tests
```

## Architecture Overview (BIBLE Framework)

### Core Structure
```
Sample_monsta5/
├── CMakeLists.txt           ← Main build configuration
├── src/
│   ├── app/                 ← Core framework (EventBus, ServiceRegistry, Service interface)
│   │   ├── EventBus.h/.cpp      ← Thread-aware pub/sub messaging
│   │   ├── ServiceRegistry.h/.cpp ← Service lifecycle management
│   │   ├── Service.h            ← IService interface
│   │   └── AudioDeviceService.h ← Audio device management
│   ├── services/            ← Business logic services
│   │   ├── GeneratorService.*       ← Sample variation generation coordinator
│   │   ├── VariationService.*       ← Variation processing
│   │   ├── LicenseSystemService.*   ← License management
│   │   ├── DrumSliceService.*       ← Drum slicing/separation
│   │   ├── FullSongService.*        ← Full song processing
│   │   ├── ProcessingCoordinatorService.* ← Processing coordination
│   │   └── variation/               ← Variation engine implementations
│   │       ├── IVariationEngine.h       ← Engine interface
│   │       ├── engines/                 ← Engine implementations
│   │       │   ├── SimpleVariationEngine.*
│   │       │   ├── CreativeVariationEngine.*
│   │       │   ├── ImpactVariationEngine.*
│   │       │   ├── PowerSynthEngine.*
│   │       │   ├── DDSPNoiseEngine.*
│   │       │   ├── SpectralCloneEngine.*
│   │       │   ├── DirectMorphEngine.*
│   │       │   └── FastSimpleVariationEngine.*
│   │       └── genome/                  ← Genetic variation algorithms
│   ├── ui/                  ← JUCE Component UI layer
│   │   ├── MainWindow.*         ← Top-level window
│   │   ├── Pages/               ← Major UI pages
│   │   ├── Components/          ← Reusable UI components
│   │   ├── WaveEditor/          ← Waveform editing UI
│   │   ├── Debug/               ← Debug console/flags
│   │   ├── Style/               ← LookAndFeel implementations
│   │   ├── Games/               ← Easter egg mini-games
│   │   └── licensing/           ← License UI widgets
│   ├── audio/               ← Audio processing
│   │   └── Preview/             ← Audio preview (SoundTouch)
│   ├── security/            ← License security
│   ├── licensing/           ← Feature management
│   ├── networking/          ← License server client
│   └── utils/               ← Utility classes
├── JUCE/                    ← JUCE framework (vendored)
├── third_party/             ← Third-party dependencies
│   ├── demucs.cpp/              ← Audio source separation
│   └── vcpkg/                   ← Package manager
├── python_bridge/           ← Python IPC bridge (for ML)
└── docs/                    ← Documentation
    └── BIBLE_CPP_CONVERSION.md  ← Architecture reference
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

### Variation Engine Architecture

All variation engines implement `IVariationEngine`:
```cpp
class IVariationEngine {
public:
    virtual juce::AudioBuffer<float> generateVariation(
        const juce::AudioBuffer<float>& source,
        double sampleRate,
        int variationIndex
    ) = 0;

    virtual void setEffectSettings(const samplar::EffectSettingsMap& settings) = 0;
};
```

**Available Engines:**
1. **SimpleVariationEngine** - Basic pitch/time variations
2. **CreativeVariationEngine** - Creative DSP effects
3. **ImpactVariationEngine** - Impact/transient focused
4. **PowerSynthEngine** - Synthesizer-style processing
5. **DDSPNoiseEngine** - DDSP-inspired noise synthesis
6. **SpectralCloneEngine** - Spectral cloning/morphing
7. **DirectMorphEngine** - Direct morphing algorithms
8. **FastSimpleVariationEngine** - Optimized simple engine

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

### Adding a New Variation Engine
1. Create header in `src/services/variation/engines/NewEngine.h`
2. Create implementation in `src/services/variation/engines/NewEngine.cpp`
3. Implement `IVariationEngine` interface
4. Register in `GeneratorService` or engine factory
5. Add to CMakeLists.txt `target_sources`
6. Add to UI engine selection dropdown

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

### Engine Registration Flow
```
CMakeLists.txt (source file)
    ↓
GeneratorService (engine factory)
    ↓
UI Engine Dropdown (user selection)
    ↓
VariationService (processing)
```

### Event Bus Topics
Common event topics used:
- `"ui.navigate"` - Page navigation
- `"processing.start"` - Start processing
- `"processing.complete"` - Processing finished
- `"license.status"` - License status changes

## Dependencies

### Required
- **JUCE 7.x** - UI, audio, threading
- **CMake 3.20+** - Build system
- **C++17 compiler** - MSVC 2022 recommended

### Optional
- **SoundTouch** - Constant-pitch time-stretch (enabled by default)
- **demucs.cpp** - Audio source separation (via CLI)
- **OpenBLAS** - BLAS acceleration for demucs

## Build Configuration Options

```cmake
WITH_SOUNDTOUCH=ON   # Enable SoundTouch (default ON)
WITH_ZEROMQ=OFF      # Enable ZeroMQ IPC (default OFF)
WITH_DEMUCS=OFF      # Enable Demucs library (default OFF - model loading issues)
WITH_DEMUCS_CLI=ON   # Build demucs CLI (default ON)
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
- Test all engines after changes (not just new ones)
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

Issues for MASTERMIND: use the mastermind-relay skill.
