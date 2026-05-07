# SAMPLAR Extraction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fork SAMPLAR v1.3.0 into a new repo at `b:\SANTI\VST PROJECT\SAMPLAR\`, strip everything that isn't S-mode + UVR audio separation + MIDI mapping + license stack, rename all identity tokens, and reach a state where `SAMPLAR.exe` (standalone) and `SAMPLAR.vst3` (plugin) compile, launch, load in a DAW, run separation end-to-end, and trigger slots via MIDI.

**Architecture:** Fork-and-strip. Clone SAMPLAR, aggressively delete unwanted features, mechanical rename pass (`samplar::` ? `samplar::`, `SAMPLAR` ? `SAMPLAR`, etc.), bootstrap own copy of Python + demucs models from SAMPLAR's `SAMPLAR_Portable/`, configure license server with new SKU. Same JUCE/CMake structure as SAMPLAR, same license server, fresh git history.

**Tech Stack:** C++17, JUCE 7.x, CMake, MSVC 2022, embedded Python 3.11, demucs.cpp, audio-separator, Inno Setup, existing license_server (Python/Render).

**Reference spec:** `docs/superpowers/specs/2026-05-03-samplar-extraction-design.md`

---

## Phase 0 — Bootstrap the SAMPLAR repo

### Task 0.1: Create SAMPLAR parent folder and clone SAMPLAR

**Files:**
- Create: `b:\SANTI\VST PROJECT\SAMPLAR\` (target folder, empty before clone)

- [ ] **Step 1: Verify target path is empty / does not exist**

```bash
ls "b:/SANTI/VST PROJECT/SAMPLAR" 2>&1 | head -3
```

Expected: "No such file or directory" or similar.

- [ ] **Step 2: Clone SAMPLAR to the new location**

```bash
cd "b:/SANTI/VST PROJECT"
git clone "b:/SANTI/SAMPLE_MONSTA/sample_monsta4/Sample_monsta5" SAMPLAR
cd SAMPLAR
```

Expected: clone completes, ~7 GB transferred (mostly JUCE submodule + python).

- [ ] **Step 3: Remove origin remote (so we never accidentally push back to SAMPLAR)**

```bash
git remote remove origin
git remote -v
```

Expected: empty output from `git remote -v`.

### Task 0.2: Create orphan main branch (fresh git history)

- [ ] **Step 1: Checkout orphan branch**

```bash
cd "b:/SANTI/VST PROJECT/SAMPLAR"
git checkout --orphan main
git rm -rf --cached . > /dev/null
```

Expected: working tree intact, index cleared.

- [ ] **Step 2: Stage everything and make initial fork commit**

```bash
git add -A
git commit -m "chore: initial fork from SAMPLAR v1.3.0 (pre-strip)"
```

Expected: one commit on `main`, no parents.

- [ ] **Step 3: Delete the inherited master branch**

```bash
git branch -D master 2>&1 || echo "no master branch"
git log --oneline | head -3
```

Expected: only the new "initial fork" commit visible.

### Task 0.3: Create top-level marker file for orientation

**Files:**
- Create: `b:\SANTI\VST PROJECT\SAMPLAR\SAMPLAR_FORK_NOTES.md`

- [ ] **Step 1: Write fork notes**

```markdown
# SAMPLAR — Fork from SAMPLAR

**Forked:** 2026-05-03 from SAMPLAR v1.3.0
**Strategy:** see `docs/superpowers/specs/2026-05-03-samplar-extraction-design.md` (lives in the SAMPLAR repo, not here)
**Strip-pass tracking:** `docs/superpowers/plans/2026-05-03-samplar-extraction.md` (lives in the SAMPLAR repo)

This file exists for orientation. Delete after first release.
```

- [ ] **Step 2: Commit**

```bash
git add SAMPLAR_FORK_NOTES.md
git commit -m "docs: add fork notes"
```

---

## Phase 1 — Strip pass (delete unwanted features)

All tasks in this phase run from `b:/SANTI/VST PROJECT/SAMPLAR`. Each task ends with a single commit so the strip is auditable.

### Task 1.1: Delete variation engines

**Files (delete):**
- `src/services/variation/` (entire folder)
- `src/services/GeneratorService.{h,cpp}`
- `src/services/VariationService.{h,cpp}`
- `src/ui/Variation/` (entire folder)
- `src/ui/Pages/EngineLabPage.{h,cpp}`

- [ ] **Step 1: Delete files**

```bash
rm -rf src/services/variation
rm -f src/services/GeneratorService.h src/services/GeneratorService.cpp
rm -f src/services/VariationService.h src/services/VariationService.cpp
rm -rf src/ui/Variation
rm -f src/ui/Pages/EngineLabPage.h src/ui/Pages/EngineLabPage.cpp
```

- [ ] **Step 2: Verify nothing under those paths remains**

```bash
ls src/services/variation 2>&1; ls src/services/GeneratorService.* 2>&1; ls src/ui/Variation 2>&1
```

Expected: all "No such file or directory".

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "strip: remove variation engines (GeneratorService, VariationService, engines/, EngineLabPage)"
```

### Task 1.2: Delete sample browser + pattern databases

**Files (delete):**
- `src/ui/Pages/SampleBrowserPage.{h,cpp}`
- `src/ui/Components/FolderTreeItem.{h,cpp}`
- `src/utils/CustomCategoryManager.{h,cpp}`
- `src/ui/Components/CategoryEditorDialog.{h,cpp}`
- `src/utils/UserPatternDatabase.{h,cpp}`
- `src/utils/RhythmicPatternDatabase.{h,cpp}`
- `src/utils/PatternLibrary.{h,cpp}`
- `src/utils/MultiBandPatternExtractor.{h,cpp}`
- `src/utils/DrumMapping.{h,cpp}`
- `src/ui/Components/PatternPreviewDialog.{h,cpp}`
- `src/ui/Components/PatternLibraryDialog.{h,cpp}`

