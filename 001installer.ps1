#!/usr/bin/env pwsh
#Requires -Version 5.1
<#
.SYNOPSIS
    Builds Windows installer for SAMPLAR from SAMPLAR_Portable folder.

.DESCRIPTION
    This script creates a Windows installer using Inno Setup 6.
    It requires the SAMPLAR_Portable folder to be created first using 000portable.ps1.

    The script:
    1. Verifies SAMPLAR_Portable folder exists with required files
    2. Checks for Inno Setup 6 installation
    3. Auto-downloads VC++ redistributable if missing
    4. Compiles installer_sm5.iss
    5. Verifies output file created

    OUTPUT: installer_output/samplar_Setup_v{git-tag}.exe

    NOTE: If installer exceeds 1.8 GB, use alternative distribution:
    - Google Drive / Dropbox for direct download
    - Split into multi-part archive
    - Host on dedicated server

.PARAMETER SkipVerification
    Skip final installer verification step.

.PARAMETER Clean
    Clean installer_output folder before build.

.PARAMETER OpenFolder
    Open installer output folder after build (default: prompt).

.EXAMPLE
    .\001installer.ps1
    Creates installer with all verifications.

.EXAMPLE
    .\001installer.ps1 -Clean
    Cleans output folder and creates fresh installer.

.NOTES
    Author: SAMPLAR
    Version: 1.0.0
    Requires: PowerShell 5.1+, Inno Setup 6
#>

param(
    [switch]$SkipVerification,
    [switch]$Clean,
    [switch]$OpenFolder
)

# ============================================================================
# CONFIGURATION
# ============================================================================

$ErrorActionPreference = "Stop"
$ProgressPreference = "Continue"

# Auto-detect version from git tag (same source as CMake/SamplarVersion.h)
$VERSION = (git describe --tags --abbrev=0 2>$null) -replace '^v',''
if (-not $VERSION) {
    Write-Host "  [WARN] No git tag found, falling back to 0.0.0" -ForegroundColor Yellow
    $VERSION = "0.0.0"
}
$PORTABLE_FOLDER = "SAMPLAR_Portable"
$ISS_FILE = "installer_sm5.iss"
$OUTPUT_DIR = "installer_output"
$OUTPUT_FILE = "samplar_Setup_v$VERSION.exe"
$INNO_PATH = "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
$VC_REDIST_URL = "https://aka.ms/vs/17/release/vc_redist.x64.exe"
$VC_REDIST_PATH = "installer_dependencies\vc_redist.x64.exe"

# Required files in SAMPLAR_Portable
# NOTE: torch_cpu.dll removed - app rebuilt with -DWITH_LIBTORCH=OFF
$REQUIRED_FILES = @(
    "SAMPLAR.exe",
    "libopenblas.dll"
)

# Required folders in SAMPLAR_Portable
$REQUIRED_FOLDERS = @(
    "python",
    "data"
)

# ============================================================================
# HELPER FUNCTIONS
# ============================================================================

function Write-Step {
    param([string]$Message)
    Write-Host ""
    Write-Host ">>> $Message" -ForegroundColor Yellow
    Write-Host ""
}

function Write-Log {
    param([string]$Message, [string]$Level = "INFO")

    $color = switch ($Level) {
        "INFO" { "Gray" }
        "OK" { "Green" }
        "WARN" { "Yellow" }
        "ERROR" { "Red" }
        default { "White" }
    }

    $prefix = switch ($Level) {
        "OK" { "[OK]" }
        "WARN" { "[WARN]" }
        "ERROR" { "[ERROR]" }
        default { "[INFO]" }
    }

    Write-Host "  $prefix $Message" -ForegroundColor $color
}

function Get-FileSize {
    param([string]$Path)
    if (Test-Path $Path) {
        $size = (Get-Item $Path).Length
        if ($size -gt 1GB) { return "{0:N2} GB" -f ($size / 1GB) }
        if ($size -gt 1MB) { return "{0:N2} MB" -f ($size / 1MB) }
        if ($size -gt 1KB) { return "{0:N2} KB" -f ($size / 1KB) }
        return "$size bytes"
    }
    return "N/A"
}

function Get-FolderSize {
    param([string]$Path)
    if (Test-Path $Path) {
        $size = (Get-ChildItem $Path -Recurse -File -ErrorAction SilentlyContinue |
                 Measure-Object -Property Length -Sum).Sum
        if ($null -eq $size) { $size = 0 }
        if ($size -gt 1GB) { return "{0:N2} GB" -f ($size / 1GB) }
        if ($size -gt 1MB) { return "{0:N2} MB" -f ($size / 1MB) }
        if ($size -gt 1KB) { return "{0:N2} KB" -f ($size / 1KB) }
        return "$size bytes"
    }
    return "N/A"
}

