#requires -Version 5.1
<#
.SYNOPSIS
  Build SAMPLAR VST3 + produce a single-file Inno Setup installer.

.DESCRIPTION
  1. Configures + builds SamplarPlugin_VST3 (Release).
     CMake POST_BUILD stages all resources INTO the .vst3 bundle's
     Contents/x86_64-win/ directory, producing a self-contained bundle.
  2. Downloads vc_redist.x64.exe to portable_cache/ if not present.
  3. Locates Inno Setup's ISCC.exe.
  4. Invokes ISCC on release_assets/installer/samplar.iss to produce
     release_assets/installer/output/SAMPLAR-Setup-<version>.exe.

.PARAMETER Version
  Version string baked into the installer filename and metadata.
  Defaults to the latest git tag (without "v") or "dev".

.PARAMETER SkipBuild
  Skip the cmake build (use existing build/SamplarPlugin_artefacts/...).
#>
[CmdletBinding()]
param(
    [string]$Version = '',
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# ---------------------------------------------------------------
# Paths
# ---------------------------------------------------------------
$ScriptDir   = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot    = Split-Path -Parent $ScriptDir
$BuildDir    = Join-Path $RepoRoot 'build'
$CacheDir    = Join-Path $RepoRoot 'portable_cache'
$InstallerDir = Join-Path $ScriptDir 'installer'
$IssFile     = Join-Path $InstallerDir 'samplar.iss'
$VcRedistDst = Join-Path $CacheDir 'vc_redist.x64.exe'

function Log-Info($m)  { Write-Host "[INFO]    $m" -ForegroundColor Cyan }
function Log-Ok($m)    { Write-Host "[OK]      $m" -ForegroundColor Green }
function Log-Warn($m)  { Write-Host "[WARN]    $m" -ForegroundColor Yellow }
function Log-Err($m)   { Write-Host "[ERR]     $m" -ForegroundColor Red }

# ---------------------------------------------------------------
# Resolve version
# ---------------------------------------------------------------
# Single source of truth: CMakeLists.txt `project(samplar VERSION x.y.z)`.
# (The same value is compiled into the plugin, so installer and binary
# can never disagree.)
if (-not $Version) {
    $cmakeText = Get-Content -Raw -LiteralPath (Join-Path $RepoRoot 'CMakeLists.txt')
    $m = [regex]::Match($cmakeText, 'project\(\s*samplar\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)')
    if ($m.Success) { $Version = $m.Groups[1].Value }
    else { Log-Err "Could not parse project(samplar VERSION x.y.z) from CMakeLists.txt"; exit 1 }
}
Log-Info "Build version : $Version"
Log-Info "Repo root     : $RepoRoot"
Log-Info "Installer     : $IssFile"

# ---------------------------------------------------------------
# Build VST3
# ---------------------------------------------------------------
if (-not $SkipBuild) {
    Log-Info "Configuring CMake..."
    & cmake -B $BuildDir -S $RepoRoot -G "Visual Studio 17 2022" -A x64 -DSAMPLAR_BUILD_STANDALONE=OFF
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }

    Log-Info "Building SamplarPlugin_VST3 Release..."
    & cmake --build $BuildDir --config Release --target SamplarPlugin_VST3
    if ($LASTEXITCODE -ne 0) { throw "Build failed." }
} else {
    Log-Warn "Skipping build (-SkipBuild)."
}

# Verify the built bundle exists
$Vst3Bundle = Join-Path $BuildDir 'SamplarPlugin_artefacts\Release\VST3\Samplar.vst3'
if (-not (Test-Path $Vst3Bundle)) {
    throw "VST3 bundle not found at $Vst3Bundle - did the build succeed?"
}
Log-Ok "VST3 bundle: $Vst3Bundle"

