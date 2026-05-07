#!/usr/bin/env pwsh
# Rebuild latest SAMPLAR and package a portable folder
# Usage examples:
#   ./make_portable.ps1                    # Release build + portable
#   ./make_portable.ps1 -Config Debug      # Debug build + portable (for testing)
#   ./make_portable.ps1 -Zip               # Also create ZIP archive
#   ./make_portable.ps1 -SkipBuild         # Just re-package from existing build

[CmdletBinding()]
param(
  [ValidateSet('Release','Debug')]
  [string]$Config = 'Release',
  [switch]$Zip,
  [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'

function Write-Section($text) {
  Write-Host "`n========================================" -ForegroundColor Cyan
  Write-Host ("  " + $text) -ForegroundColor Cyan
  Write-Host "========================================" -ForegroundColor Cyan
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $scriptRoot

# Build tree lives alongside repo root at ..\build\sm5 (consistent with current project layout)
$BuildDir = Resolve-Path (Join-Path $scriptRoot "..\build\sm5") -ErrorAction SilentlyContinue
if (-not $BuildDir) {
  $BuildDir = Join-Path $scriptRoot "..\build\sm5"
  New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
}

Write-Section "SAMPLAR - Rebuild & Portable Package ($Config)"

if (-not $SkipBuild) {
  # Configure if needed
  $cache = Join-Path $BuildDir "CMakeCache.txt"
  if (-not (Test-Path $cache)) {
    Write-Host "Configuring CMake build tree..." -ForegroundColor Yellow
    & cmake -S "$scriptRoot" -B "$BuildDir" | Write-Host
  }

  Write-Host "Building ($Config)..." -ForegroundColor Yellow
  & cmake --build "$BuildDir" --config $Config -j 16 --target SM5App
  if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed. See output above."
  }
} else {
  Write-Host "Skipping build as requested (-SkipBuild)." -ForegroundColor Yellow
}

$ArtefactsDir = Join-Path $BuildDir "SM5App_artefacts\$Config"
$ExePath      = Join-Path $ArtefactsDir "SAMPLAR.exe"

if (-not (Test-Path $ExePath)) {
  throw "Expected executable not found: $ExePath"
}

Write-Host "Build OK -> $ExePath" -ForegroundColor Green

# Create/refresh the portable folder using existing script
Write-Host "Creating portable folder via build_portable.ps1..." -ForegroundColor Yellow
& "$scriptRoot\build_portable.ps1"
if ($LASTEXITCODE -ne 0) {
  throw "Portable packaging failed."
}

$PortableDir = Join-Path $scriptRoot "Samplar_Portable"

if ($Zip) {
  $ZipPath = Join-Path $scriptRoot "Samplar_Portable.zip"
  Write-Host "Creating ZIP: $ZipPath" -ForegroundColor Yellow
  if (Test-Path $ZipPath) { Remove-Item -Force $ZipPath }
  Compress-Archive -Path $PortableDir -DestinationPath $ZipPath -Force
  $zipSizeMB = [math]::Round(((Get-Item $ZipPath).Length / 1MB), 2)
  Write-Host "ZIP created ($zipSizeMB MB): $ZipPath" -ForegroundColor Green
}

Write-Host "`nDone. Portable output:" -ForegroundColor Green
Write-Host "  Folder: $PortableDir" -ForegroundColor Cyan
if ($Zip) { Write-Host "  ZIP:    $ZipPath" -ForegroundColor Cyan }

Write-Host ""
Write-Host "NEXT STEP:" -ForegroundColor Yellow
Write-Host "  To create the installer, run:" -ForegroundColor White
Write-Host "  .\make_installer.ps1" -ForegroundColor Cyan
Write-Host ""
