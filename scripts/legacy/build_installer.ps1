# SAMPLAR - Installer Build Script
# This script builds the Inno Setup installer for SAMPLAR
#
# Prerequisites:
# 1. Inno Setup must be installed (https://jrsoftware.org/isinfo.php)
# 2. SAMPLAR must be built in Release mode
# 3. Models must be in the models/ directory

param(
    [switch]$SkipBuild = $false,
    [switch]$Clean = $false
)

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  SAMPLAR - Installer Builder  " -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Find Inno Setup
$InnoSetupPaths = @(
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    "C:\Program Files\Inno Setup 6\ISCC.exe",
    "C:\Program Files (x86)\Inno Setup 5\ISCC.exe",
    "C:\Program Files\Inno Setup 5\ISCC.exe"
)

$ISCC = $null
foreach ($path in $InnoSetupPaths) {
    if (Test-Path $path) {
        $ISCC = $path
        break
    }
}

if (-not $ISCC) {
    Write-Host "❌ ERROR: Inno Setup not found!" -ForegroundColor Red
    Write-Host ""
    Write-Host "Please install Inno Setup from:" -ForegroundColor Yellow
    Write-Host "https://jrsoftware.org/isinfo.php" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Searched in:" -ForegroundColor Gray
    foreach ($path in $InnoSetupPaths) {
        Write-Host "  - $path" -ForegroundColor Gray
    }
    exit 1
}

Write-Host "✅ Found Inno Setup: $ISCC" -ForegroundColor Green
Write-Host ""

# Check if Release build exists
$ExePath = "build\SM5App_artefacts\Release\SAMPLAR.exe"
if (-not (Test-Path $ExePath)) {
    if ($SkipBuild) {
        Write-Host "❌ ERROR: Release build not found!" -ForegroundColor Red
        Write-Host "Path: $ExePath" -ForegroundColor Gray
        Write-Host ""
        Write-Host "Please build the project first:" -ForegroundColor Yellow
        Write-Host "  .\build_release.ps1" -ForegroundColor Yellow
        exit 1
    } else {
        Write-Host "⚠️  Release build not found, building now..." -ForegroundColor Yellow
        Write-Host ""
        
        if (Test-Path "build_release.ps1") {
            & .\build_release.ps1 -Clean:$Clean
            if ($LASTEXITCODE -ne 0) {
                Write-Host "❌ Build failed!" -ForegroundColor Red
                exit 1
            }
        } else {
            Write-Host "❌ ERROR: build_release.ps1 not found!" -ForegroundColor Red
            exit 1
        }
    }
}

Write-Host "✅ Found Release build: $ExePath" -ForegroundColor Green
Write-Host ""

# Check for required files
$RequiredFiles = @(
    @{Path="build\SM5App_artefacts\Release\SAMPLAR.exe"; Name="Main executable"},
    @{Path="build\SM5App_artefacts\Release\libopenblas.dll"; Name="OpenBLAS DLL"},
    @{Path="build\SM5App_artefacts\Release\python_bridge\demucs_separator.py"; Name="Python bridge"},
    @{Path="models\ggml-model-htdemucs-4s-f16.bin"; Name="Demucs model"},
    @{Path="LICENSE.txt"; Name="License file"},
    @{Path="installer.iss"; Name="Installer script"}
)

$MissingFiles = @()
foreach ($file in $RequiredFiles) {
    if (Test-Path $file.Path) {
        $size = (Get-Item $file.Path).Length
        $sizeStr = if ($size -gt 1GB) { "{0:N2} GB" -f ($size / 1GB) }
                   elseif ($size -gt 1MB) { "{0:N2} MB" -f ($size / 1MB) }
                   elseif ($size -gt 1KB) { "{0:N2} KB" -f ($size / 1KB) }
                   else { "$size bytes" }
        Write-Host "✅ $($file.Name): $sizeStr" -ForegroundColor Green
    } else {
        Write-Host "❌ $($file.Name): NOT FOUND" -ForegroundColor Red
        $MissingFiles += $file.Path
    }
}

