---
name: samplar-release
description: Build the SAMPLAR commercial Inno Setup installer. Mandatory preflight verifies the two ONNX models (htdemucs.onnx + beat_this.onnx) are present so they can NEVER be silently omitted from a release.
---

# SAMPLAR Release Skill

> **Invoke this skill any time you build the commercial installer.**
> It enforces the contract: the installer MUST ship with both ONNX models or the build is aborted.

## The non-negotiable preflight

Two model files MUST be present in the repo before producing an installer. If either is missing, **stop and fail loudly**. These are the only two things that have caused silent-broken releases in the past.

| Model | Required path (repo-relative) | Approx size |
|---|---|---|
| **Demucs 4-stem (audio separation)** | `models/htdemucs.onnx` | ~290 MB |
| **Beat This! (tempo detection)** | `third_party/beat_this/models/beat_this.onnx` | ~85 MB |

### Preflight check (run this FIRST, every time)

```powershell
$repo = (Resolve-Path 'b:\SANTI\VST PROJECT\SAMPLAR').Path
$required = @{
    'Demucs htdemucs.onnx' = "$repo\models\htdemucs.onnx"
    'Beat This! ONNX'      = "$repo\third_party\beat_this\models\beat_this.onnx"
}
$fail = $false
foreach ($name in $required.Keys) {
    $p = $required[$name]
    if (-not (Test-Path $p)) {
        Write-Host "[FAIL] $name MISSING at $p" -ForegroundColor Red
        $fail = $true
    } else {
        $mb = [math]::Round((Get-Item $p).Length/1MB,1)
        if ($mb -lt 10) {
            Write-Host "[FAIL] $name suspiciously small ($mb MB) at $p" -ForegroundColor Red
            $fail = $true
        } else {
            Write-Host "[OK]   $name ($mb MB)"
        }
    }
}
if ($fail) {
    Write-Error "ABORT: required ONNX models missing. Do NOT ship without them."
    exit 1
}
```

If the preflight fails, **DO NOT** proceed. Source the missing model:
- `htdemucs.onnx` — copy from `b:\SANTI\SAMPLE_MONSTA\sample_monsta4\Sample_monsta5\resources\models\htdemucs.onnx` (the SM5 ONNX port) OR re-export from `smank/htdemucs-onnx` on HuggingFace.
- `beat_this.onnx` — copy from `b:\SANTI\SAMPLE_MONSTA\sample_monsta4\Sample_monsta5\third_party\beat_this\models\beat_this.onnx` OR re-export from the Beat This! repo.

## Release workflow

### Step 1 — Decide the version

Pick the next semver tag. Bump the project version in `CMakeLists.txt` (THREE spots):
- Line ~7: `project(samplar VERSION X.Y.Z LANGUAGES CXX)`
- The two `VERSION "X.Y.Z"` lines under `juce_add_plugin(...)` and the standalone target

Verify:
```powershell
grep -n "project(samplar VERSION\|VERSION \"1\." "b:\SANTI\VST PROJECT\SAMPLAR\CMakeLists.txt"
```
All three must read the same `X.Y.Z`.

### Step 2 — Preflight (mandatory)

Run the preflight script above. **Do not proceed if it fails.**

### Step 3 — Build the installer

```powershell
Set-Location 'b:\SANTI\VST PROJECT\SAMPLAR'
& powershell -NoLogo -NoProfile -ExecutionPolicy Bypass `
    -File '.\release_assets\build_installer.ps1' `
    -Version 'X.Y.Z'
```

