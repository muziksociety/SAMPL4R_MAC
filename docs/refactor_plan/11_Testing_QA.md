# 11 – Testing and QA

Requires: prior modules
Provides: testing plan and acceptance criteria across layers.

Goals
- Unit tests (Catch2/GoogleTest) for services, event bus, parameters, and utility math.
- Golden audio tests: offline renders against fixtures; compare RMS/SNR and tolerances.
- UI smoke: simple launch/navigate/register/destroy tests; no realtime UI in CI.

Deliverables
- `tests/` C++ unit tests; `tests/fixtures/audio/*`.
- Test harness to render graphs offline for golden outputs.

Acceptance
- CI passes unit and golden tests; coverage for effects core.