# ---------------------------------------------------------------
# Preflight: HARD FAIL if any runtime DLL / model is missing, has the wrong
# bits, or a stale model would ship. (GPU stem separation = ORT 1.19.2
# DirectML build + DirectML 1.15.1 + htdemucs_body.onnx, same bits as SM5.)
# ---------------------------------------------------------------
$BinDir = Join-Path $Vst3Bundle 'Contents\x86_64-win'
$Required = @(
    @{ Path = 'SAMPLAR.vst3';               Sha = $null },
    @{ Path = 'onnxruntime.dll';            Sha = '410de3464172dae2945a4663f274e34c59965bc8233b0411de16baab56dda1a7' },
    @{ Path = 'DirectML.dll';               Sha = 'f3b672b9fe6d8228ae978e74217a9bb1d6605643550be166eab15f1c09eddb86' },
    @{ Path = 'models\htdemucs_body.onnx';  Sha = '236be9938a4ef30dd388817b87d6144a5fd7f197b3f6961a9092f9b6d0c6f2c5' },
    @{ Path = 'models\beat_this.onnx';      Sha = $null },
    @{ Path = 'data\samplar_bundle.marker'; Sha = $null },
    @{ Path = '..\Resources\licenses\DirectML-LICENSE.txt';  Sha = $null },
    @{ Path = '..\Resources\licenses\onnxruntime-LICENSE.txt'; Sha = $null }
)
$Forbidden = @('models\htdemucs.onnx', 'onnxruntime_providers_shared.dll', 'tools', 'BACKGROUNDS', 'Samples')
$preflightErrors = @()
foreach ($r in $Required) {
    $f = Join-Path $BinDir $r.Path
    if (-not (Test-Path -LiteralPath $f)) { $preflightErrors += "missing: $($r.Path)"; continue }
    $len = (Get-Item -LiteralPath $f).Length
    if ($len -le 0) { $preflightErrors += "empty: $($r.Path)"; continue }
    if ($r.Sha) {
        $h = (Get-FileHash -Algorithm SHA256 -LiteralPath $f).Hash.ToLower()
        if ($h -ne $r.Sha) { $preflightErrors += "wrong bits: $($r.Path) sha256=$h (expected $($r.Sha))"; continue }
    }
    Log-Ok ("preflight: {0} ({1:N1} MB)" -f $r.Path, ($len / 1MB))
}
foreach ($p in $Forbidden) {
    if (Test-Path -LiteralPath (Join-Path $BinDir $p)) { $preflightErrors += "must not ship: $p" }
}
if ($preflightErrors.Count -gt 0) {
    foreach ($e in $preflightErrors) { Log-Err "preflight: $e" }
    throw "Bundle preflight failed ($($preflightErrors.Count) problem(s)) - fix the VST3 staging before building the installer."
}
Log-Ok "Bundle preflight passed (GPU runtime + single separation model)"

# ---------------------------------------------------------------
# Ensure VC++ Redist is cached
# ---------------------------------------------------------------
New-Item -ItemType Directory -Force -Path $CacheDir | Out-Null
if (-not (Test-Path $VcRedistDst)) {
    Log-Info "Downloading vc_redist.x64.exe..."
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -Uri 'https://aka.ms/vs/17/release/vc_redist.x64.exe' -OutFile $VcRedistDst -UseBasicParsing
        Log-Ok "Cached vc_redist.x64.exe at $VcRedistDst"
    } catch {
        throw "vc_redist.x64.exe download failed: $($_.Exception.Message)"
    }
} else {
    Log-Ok "vc_redist.x64.exe already cached"
}

# ---------------------------------------------------------------
# Locate ISCC.exe (Inno Setup compiler)
# ---------------------------------------------------------------
$Iscc = $null
foreach ($candidate in @(
    'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
    'C:\Program Files\Inno Setup 6\ISCC.exe',
    'C:\Program Files (x86)\Inno Setup 5\ISCC.exe'
)) {
    if (Test-Path $candidate) { $Iscc = $candidate; break }
}
if (-not $Iscc) {
    # Try PATH
    $found = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($found) { $Iscc = $found.Source }
}
if (-not $Iscc) {
    throw "Inno Setup not found. Install from https://jrsoftware.org/isdl.php and re-run."
}
Log-Ok "Inno Setup: $Iscc"

# ---------------------------------------------------------------
# Compile installer
# ---------------------------------------------------------------
$OutputDir = Join-Path $InstallerDir 'output'
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

Log-Info "Compiling installer..."
$sw = [System.Diagnostics.Stopwatch]::StartNew()
& $Iscc "/DMyAppVersion=$Version" $IssFile
$sw.Stop()
if ($LASTEXITCODE -ne 0) { throw "ISCC failed (exit $LASTEXITCODE)." }

$SetupExe = Join-Path $OutputDir "SAMPLAR-Setup-$Version.exe"
if (-not (Test-Path $SetupExe)) {
    throw "ISCC reported success but $SetupExe not found."
}

$sizeMB = [math]::Round((Get-Item $SetupExe).Length / 1MB, 1)
Log-Ok ("Installer built in {0}s : {1} ({2} MB)" -f [int]$sw.Elapsed.TotalSeconds, $SetupExe, $sizeMB)

Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  SAMPLAR installer ready" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  Path : $SetupExe"
Write-Host "  Size : $sizeMB MB"
Write-Host "  Version: $Version"
Write-Host ""
Write-Host "  Distribute that .exe directly. Users double-click,"
Write-Host "  UAC-elevate, and the VST3 is registered at"
Write-Host "  C:\Program Files\Common Files\VST3\Samplar.vst3"
Write-Host ""