# ============================================================================
# MAIN SCRIPT
# ============================================================================

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $scriptRoot

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  SAMPLAR - Installer Builder" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Version: $VERSION" -ForegroundColor Gray
Write-Host "  Source: $PORTABLE_FOLDER" -ForegroundColor Gray
Write-Host "  Output: $OUTPUT_DIR\$OUTPUT_FILE" -ForegroundColor Gray
Write-Host ""

# ============================================================================
# Step 1: VERIFY SAMPLAR_PORTABLE FOLDER
# ============================================================================

Write-Step "Step 1: Verifying SAMPLAR_Portable folder..."

if (-not (Test-Path $PORTABLE_FOLDER)) {
    Write-Log "SAMPLAR_Portable folder not found!" "ERROR"
    Write-Host ""
    Write-Host "  The SAMPLAR_Portable folder does not exist." -ForegroundColor Red
    Write-Host "  Please run 000portable.ps1 first to create it:" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "    .\000portable.ps1" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "  This script packages the built application with all dependencies." -ForegroundColor Gray
    exit 1
}
Write-Log "SAMPLAR_Portable folder exists" "OK"

# Check required files
$missingFiles = @()
foreach ($file in $REQUIRED_FILES) {
    $filePath = Join-Path $PORTABLE_FOLDER $file
    if (Test-Path $filePath) {
        Write-Log "Found: $file ($(Get-FileSize $filePath))"
    } else {
        Write-Log "Missing: $file" "ERROR"
        $missingFiles += $file
    }
}

# Check required folders
$missingFolders = @()
foreach ($folder in $REQUIRED_FOLDERS) {
    $folderPath = Join-Path $PORTABLE_FOLDER $folder
    if (Test-Path $folderPath) {
        Write-Log "Found: $folder/ ($(Get-FolderSize $folderPath))"
    } else {
        Write-Log "Missing: $folder/" "ERROR"
        $missingFolders += $folder
    }
}

# Report total portable size
$totalSize = Get-FolderSize $PORTABLE_FOLDER
Write-Log "Total portable size: $totalSize"

if ($missingFiles.Count -gt 0 -or $missingFolders.Count -gt 0) {
    Write-Host ""
    Write-Log "SAMPLAR_Portable is incomplete!" "ERROR"
    Write-Host ""
    Write-Host "  Missing files: $($missingFiles -join ', ')" -ForegroundColor Red
    Write-Host "  Missing folders: $($missingFolders -join ', ')" -ForegroundColor Red
    Write-Host ""
    Write-Host "  Please run 000portable.ps1 to create a complete portable folder:" -ForegroundColor Yellow
    Write-Host "    .\000portable.ps1" -ForegroundColor Cyan
    exit 1
}
Write-Log "SAMPLAR_Portable validation passed" "OK"

# ============================================================================
# Step 2: CHECK INNO SETUP
# ============================================================================

Write-Step "Step 2: Checking Inno Setup 6..."

if (-not (Test-Path $INNO_PATH)) {
    Write-Log "Inno Setup 6 not found at: $INNO_PATH" "ERROR"
    Write-Host ""
    Write-Host "  Inno Setup 6 is required to compile the installer." -ForegroundColor Red
    Write-Host ""
    Write-Host "  Install options:" -ForegroundColor Yellow
    Write-Host "    Option 1: winget install JRSoftware.InnoSetup" -ForegroundColor Cyan
    Write-Host "    Option 2: Download from https://jrsoftware.org/isdl.php" -ForegroundColor Cyan
    Write-Host ""
    exit 1
}
Write-Log "Inno Setup 6 found" "OK"

# ============================================================================
# Step 3: CHECK ICON FILE
# ============================================================================

Write-Step "Step 3: Checking icon file..."

