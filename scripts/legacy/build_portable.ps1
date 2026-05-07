#!/usr/bin/env pwsh
# Build Portable Version of SAMPLAR
# Creates a self-contained folder that can be copied to any Windows computer

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  SAMPLAR - Portable Builder" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Configuration
$portableDir = "Samplar_Portable"
$releaseDir = "..\build\sm5\SM5App_artefacts\Release"

# Clean old portable build
if (Test-Path $portableDir) {
    Write-Host "Cleaning old portable build..." -ForegroundColor Yellow
    Remove-Item -Path $portableDir -Recurse -Force
}

# Create portable directory structure
Write-Host "Creating portable directory structure..." -ForegroundColor Cyan
New-Item -ItemType Directory -Path $portableDir | Out-Null
New-Item -ItemType Directory -Path "$portableDir\models" | Out-Null
New-Item -ItemType Directory -Path "$portableDir\plugins" | Out-Null
New-Item -ItemType Directory -Path "$portableDir\python_bridge" | Out-Null
New-Item -ItemType Directory -Path "$portableDir\assets" | Out-Null
New-Item -ItemType Directory -Path "$portableDir\logs" | Out-Null

# Copy main executable
Write-Host "Copying main executable..." -ForegroundColor Cyan
Copy-Item -Path "$releaseDir\SAMPLAR.exe" -Destination $portableDir
$exeSize = (Get-Item "$portableDir\SAMPLAR.exe").Length / 1MB
Write-Host "  [OK] SAMPLAR.exe ($([math]::Round($exeSize, 2)) MB)" -ForegroundColor Green

# Copy OpenBLAS DLL
Write-Host "Copying OpenBLAS DLL..." -ForegroundColor Cyan
Copy-Item -Path "$releaseDir\libopenblas.dll" -Destination $portableDir
$blasSize = (Get-Item "$portableDir\libopenblas.dll").Length / 1MB
Write-Host "  [OK] libopenblas.dll ($([math]::Round($blasSize, 2)) MB)" -ForegroundColor Green

# Copy Demucs C++ CLI executable (REQUIRED for full song processing)
Write-Host "Copying Demucs C++ CLI..." -ForegroundColor Cyan
$demucsCli = "..\build\sm5\third_party\demucs.cpp\Release\demucs_mt.cpp.main.exe"
if (Test-Path $demucsCli) {
    Copy-Item -Path $demucsCli -Destination $portableDir
    $cliSize = (Get-Item "$portableDir\demucs_mt.cpp.main.exe").Length / 1MB
    Write-Host "  [OK] demucs_mt.cpp.main.exe ($([math]::Round($cliSize, 2)) MB)" -ForegroundColor Green
} else {
    Write-Host "  [ERROR] demucs_mt.cpp.main.exe not found!" -ForegroundColor Red
    Write-Host "  Full song processing will NOT work without this file!" -ForegroundColor Red
    Write-Host "  Expected location: $demucsCli" -ForegroundColor Yellow
}

# Copy Demucs model
Write-Host "Copying Demucs model..." -ForegroundColor Cyan
Copy-Item -Path "models\ggml-model-htdemucs-4s-f16.bin" -Destination "$portableDir\models\"
$modelSize = (Get-Item "$portableDir\models\ggml-model-htdemucs-4s-f16.bin").Length / 1MB
Write-Host "  [OK] ggml-model-htdemucs-4s-f16.bin ($([math]::Round($modelSize, 2)) MB)" -ForegroundColor Green

# Copy Python bridge (DEPRECATED - kept for backward compatibility)
Write-Host "Copying Python bridge (deprecated)..." -ForegroundColor Yellow
Copy-Item -Path "$releaseDir\python_bridge\*" -Destination "$portableDir\python_bridge\" -Recurse
Write-Host "  [OK] Python bridge files (not used)" -ForegroundColor Yellow

# Copy REF.png logo
Write-Host "Copying logo..." -ForegroundColor Cyan
Copy-Item -Path "plugins\REF.png" -Destination "$portableDir\plugins\"
Write-Host "  [OK] REF.png" -ForegroundColor Green

# Copy game assets
Write-Host "Copying game assets..." -ForegroundColor Cyan

# Flappy Bird
if (Test-Path "assets\flappy") {
    New-Item -ItemType Directory -Path "$portableDir\assets\flappy" -Force | Out-Null
    Copy-Item -Path "assets\flappy\*" -Destination "$portableDir\assets\flappy\" -Recurse
    $flappyCount = (Get-ChildItem "$portableDir\assets\flappy" -File).Count
    Write-Host "  [OK] Flappy Bird ($flappyCount files)" -ForegroundColor Green
}

# Snake
if (Test-Path "assets\gamesnake") {
    New-Item -ItemType Directory -Path "$portableDir\assets\gamesnake" -Force | Out-Null
    Copy-Item -Path "assets\gamesnake\*" -Destination "$portableDir\assets\gamesnake\" -Recurse
    $snakeCount = (Get-ChildItem "$portableDir\assets\gamesnake" -Recurse -File).Count
    Write-Host "  [OK] Snake ($snakeCount files)" -ForegroundColor Green
}

