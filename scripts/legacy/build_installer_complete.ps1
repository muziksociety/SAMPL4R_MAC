#!/usr/bin/env pwsh
# Complete Installer Build Script for SAMPLAR
# This script:
# 1. Creates the application icon from REF.png
# 2. Rebuilds the application with the icon embedded
# 3. Creates the portable version
# 4. Builds the installer with VC++ redistributable bundled

[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [switch]$SkipIcon,
    [ValidateSet('Release','Debug')]
    [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'

function Write-Section($text) {
    Write-Host "`n========================================" -ForegroundColor Cyan
    Write-Host ("  " + $text) -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
}

function Write-Step($text) {
    Write-Host "`n>>> $text" -ForegroundColor Yellow
}

function Write-Success($text) {
    Write-Host "[SUCCESS] $text" -ForegroundColor Green
}

function Write-Error-Custom($text) {
    Write-Host "[ERROR] $text" -ForegroundColor Red
}

function Write-Warning-Custom($text) {
    Write-Host "[WARNING] $text" -ForegroundColor Yellow
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $scriptRoot

Write-Section "SAMPLAR - Complete Installer Build"
Write-Host "Configuration: $Config" -ForegroundColor Cyan
Write-Host ""

# ============================================
# STEP 1: Create Icon
# ============================================
if (-not $SkipIcon) {
    Write-Section "STEP 1: Creating Application Icon"
    
    $iconFile = "Samplar.ico"
    $refPng = "plugins\REF.png"
    
    if (-not (Test-Path $refPng)) {
        Write-Error-Custom "Source image not found: $refPng"
        exit 1
    }
    
    # Check if ImageMagick is available
    $magickPath = Get-Command "magick" -ErrorAction SilentlyContinue
    
    if ($magickPath) {
        Write-Step "Using ImageMagick to create multi-resolution icon..."
        & magick convert "$refPng" -define icon:auto-resize=256,128,64,48,32,16 "$iconFile"
        
        if ($LASTEXITCODE -eq 0) {
            $icoSize = (Get-Item $iconFile).Length / 1KB
            Write-Success "Icon created: $iconFile ($([math]::Round($icoSize, 2)) KB)"
            Write-Host "  Resolutions: 256x256, 128x128, 64x64, 48x48, 32x32, 16x16" -ForegroundColor Gray
        } else {
            Write-Error-Custom "ImageMagick conversion failed"
            exit 1
        }
    } else {
        Write-Warning-Custom "ImageMagick not found. Using .NET fallback (single resolution)..."
        
        Add-Type -AssemblyName System.Drawing
        
        try {
            $bitmap = [System.Drawing.Bitmap]::FromFile((Resolve-Path $refPng).Path)
            $icon256 = New-Object System.Drawing.Icon($bitmap.GetHicon())
            $fileStream = [System.IO.File]::Create($iconFile)
            $icon256.Save($fileStream)
            $fileStream.Close()
            $bitmap.Dispose()
            $icon256.Dispose()
            
            $icoSize = (Get-Item $iconFile).Length / 1KB
            Write-Success "Icon created: $iconFile ($([math]::Round($icoSize, 2)) KB)"
            Write-Warning-Custom "Single resolution only. Install ImageMagick for better quality:"
            Write-Host "  winget install ImageMagick.ImageMagick" -ForegroundColor Cyan
        }
        catch {
            Write-Error-Custom "Failed to create icon: $_"
            exit 1
        }
    }
} else {
    Write-Section "STEP 1: Skipping Icon Creation"
    Write-Host "Using existing icon file" -ForegroundColor Gray
}

# ============================================
# STEP 2: Build Application
# ============================================
if (-not $SkipBuild) {
    Write-Section "STEP 2: Building Application ($Config)"
    
    $BuildDir = Resolve-Path (Join-Path $scriptRoot "..\build\sm5") -ErrorAction SilentlyContinue
    if (-not $BuildDir) {
        $BuildDir = Join-Path $scriptRoot "..\build\sm5"
        New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
    }
    
    # Configure if needed
    $cache = Join-Path $BuildDir "CMakeCache.txt"
    if (-not (Test-Path $cache)) {
        Write-Step "Configuring CMake build tree..."
        & cmake -S "$scriptRoot" -B "$BuildDir"
        if ($LASTEXITCODE -ne 0) {
            Write-Error-Custom "CMake configuration failed"
            exit 1
        }
    }
    
    Write-Step "Building ($Config)..."
    & cmake --build "$BuildDir" --config $Config -j 16 --target SM5App
    if ($LASTEXITCODE -ne 0) {
        Write-Error-Custom "Build failed"
        exit 1
    }
    
    $ArtefactsDir = Join-Path $BuildDir "SM5App_artefacts\$Config"
    $ExePath = Join-Path $ArtefactsDir "SAMPLAR.exe"
    
    if (-not (Test-Path $ExePath)) {
        Write-Error-Custom "Expected executable not found: $ExePath"
        exit 1
    }
    
    $exeSize = (Get-Item $ExePath).Length / 1MB
    Write-Success "Build complete: $ExePath ($([math]::Round($exeSize, 2)) MB)"
} else {
    Write-Section "STEP 2: Skipping Build"
    Write-Host "Using existing build artifacts" -ForegroundColor Gray
}

# ============================================
# STEP 3: Create Portable Version
# ============================================
Write-Section "STEP 3: Creating Portable Version"

Write-Step "Running build_portable.ps1..."
& "$scriptRoot\build_portable.ps1"
if ($LASTEXITCODE -ne 0) {
    Write-Error-Custom "Portable packaging failed"
    exit 1
}

$PortableDir = Join-Path $scriptRoot "Samplar_Portable"
if (-not (Test-Path $PortableDir)) {
    Write-Error-Custom "Portable directory not created: $PortableDir"
    exit 1
}

$portableSize = (Get-ChildItem $PortableDir -Recurse -File | Measure-Object -Property Length -Sum).Sum / 1MB
Write-Success "Portable version created: $PortableDir ($([math]::Round($portableSize, 2)) MB)"

# ============================================
# STEP 4: Check Prerequisites
# ============================================
Write-Section "STEP 4: Checking Installer Prerequisites"

# Check for Inno Setup
$innoSetupPath = "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
if (-not (Test-Path $innoSetupPath)) {
    Write-Error-Custom "Inno Setup not found at: $innoSetupPath"
    Write-Host ""
    Write-Host "Please install Inno Setup 6:" -ForegroundColor Yellow
    Write-Host "  Download: https://jrsoftware.org/isdl.php" -ForegroundColor Cyan
    Write-Host "  Or use winget: winget install JRSoftware.InnoSetup" -ForegroundColor Cyan
    exit 1
}
Write-Success "Inno Setup found: $innoSetupPath"

# Check for VC++ Redistributable
$vcRedistPath = "installer_dependencies\vc_redist.x64.exe"
if (-not (Test-Path $vcRedistPath)) {
    Write-Warning-Custom "VC++ Redistributable not found: $vcRedistPath"
    Write-Host ""
    Write-Host "Downloading VC++ Redistributable..." -ForegroundColor Yellow
    
    # Create directory if it doesn't exist
    $vcRedistDir = "installer_dependencies"
    if (-not (Test-Path $vcRedistDir)) {
        New-Item -ItemType Directory -Path $vcRedistDir | Out-Null
    }
    
    # Download VC++ Redistributable
    $vcRedistUrl = "https://aka.ms/vs/17/release/vc_redist.x64.exe"
    try {
        Invoke-WebRequest -Uri $vcRedistUrl -OutFile $vcRedistPath -UseBasicParsing
        $vcSize = (Get-Item $vcRedistPath).Length / 1MB
        Write-Success "Downloaded VC++ Redistributable ($([math]::Round($vcSize, 2)) MB)"
    }
    catch {
        Write-Error-Custom "Failed to download VC++ Redistributable: $_"
        Write-Host ""
        Write-Host "Please download manually:" -ForegroundColor Yellow
        Write-Host "  URL: $vcRedistUrl" -ForegroundColor Cyan
        Write-Host "  Save to: $vcRedistPath" -ForegroundColor Cyan
        exit 1
    }
} else {
    $vcSize = (Get-Item $vcRedistPath).Length / 1MB
    Write-Success "VC++ Redistributable found ($([math]::Round($vcSize, 2)) MB)"
}

# ============================================
# STEP 5: Build Installer
# ============================================
Write-Section "STEP 5: Building Installer"

$installerScript = "installer.iss"
if (-not (Test-Path $installerScript)) {
    Write-Error-Custom "Installer script not found: $installerScript"
    exit 1
}

Write-Step "Compiling installer with Inno Setup..."
& $innoSetupPath $installerScript
if ($LASTEXITCODE -ne 0) {
    Write-Error-Custom "Installer compilation failed"
    exit 1
}

# ============================================
# STEP 6: Verify Output
# ============================================
Write-Section "STEP 6: Verifying Installer"

$installerOutput = "installer_output\Samplar_Setup_v1.0.0.exe"
if (-not (Test-Path $installerOutput)) {
    Write-Error-Custom "Installer not created: $installerOutput"
    exit 1
}

$installerSize = (Get-Item $installerOutput).Length / 1MB
Write-Success "Installer created: $installerOutput ($([math]::Round($installerSize, 2)) MB)"

# ============================================
# COMPLETION SUMMARY
# ============================================
Write-Section "BUILD COMPLETE!"

Write-Host ""
Write-Host "OUTPUT FILES:" -ForegroundColor Cyan
Write-Host "  Icon:      Samplar.ico" -ForegroundColor White
Write-Host "  Portable:  $PortableDir" -ForegroundColor White
Write-Host "  Installer: $installerOutput" -ForegroundColor White
Write-Host ""
Write-Host "INSTALLER FEATURES:" -ForegroundColor Cyan
Write-Host "  ✓ Embeds application icon" -ForegroundColor Green
Write-Host "  ✓ Installs VC++ Redistributable automatically" -ForegroundColor Green
Write-Host "  ✓ Creates desktop shortcut (user selectable)" -ForegroundColor Green
Write-Host "  ✓ Creates Start Menu shortcuts" -ForegroundColor Green
Write-Host "  ✓ Includes all dependencies" -ForegroundColor Green
Write-Host "  ✓ Professional uninstaller" -ForegroundColor Green
Write-Host ""
Write-Host "NEXT STEPS:" -ForegroundColor Yellow
Write-Host "  1. Test the installer on a clean Windows machine" -ForegroundColor White
Write-Host "  2. Verify the icon appears correctly" -ForegroundColor White
Write-Host "  3. Confirm desktop shortcut works" -ForegroundColor White
Write-Host "  4. Check that VC++ redist installs silently" -ForegroundColor White
Write-Host ""
Write-Host "To test the installer:" -ForegroundColor Cyan
Write-Host "  .\\$installerOutput" -ForegroundColor White
Write-Host ""

