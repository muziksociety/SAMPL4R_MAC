---
name: sm5-release-linter
description: Run and maintain the SAMPLAR release linter suite — 7 checks that catch commercial-release bugs before shipping AND verify the built exe will run on any end-user's machine. Covers structural drift (source/CMake, engine registry, installer manifest, event bus, service registration) AND runtime portability (DLL dependency walker against the built exe, hardcoded dev-path scanner). Use when the user asks to "lint SAMPLAR", "run release linter", "release check", "check release readiness", "pre-release gate", "verify before ship", "will it work on anyone's computer", "runtime portability check", "DLL check", or mentions orphan files, missing engines, installer drift, dead events, unregistered services, missing DLLs, or dev-path leaks. Also use when triaging findings, adding a new check, or planning a pristine VM test (see references/pristine_vm_test.md).
---

# SAMPLAR Release Linter

Seven Python linters at [tools/release_linter/](tools/release_linter/) that catch structural bugs AND runtime-portability bugs before SAMPLAR ships. All run via one command and exit non-zero on drift.

## Quick start

```bash
python tools/release_linter/run_all.py
```

Emits three reports in `tools/release_linter/reports/`:
- `combined_report.json` — machine-readable, all 5 checks
- `release_linter.dot` — Graphviz DOT vault map (render with `dot -Tsvg`)
- `release_linter.html` — self-contained dashboard, open in browser

Exit codes: `0` clean, `1` drift, `2` tool error.

## The 8 checks

**Structural (1-5):** do all the pieces wire together?

| # | Script | Catches |
|---|---|---|
| 1 | `check_sources.py` | `.cpp` on disk not in `CMakeLists.txt` `target_sources` (silent drop at build) |
| 2 | `check_engines.py` | `IVariationEngine` subclass missing from `GeneratorService` **or** UI surfaces |
| 3 | `check_installer.py` | Runtime resources referenced in code but not shipped by `installer_sm5.iss` |
| 4 | `check_events.py` | EventBus topics with publishers but no subscribers (or vice versa) |
| 5 | `check_services.py` | `IService` subclasses never registered in `ServiceRegistry` |

**Runtime portability (6-7):** will the exe actually run on any Windows machine?