- [ ] **Step 1: Delete files**

```bash
rm -f src/ui/Pages/SampleBrowserPage.h src/ui/Pages/SampleBrowserPage.cpp
rm -f src/ui/Components/FolderTreeItem.h src/ui/Components/FolderTreeItem.cpp
rm -f src/utils/CustomCategoryManager.h src/utils/CustomCategoryManager.cpp
rm -f src/ui/Components/CategoryEditorDialog.h src/ui/Components/CategoryEditorDialog.cpp
rm -f src/utils/UserPatternDatabase.h src/utils/UserPatternDatabase.cpp
rm -f src/utils/RhythmicPatternDatabase.h src/utils/RhythmicPatternDatabase.cpp
rm -f src/utils/PatternLibrary.h src/utils/PatternLibrary.cpp
rm -f src/utils/MultiBandPatternExtractor.h src/utils/MultiBandPatternExtractor.cpp
rm -f src/utils/DrumMapping.h src/utils/DrumMapping.cpp
rm -f src/ui/Components/PatternPreviewDialog.h src/ui/Components/PatternPreviewDialog.cpp
rm -f src/ui/Components/PatternLibraryDialog.h src/ui/Components/PatternLibraryDialog.cpp
```

- [ ] **Step 2: Commit**

```bash
git add -A
git commit -m "strip: remove sample browser + pattern databases"
```

### Task 1.3: Delete wave editor + pre-slice + step sequencer

**Files (delete):**
- `src/ui/WaveEditor/` (entire folder)
- `src/ui/Pages/WavePreviewPage.{h,cpp}`
- `src/ui/Components/StepSequencerComponent.{h,cpp}`
- `src/services/BeatAnalysisService.{h,cpp}`
- `src/services/PreSliceProcessingService.{h,cpp}`
- `src/services/DrumSliceService.{h,cpp}`
- `src/utils/DrumAnalyzer.{h,cpp}`
- `src/utils/ProcessingPresetManager.{h,cpp}`
- `src/ui/Components/SpectralSeparateDialog.{h,cpp}`

**Hold for verification (delete only if confirmed unused by SModePage):**
- `src/ui/Components/EqDividerComponent.{h,cpp}`
- `src/ui/Components/BeatGridOverlay.{h,cpp}`
- `src/ui/Components/ThresholdLineOverlay.{h,cpp}`
- `src/ui/Components/DraggableMarker.{h,cpp}`
- `src/ui/Components/WaveformZoomController.{h,cpp}`
- `src/ui/Components/TooltipOverlay.{h,cpp}`

- [ ] **Step 1: Grep SModePage to see which "Hold for verification" components it references**

```bash
grep -E "EqDivider|BeatGrid|ThresholdLine|DraggableMarker|WaveformZoom|TooltipOverlay" src/ui/Pages/SModePage.* src/ui/Components/smode/*.* 2>&1
```

Expected output guides next step. If a component is referenced, KEEP it; otherwise, delete.

- [ ] **Step 2: Delete confirmed-removable files**

```bash
rm -rf src/ui/WaveEditor
rm -f src/ui/Pages/WavePreviewPage.h src/ui/Pages/WavePreviewPage.cpp
rm -f src/ui/Components/StepSequencerComponent.h src/ui/Components/StepSequencerComponent.cpp
rm -f src/services/BeatAnalysisService.h src/services/BeatAnalysisService.cpp
rm -f src/services/PreSliceProcessingService.h src/services/PreSliceProcessingService.cpp
rm -f src/services/DrumSliceService.h src/services/DrumSliceService.cpp
rm -f src/utils/DrumAnalyzer.h src/utils/DrumAnalyzer.cpp
rm -f src/utils/ProcessingPresetManager.h src/utils/ProcessingPresetManager.cpp
rm -f src/ui/Components/SpectralSeparateDialog.h src/ui/Components/SpectralSeparateDialog.cpp
```

- [ ] **Step 3: Delete the Hold-for-verification files NOT referenced in Step 1**

```bash
# Remove only those that DID NOT appear in Step 1 grep output.
# Example if EqDivider was referenced — DO NOT remove it.
# Adjust this command per Step 1 result.
rm -f src/ui/Components/{EqDividerComponent,BeatGridOverlay,ThresholdLineOverlay,DraggableMarker,WaveformZoomController,TooltipOverlay}.{h,cpp}
```

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "strip: remove wave editor, pre-slice, step sequencer, drum analyzer"
```

### Task 1.4: Delete ML classifier, mentor system, games

**Files (delete):**
- `src/utils/ml/DrumMLClassifier.{h,cpp}` (and any peers in `src/utils/ml/`)
- `src/mentor/` (entire folder)
- `src/ui/Pages/ProcessingVisualizationDemo.{h,cpp}`
- `src/ui/Games/` (entire folder)

- [ ] **Step 1: Delete files**

```bash
rm -rf src/utils/ml
rm -rf src/mentor
rm -f src/ui/Pages/ProcessingVisualizationDemo.h src/ui/Pages/ProcessingVisualizationDemo.cpp
rm -rf src/ui/Games
```

- [ ] **Step 2: Commit**

```bash
git add -A
git commit -m "strip: remove ML classifier, mentor system, games"
```

### Task 1.5: Delete pattern data files + sample folders + unused models

**Files (delete):**
- `data/patterns_with_rolls.json` (53 MB)
- `data/latin_patterns.json` (34 KB)
- `SAMPLAR_Portable/Samples/` (entire folder)
- `SAMPLAR_Portable/data/patterns_with_rolls.json`
- `SAMPLAR_Portable/data/latin_patterns.json`
- Any models in `SAMPLAR_Portable/models/` not on the demucs htdemucs_ft list (see spec Section 6.1)

- [ ] **Step 1: Delete data files**

```bash
rm -f data/patterns_with_rolls.json data/latin_patterns.json
rm -f SAMPLAR_Portable/data/patterns_with_rolls.json SAMPLAR_Portable/data/latin_patterns.json
rm -rf SAMPLAR_Portable/Samples
```

- [ ] **Step 2: Identify and delete non-demucs models**

```bash
ls SAMPLAR_Portable/models/
# Read installer_sm5.iss lines 415-440 to confirm the demucs file list.
# DELETE any model files NOT in that list (e.g. drumsep, drum-classifier).
```

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "strip: remove pattern data, samples folder, non-demucs models"
```