This:
1. Configures CMake.
2. Builds `SamplarPlugin_VST3` Release.
3. POST_BUILD stages all resources INTO the `.vst3` bundle's `Contents\x86_64-win\` directory.
4. Downloads `vc_redist.x64.exe` to `portable_cache\` if not already cached.
5. Locates `ISCC.exe` (Inno Setup 6).
6. Compiles `release_assets\installer\samplar.iss`.
7. Output: `release_assets\installer\output\SAMPLAR-Setup-X.Y.Z.exe` (~459 MB at the time of writing).

### Step 4 — Verify the installer contains both models

After build, the bundle at `build\SamplarPlugin_artefacts\Release\VST3\Samplar.vst3\Contents\x86_64-win\models\` MUST contain both files:

```powershell
$bundle = 'b:\SANTI\VST PROJECT\SAMPLAR\build\SamplarPlugin_artefacts\Release\VST3\Samplar.vst3\Contents\x86_64-win\models'
foreach ($m in 'htdemucs.onnx','beat_this.onnx') {
    if (Test-Path "$bundle\$m") {
        $mb = [math]::Round((Get-Item "$bundle\$m").Length/1MB,1)
        Write-Host "[OK]   bundled: $m ($mb MB)"
    } else {
        Write-Host "[FAIL] $m MISSING from bundle - INSTALLER IS BROKEN" -ForegroundColor Red
    }
}
```

If either is missing the installer is broken even if it compiled. Re-check the POST_BUILD step in `CMakeLists.txt` around `_SAMPLAR_VST3_STAGE_SCRIPT`.

### Step 5 — Commit + tag

```powershell
cd 'b:\SANTI\VST PROJECT\SAMPLAR'
git add CMakeLists.txt
git commit -m "release: bump version to X.Y.Z"
git tag -a "vX.Y.Z" -m "SAMPLAR vX.Y.Z"
git push origin master
git push origin "vX.Y.Z"
```

### Step 6 — Smoke-test the installer

On a clean machine (or after uninstalling the dev VST3):
1. Run `SAMPLAR-Setup-X.Y.Z.exe` — accepts UAC, completes silently.
2. Confirm `C:\Program Files\Common Files\VST3\Samplar.vst3\Contents\x86_64-win\` contains:
   - `SAMPLAR.vst3` (binary)
   - `onnxruntime.dll`
   - `models\htdemucs.onnx`
   - `models\beat_this.onnx`
   - `BACKGROUNDS\`, `Samples\`, `data\`, `tools\ffmpeg.exe`
3. Open a DAW, rescan VST3 plugins, load SAMPLAR.
4. Drop a song in, run S-Mode separation, confirm all 4 stems produced (Vocals/Bass/Drums/Melody).
5. Confirm tempo detection works (Beat This! ONNX model loads + reports BPM).

## Red flags / things that have gone wrong before

1. **POST_BUILD staging skipped models silently** — the `samplar_vst3_stage.ps1` script wraps copies in `try/catch` with `SilentlyContinue`. If the source `models/htdemucs.onnx` is missing, the build still succeeds but the installer ships without the model. **Preflight prevents this.**
2. **Wrong source path** for `beat_this.onnx` — it lives at `third_party/beat_this/models/beat_this.onnx`, NOT `models/beat_this.onnx`. The POST_BUILD has it right; the preflight enforces it.
3. **Forgetting to bump version in all 3 CMakeLists spots** — the JUCE-reported plugin version will diverge from the installer filename if you only bump one. Always grep for the old version after bumping.
4. **Inno Setup not on PATH** — `build_installer.ps1` falls back to `C:\Program Files (x86)\Inno Setup 6\ISCC.exe`. If Inno is installed elsewhere, set the path explicitly or add Inno to PATH.
5. **vc_redist.x64.exe download fails** — the script downloads from `aka.ms/vs/17/release/vc_redist.x64.exe`. If offline, drop the file manually at `portable_cache\vc_redist.x64.exe` and re-run.

## Critical reminders

- **NEVER** ship an installer without running the model preflight. Without ONNX models the plugin silently refuses to separate audio or detect tempo and users will assume the product is broken.
- **NEVER** distribute the build artefact's `.vst3` bundle directly. Always go through the Inno installer so users get the VC++ runtime, registry entry, and uninstall support.
- **NEVER** install over a portable layout — the legacy `portable_root.txt` mechanism is no longer used (deprecated 2026-05-18). Delete `%APPDATA%\Samplar\portable_root.txt` if it exists.