$iconPath = "samplar.ico"
if (-not (Test-Path $iconPath)) {
    Write-Log "Icon file not found: $iconPath" "WARN"

    # Try to create from REF.png
    $refPng = "plugins\REF.png"
    if (-not (Test-Path $refPng)) {
        $refPng = Join-Path $PORTABLE_FOLDER "REF.png"
    }

    if (Test-Path $refPng) {
        Write-Log "Attempting to create icon from REF.png..."

        # Check for ImageMagick
        $magick = Get-Command magick -ErrorAction SilentlyContinue
        if ($magick) {
            try {
                & magick $refPng -define icon:auto-resize=256,128,64,48,32,16 $iconPath
                if (Test-Path $iconPath) {
                    Write-Log "Created icon with ImageMagick" "OK"
                }
            } catch {
                Write-Log "ImageMagick failed: $_" "WARN"
            }
        } else {
            Write-Log "ImageMagick not available, using .NET fallback..."

            # .NET fallback for icon creation
            try {
                Add-Type -AssemblyName System.Drawing
                $bitmap = [System.Drawing.Image]::FromFile((Resolve-Path $refPng))
                $icon = [System.Drawing.Icon]::FromHandle($bitmap.GetHicon())
                $fs = [System.IO.FileStream]::new($iconPath, [System.IO.FileMode]::Create)
                $icon.Save($fs)
                $fs.Close()
                $icon.Dispose()
                $bitmap.Dispose()

                if (Test-Path $iconPath) {
                    Write-Log "Created icon with .NET" "OK"
                }
            } catch {
                Write-Log ".NET icon creation failed: $_" "ERROR"
                Write-Host ""
                Write-Host "  Please create samplar.ico manually or install ImageMagick." -ForegroundColor Yellow
                exit 1
            }
        }
    } else {
        Write-Log "Source image not found: $refPng" "ERROR"
        Write-Host ""
        Write-Host "  Please provide samplar.ico or REF.png for icon creation." -ForegroundColor Yellow
        exit 1
    }
} else {
    Write-Log "Icon file found ($(Get-FileSize $iconPath))" "OK"
}

# ============================================================================
# Step 4: CHECK LICENSE FILE
# ============================================================================

Write-Step "Step 4: Checking license file..."

$licensePath = "LICENSE.txt"
if (-not (Test-Path $licensePath)) {
    Write-Log "LICENSE.txt not found - this is required for EULA display" "ERROR"
    Write-Host ""
    Write-Host "  The LICENSE.txt file is required for the installer EULA." -ForegroundColor Red
    Write-Host "  Please create LICENSE.txt with your license terms." -ForegroundColor Yellow
    exit 1
}
Write-Log "LICENSE.txt found ($(Get-FileSize $licensePath))" "OK"

# ============================================================================
# Step 5: CHECK VC++ REDISTRIBUTABLE
# ============================================================================

Write-Step "Step 5: Checking VC++ Redistributable..."

if (-not (Test-Path $VC_REDIST_PATH)) {
    Write-Log "VC++ Redistributable not found: $VC_REDIST_PATH" "WARN"

    # Create directory if needed
    $vcDir = Split-Path $VC_REDIST_PATH -Parent
    if (-not (Test-Path $vcDir)) {
        New-Item -ItemType Directory -Path $vcDir -Force | Out-Null
        Write-Log "Created directory: $vcDir"
    }

    # Download
    Write-Log "Downloading VC++ Redistributable..."
    Write-Log "URL: $VC_REDIST_URL"

    try {
        $ProgressPreference = 'SilentlyContinue'
        Invoke-WebRequest -Uri $VC_REDIST_URL -OutFile $VC_REDIST_PATH -UseBasicParsing
        $ProgressPreference = 'Continue'

        if (Test-Path $VC_REDIST_PATH) {
            Write-Log "Downloaded VC++ Redistributable ($(Get-FileSize $VC_REDIST_PATH))" "OK"
        }
    } catch {
        Write-Log "Download failed: $_" "ERROR"
        Write-Host ""
        Write-Host "  Please download manually:" -ForegroundColor Yellow
        Write-Host "    URL: $VC_REDIST_URL" -ForegroundColor Cyan
        Write-Host "    Save to: $VC_REDIST_PATH" -ForegroundColor Cyan
        exit 1
    }
} else {
    Write-Log "VC++ Redistributable found ($(Get-FileSize $VC_REDIST_PATH))" "OK"
}

# ============================================================================
# Step 6: CHECK ISS FILE
# ============================================================================

Write-Step "Step 6: Checking installer script..."

if (-not (Test-Path $ISS_FILE)) {
    Write-Log "Installer script not found: $ISS_FILE" "ERROR"
    Write-Host ""
    Write-Host "  The installer_sm5.iss file is required." -ForegroundColor Red
    exit 1
}
Write-Log "Installer script found: $ISS_FILE" "OK"

# ============================================================================
# Step 7: CLEAN OUTPUT (if requested)
# ============================================================================

if ($Clean) {
    Write-Step "Step 7: Cleaning output folder..."

    if (Test-Path $OUTPUT_DIR) {
        Remove-Item -Path $OUTPUT_DIR -Recurse -Force
        Write-Log "Removed $OUTPUT_DIR"
    }

    New-Item -ItemType Directory -Path $OUTPUT_DIR -Force | Out-Null
    Write-Log "Created fresh $OUTPUT_DIR" "OK"
} else {
    # Ensure output directory exists
    if (-not (Test-Path $OUTPUT_DIR)) {
        New-Item -ItemType Directory -Path $OUTPUT_DIR -Force | Out-Null
        Write-Log "Created $OUTPUT_DIR"
    }
}

# ============================================================================
# Step 8: BUILD INSTALLER
# ============================================================================

