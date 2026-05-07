# SAMPLAR — Extraction Design

**Date:** 2026-05-03
**Status:** all 7 sections approved by user; ready for implementation planning
**Source project:** SAMPLAR (`b:\SANTI\SAMPLE_MONSTA\sample_monsta4\Sample_monsta5`)
**Target project:** SAMPLAR (`b:\SANTI\VST PROJECT\SAMPLAR`)
**Target version:** v0.1.0 (new product, fresh version line)
**Strategy:** Fork-and-strip from SAMPLAR v1.3.0

---

## Section 1 — Architecture

See conversation. Repo at `b:\SANTI\VST PROJECT\SAMPLAR`. Git fork from SAMPLAR v1.3.0 with optional history orphan. Output binaries `SAMPLAR.exe` (standalone) + `SAMPLAR.vst3`. Same JUCE/CMake structure. Identity rename: `samplar::` ? `samplar::`, `SAMPLAR` ? `SAMPLAR`, bundle ID `com.muziksociety.samplar`, license SKU `samplar`, AppData `%APPDATA%\Samplar\`.

---

## Section 2 — KEEP (the surface area SAMPLAR needs)

### Core infrastructure (community 0 in graphify)
- `src/app/`: App, EventBus, ServiceRegistry, ServiceLocator, AudioDeviceService, Service.h, SamplarVersion.h
- `src/plugin/`: SamplarAudioProcessor + Editor (rename to SamplarAudioProcessor)
- `src/utils/`: DataPaths, LogPaths, Tracing, FontWarmer, LicenseFeatureFlags

### S-mode core
- `src/services/smode/` — entire folder
- `src/ui/Pages/SModePage.*` — becomes the home page
- `src/ui/Components/smode/` — entire folder (SequencerSlotComponent, SModeMidiLearnOverlay, etc.)

### MIDI integration
- `src/services/MidiMappingService.*` — kept whole. S-mode-specific code stays in. SAMPLAR-specific bits (e.g. browser auto-map) become dead code that we either prune in pass 2 or leave for `MidiMappingService::autoMapBrowserPage()` to be removed.

### Audio separation (UVR / demucs)
- `src/services/FullSongSeparationService.*`
- `src/services/FullSongService.*` (Python orchestration)
- `src/services/ProcessingCoordinatorService.*` if used by separation flow
- `third_party/demucs.cpp/` — kept
- `python_bridge/` — kept

### License stack
- `src/services/LicenseSystemService.*`
- `src/security/` (LicenseCache, HardwareFingerprintGenerator)
- `src/networking/` (LicenseServer client, OAuthBrowserAuthenticator)
- `src/ui/licensing/` (LicenseActivationDialog and related)

### Audio playback (S-mode needs)
- `src/audio/SamplePreviewCache.*`
- `src/audio/SequencerAudioSource.*`
- `src/audio/PluginPreviewService.*`
- `src/audio/Preview/SoundTouchSource.*` (used by previews)
- `src/audio/MicroFade.*`

### UI shell
- `src/ui/MainWindow.*` — kept, gutted of non-SAMPLAR pages
- `src/ui/Style/` — LookAndFeel
- `src/ui/Components/SettingsDialog.*` — kept (settings page is universal)
- Update-check button + dialog (kept — user wants update notifications)

### Build infrastructure
- `CMakeLists.txt` — kept, edited heavily
- `JUCE/` — wholesale copy
- `third_party/vcpkg/` — kept
- `installer_sm5.iss` ? `installer_samplar.iss` — adapted
- `000portable.ps1`, `001installer.ps1` — adapted

---

## Section 3 — DELETE (the strip pass)

### Variation engines (entire feature gone)
- `src/services/variation/` — entire folder
- `src/services/GeneratorService.*`, `VariationService.*`
- `src/ui/Variation/` — entire folder (VariationExpansionGrid etc.)
- `src/ui/Pages/EngineLabPage.*`

### Sample browser (entire feature gone — user explicitly said "no SampleBrowser")
- `src/ui/Pages/SampleBrowserPage.*`
- `src/ui/Components/FolderTreeItem.*`
- `src/utils/CustomCategoryManager.*`
- `src/ui/Components/CategoryEditorDialog.*`
- `src/utils/UserPatternDatabase.*`, `RhythmicPatternDatabase.*`, `PatternLibrary.*`, `MultiBandPatternExtractor.*`, `DrumMapping.*`
- `src/ui/Components/PatternPreviewDialog.*`, `PatternLibraryDialog.*`

### Wave editor / pre-slice / step sequencer
- `src/ui/WaveEditor/` — entire folder
- `src/ui/Pages/WavePreviewPage.*`
- `src/ui/Components/StepSequencerComponent.*`
- `src/services/BeatAnalysisService.*`
- `src/services/PreSliceProcessingService.*`
- `src/services/DrumSliceService.*`
- `src/utils/DrumAnalyzer.*`
- `src/utils/ProcessingPresetManager.*`
- `src/ui/Components/EqDividerComponent.*`, `BeatGridOverlay.*`, `ThresholdLineOverlay.*`, `DraggableMarker.*`, `WaveformZoomController.*`, `TooltipOverlay.*` (only if not used by S-mode UI — verify in pass 2)
- `src/ui/Components/SpectralSeparateDialog.*`

### ML classifier (drum auto-classification — not needed for S-mode)
- `src/utils/ml/DrumMLClassifier.*`

### Mentor system (tutorials/guidance — not needed for v1)
- `src/mentor/` — entire folder
- `src/ui/Pages/ProcessingVisualizationDemo.*`

### Games / easter eggs
- `src/ui/Games/` — entire folder

### Data files (only S-mode-relevant kept)
- `data/patterns_with_rolls.json` (53 MB) — DELETE
- `data/latin_patterns.json` (34 KB) — DELETE
- Pattern databases not needed without sample browser

### Models (only separation models kept)
- Keep: `htdemucs_ft` weights (separation)
- Delete: any drumsep, drum-classifier, or other model files SAMPLAR ships

### Background assets
- `BACKGROUNDS/` — keep only the GIFs/PNGs that SModePage actually references; drop the rest (audit in pass 2)

### Samples folder
- `Samples/808 kit/` — DELETE (sample browser only)
- `Samples/GM/` — DELETE
- Keep none unless SModePage has built-in demo content

---

## Section 4 — Identity rename pass

Done as a single mechanical pass after the strip in Section 3 completes. Order matters: do file renames LAST so git can track them.

### 4.1 String/token replacements (project-wide find-and-replace)

| From | To | Scope |
|---|---|---|
| `namespace samplar` | `namespace samplar` | All `.h`/`.cpp` in `src/` |
| `samplar::` | `samplar::` | All `.h`/`.cpp` in `src/` |
| `SAMPLAR_VERSION` | `SAMPLAR_VERSION` | macros |
| `SAMPLAR_TRACE_*` | `SAMPLAR_TRACE_*` | tracing macros |
| `SAMPLAR_DEV_RESOURCE_PATH` | `SAMPLAR_DEV_RESOURCE_PATH` | CMake flag |
| `SAMPLAR_ENABLE_DEV_FALLBACK` | `SAMPLAR_ENABLE_DEV_FALLBACK` | CMake flag |
| `[SAMPLAR...]` | `[SAMPLAR...]` | log prefixes |
| `SAMPLAR` | `SAMPLAR` | user-visible strings |
| `SAMPLAR` | `SAMPLAR` | user-visible strings |
| `Samplar` (path token) | `Samplar` | AppData folders, log dirs |
| `samplar` | `samplar` | bundle IDs, license SKUs |
| `MUZIKSOCIETY/SAMPLAR` | `MUZIKSOCIETY/SAMPLAR` | registry HKCU/HKLM keys |

Run the replacement in this order to avoid partial-substring collisions:
1. `SAMPLAR` first (most specific)
2. `Samplar` (path tokens — must not match `samplar` URL)
3. `SAMPLAR` (less specific)
4. `samplar` (lowercase)
5. `SAMPLAR_` macro prefix
6. `samplar::` namespace
7. `SAMPLAR` standalone (verify each match — risk of false positives in comments)

### 4.2 File renames (last — preserves git history)

| Old | New |
|---|---|
| `src/plugin/SamplarAudioProcessor.{h,cpp}` | `src/plugin/SamplarAudioProcessor.{h,cpp}` |
| `src/plugin/SamplarAudioProcessorEditor.{h,cpp}` | `src/plugin/SamplarAudioProcessorEditor.{h,cpp}` |
| `src/app/SamplarVersion.h.in` | `src/app/SamplarVersion.h.in` |
| `installer_sm5.iss` | `installer_samplar.iss` |
| `SAMPLAR_Portable/` (in stripped repo, before any build) | `SAMPLAR_Portable/` |

CMake target names (in `CMakeLists.txt`):
- `SM5App` ? `SamplarApp`
- `SM5Plugin` ? `SamplarPlugin`
- `SM5Resources` ? `SamplarResources`
- `SM5Tests` ? `SamplarTests` (if kept; can drop tests for v0.1)
- `SM5LicenseCleaner` ? `SamplarLicenseCleaner`

### 4.3 JUCE manifest values (in CMakeLists.txt)

```cmake
juce_add_gui_app(SamplarApp
    PRODUCT_NAME "SAMPLAR"
    COMPANY_NAME "Muziksociety"
    COMPANY_WEBSITE "https://samplar.com"      # or samplemonsta.com/samplar
    COMPANY_EMAIL "muziksociety@gmail.com"
    BUNDLE_ID "com.muziksociety.samplar"
    VERSION "0.1.0"                             # reset version - this is a new product
    ICON_BIG "assets/samplar_icon_512.png"      # NEW icon - placeholder for now
    ICON_SMALL "assets/samplar_icon_64.png"
)