# Space Shooter
if (Test-Path "assets\shooter") {
    New-Item -ItemType Directory -Path "$portableDir\assets\shooter" -Force | Out-Null
    Copy-Item -Path "assets\shooter\*" -Destination "$portableDir\assets\shooter\" -Recurse
    $shooterCount = (Get-ChildItem "$portableDir\assets\shooter" -File).Count
    Write-Host "  [OK] Space Shooter ($shooterCount files)" -ForegroundColor Green
}

# Background images
if (Test-Path "assets\backgrounds") {
    New-Item -ItemType Directory -Path "$portableDir\assets\backgrounds" -Force | Out-Null
    Copy-Item -Path "assets\backgrounds\*" -Destination "$portableDir\assets\backgrounds\" -Recurse
    $bgCount = (Get-ChildItem "$portableDir\assets\backgrounds" -File).Count
    Write-Host "  [OK] Backgrounds ($bgCount files)" -ForegroundColor Green
}

# Copy VC++ Redistributable (for manual installation if needed)
Write-Host "Copying VC++ Redistributable..." -ForegroundColor Cyan
if (Test-Path "installer_dependencies\vc_redist.x64.exe") {
    Copy-Item -Path "installer_dependencies\vc_redist.x64.exe" -Destination $portableDir
    $vcSize = (Get-Item "$portableDir\vc_redist.x64.exe").Length / 1MB
    Write-Host "  [OK] vc_redist.x64.exe ($([math]::Round($vcSize, 2)) MB)" -ForegroundColor Green
} else {
    Write-Host "  [WARN] vc_redist.x64.exe not found (run download_vcredist.ps1 first)" -ForegroundColor Yellow
}

# Create README
Write-Host "Creating README..." -ForegroundColor Cyan
$readme = @"
========================================
  SAMPLAR - Portable Version
========================================

This is a portable version of SAMPLAR that can be run from any folder
without installation.

REQUIREMENTS:
-------------
- Windows 10/11 (64-bit)
- Visual C++ 2022 Redistributable (x64)
  * If the app crashes on startup, run: vc_redist.x64.exe
  * This installs the required C++ runtime libraries

HOW TO USE:
-----------
1. Copy this entire folder to any location
2. Run "SAMPLAR.exe"
3. If it crashes, install vc_redist.x64.exe first, then try again

FOLDER STRUCTURE:
-----------------
SAMPLAR.exe       - Main application
demucs_mt.cpp.main.exe    - Demucs C++ CLI for full song processing (REQUIRED)
libopenblas.dll           - Math library for audio processing
vc_redist.x64.exe         - VC++ Runtime installer (run if app crashes)
models/                   - AI models for vocal separation (80 MB)
plugins/                  - Logo and branding assets
python_bridge/            - [DEPRECATED] Python integration (not used)
assets/                   - Game assets (Flappy Bird, Snake, Space Shooter)
logs/                     - Application logs (created on first run)

LOGS:
-----
Logs are written to: %APPDATA%\Samplar\logs\SAMPLAR_console.log
To view logs: notepad "%APPDATA%\Samplar\logs\SAMPLAR_console.log"

TROUBLESHOOTING:
----------------
1. App crashes on startup:
   - Run vc_redist.x64.exe to install VC++ Runtime
   - Check logs at: %APPDATA%\Samplar\logs\SAMPLAR_console.log

2. Missing DLL errors:
   - Make sure libopenblas.dll is in the same folder as the .exe

3. Models not loading:
   - Ensure models/ folder contains ggml-model-htdemucs-4s-f16.bin

VERSION: 1.0.0
BUILD DATE: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")

========================================
"@

Set-Content -Path "$portableDir\README.txt" -Value $readme
Write-Host "  [OK] README.txt" -ForegroundColor Green

# Calculate total size
$totalSize = (Get-ChildItem $portableDir -Recurse -File | Measure-Object -Property Length -Sum).Sum / 1MB

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "  PORTABLE BUILD COMPLETE!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "Portable folder: $portableDir" -ForegroundColor Cyan
Write-Host "Total size: $([math]::Round($totalSize, 2)) MB" -ForegroundColor Cyan
Write-Host ""
Write-Host "NEXT STEPS:" -ForegroundColor Yellow
Write-Host "  1. Copy the '$portableDir' folder to a USB drive" -ForegroundColor White
Write-Host "  2. Copy it to the other computer" -ForegroundColor White
Write-Host "  3. Run 'SAMPLAR.exe'" -ForegroundColor White
Write-Host "  4. If it crashes, run 'vc_redist.x64.exe' first" -ForegroundColor White
Write-Host ""
Write-Host "To create a ZIP archive:" -ForegroundColor Yellow
Write-Host "  Compress-Archive -Path '$portableDir' -DestinationPath 'Samplar_Portable.zip'" -ForegroundColor White
Write-Host ""