Write-Step "Step 8: Building installer with Inno Setup..."

Write-Log "This may take several minutes for compression..."
Write-Log "Source size: $(Get-FolderSize $PORTABLE_FOLDER)"

$startTime = Get-Date

try {
    # Run Inno Setup compiler, passing version from git tag
    $process = Start-Process -FilePath $INNO_PATH -ArgumentList "/DMyAppVersion=$VERSION", $ISS_FILE -Wait -PassThru -NoNewWindow

    if ($process.ExitCode -ne 0) {
        Write-Log "Inno Setup compiler returned exit code: $($process.ExitCode)" "ERROR"
        Write-Host ""
        Write-Host "  Check the Inno Setup output above for error details." -ForegroundColor Yellow
        exit 1
    }
} catch {
    Write-Log "Failed to run Inno Setup: $_" "ERROR"
    exit 1
}

$endTime = Get-Date
$duration = $endTime - $startTime
Write-Log "Build completed in $($duration.TotalMinutes.ToString('N1')) minutes" "OK"

# ============================================================================
# Step 9: VERIFY OUTPUT
# ============================================================================

if (-not $SkipVerification) {
    Write-Step "Step 9: Verifying installer..."

    $installerPath = Join-Path $OUTPUT_DIR $OUTPUT_FILE

    if (-not (Test-Path $installerPath)) {
        Write-Log "Installer not created: $installerPath" "ERROR"
        exit 1
    }

    $installerSize = Get-FileSize $installerPath
    $installerInfo = Get-Item $installerPath

    Write-Log "Installer created successfully!" "OK"
    Write-Log "  File: $installerPath"
    Write-Log "  Size: $installerSize"
    Write-Log "  Created: $($installerInfo.LastWriteTime)"

    # Check if installer exceeds GitHub release limit
    $sizeBytes = $installerInfo.Length
    $sizeMB = $sizeBytes / 1MB

    if ($sizeMB -gt 1800) {
        Write-Log "WARNING: Installer exceeds 1.8 GB - may not upload to GitHub Releases" "WARN"
        Write-Host ""
        Write-Host "  Alternative distribution options:" -ForegroundColor Yellow
        Write-Host "    - Google Drive / Dropbox" -ForegroundColor Gray
        Write-Host "    - Split into multi-part archive" -ForegroundColor Gray
        Write-Host "    - Dedicated download server" -ForegroundColor Gray
    }
}

# ============================================================================
# SUMMARY
# ============================================================================

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "  INSTALLER BUILD COMPLETE!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""

$installerPath = Join-Path $OUTPUT_DIR $OUTPUT_FILE
if (Test-Path $installerPath) {
    Write-Host "OUTPUT:" -ForegroundColor Cyan
    Write-Host "  $installerPath" -ForegroundColor White
    Write-Host "  Size: $(Get-FileSize $installerPath)" -ForegroundColor White
    Write-Host ""
}

Write-Host "FEATURES:" -ForegroundColor Cyan
Write-Host "  [X] SAMPLAR icon embedded" -ForegroundColor Green
Write-Host "  [X] VC++ Redistributable bundled (auto-install)" -ForegroundColor Green
Write-Host "  [X] Desktop shortcut (checked by default)" -ForegroundColor Green
Write-Host "  [X] Start Menu shortcuts" -ForegroundColor Green
Write-Host "  [X] Embedded Python environment (1.4 GB)" -ForegroundColor Green
Write-Host "  [X] Pattern database included" -ForegroundColor Green
Write-Host "  [X] LICENSE.txt EULA display" -ForegroundColor Green
Write-Host "  [X] 2.5 GB disk space check" -ForegroundColor Green
Write-Host "  [X] Extraction delay warning" -ForegroundColor Green
Write-Host "  [X] Professional uninstaller" -ForegroundColor Green
Write-Host ""

Write-Host "NEXT STEPS:" -ForegroundColor Yellow
Write-Host "  1. Test the installer:" -ForegroundColor White
Write-Host "     $installerPath" -ForegroundColor Cyan
Write-Host ""
Write-Host "  2. Verify on clean Windows 10/11 VM" -ForegroundColor White
Write-Host ""
Write-Host "  3. Note: Unsigned installer will trigger SmartScreen" -ForegroundColor White
Write-Host "     Users must click 'More info' > 'Run anyway'" -ForegroundColor Gray
Write-Host ""

# Open output folder (if requested or prompted)
if ($OpenFolder) {
    explorer.exe $OUTPUT_DIR
} else {
    $response = Read-Host "Open installer output folder? (Y/n)"
    if ($response -ne 'n' -and $response -ne 'N') {
        explorer.exe $OUTPUT_DIR
    }
}

Write-Host ""
Write-Host "Done!" -ForegroundColor Green
Write-Host ""

