














# 03c – Processing Page (Workflows Hub)

Requires: `Sample_monsta5/docs/refactor_plan/02_EventBus_ServiceRegistry.md`, `Sample_monsta5/docs/refactor_plan/07_Python_IPC_Integration.md`
Provides: parity with SM4 processing workflows (e.g., separation, offline effects, batch operations).

Goals
- Central hub for processing tasks:
  - Source selection (file/folder/drag-drop)
  - Task cards: Separation (Demucs), Batch FX (offline), Detection/Variation/Generation
  - Queue with progress and cancel
  - Preset selection for common pipelines
- IPC-backed separation via `SeparationClientService`
- Offline processing via `OfflineOps` (later)

Deliverables
- `src/ui/Pages/ProcessingPage.h/.cpp`
- Event contracts:
  - `processing.enqueue { op, inputs, options }`
  - `processing.progress { id, pct, msg }`
  - `processing.result { id, outputs }`
  - `processing.error { id, code, msg }`
- Services used:
  - `SeparationClientService` (IPC to Python)
  - `OfflineOpsService` (future C++ offline effects)

Acceptance
- Users can enqueue separation tasks and see progress/results.
- Queue handles multiple items; cancel works; errors surface clearly.
- Navigation to Sample Browser/Wave Editor from outputs works.

