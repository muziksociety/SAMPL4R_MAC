#!/usr/bin/env pwsh
# Make Installer - Quick installer build script for SAMPLAR
# This script builds the installer from the existing portable version
# Run this after making changes and building the portable version

$ErrorActionPreference = 'Stop'

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  SAMPLAR - Installer Builder" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $scriptRoot

# ============================================
# Step 1: Check Prerequisites
# ============================================
Write-Host ">>> Checking prerequisites..." -ForegroundColor Yellow
Write-Host ""

# Check for Inno Setup
$innoPath = "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
if (-not (Test-Path $innoPath)) {
    Write-Host "[ERROR] Inno Setup not found at: $innoPath" -ForegroundColor Red
    Write-Host ""
    Write-Host "Please install Inno Setup 6:" -ForegroundColor Yellow
    Write-Host "  winget install JRSoftware.InnoSetup" -ForegroundColor Cyan
    Write-Host "  Or download: https://jrsoftware.org/isdl.php" -ForegroundColor Cyan
    exit 1
}
Write-Host "[OK] Inno Setup found" -ForegroundColor Green

# Check for portable folder
$portableDir = "Samplar_Portable"
if (-not (Test-Path $portableDir)) {
    Write-Host "[ERROR] Portable folder not found: $portableDir" -ForegroundColor Red
    Write-Host ""
    Write-Host "Please run make_portable.ps1 first to create the portable version:" -ForegroundColor Yellow
    Write-Host "  .\make_portable.ps1" -ForegroundColor Cyan
    exit 1
}
Write-Host "[OK] Portable folder found" -ForegroundColor Green

# Check for VC++ redistributable
$vcRedist = "installer_dependencies\vc_redist.x64.exe"
if (-not (Test-Path $vcRedist)) {
    Write-Host "[WARNING] VC++ Redistributable not found: $vcRedist" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Downloading VC++ Redistributable..." -ForegroundColor Yellow
    
    # Create directory if needed
    $vcRedistDir = "installer_dependencies"
    if (-not (Test-Path $vcRedistDir)) {
        New-Item -ItemType Directory -Path $vcRedistDir | Out-Null
    }
    
    # Download
    $vcRedistUrl = "https://aka.ms/vs/17/release/vc_redist.x64.exe"
    try {
        Invoke-WebRequest -Uri $vcRedistUrl -OutFile $vcRedist -UseBasicParsing
        $vcSize = (Get-Item $vcRedist).Length / 1048576
        $vcSizeRounded = [math]::Round($vcSize, 2)
        Write-Host "[OK] Downloaded VC++ Redistributable ($vcSizeRounded MB)" -ForegroundColor Green
    }
    catch {
        Write-Host "[ERROR] Failed to download VC++ Redistributable: $_" -ForegroundColor Red
        Write-Host ""
        Write-Host "Please download manually:" -ForegroundColor Yellow
        Write-Host "  URL: $vcRedistUrl" -ForegroundColor Cyan
        Write-Host "  Save to: $vcRedist" -ForegroundColor Cyan
        exit 1
    }
} else {
    Write-Host "[OK] VC++ Redistributable found" -ForegroundColor Green
}

# Check for installer script
if (-not (Test-Path "installer.iss")) {
    Write-Host "[ERROR] Installer script not found: installer.iss" -ForegroundColor Red
    exit 1
}
Write-Host "[OK] Installer script found" -ForegroundColor Green

# Check for icon
if (-not (Test-Path "samplar.ico")) {
    Write-Host "[WARNING] Icon file not found: samplar.ico" -ForegroundColor Yellow
    Write-Host "Creating icon from REF.png..." -ForegroundColor Yellow

    if (Test-Path "plugins\REF.png") {
        & "$scriptRoot\create_icon.ps1"
        if ($LASTEXITCODE -ne 0) {
            Write-Host "[ERROR] Failed to create icon" -ForegroundColor Red
            exit 1
        }
    } else {
        Write-Host "[ERROR] Source image not found: plugins\REF.png" -ForegroundColor Red
        exit 1
    }
} else {
    Write-Host "[OK] Icon file found" -ForegroundColor Green
}

Write-Host ""

# ============================================
# Step 2: Build Installer
# ============================================
Write-Host ">>> Building installer with Inno Setup..." -ForegroundColor Yellow
Write-Host ""

# Run Inno Setup compiler
& $innoPath "installer.iss"

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "[ERROR] Installer compilation failed" -ForegroundColor Red
    exit 1
}

# ============================================
# Step 3: Verify Output
# ============================================
Write-Host ""
Write-Host ">>> Verifying installer..." -ForegroundColor Yellow
Write-Host ""

# Read version from VERSION.txt
$version = "2.0.0"
if (Test-Path "VERSION.txt") {
    $version = (Get-Content "VERSION.txt" -Raw).Trim()
}

$installerOutput = "installer_output\samplar_Setup_v$version.exe"
if (-not (Test-Path $installerOutput)) {
    Write-Host "[ERROR] Installer not created: $installerOutput" -ForegroundColor Red
    exit 1
}

$installerSize = (Get-Item $installerOutput).Length / 1048576
$installerSizeRounded = [math]::Round($installerSize, 2)
$installerTime = (Get-Item $installerOutput).LastWriteTime

Write-Host "[SUCCESS] Installer created!" -ForegroundColor Green
Write-Host ""
Write-Host "  File: $installerOutput" -ForegroundColor Cyan
Write-Host "  Size: $installerSizeRounded MB" -ForegroundColor Cyan
Write-Host "  Created: $installerTime" -ForegroundColor Cyan

# ============================================
# Summary
# ============================================
Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "  INSTALLER BUILD COMPLETE!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""

Write-Host "OUTPUT:" -ForegroundColor Cyan
Write-Host "  $installerOutput" -ForegroundColor White
Write-Host "  Size: $installerSizeRounded MB" -ForegroundColor White
Write-Host ""

Write-Host "FEATURES:" -ForegroundColor Cyan
Write-Host "  ✓ SAMPLAR icon embedded" -ForegroundColor Green
Write-Host "  ✓ VC++ Redistributable bundled (auto-install)" -ForegroundColor Green
Write-Host "  ✓ Desktop shortcut with icon" -ForegroundColor Green
Write-Host "  ✓ Start Menu shortcuts" -ForegroundColor Green
Write-Host "  ✓ All dependencies included" -ForegroundColor Green
Write-Host "  ✓ Professional uninstaller" -ForegroundColor Green
Write-Host ""

Write-Host "NEXT STEPS:" -ForegroundColor Yellow
Write-Host "  1. Test the installer:" -ForegroundColor White
Write-Host "     $installerOutput" -ForegroundColor Cyan
Write-Host ""
Write-Host "  2. Verify VC++ installation:" -ForegroundColor White
Write-Host "     .\test_vcredist_detection.ps1" -ForegroundColor Cyan
Write-Host ""
Write-Host "  3. Distribute to users!" -ForegroundColor White
Write-Host ""

# Open output folder
$response = Read-Host "Open installer output folder? (Y/n)"
if ($response -ne 'n' -and $response -ne 'N') {
    explorer.exe "installer_output"
}

Write-Host ""
Write-Host "Done! 🚀" -ForegroundColor Green
Write-Host ""

