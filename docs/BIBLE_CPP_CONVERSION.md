# SAMPLAR – BIBLE Architecture for C++/JUCE

## Executive Summary
- Recommendation: adopt a native C++/JUCE architecture that preserves the organizational principles of your current BIBLE.md (services, event bus, plugin contracts, clear ownership) while embracing JUCE idioms for UI, audio, and threading.
- Why: it removes cross‑toolkit embedding fragility, improves realtime behavior, and simplifies packaging. It also keeps your team’s mental model (events, services, factories) intact.
- Alternative for near‑term delivery: run heavy Python ML in a companion process (IPC) while JUCE owns the UI/audio. Migrate ML to ONNX/LibTorch later if desired.

## Is This The Right Approach?
- Yes, if your goals are stability, realtime performance, and predictable releases. A single‑stack JUCE app with BIBLE‑style organization gives you clean separation of concerns without the mixed‑framework pitfalls you’ve hit.
- Consider the IPC hybrid if you need Demucs/ML today without a C++ port. You can keep the C++/JUCE core and call Python services out‑of‑process.

Pros
- Realtime correctness: JUCE audio thread stays lock‑free and deterministic.
- Simpler deployment: one C++ app; optional ML companion instead of embedded Python.
- Clear layering: services and contracts mirror your current patterns.

Cons
- Initial re‑platforming effort (UI + service plumbing in C++).
- ML models require IPC or later ONNX/LibTorch conversion.

Decision: proceed with JUCE+BIBLE design; use IPC to integrate Python ML where needed.

---

## Guiding Principles (BIBLE → C++ Mapping)
- Services: long‑lived components with explicit lifecycles (`initialise()`, `shutdown()`), minimal global state.
- Event bus: typed, decoupled pub/sub with delivery guarantees and threading rules.
- Plugins: discoverable modules that register services/factories via a central registry.
- Contracts: JSON/INI schemas and C++ interfaces define inputs/outputs and UI controls.
- Determinism: no blocking or allocations on the audio thread; UI work on the message thread.

## High‑Level Architecture
- App core (`samplar::App`): owns JUCE `JUCEApplication`, service registry, event bus, config, lifecycle.
- Event bus (`samplar::EventBus`): lock‑free single‑producer/single‑consumer queues per topic; marshalling helpers to/from the message thread.
- Services: e.g., `WaveEditorService`, `AudioEngineService`, `PresetService`, `SeparationClientService` (IPC to Python).
- UI: JUCE `Component` tree; parameter controls bound via a thin state layer.
- Audio: dedicated audio engine with a graph (JUCE dsp + custom nodes), hard RT constraints.

## Directory Layout (proposed)
```
Sample_monsta5/
  CMakeLists.txt
  src/
    app/
      App.cpp / App.h                // JUCEApplication + bootstrap
      ServiceRegistry.h              // registration + ownership
      EventBus.h / EventBus.cpp      // pub/sub
      Config.h / Config.cpp          // PropertiesFile / JSON
    ui/
      MainWindow.h/.cpp              // top‑level window
      WaveEditorView.h/.cpp          // editor UI
      Controls/…                     // reusable widgets
    audio/
      Engine.h/.cpp                  // audio device, graph
      Nodes/…                        // compressor, EQ, file source
    services/
      WaveEditorService.h/.cpp
      PresetService.h/.cpp
      SeparationClientService.h/.cpp // Python IPC client
    plugins/
      registry.cpp                   // static plugin registration
  resources/
  third_party/
  docs/
```

## Core Interfaces (sketch)
```cpp
// EventBus.h (thread‑aware pub/sub)
namespace samplar {
struct EventBase { virtual ~EventBase() = default; };
using HandlerId = uint64_t;
class EventBus {
public:
  template <typename E> HandlerId subscribe(std::function<void(const E&)> cb);
  template <typename E> void publish(const E& e);
  void pumpOnMessageThread(); // deliver queued events to message thread handlers
};
}
```
```cpp
// Service.h
namespace samplar {
struct IService { virtual ~IService() = default; virtual void initialise() = 0; virtual void shutdown() = 0; };
}
```
```cpp
// WaveEditorService.h
class WaveEditorService : public samplar::IService {
public:
  void initialise() override; // subscribe to open/edit events
  void shutdown() override;
  std::unique_ptr<juce::Component> createEditor(const AudioBuffer<float>&, double sampleRate);
};
```