juce_add_plugin(SamplarPlugin
    COMPANY_NAME "Muziksociety"
    BUNDLE_ID "com.muziksociety.samplar.vst3"
    PLUGIN_MANUFACTURER_CODE Mzks
    PLUGIN_CODE Smpl                            # 4-char VST3 plugin code (must be unique vs SAMPLAR)
    FORMATS VST3 Standalone
    PRODUCT_NAME "SAMPLAR"
)
```

### 4.4 What stays untouched

- `JUCE/` — vendored framework, no renames
- `third_party/` — external deps, no renames
- Internal C++ class names that don't carry "SAMPLAR" prefix (e.g. `SModeService`, `MidiMappingService`, `App`, `EventBus`) — these stay; their namespace just changes from `samplar::` to `samplar::`
- The `SModeService` class name itself stays (it's the core feature, not branded)

### 4.5 Icon + branding (deferred to post-v0.1)

- Drop a placeholder icon initially (a colored square works)
- Real icon design happens later — not blocking the build
- Splash/loading assets reused as-is from SAMPLAR for dev; replaced before any release

## Section 5 — License server new SKU

The existing `license_server` (at `B:\SANTI\license_server`, deployed on Render at `samplemonsta.com`) is multi-product capable — every endpoint already takes a `product` parameter. Adding SAMPLAR is configuration, not code surgery.

### 5.1 License server changes (in `license_server` repo)

| Change | File | Action |
|---|---|---|
| Register product | `data/products.json` (or equivalent registry) | Add entry: `{"id": "samplar", "name": "SAMPLAR", "active": true}` |
| Releases registry | `data/releases.json` | Add `samplar` block with empty `releases: []` and `latest_version: "0.0.0"` (populated on first SAMPLAR release) |
| Activation endpoint | `plugins/license_activation/` | Already accepts `product` query param — verify it routes `samplar` correctly. Likely no code change. |
| Update check endpoint | `plugins/update_checker/` | Already filters releases by `product` — no code change. |
| User portal (dashboard) | `plugins/user_portal/templates/dashboard.html` | Add SAMPLAR section: license activation card + download buttons (deferred until first release) |
| Stripe / payment SKU | `data/stripe_products.json` (or similar) | Add SAMPLAR Stripe product. **DEFERRED** — pre-launch concern, not pre-build. |

**Verification command after server change:**
```bash
curl "https://samplemonsta.com/api/update/check?current_version=0.0.0&product=samplar"
# Expected: {"update_available": false, "latest_version": "0.0.0"}  (or similar)
```

### 5.2 SAMPLAR repo changes

| Component | What changes |
|---|---|
| `LicenseSystemService` constructor / config | Hard-coded product key changes from `"sample_monsta"` to `"samplar"`. Find the constant (likely a `kProductKey` static or a constructor arg) and update. |
| `UpdateCheckService` | Same — change product key from `sample_monsta` ? `samplar`. |
| `LicenseFeatureFlags` | Replace SAMPLAR's feature flag enum/map with SAMPLAR's. **v0.1 has zero feature flags** — license is binary (activated / not). Variation/engine flags from SAMPLAR are deleted. |
| `LicenseActivationDialog` UI strings | "Activate SAMPLAR" ? "Activate SAMPLAR". License agreement / EULA text — needs SAMPLAR-specific version. Use SAMPLAR's as a starting template. |
| Hardware fingerprint generator | **No change** — same algorithm, same hash. A user on the same machine will get the same fingerprint for SAMPLAR and SAMPLAR; the server distinguishes by product key. |
| OAuth flow | **No change** — same OAuth provider, same client. Only the `product` parameter in the redirect URL differs. |
| License cache file | Change cached path from `%APPDATA%\Samplar\license.json` ? `%APPDATA%\Samplar\license.json` (handled by Section 4's path token rename). |

### 5.3 Crucial design decision: shared vs. independent license database

User-side question worth flagging: should a single SAMPLAR license activate on multiple machines (cross-machine policy, like SAMPLAR's N-activations-per-license model)? Default answer: **yes, identical to SAMPLAR's policy** — N activations per license, same hardware fingerprint algorithm, same per-machine deactivation flow.

If you ever want SAMPLAR to share an activation with SAMPLAR (i.e. SAMPLAR owners get SAMPLAR free), that's a bundle/discount problem, solved at the **payment processor** (Stripe coupon), not the license server. Out of scope for v0.1.

### 5.4 What's deferred to first SAMPLAR release

- Stripe / payment SKU creation
- Pricing decision
- License activation email template
- Dashboard UI for SAMPLAR (download buttons, changelog)
- Cloudflare R2 bucket prefix for SAMPLAR downloads (`downloads/Samplar_*` vs `downloads/samplar_*`)

These are all "post-build, pre-ship" concerns. They block release, not development.

## Section 6 — Python/models bootstrap

User chose Option B (own copy from day one). One-time bootstrap from SAMPLAR; from then on, independent maintenance.

### 6.1 One-time copy at fork creation

After the strip + rename pass produces a buildable SAMPLAR repo, populate runtime resources:

```powershell
# From SAMPLAR repo root
$sm5 = "B:\SANTI\SAMPLE_MONSTA\sample_monsta4\Sample_monsta5"