if ($MissingFiles.Count -gt 0) {
    Write-Host ""
    Write-Host "❌ ERROR: Missing required files!" -ForegroundColor Red
    Write-Host "Missing:" -ForegroundColor Yellow
    foreach ($file in $MissingFiles) {
        Write-Host "  - $file" -ForegroundColor Yellow
    }
    exit 1
}

Write-Host ""

# Check optional assets
Write-Host "Checking optional assets..." -ForegroundColor Cyan
$OptionalAssets = @(
    @{Path="assets\flappy"; Name="Flappy Bird assets"},
    @{Path="assets\gamesnake"; Name="Snake game assets"},
    @{Path="assets\shooter"; Name="Space Shooter assets"},
    @{Path="BACKGROUNDS"; Name="Background images"}
)

foreach ($asset in $OptionalAssets) {
    if (Test-Path $asset.Path) {
        $count = (Get-ChildItem -Path $asset.Path -Recurse -File).Count
        Write-Host "✅ $($asset.Name): $count files" -ForegroundColor Green
    } else {
        Write-Host "⚠️  $($asset.Name): NOT FOUND (optional)" -ForegroundColor Yellow
    }
}

Write-Host ""

# Create output directory
$OutputDir = "installer_output"
if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
    Write-Host "✅ Created output directory: $OutputDir" -ForegroundColor Green
} else {
    Write-Host "✅ Output directory exists: $OutputDir" -ForegroundColor Green
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Building Installer...                " -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Build installer
Write-Host "Running Inno Setup compiler..." -ForegroundColor Yellow
Write-Host "Command: $ISCC installer.iss" -ForegroundColor Gray
Write-Host ""

$StartTime = Get-Date

& $ISCC "installer.iss"

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "❌ Installer build FAILED!" -ForegroundColor Red
    exit 1
}

$Duration = (Get-Date) - $StartTime

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "  ✅ INSTALLER BUILD SUCCESSFUL!       " -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""

# Find the generated installer
$InstallerFiles = Get-ChildItem -Path $OutputDir -Filter "*.exe" | Sort-Object LastWriteTime -Descending

if ($InstallerFiles.Count -gt 0) {
    $Installer = $InstallerFiles[0]
    $InstallerSize = $Installer.Length
    $SizeGB = $InstallerSize / 1GB
    $SizeMB = $InstallerSize / 1MB
    
    Write-Host "Installer Details:" -ForegroundColor Cyan
    Write-Host "   File: $($Installer.Name)" -ForegroundColor White
    Write-Host "   Path: $($Installer.FullName)" -ForegroundColor Gray
    Write-Host ("   Size: {0:N2} MB ({1:N2} GB)" -f $SizeMB, $SizeGB) -ForegroundColor White
    Write-Host ("   Build Time: {0:N1} seconds" -f $Duration.TotalSeconds) -ForegroundColor White
    Write-Host ""
    
    Write-Host "Ready to test on a fresh computer!" -ForegroundColor Green
    Write-Host ""
    Write-Host "Next Steps:" -ForegroundColor Yellow
    Write-Host "  1. Copy installer to a USB drive or cloud storage" -ForegroundColor White
    Write-Host "  2. Run on a fresh Windows 10/11 machine" -ForegroundColor White
    Write-Host "  3. Test all features (audio processing, games, etc.)" -ForegroundColor White
    Write-Host "  4. Check that models are loading correctly" -ForegroundColor White
    Write-Host ""
    
    # Open output folder
    Write-Host "Opening output folder..." -ForegroundColor Cyan
    Start-Process explorer.exe -ArgumentList $OutputDir
    
} else {
    Write-Host "⚠️  Warning: Installer file not found in output directory" -ForegroundColor Yellow
}

Write-Host "Done!" -ForegroundColor Green

