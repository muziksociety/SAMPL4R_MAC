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
if (-not $Version) {
    try {
        Push-Location $RepoRoot
        $tag = git describe --tags --abbrev=0 2>$null
        Pop-Location
        if ($LASTEXITCODE -eq 0 -and $tag) { $Version = $tag -replace '^v','' } else { $Version = 'dev' }
    } catch { $Version = 'dev' }
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