---

## Phase 2 — CMake source-list cleanup

After Phase 1, CMake still references deleted files. Build will fail until these are removed.

### Task 2.1: Remove deleted source files from CMakeLists.txt target_sources

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Find all references to deleted files**

```bash
grep -nE "GeneratorService|VariationService|EngineLabPage|SampleBrowserPage|FolderTreeItem|CustomCategoryManager|CategoryEditorDialog|UserPatternDatabase|RhythmicPatternDatabase|PatternLibrary|MultiBandPatternExtractor|DrumMapping|PatternPreviewDialog|PatternLibraryDialog|WaveEditor|WavePreviewPage|StepSequencerComponent|BeatAnalysisService|PreSliceProcessingService|DrumSliceService|DrumAnalyzer|ProcessingPresetManager|SpectralSeparateDialog|DrumMLClassifier|mentor|ProcessingVisualizationDemo|Games" CMakeLists.txt
```

- [ ] **Step 2: Remove each matching line from target_sources blocks**

Open `CMakeLists.txt` and delete every line found in Step 1. They will be inside `target_sources(... PRIVATE ...)` blocks for `SM5App`, `SM5Plugin`, etc.

- [ ] **Step 3: Verify no orphan refs remain**

```bash
grep -nE "GeneratorService|VariationService|EngineLabPage|SampleBrowserPage|WaveEditor|StepSequencer|DrumSlice|mentor/" CMakeLists.txt
```

Expected: empty output.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: remove deleted source files from CMakeLists.txt target_sources"
```

### Task 2.2: Remove deleted assets from JUCE BinaryData generator

**Files:**
- Modify: `CMakeLists.txt` (the `juce_add_binary_data` block for `SM5Resources`)

- [ ] **Step 1: Identify which BinaryData entries reference deleted assets**

```bash
grep -nA1 "juce_add_binary_data" CMakeLists.txt
```

- [ ] **Step 2: Remove entries for assets we no longer ship**

Likely candidates: pattern JSON files, sample WAVs from `Samples/`, mentor assets, game assets. Keep BACKGROUNDS entries until Phase 3 audit.

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: remove deleted assets from BinaryData generator"
```

---

## Phase 3 — Identity rename pass

Order matters per spec Section 4.1 to avoid substring collisions.

### Task 3.1: Replace "SAMPLAR" (most specific) ? "SAMPLAR"

- [ ] **Step 1: Find all occurrences**

```bash
grep -rln "SAMPLAR" --include="*.cpp" --include="*.h" --include="*.h.in" --include="*.txt" --include="*.iss" --include="*.ps1" --include="*.json" --include="*.md" --exclude-dir=JUCE --exclude-dir=third_party --exclude-dir=python --exclude-dir=models | head -50
```

- [ ] **Step 2: Run sed replacement** (PowerShell — Windows-safe)

```powershell
Get-ChildItem -Recurse -Include *.cpp,*.h,*.h.in,*.txt,*.iss,*.ps1,*.json,*.md `
  | Where-Object { $_.FullName -notmatch '\\(JUCE|third_party|python|models)\\' } `
  | ForEach-Object {
      (Get-Content $_.FullName -Raw) -replace 'SAMPLAR', 'SAMPLAR' `
        | Set-Content -Path $_.FullName -NoNewline
    }
```

- [ ] **Step 3: Verify**

```bash
grep -rl "SAMPLAR" --include="*.cpp" --include="*.h" --include="*.txt" --exclude-dir=JUCE --exclude-dir=third_party --exclude-dir=python --exclude-dir=models | head
```

Expected: empty.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "rename: 'SAMPLAR' -> 'SAMPLAR'"
```

### Task 3.2: Replace "Samplar" (path token) ? "Samplar"

- [ ] **Step 1: PowerShell replacement**

```powershell
Get-ChildItem -Recurse -Include *.cpp,*.h,*.h.in,*.txt,*.iss,*.ps1,*.json,*.md `
  | Where-Object { $_.FullName -notmatch '\\(JUCE|third_party|python|models)\\' } `
  | ForEach-Object {
      (Get-Content $_.FullName -Raw) -replace 'Samplar', 'Samplar' `
        | Set-Content -Path $_.FullName -NoNewline
    }
```

- [ ] **Step 2: Verify**

```bash
grep -rl "Samplar" --include="*.cpp" --include="*.h" --include="*.txt" --exclude-dir=JUCE --exclude-dir=third_party | head
```

Expected: empty.

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "rename: Samplar path token -> Samplar"
```

### Task 3.3: Replace "SAMPLAR" (without 5) ? "SAMPLAR"

- [ ] **Step 1: Replacement**

```powershell
Get-ChildItem -Recurse -Include *.cpp,*.h,*.h.in,*.txt,*.iss,*.ps1,*.json,*.md `
  | Where-Object { $_.FullName -notmatch '\\(JUCE|third_party|python|models)\\' } `
  | ForEach-Object {
      (Get-Content $_.FullName -Raw) -replace 'SAMPLAR', 'SAMPLAR' `
        | Set-Content -Path $_.FullName -NoNewline
    }
