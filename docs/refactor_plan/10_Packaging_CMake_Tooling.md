# 10 – Packaging, CMake, and Optional Features

Requires: `Sample_monsta5/docs/refactor_plan/01_Core.md`
Provides: build system, dependencies, feature flags, installers.

Goals
- CMake project with JUCE (FetchContent or submodule). Targets for app and optional components.
- Feature flags: `WITH_ONNX`, `WITH_LIBTORCH`, `WITH_IPC_ZMQ`, `WITH_OPENGL`.
- Windows packaging: installer + portable zip; appdata paths.

Deliverables
- `CMakeLists.txt` root and per-module CMake files.
- `cmake/Find*.cmake` as needed (ONNX, ZMQ, etc.).
- Packaging scripts for Windows (Inno Setup) and macOS/Linux notes.

Acceptance
- Builds on CI matrix; optional features toggled; artifacts produced.