# Python embedded interpreter + site-packages (~1.4 GB)
robocopy "$sm5\SAMPLAR_Portable\python" "SAMPLAR_Portable\python" /E /MT:16 /NFL /NDL /NJH /NJS

# Demucs separation models only (drop drumsep, drum-classifier, anything else)
mkdir SAMPLAR_Portable\models
Copy-Item "$sm5\SAMPLAR_Portable\models\htdemucs_ft*" SAMPLAR_Portable\models\
Copy-Item "$sm5\SAMPLAR_Portable\models\f7e0c4bc-*" SAMPLAR_Portable\models\  # demucs ft drums
Copy-Item "$sm5\SAMPLAR_Portable\models\d12395a8-*" SAMPLAR_Portable\models\
Copy-Item "$sm5\SAMPLAR_Portable\models\92cfc3b6-*" SAMPLAR_Portable\models\
Copy-Item "$sm5\SAMPLAR_Portable\models\04573f0d-*" SAMPLAR_Portable\models\
# (model file list comes from installer_sm5.iss lines 418-438; verify before running)

# Demucs CLI fallback executables
Copy-Item "$sm5\SAMPLAR_Portable\demucs_mt*.exe" SAMPLAR_Portable\

# DLLs the standalone needs (PyTorch + dependencies, audio-separator)
Copy-Item "$sm5\SAMPLAR_Portable\*.dll" SAMPLAR_Portable\