```

- [ ] **Step 2: Verify + commit**

```bash
grep -rl "SAMPLAR" --include="*.cpp" --include="*.h" --exclude-dir=JUCE --exclude-dir=third_party | head
git add -A
git commit -m "rename: 'SAMPLAR' -> 'SAMPLAR'"
```

### Task 3.4: Replace "samplar" (lowercase) ? "samplar"

**Important:** does NOT touch URLs to `samplemonsta.com` — verify before commit.

- [ ] **Step 1: Replacement**

```powershell
Get-ChildItem -Recurse -Include *.cpp,*.h,*.h.in,*.txt,*.iss,*.ps1,*.json,*.md `
  | Where-Object { $_.FullName -notmatch '\\(JUCE|third_party|python|models)\\' } `
  | ForEach-Object {
      $content = Get-Content $_.FullName -Raw
      # Skip URL token (samplemonsta.com) — protect by replacing temporarily
      $content = $content -replace 'samplar\.com', 'samplemonsta.com'
      $content = $content -replace 'samplar', 'samplar'
      $content = $content -replace 'samplemonsta.com', 'samplemonsta.com'
      Set-Content -Path $_.FullName -Value $content -NoNewline
    }
```

- [ ] **Step 2: Verify URLs intact**

```bash
grep -rn "samplemonsta.com" --include="*.cpp" --include="*.h" --include="*.iss" --exclude-dir=JUCE | head
grep -rln "samplar" --include="*.cpp" --include="*.h" --exclude-dir=JUCE | head
```

Expected: URLs still present, `samplar` token replaced everywhere else.

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "rename: samplar token -> samplar (URLs preserved)"
```

### Task 3.5: Replace SAMPLAR_ macro prefix ? SAMPLAR_

- [ ] **Step 1: Replacement**

```powershell
Get-ChildItem -Recurse -Include *.cpp,*.h,*.h.in,*.txt,*.cmake `
  | Where-Object { $_.FullName -notmatch '\\(JUCE|third_party)\\' } `
  | ForEach-Object {
      (Get-Content $_.FullName -Raw) -replace 'SAMPLAR_', 'SAMPLAR_' `
        | Set-Content -Path $_.FullName -NoNewline
    }
```

- [ ] **Step 2: Verify + commit**

```bash
grep -rl "SAMPLAR_" --include="*.cpp" --include="*.h" --exclude-dir=JUCE --exclude-dir=third_party | head
git add -A
git commit -m "rename: SAMPLAR_* macros -> SAMPLAR_*"
```

### Task 3.6: Replace `samplar::` namespace ? `samplar::`

- [ ] **Step 1: Replacement**

```powershell
Get-ChildItem -Recurse -Include *.cpp,*.h,*.h.in `
  | Where-Object { $_.FullName -notmatch '\\(JUCE|third_party)\\' } `
  | ForEach-Object {
      $c = Get-Content $_.FullName -Raw
      $c = $c -replace 'namespace samplar\b', 'namespace samplar'
      $c = $c -replace 'samplar::', 'samplar::'
      Set-Content -Path $_.FullName -Value $c -NoNewline
    }
```

- [ ] **Step 2: Verify + commit**

```bash
grep -rln "samplar::\|namespace samplar" --include="*.cpp" --include="*.h" --exclude-dir=JUCE --exclude-dir=third_party | head
git add -A
git commit -m "rename: namespace samplar -> samplar"
```

### Task 3.7: Audit and replace remaining bare `SAMPLAR` tokens manually

- [ ] **Step 1: Find remaining bare-SAMPLAR references**

```bash
grep -rn "\bSM5\b" --include="*.cpp" --include="*.h" --include="*.txt" --exclude-dir=JUCE --exclude-dir=third_party | head -50
```

- [ ] **Step 2: Manually edit each match**

Most will be in comments, log strings, or version manifest entries. Replace with SAMPLAR or remove if obsolete. Watch for false positives (e.g. SAMPLAR inside file paths to OLD SAMPLAR repo — if a path is meant to point at the source SAMPLAR, leave it).

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "rename: clean up remaining bare SAMPLAR tokens"
```

### Task 3.8: File renames (preserves git history)

- [ ] **Step 1: Rename plugin source files**

```bash
git mv src/plugin/SamplarAudioProcessor.h src/plugin/SamplarAudioProcessor.h
git mv src/plugin/SamplarAudioProcessor.cpp src/plugin/SamplarAudioProcessor.cpp
git mv src/plugin/SamplarAudioProcessorEditor.h src/plugin/SamplarAudioProcessorEditor.h
git mv src/plugin/SamplarAudioProcessorEditor.cpp src/plugin/SamplarAudioProcessorEditor.cpp
```

- [ ] **Step 2: Rename version template**

```bash
git mv src/app/SamplarVersion.h.in src/app/SamplarVersion.h.in
```

- [ ] **Step 3: Rename installer + portable folder**

```bash
git mv installer_sm5.iss installer_samplar.iss
git mv SAMPLAR_Portable SAMPLAR_Portable
```

- [ ] **Step 4: Update #include directives that reference renamed headers**

```powershell
Get-ChildItem -Recurse -Include *.cpp,*.h `
  | Where-Object { $_.FullName -notmatch '\\(JUCE|third_party)\\' } `
  | ForEach-Object {
      $c = Get-Content $_.FullName -Raw
      $c = $c -replace 'SamplarAudioProcessor', 'SamplarAudioProcessor'
      $c = $c -replace 'SM5Version\.h', 'SamplarVersion.h'
      Set-Content -Path $_.FullName -Value $c -NoNewline
    }
```