| # | Script | Catches |
|---|---|---|
| 6 | `check_runtime_deps.py` | DLL imports of `SAMPLAR.exe` not shipped by installer AND not a Windows system DLL — the "missing DLL" crash class |
| 7 | `check_hardcoded_paths.py` | Absolute dev-machine paths (`B:\SANTI\`, `C:\Users\Muzik\`) in source or baked into the exe's string table |

**Staging sync (8):** is the portable bundle in sync with the fresh build?

| # | Script | Catches |
|---|---|---|
| 8 | `check_staging_sync.py` | `SAMPLAR_Portable/` binaries stale or missing vs `build/…/Release/` outputs — prevents shipping an installer that contains yesterday's exe |

Checks 6 and 7 **require a build to exist** at `build/SM5App_artefacts/Release/SAMPLAR.exe`. If missing, rebuild first with `cmake --build build --config Release --target SM5App`.

Run one at a time with `python tools/release_linter/check_<name>.py` (add `--json report.json` for machine output).

## Triage decision tree

When a check reports drift, **do not immediately fix or delete**. Triage first:

```
Finding: <file/symbol/topic> is flagged
  │
  ├─ Grep the codebase for external callers/includes/refs
  │    │
  │    ├─ ZERO refs outside the flagged unit
  │    │    → Dead code. Options: delete, or allowlist if kept intentionally
  │    │
  │    └─ HAS external refs
  │         │
  │         ├─ Refs use a different alias (camelCase ↔ snake_case ↔ enum name)
  │         │    → Linter heuristic false positive. Fix the linter's matcher,
  │         │      don't touch the code.
  │         │
  │         ├─ Refs use a string ID the linter didn't scan
  │         │    → Same: extend the linter, not the code.
  │         │
  │         └─ Refs are real and wired correctly
  │              → Investigate why linter still flags. May be an allowlist miss.
```

**Proven examples** (see session memory):
- **9 mini-games in `src/ui/Games/`** — grep showed zero external refs → allowlisted as dead code
- **`DirectMorphEngine`** — grep showed 3 UI hits via snake_case `"direct_morph"` → fixed linter to match stems AND snake_case
- **`audio-separator.exe`** — grep showed it lives under `python/Scripts/*` wildcard → allowlisted (shipped transitively)
- **`demucs_onnx.exe` + 3 onnxruntime DLLs** — grep showed entire ONNX backend code path → real bug, refactored out

## Allowlist files

When a finding is intentional, add it to the matching allowlist with a comment explaining **why**. Allowlists are living documentation of known-good patterns.

| Allowlist | Silences | Format |
|---|---|---|
| `intentional_orphans.txt` | Check #1 orphan `.cpp` files | one repo-relative path per line |
| `intentional_unshipped_resources.txt` | Check #3 missing binaries/dirs | one token per line (basename or dir name) |
| `intentional_orphan_events.txt` | Check #4 published-only/subscribed-only topics | one topic string per line |
| `intentional_unregistered_services.txt` | Check #5 unregistered `IService` subclasses | one class name per line |

**Rule:** every allowlist entry MUST have a `#` comment above it explaining the reason. These files become the go-to reference for why SAMPLAR's architecture looks the way it does.

Check #2 has no allowlist — every variation engine must be wired end-to-end.

## When to run

- **Before every release build** — run as a pre-release gate. Non-zero exit means STOP.
- **After adding a new `.cpp` file** — Check #1 catches missing `CMakeLists.txt` entries
- **After adding a new variation engine** — Check #2 verifies backend + UI wiring
- **After adding a runtime resource** — Check #3 verifies installer coverage
- **After adding a new service or event topic** — Checks #4, #5
- **When triaging a user-reported "feature doesn't work in installer"** — Check #3 is the first stop

## Extending the linter

To add a new check:

1. Create `tools/release_linter/check_<name>.py` following the existing pattern:
   - `analyze()` returns a dict
   - `print_report(report)` for human output
   - `main()` with `--json` and `--quiet` flags
   - Exit code: `0` clean, `1` drift, `2` tool error
2. Add an entry to the `CHECKS` list in `run_all.py`
3. If the new check has allowlist semantics, create `intentional_<name>.txt` alongside the script
4. Run `python tools/release_linter/run_all.py` to verify integration
5. Triage any first-run findings using the decision tree above

## Anti-patterns

- **Do not "fix" a finding by deleting code without triaging first.** The code may be used via an alias the linter didn't catch.
- **Do not add entries to an allowlist without a `# why` comment.** Allowlists are documentation, not a suppression dump.
- **Do not tighten a linter to eliminate a false positive at the cost of missing real drift.** Fix the matcher (add an alias form, snake_case conversion, etc.) rather than narrowing the scan.
- **Do not skip running the linter before a release build.** The whole point is to catch the bugs CMake + the C++ compiler can't see (silent drops, backend/UI drift, installer misses).

## Interpreting the HTML dashboard

`reports/release_linter.html` is an **interactive force-directed graph** (vis-network). Nodes are the root, the 7 checks, and drill-down leaves (engines, services, topics, findings). Green nodes pass, red nodes are drift, purple leaves are individual findings connected by red dashed edges. Drag to pan, scroll to zoom, click any node to focus. Loads vis-network from CDN — requires internet on first open, then cached.

## "Will this work on anyone's computer?" — the full gate

The linter suite answers this question across **two layers**:

**Layer 1 — Static (automatable, Checks #1-#7):**
- Every source file compiles into the shipped exe → Check #1
- Every engine/service/topic/resource is wired end-to-end → Checks #2-#5
- Every DLL the exe imports is shipped or on a fresh Windows install → Check #6
- No dev-machine paths leak into the binary → Check #7

**If all 7 pass, the exe is guaranteed to LOAD on any Windows 10 (1809+) or Windows 11 x64 machine after the installer runs.** That's a strong guarantee, but not a complete one — it doesn't prove the *features* work, just that the exe doesn't crash before `main()`.

**Layer 2 — Runtime (manual, requires a VM):**
For full feature verification, run the pristine VM test documented in [references/pristine_vm_test.md](references/pristine_vm_test.md). It covers the 12 acceptance criteria static analysis can't automate: installer SmartScreen, VC++ redist install, first-run `%APPDATA%` creation, license activation, UVR separation, audio device acquisition, first-run model download, and state persistence across relaunches.

**Recommended release ritual:**
1. Build SM5App (`cmake --build build --config Release --target SM5App`)  ← Phase 3.9 will rename this target
2. Build SM5Plugin (`cmake --build build --config Release --target SM5Plugin`)  ← Phase 3.9 will rename this target
3. Sync staging — copy both fresh artefacts into `SAMPLAR_Portable/` so the
   portable bundle and installer-source match the just-built exe:
   ```bash
   cp "build/SM5App_artefacts/Release/SAMPLAR.exe" "SAMPLAR_Portable/SAMPLAR.exe"
   mkdir -p "SAMPLAR_Portable/VST3/SAMPLAR.vst3/Contents/x86_64-win"
   cp -r "build/SM5Plugin_artefacts/Release/VST3/SAMPLAR.vst3/Contents/x86_64-win/." \
         "SAMPLAR_Portable/VST3/SAMPLAR.vst3/Contents/x86_64-win/"
   ```
   Check #8 (`staging_sync`) will fail the linter otherwise.
4. Run `python tools/release_linter/run_all.py` — must be exit 0
5. Build installer (`iscc installer_sm5.iss`)
6. Run the pristine VM test (reference doc above) on a `fresh-win10-pro` snapshot
7. If all pass, ship. If any fail, fix and repeat.

This skill has no automatic version-string bump — SAMPLAR reads its version from
`CMakeLists.txt` `project(... VERSION ...)`. Bump manually before step 1 if
this is a user-facing release.

Anything less than this ritual means you're shipping on faith.
