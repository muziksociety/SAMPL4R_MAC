# 02 – Event Bus and Service Registry

Requires: `Sample_monsta5/docs/refactor_plan/01_Core.md`
Provides: foundation for plugin wiring; needed by all UI/Services.

Goals
- Implement a typed event bus with thread-aware delivery (message thread for UI).
- Provide `subscribe<E>(...)`, `publish<E>(...)`, `pumpOnMessageThread()`.
- Define `IService` interface; ensure deterministic init order.

Deliverables
- `src/app/EventBus.h/.cpp` with per-topic queues and handler IDs.
- `src/app/Service.h` base interface + helper macros.
- Logging utilities and assertions for thread context.

Acceptance
- Handlers receive events on the correct thread.
- Services can subscribe during `initialise()` and receive test events.

Links
- Consumed by `03_UI_Shell_and_Navigation.md`, `06_Plugins_Mapping.md`.