- [ ] **Step 5: Update CMakeLists.txt path references**

```powershell
$cmake = Get-Content CMakeLists.txt -Raw
$cmake = $cmake -replace 'SamplarAudioProcessor', 'SamplarAudioProcessor'
$cmake = $cmake -replace 'SM5Version\.h', 'SamplarVersion.h'
$cmake = $cmake -replace 'installer_sm5\.iss', 'installer_samplar.iss'
$cmake = $cmake -replace 'SAMPLAR_Portable', 'SAMPLAR_Portable'
Set-Content CMakeLists.txt -Value $cmake -NoNewline
```

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "rename: file renames + include path updates"
```

### Task 3.9: CMake target name rename + JUCE manifest update

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Replace CMake target names**

```powershell
$cmake = Get-Content CMakeLists.txt -Raw
$cmake = $cmake -replace '\bSM5App\b', 'SamplarApp'
$cmake = $cmake -replace '\bSM5Plugin\b', 'SamplarPlugin'
$cmake = $cmake -replace '\bSM5Resources\b', 'SamplarResources'
$cmake = $cmake -replace '\bSM5Tests\b', 'SamplarTests'
$cmake = $cmake -replace '\bSM5LicenseCleaner\b', 'SamplarLicenseCleaner'
Set-Content CMakeLists.txt -Value $cmake -NoNewline
```

- [ ] **Step 2: Manually update JUCE manifest values**

Find the `juce_add_gui_app(SamplarApp ...)` block and update:
- `PRODUCT_NAME "SAMPLAR"`
- `COMPANY_WEBSITE "https://samplemonsta.com/samplar"` (or just `samplemonsta.com` for now)
- `BUNDLE_ID "com.muziksociety.samplar"`
- `VERSION "0.1.0"`
- `ICON_BIG "assets/samplar_icon_512.png"` (placeholder — see Task 3.10)
- `ICON_SMALL "assets/samplar_icon_64.png"`

Find the `juce_add_plugin(SamplarPlugin ...)` block and update:
- `BUNDLE_ID "com.muziksociety.samplar.vst3"`
- `PLUGIN_MANUFACTURER_CODE Mzks`
- `PLUGIN_CODE Smpl` (must differ from SAMPLAR's plugin code)
- `PRODUCT_NAME "SAMPLAR"`

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "rename: CMake targets + JUCE manifest -> SAMPLAR"
```

### Task 3.10: Add placeholder icon assets

**Files:**
- Create: `assets/samplar_icon_512.png` (placeholder, 512x512)
- Create: `assets/samplar_icon_64.png` (placeholder, 64x64)

- [ ] **Step 1: Create placeholder PNGs**

If you have an image editor: 512x512 solid color (e.g. orange) saved as `assets/samplar_icon_512.png`, scaled to 64x64 for the small icon.

If not:

```bash
mkdir -p assets
# Copy SAMPLAR's icon as a temporary placeholder
cp "b:/SANTI/SAMPLE_MONSTA/sample_monsta4/Sample_monsta5/assets/icon_512.png" assets/samplar_icon_512.png 2>&1 || echo "(no SAMPLAR icon to copy — image editor needed)"
cp "b:/SANTI/SAMPLE_MONSTA/sample_monsta4/Sample_monsta5/assets/icon_64.png" assets/samplar_icon_64.png 2>&1 || echo "(no SAMPLAR icon to copy — image editor needed)"
```

- [ ] **Step 2: Commit**

```bash
git add assets/
git commit -m "feat: add placeholder icon assets (replace before release)"
```

---

## Phase 4 — Python + models bootstrap

### Task 4.1: Robocopy SAMPLAR's Python bundle

- [ ] **Step 1: Verify SAMPLAR_Portable\python\ exists at source**

```bash
ls "b:/SANTI/SAMPLE_MONSTA/sample_monsta4/Sample_monsta5/SAMPLAR_Portable/python/python.exe" 2>&1
```

Expected: file exists.

- [ ] **Step 2: Copy Python bundle**

```powershell
robocopy `
  "b:\SANTI\SAMPLE_MONSTA\sample_monsta4\Sample_monsta5\SAMPLAR_Portable\python" `
  "SAMPLAR_Portable\python" `
  /E /MT:16 /NFL /NDL /NJH /NJS
```

Expected: ~1.4 GB copied. Robocopy exit codes 0-7 are success.

- [ ] **Step 3: Verify**

```bash
ls "SAMPLAR_Portable/python/python.exe" 2>&1
ls "SAMPLAR_Portable/python/Lib/site-packages/audio_separator" 2>&1 | head -3
```

Expected: both exist.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "feat: bootstrap embedded Python from SAMPLAR"
```

### Task 4.2: Copy demucs models

- [ ] **Step 1: Read SAMPLAR's installer file list**

```bash
sed -n '415,440p' "b:/SANTI/SAMPLE_MONSTA/sample_monsta4/Sample_monsta5/installer_sm5.iss"
```

Note the model filenames listed.

- [ ] **Step 2: Copy demucs models only**

```powershell
mkdir SAMPLAR_Portable\models -Force
$src = "b:\SANTI\SAMPLE_MONSTA\sample_monsta4\Sample_monsta5\SAMPLAR_Portable\models"
Copy-Item "$src\htdemucs_ft*" SAMPLAR_Portable\models\ -Force
Copy-Item "$src\f7e0c4bc*"   SAMPLAR_Portable\models\ -Force
Copy-Item "$src\d12395a8*"   SAMPLAR_Portable\models\ -Force
Copy-Item "$src\92cfc3b6*"   SAMPLAR_Portable\models\ -Force
Copy-Item "$src\04573f0d*"   SAMPLAR_Portable\models\ -Force
```