## Eventing and Threading
- Message thread: all JUCE UI work. EventBus delivers UI‑targeted events here.
- Audio thread: no locks, no allocations, no bus publish. Use atomics to share state from UI to RT.
- Background threads: file I/O, render/export, ML calls via IPC.
- Delivery semantics: handlers declare the required thread; bus schedules accordingly.

## Audio Engine
- Use JUCE `AudioProcessorGraph` or a custom node chain.
- Parameter model: atomic floats/ints with smoothing; UI binds to parameter tree, engine polls/smoothed on audio thread.
- File I/O: background decode → ring buffer → engine.

## UI Architecture
- DocumentWindow → MainWindow → WaveEditorView.
- Controls: consistent parameter binding helpers; undo/redo; preset serialization.
- Async ops: progress modal for separation/export tasks, cancel support.

## Persistence and Presets
- `PropertiesFile` for app settings; JSON for presets and parameter snapshots.
- Contract versioning: include `schemaVersion` to allow migrations.

## Plugin System
- Static registry: each plugin/service exposes a `registerWith(App&)` function.
- Contracts and factories: services publish available UI factories and operations through the bus.

## Python ML Integration (bridged)
- Separate document details IPC, but C++ side exposes `SeparationClientService` with async API:
  - `requestSeparation(file, model, options) -> future<Result>`
  - Emits progress events; returns stems via temp files or shared memory.

## Build and Tooling
- CMake + JUCE (FetchContent or existing modules).
- Optional components: ONNX Runtime/LibTorch (compile‑time flags); ZeroMQ/gRPC for IPC.
- Continuous Integration: matrix by OS; artifacts per configuration.

## Testing Strategy
- C++: GoogleTest/Catch2 for services, event bus, parameter math.
- Golden audio tests: run offline renders and compare RMS/SNR against fixtures.
- UI smoke tests: plug‑in contract validators, no realtime UI tests on CI.

## Migration Plan (from SM4)
1. Bootstrap app shell (App/MainWindow/EventBus/ServiceRegistry).
2. Port Wave Editor UI + minimal engine (file source → gain → output).
3. Port core effects (Compressor/EQ) and parameters; verify golden outputs.
4. IPC service to Python separation; hook up progress/cancel.
5. Presets and project/session save/restore.
6. Replace remaining legacy integrations; cut over.

## Risks and Mitigations
- ML latency: keep out‑of‑process; use progress + cancel; stream chunks if needed.
- Realtime regressions: strict audio‑thread rules; benchmarks; lock‑order audits.
- Packaging complexity: modularize optional ML; ship CPU‑only by default.

## MANDATORY CONSOLE LOGGING RULE

**ALL code must include comprehensive console logging for debugging and monitoring.**

### Logging Requirements:
- **Every function entry/exit** must be logged with clear identifiers
- **Every major step** within functions must be logged with step numbers
- **All file operations** (read, write, create, delete) must be logged with full paths
- **All external process calls** must log the exact command and arguments
- **All error conditions** must be logged with detailed context
- **All success conditions** must be logged with confirmation messages

### Logging Format Standards:
```cpp
// Function entry
juce::Logger::writeToLog("🎵 [ServiceName] === STARTING FUNCTION_NAME ===");

// Step logging
juce::Logger::writeToLog("🎵 [ServiceName] Step 1: Description of what's happening");

// Success logging
juce::Logger::writeToLog("✅ [ServiceName] Success message with details");

// Error logging
juce::Logger::writeToLog("❌ [ServiceName] Error message with full context");

// File operations
juce::Logger::writeToLog("📁 [ServiceName] File operation: " + file.getFullPathName());

// External processes
juce::Logger::writeToLog("🔧 [ServiceName] Running command: " + command);
for (int i = 0; i < args.size(); ++i) {
    juce::Logger::writeToLog("🔧 [ServiceName]   arg[" + juce::String(i) + "]: '" + args[i] + "'");
}
```

### Emoji Conventions:
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

## Decision Record
- Adopt JUCE‑native app with BIBLE‑style organization.
- Use IPC for Python ML initially; revisit ONNX/LibTorch later.
- **MANDATORY**: All code must include comprehensive console logging as specified above.