# VC++ redistributable (first-run installer)
Copy-Item "$sm5\SAMPLAR_Portable\vc_redist.x64.exe" SAMPLAR_Portable\
```

**Total bootstrap copy:** ~1.8 GB (1.4 GB Python + 370 MB models + ~50 MB DLLs/exes + 30 MB vcredist).

**Drop these SAMPLAR portable artifacts** (not needed for SAMPLAR):
- `SAMPLAR_Portable\BACKGROUNDS\*` — most are SAMPLAR-specific, audit and copy only the ones SModePage references
- `SAMPLAR_Portable\Samples\` — entire folder skipped (no sample browser)
- `SAMPLAR_Portable\data\patterns_with_rolls.json` — skipped
- `SAMPLAR_Portable\data\latin_patterns.json` — skipped

### 6.2 Build-time path resolution

The pre-existing `DataPaths.h` mechanism (Strategy 0 = portable pointer, Strategy 1 = registry, Strategy 2 = exe parent dir, etc.) already handles all the deployment scenarios SAMPLAR will use. After the rename pass changes `SAMPLAR_Portable/` ? `SAMPLAR_Portable/` in CMake's resource-copy logic, `DataPaths::getAppDirectory()` works without code changes.

CMake `SAMPLAR_DEV_RESOURCE_PATH` flag (renamed from SAMPLAR's): for dev builds where the binary is in `build/` and resources are in `SAMPLAR_Portable/`, this flag bakes in the absolute path. Off in release builds (per existing memory: "MUST be OFF for release").

### 6.3 Portable build script (`000portable.ps1`)

Reuse SAMPLAR's `000portable.ps1` adapted for SAMPLAR. Changes needed:
- `$OutputDir` default: `.\SAMPLAR_Portable` (was `.\SAMPLAR_Portable`)
- VST3 source path: `build\SamplarPlugin_artefacts\$BuildConfig\VST3\SAMPLAR.vst3` (renamed targets per Section 4)
- Drop the steps that copy SAMPLAR-specific data files (patterns_with_rolls.json etc.)
- Drop the steps that copy SAMPLAR-specific Samples folders
- Keep all Python/models/demucs CLI logic — it's identical
- Keep the `-SkipPythonSetup` and `-SkipModelDownload` flags

The fix we already made to SAMPLAR's 000portable.ps1 (`SAMPLAR.vst3` filename instead of `SAMPLAR.vst3`) needs to forward into the SAMPLAR copy: source is now `SAMPLAR.vst3`.

### 6.4 Going forward: independent maintenance

After bootstrap:
- SAMPLAR's `python/` and `models/` evolve independently from SAMPLAR's
- If `audio-separator` releases a new version, you `pip install -U` inside SAMPLAR's venv — does **not** auto-propagate to SAMPLAR
- If a new demucs model ships, you decide per-product whether to bundle it
- Both products keep working without coordination
- The disk cost (~1.8 GB duplicate) is the price of independence

### 6.5 Pre-implementation verification

Before the strip pass writes anything, confirm by reading two files:
1. `installer_sm5.iss` model section (lines ~415-440) — get the **exact** model file list to copy
2. `SAMPLAR_Portable\SAMPLAR_portable.json` — confirm it has no SAMPLAR-specific paths that need rewriting

## Section 7 — Build verification + deferred items

### 7.1 Build verification milestones

Five gates from "fork created" to "v0.1 ready for development." Each must pass before moving to the next; if a gate fails, fix that gate before proceeding.

**Gate 1 — Compiles.** After strip + rename + bootstrap copy:
```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target SamplarApp SamplarPlugin
```
Expected: zero errors. Warnings tolerable for v0.1. Both `SAMPLAR.exe` and `SAMPLAR.vst3` produced.

If linker errors reference deleted SAMPLAR services (e.g. `GeneratorService`), find the dangling include or registration call and remove it. This is the highest-risk gate.

**Gate 2 — Standalone launches.** Run `build\SamplarApp_artefacts\Release\SAMPLAR.exe` from the SAMPLAR repo with `SAMPLAR_DEV_RESOURCE_PATH` pointed at `SAMPLAR_Portable\`. Expected: window opens, no crash, log shows `samplar::App::initialise() ENTER`, S-mode page renders with empty slots.

**Gate 3 — VST3 loads in a DAW.** Drop `SAMPLAR.vst3` into `%LOCALAPPDATA%\Programs\Common\VST3\` (dev location, no admin needed). Open Reaper or Ableton (skip FL Studio for dev). Expected: SAMPLAR appears in plugin list, instantiates, log shows `samplar::SamplarAudioProcessor` constructor + service init.

**Gate 4 — Separation works.** In standalone or VST3, drop a song file ? trigger separate ? expected: Python subprocess launches, demucs runs, stems land in S-mode slots. End-to-end of the unified workflow from Section 1 of this design.

**Gate 5 — MIDI plays.** Send MIDI notes to the slots (DAW sequencer or external controller). Expected: slots trigger sample playback. MIDI learn assigns notes to slots.

**Optional Gate 6 — License activation.** Configure SAMPLAR with a test license on `samplemonsta.com` license server (using the new `samplar` product key from Section 5). Expected: activation dialog accepts license, app unlocks. **This gate can wait until first paid release** — for pure development work, license can be stubbed (always-licensed, or feature-flag controlled).

### 7.2 Deferred items (intentionally NOT in v0.1)

None of these block development. All become real concerns at first release.

| Item | When to address |
|---|---|
| Stripe SKU + pricing | Pre-launch (post-build) |
| EULA / license agreement text | Pre-launch (legal review) |
| New icon design | Pre-launch (designer) |
| Dashboard UI on samplemonsta.com | Pre-launch (license_server PR) |
| Cloudflare R2 bucket prefix `downloads/Samplar_*` | Pre-launch |
| Code signing (EV cert / Azure Trusted Signing) | Pre-launch (or "post-launch when revenue justifies") |
| AV false-positive submissions | Pre-launch + ongoing |
| macOS build | v0.2 or later (Windows-only for v0.1) |
| Update notification testing | After 0.0.1 ? 0.0.2 release |
| `SamplarTests` target adapted | v0.x — drop tests entirely for v0.1 if they're SAMPLAR-coupled |
| Mentor / tutorial system port | If ever — could be a SAMPLAR differentiator later |
| Folder cleanup (DLLs at root, vc_redist, etc.) | When you'd rezip for a release anyway |
| `SAMPLAR_Portable\BACKGROUNDS\` audit (which assets SModePage uses) | Strip-pass time, but acceptable to over-copy and prune post-v0.1 |

### 7.3 Risk register (post-design, pre-implementation concerns)

Three risks worth flagging before implementation kicks off:

1. **MidiMappingService entanglement.** Gate 1 will reveal whether MidiMappingService genuinely compiles standalone or if it pulls in deleted services through transitive includes. Mitigation: keep the file whole through Gate 1, prune SAMPLAR-specific methods post-v0.1 once everything builds.

2. **JUCE BinaryData regeneration.** SAMPLAR's `juce_binarydata_SM5Resources` references many assets (BACKGROUNDS, REF.png, etc.). After deleting most of them, BinaryData generation may fail or produce a shrunken target with broken Image::loadFrom calls. Mitigation: on first build break, expect to either delete unused asset references in `CMakeLists.txt` OR ship a stripped BinaryData with placeholder bytes for the deleted entries.

3. **Hidden SAMPLAR-isms in `SModePage`.** SModePage might use BeatGridOverlay, ThresholdLineOverlay, or other components we listed for deletion. Mitigation: 5 verification items at end of Section 3 — grep them at strip time, restore any genuinely-needed Components/* file before declaring strip complete.

### 7.4 Estimated time-to-Gate-2 (compile + run)

For a developer familiar with the codebase: **2-4 days of focused work**. Most of the time is in Gate 1 (compile errors from transitive deletes). Once it compiles, Gates 2-5 are usually quick.

---

## Open verification items (post-approval, pre-implementation)

These need a code grep before the strip pass executes — won't block design approval but worth listing:

1. Confirm SModePage doesn't import any pattern-database UI (PatternPreviewDialog etc.)
2. Confirm MidiMappingService's SAMPLAR-specific topics (`midi.pad.triggered` etc.) only refer to S-mode slots, not browser tiles
3. Confirm FullSongSeparationService is genuinely independent of GeneratorService/VariationService
4. Confirm AudioEngine pieces used by S-mode don't pull in DrumSliceService or BeatAnalysisService
5. Inventory SModePage's UI dependencies — which Components/* files does it actually use, vs. which are SAMPLAR-only

These get done as part of the strip-and-build cycle, not the design.