- [ ] **Step 3: Verify**

```bash
ls SAMPLAR_Portable/models/
```

Expected: ~5-8 files matching the demucs htdemucs_ft set, ~370 MB total.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "feat: bootstrap demucs models (htdemucs_ft only)"
```

### Task 4.3: Copy DLLs, demucs CLI executables, vc_redist

- [ ] **Step 1: Copy runtime files**

```powershell
$src = "b:\SANTI\SAMPLE_MONSTA\sample_monsta4\Sample_monsta5\SAMPLAR_Portable"
Copy-Item "$src\*.dll"           SAMPLAR_Portable\ -Force
Copy-Item "$src\demucs_mt*.exe"  SAMPLAR_Portable\ -Force
Copy-Item "$src\vc_redist.x64.exe" SAMPLAR_Portable\ -Force
```

- [ ] **Step 2: Verify**

```bash
ls SAMPLAR_Portable/*.dll | wc -l   # expect ~12
ls SAMPLAR_Portable/demucs_mt*.exe | wc -l   # expect 2
```

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "feat: bootstrap DLLs + demucs CLI + vc_redist"
```

### Task 4.4: Copy audited BACKGROUNDS

- [ ] **Step 1: Identify SModePage's background asset references**

```bash
grep -E "\.(gif|png|jpg)" src/ui/Pages/SModePage.cpp src/ui/Components/smode/*.cpp | grep -oE "[a-zA-Z0-9_-]+\.(gif|png|jpg)" | sort -u
```

- [ ] **Step 2: Copy only those asset files**

For each filename from Step 1, copy the matching file from `b:\SANTI\SAMPLE_MONSTA\sample_monsta4\Sample_monsta5\SAMPLAR_Portable\BACKGROUNDS\` to `SAMPLAR_Portable\BACKGROUNDS\`.

```powershell
mkdir SAMPLAR_Portable\BACKGROUNDS -Force
$src = "b:\SANTI\SAMPLE_MONSTA\sample_monsta4\Sample_monsta5\SAMPLAR_Portable\BACKGROUNDS"
# Example - replace with actual filenames from Step 1
Copy-Item "$src\TILESET.png" SAMPLAR_Portable\BACKGROUNDS\ -Force
Copy-Item "$src\Lava.gif"    SAMPLAR_Portable\BACKGROUNDS\ -Force -ErrorAction SilentlyContinue
# ... etc
```

If a background asset doesn't exist at the source, log and skip — placeholder needed before release.

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "feat: bootstrap BACKGROUNDS (S-mode subset)"
```

---

## Phase 5 — License server new SKU

### Task 5.1: Add SAMPLAR product entry to license server

**Repo:** `B:\SANTI\license_server` (different from SAMPLAR repo)

**Files:**
- Modify: `B:\SANTI\license_server\data\products.json` (or equivalent — verify name in repo)
- Modify: `B:\SANTI\license_server\data\releases.json`

- [ ] **Step 1: Add samplar product**

Open `data/products.json` and add:

```json
{
  "id": "samplar",
  "name": "SAMPLAR",
  "active": true,
  "description": "Stems-to-pads sampler with built-in audio separation"
}
```

- [ ] **Step 2: Add samplar release block**

Open `data/releases.json` and add at the end of `products` object (before closing brace):

```json
"samplar": {
  "latest_version": "0.0.0",
  "download_url": "https://samplemonsta.com/portal/samplar",
  "releases": []
}
```

- [ ] **Step 3: Test endpoint**

After committing + pushing + Render redeploy:

```bash
curl "https://samplemonsta.com/api/update/check?current_version=0.0.0&product=samplar"
```

Expected: HTTP 200 with `{"update_available": false, ...}` or similar non-error JSON.

- [ ] **Step 4: Commit license_server changes**

```bash
cd B:/SANTI/license_server
git add data/products.json data/releases.json
git commit -m "feat: register SAMPLAR product (v0 placeholder)"
git push
```

---

## Phase 6 — SAMPLAR repo license code adaptation

### Task 6.1: Update LicenseSystemService product key

**Files:**
- Modify: `src/services/LicenseSystemService.{h,cpp}` in SAMPLAR repo

- [ ] **Step 1: Find product key constant**

```bash
cd "b:/SANTI/VST PROJECT/SAMPLAR"
grep -n "sample_monsta\|kProductKey\|productKey\|product_key" src/services/LicenseSystemService.* | head -20
```

- [ ] **Step 2: Change the literal `"sample_monsta"` to `"samplar"`**

Edit each match found in Step 1.

- [ ] **Step 3: Verify**

```bash
grep -n "sample_monsta\|samplar" src/services/LicenseSystemService.*
```

Expected: only `samplar` references, no remaining `sample_monsta`.

- [ ] **Step 4: Commit**

```bash
git add src/services/LicenseSystemService.*
git commit -m "feat(license): set product key to 'samplar'"
```

### Task 6.2: Update UpdateCheckService product key

**Files:**
- Modify: `src/services/UpdateCheckService.{h,cpp}`

- [ ] **Step 1: Same pattern as Task 6.1**

```bash
grep -n "sample_monsta\|productKey\|product_key" src/services/UpdateCheckService.*
```

Edit matches ? `samplar`.

- [ ] **Step 2: Commit**

```bash
git add src/services/UpdateCheckService.*
git commit -m "feat(update): set product key to 'samplar'"
```

### Task 6.3: Wipe SAMPLAR-specific feature flags from LicenseFeatureFlags

**Files:**
- Modify: `src/utils/LicenseFeatureFlags.{h,cpp}`

- [ ] **Step 1: Read current flag list**

```bash
cat src/utils/LicenseFeatureFlags.h
```

- [ ] **Step 2: Replace SAMPLAR flag enum/map with SAMPLAR-minimal**

Open `LicenseFeatureFlags.h` and replace the flag list with:

```cpp
// SAMPLAR v0.1: license is binary (activated / not). No feature gates.
// Add real feature flags here when SAMPLAR ships a freemium tier or
// engine-level paywalls.
enum class FeatureFlag {
    // (intentionally empty for v0.1)
};
```

Update `LicenseFeatureFlags.cpp` to remove SAMPLAR-specific bit-mapping. Keep the class skeleton so call sites compile.

- [ ] **Step 3: Find and stub callers that reference deleted flags**

```bash
grep -rn "FeatureFlag::" src/ --include="*.cpp" --include="*.h" | head
```

For each caller, replace the gated check with `return true` or remove the gate entirely (depending on whether the gated feature is kept or deleted in SAMPLAR).

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "feat(license): strip SAMPLAR feature flags (binary licensing for v0.1)"
```

### Task 6.4: Update license activation dialog strings

**Files:**
- Modify: `src/ui/licensing/LicenseActivationDialog.{h,cpp}` (and any peer files)

- [ ] **Step 1: Find dialog text strings**

```bash
grep -n "SAMPLAR\|License key\|Activate" src/ui/licensing/LicenseActivationDialog.*
```

- [ ] **Step 2: Update strings to SAMPLAR (most are already replaced by Phase 3, but EULA text needs explicit SAMPLAR reference)**

Open the EULA / agreement text section and replace any "SAMPLAR" lingering with "SAMPLAR".

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "ui(license): update activation dialog strings to SAMPLAR"
```

---

## Phase 7 — Build Gate 1: Compiles

### Task 7.1: CMake configure

- [ ] **Step 1: Run configure**

```bash
cd "b:/SANTI/VST PROJECT/SAMPLAR"
cmake -B build -G "Visual Studio 17 2022" -A x64 -DSAMPLAR_DEV_RESOURCE_PATH="b:/SANTI/VST PROJECT/SAMPLAR/SAMPLAR_Portable" 2>&1 | tail -40
```

Expected: configure succeeds, generates `build/SAMPLAR.sln`.

If configure fails:
- "target SM5App not found" ? Phase 3 rename pass missed a CMake reference. Find it, fix, retry.
- "Cannot find file X" ? orphan source ref from Phase 1 deletion. Edit CMakeLists.txt to remove.
- "Cannot find SamplarVersion.h.in" ? check file rename in Task 3.8.

- [ ] **Step 2: Commit any CMake fixes**

```bash
git add CMakeLists.txt
git commit -m "build: fix CMake configure errors"
```

### Task 7.2: First build attempt

- [ ] **Step 1: Build SamplarApp + SamplarPlugin**

```bash
cmake --build build --config Release --target SamplarApp SamplarPlugin 2>&1 | tail -80
```

This WILL fail on first run. The errors are the work.

- [ ] **Step 2: Triage errors**

Common patterns:
- **Compile error: "GeneratorService.h not found"** ? an include directive references a deleted file. Find: `grep -rn "include.*GeneratorService" src/`. Fix: remove the include and any code that uses the type.
- **Linker error: "unresolved external symbol samplar::Foo"** ? namespace rename missed a spot. Find: `grep -rn "samplar::Foo" src/`. Fix.
- **BinaryData errors:** assets removed from disk but still in CMake. Fix: remove from BinaryData generator block.
- **JUCE module errors:** plugin manifest issue. Fix: revisit Task 3.9 manifest values.

- [ ] **Step 3: Iterate**

Fix one error class at a time. Commit after each fix:

```bash
git add -A
git commit -m "build: fix [error description]"
cmake --build build --config Release --target SamplarApp SamplarPlugin 2>&1 | tail -40
```

Repeat until: zero errors, both `SAMPLAR.exe` and `SAMPLAR.vst3` produced.

- [ ] **Step 4: Verify build artifacts exist**

```bash
ls "build/SamplarApp_artefacts/Release/SAMPLAR.exe"
ls "build/SamplarPlugin_artefacts/Release/VST3/SAMPLAR.vst3/Contents/x86_64-win/SAMPLAR.vst3"
```

Expected: both files exist.

- [ ] **Step 5: Final commit on Gate 1**

```bash
git add -A
git commit -m "build: Gate 1 PASS - SAMPLAR compiles standalone + VST3"
```

---

## Phase 8 — Build Gate 2: Standalone launches

### Task 8.1: Run SAMPLAR.exe and verify boot

- [ ] **Step 1: Launch standalone**

```bash
"build/SamplarApp_artefacts/Release/SAMPLAR.exe" &
```

Expected: window opens within ~5 seconds, no crash dialog.

- [ ] **Step 2: Tail the log file**

```bash
tail -100 SAMPLAR_Portable/logs/SAMPLAR_console.log 2>&1 || tail -100 SAMPLAR_Portable/logs/samplar_console.log 2>&1 | head -50
```

Expected: log lines starting with `[App]` showing `samplar::App::initialise() ENTER` and successful service registration.

- [ ] **Step 3: Visual check**

In the SAMPLAR window: confirm SModePage renders with empty slots (or whatever the S-mode home view is).

- [ ] **Step 4: Close + commit gate marker**

```bash
git commit --allow-empty -m "verify: Gate 2 PASS - SAMPLAR standalone launches"
```

---

## Phase 9 — Build Gate 3: VST3 loads in DAW

### Task 9.1: Install VST3 to user-local folder

- [ ] **Step 1: Copy VST3 bundle**

```powershell
$src = "build\SamplarPlugin_artefacts\Release\VST3\SAMPLAR.vst3"
$dst = "$env:LOCALAPPDATA\Programs\Common\VST3\SAMPLAR.vst3"
if (Test-Path $dst) { Remove-Item $dst -Recurse -Force }
Copy-Item $src $dst -Recurse -Force
```

- [ ] **Step 2: Open Reaper (or Ableton Live 11+)**

Add `%LOCALAPPDATA%\Programs\Common\VST3` to scan paths if needed. Rescan plugins.

Expected: SAMPLAR appears in plugin list.

- [ ] **Step 3: Instantiate plugin**

Add SAMPLAR as a VST3 instrument on a track. Verify:
- No DAW crash
- Plugin window opens
- SModePage renders identically to standalone

- [ ] **Step 4: Tail VST3 log**

```bash
tail -100 "%APPDATA%/Samplar/logs/samplar_console.log"
```

Expected: log shows `samplar::SamplarAudioProcessor` constructor + service init + portable pointer found (since standalone wrote it on first launch).

- [ ] **Step 5: Commit gate marker**

```bash
cd "b:/SANTI/VST PROJECT/SAMPLAR"
git commit --allow-empty -m "verify: Gate 3 PASS - VST3 loads in DAW"
```

---

## Phase 10 — Build Gate 4: Separation works end-to-end

### Task 10.1: Run separation flow in standalone

- [ ] **Step 1: Drop a song file onto SAMPLAR**

Use any .wav or .mp3 file = 5 minutes. The unified workflow (per spec Section 1) should: detect drop ? trigger separation ? run demucs ? place stems in S-mode slots.

- [ ] **Step 2: Watch log for Python subprocess**

```bash
tail -f SAMPLAR_Portable/logs/samplar_console.log
```

Expected log lines: `[FullSongSeparationService] Starting separation`, Python subprocess launch, demucs progress, stems written, slots populated.

- [ ] **Step 3: Verify stems on disk**

Stems should land somewhere predictable (likely `%APPDATA%\Samplar\separations\` or similar — check spec / DataPaths).

- [ ] **Step 4: Verify slots populated in UI**

S-mode slots should show stem waveforms (drums, bass, vocals, other).

- [ ] **Step 5: Commit gate marker**

```bash
git commit --allow-empty -m "verify: Gate 4 PASS - separation end-to-end works"
```

---

## Phase 11 — Build Gate 5: MIDI plays slots

### Task 11.1: Trigger slots via MIDI

- [ ] **Step 1: With slots populated from Gate 4, send MIDI notes**

In standalone: configure audio device + MIDI input. Send notes from a MIDI controller or computer keyboard.

In VST3 (DAW): add MIDI clip to the SAMPLAR track, place notes, play.

- [ ] **Step 2: Verify slot triggering**

Each MIDI note (mapped per slot) should trigger the corresponding stem playback.

- [ ] **Step 3: Test MIDI learn**

Use SModeMidiLearnOverlay: select a slot ? play a note ? confirm assignment is persisted.

- [ ] **Step 4: Commit gate marker**

```bash
git commit --allow-empty -m "verify: Gate 5 PASS - MIDI triggers slots"
```

---

## Phase 12 — Final cleanup + push

### Task 12.1: Set up remote (GitHub or local bare repo)

- [ ] **Step 1: Create the destination remote**

If GitHub: create empty `Muziksociety/Samplar` repo. If local-only: skip — fine for now.

- [ ] **Step 2: Add remote and push**

```bash
git remote add origin <url-of-new-remote>
git push -u origin main
```

- [ ] **Step 3: Verify**

```bash
git remote -v
```

### Task 12.2: Tag v0.1.0-alpha

- [ ] **Step 1: Tag**

```bash
git tag -a v0.1.0-alpha -m "Initial SAMPLAR fork-and-strip — Gates 1-5 pass"
git push origin v0.1.0-alpha
```

### Task 12.3: Commit + push to license_server (deferred SAMPLAR product activation)

Already done in Phase 5. Verify deploy:

```bash
curl "https://samplemonsta.com/api/update/check?current_version=0.1.0-alpha&product=samplar"
```

Expected: HTTP 200, `update_available: false`.

---

## Self-Review Checklist (run after writing this plan)

1. **Spec coverage:**
   - Section 1 (architecture) ? Phases 0, 3, 7 ?
   - Section 2 (KEEP) ? implicit in Phase 1 (delete everything else) ?
   - Section 3 (DELETE) ? Phase 1 tasks 1.1-1.5 ?
   - Section 4 (rename) ? Phase 3 tasks 3.1-3.10 ?
   - Section 5 (license SKU) ? Phase 5 + Phase 6 ?
   - Section 6 (python/models) ? Phase 4 ?
   - Section 7 (build gates) ? Phases 7, 8, 9, 10, 11 ?
   - Risk register (MidiMappingService entanglement, BinaryData breakage, SModePage deps) ? addressed in Task 7.2 triage steps ?

2. **Placeholder scan:** No "TBD" / "TODO" / "implement later" in any task body. (Confirmed.)

3. **Type consistency:** `samplar::` namespace used throughout. CMake target names consistent (`SamplarApp`, `SamplarPlugin`). Pointer file path consistent (`%APPDATA%\Samplar\portable_root.txt`, NOT `Samplar`).

4. **Ambiguity check:**
   - "Hold for verification" in Task 1.3 — explicitly resolved by Step 1 grep. ?
   - BACKGROUNDS audit in Task 4.4 — explicitly listed (grep + per-file copy). ?
   - License feature flags in Task 6.3 — explicit "binary licensing for v0.1, no flags." ?

---

## Total estimated time

For a developer familiar with SAMPLAR: **2-4 days of focused work**, mostly in Phase 7 Task 7.2 (compile error iteration). Phases 0-6 are mechanical (1 day combined). Phases 8-11 are quick verification (a few hours combined) once Gate 1 passes.
