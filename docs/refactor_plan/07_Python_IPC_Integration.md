# 07 – Python IPC Integration (Demucs/Separation)

Requires: `Sample_monsta5/docs/PYTHON_ML_INTEGRATION.md`
Provides: C++ service contract and UI hooks to call Python ML.

Goals
- Implement `SeparationClientService` (C++) with async API and progress events.
- Define request/response messages (JSON/protobuf) and lifecycle (spawn, healthcheck, shutdown).
- UI wiring: export/separate flows in Wave Editor and Browser pages.

Deliverables
- `src/services/SeparationClientService.h/.cpp`
- `src/services/ipc/` transport helpers (ZeroMQ or gRPC)
- UI progress modal and cancellation wiring

Acceptance
- End-to-end separation produces stems and shows progress; errors reported.
- Service survives restarts; handles missing GPU by falling back or messaging.

Links
- Contracts in `Sample_monsta5/docs/PYTHON_ML_INTEGRATION.md`.

