#Requires -Version 5.1
<#
.SYNOPSIS
    Creates a portable version of SAMPLAR with all dependencies.

.DESCRIPTION
    This script builds a self-contained portable folder containing:
    - SAMPLAR application (exe + dlls)
    - Embedded Python 3.11 with audio-separator
    - Pre-downloaded demucs model for Full Song workflow
    - Pattern database files

.NOTES
    Run from the Sample_monsta5 root directory.
    Requires internet connection for Python packages and model download.
#>

param(
    [string]$OutputDir = ".\SAMPLAR_Portable",
    [string]$BuildConfig = "Release",
    [switch]$SkipPythonSetup,
    [switch]$SkipModelDownload,
    [switch]$Clean
)

# ============================================================================
# CONFIGURATION
# ============================================================================

$ErrorActionPreference = "Stop"
$ProgressPreference = "Continue"

$PYTHON_VERSION = "3.11.9"
$PYTHON_EMBED_URL = "https://www.python.org/ftp/python/$PYTHON_VERSION/python-$PYTHON_VERSION-embed-amd64.zip"
$GET_PIP_URL = "https://bootstrap.pypa.io/get-pip.py"
$DEMUCS_MODEL = "htdemucs_ft_drums"

# Paths
$ScriptDir = $PSScriptRoot
$BuildDir = Join-Path $ScriptDir "build\SM5App_artefacts\$BuildConfig"
$DataDir = Join-Path $ScriptDir "data"

# ============================================================================
# HELPER FUNCTIONS
# ============================================================================

function Write-Step {
    param([string]$Message)
    Write-Host "`n" -NoNewline
    Write-Host "" -ForegroundColor Cyan
    Write-Host "  $Message" -ForegroundColor Cyan
    Write-Host "" -ForegroundColor Cyan
}

function Write-Info {
    param([string]$Message)
    Write-Host "  [INFO] $Message" -ForegroundColor Gray
}

function Write-Success {
    param([string]$Message)
    Write-Host "  [OK] $Message" -ForegroundColor Green
}

function Write-Warning {
    param([string]$Message)
    Write-Host "  [WARN] $Message" -ForegroundColor Yellow
}

function Write-Error {
    param([string]$Message)
    Write-Host "  [ERROR] $Message" -ForegroundColor Red
}

function Test-CommandExists {
    param([string]$Command)
    $null -ne (Get-Command $Command -ErrorAction SilentlyContinue)
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
        $size = (Get-ChildItem $Path -Recurse -File | Measure-Object -Property Length -Sum).Sum
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

Write-Host @"

  
                                                                         
                                 
                            
                             
                              
                               
                                  
                                                                         
                    
             
                    
                   
                     
                           
                                                                         
                      PORTABLE BUILDER v1.0                              
  

"@ -ForegroundColor Magenta

Write-Host "  Output Directory: $OutputDir"
Write-Host "  Build Config:     $BuildConfig"
Write-Host "  Python Version:   $PYTHON_VERSION"
Write-Host ""

# ============================================================================
# STEP 0: CLEAN (if requested)
# ============================================================================

if ($Clean -and (Test-Path $OutputDir)) {
    Write-Step "CLEANING EXISTING PORTABLE FOLDER"
    Remove-Item -Path $OutputDir -Recurse -Force
    Write-Success "Cleaned $OutputDir"
}

# ============================================================================
# STEP 1: VERIFY BUILD EXISTS
# ============================================================================

Write-Step "STEP 1: VERIFYING BUILD"

$ExePath = Join-Path $BuildDir "SAMPLAR.exe"
if (-not (Test-Path $ExePath)) {
    Write-Error "Build not found at: $ExePath"
    Write-Info "Please build the project first with:"
    Write-Info "  cmake --build build --config $BuildConfig"
    exit 1
}
Write-Success "Found application: $ExePath"
Write-Info "Size: $(Get-FileSize $ExePath)"

# Check for DLLs
$DllFiles = Get-ChildItem -Path $BuildDir -Filter "*.dll" -ErrorAction SilentlyContinue
Write-Info "Found $($DllFiles.Count) DLL files"

# ============================================================================
# STEP 2: CREATE FOLDER STRUCTURE
# ============================================================================

Write-Step "STEP 2: CREATING FOLDER STRUCTURE"

$Folders = @(
    $OutputDir,
    (Join-Path $OutputDir "data"),
    (Join-Path $OutputDir "python"),
    (Join-Path $OutputDir "models")
)

foreach ($Folder in $Folders) {
    if (-not (Test-Path $Folder)) {
        New-Item -ItemType Directory -Path $Folder -Force | Out-Null
        Write-Success "Created: $Folder"
    } else {
        Write-Info "Exists: $Folder"
    }
}

# ============================================================================
# STEP 3: COPY APPLICATION FILES
# ============================================================================

Write-Step "STEP 3: COPYING APPLICATION FILES"

# Copy main executable
Copy-Item -Path $ExePath -Destination $OutputDir -Force
Write-Success "Copied: SAMPLAR.exe"

# Copy all DLLs from build directory
foreach ($Dll in $DllFiles) {
    Copy-Item -Path $Dll.FullName -Destination $OutputDir -Force
    Write-Info "Copied: $($Dll.Name)"
}

# Copy Visual C++ runtime DLLs if present in build
$VCRuntimeDlls = @("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll")
foreach ($VCDll in $VCRuntimeDlls) {
    $VCPath = Join-Path $BuildDir $VCDll
    if (Test-Path $VCPath) {
        Copy-Item -Path $VCPath -Destination $OutputDir -Force
        Write-Info "Copied: $VCDll"
    }
}

# Download VC++ Redistributable for target machines that don't have it
$VCRedistUrl = "https://aka.ms/vs/17/release/vc_redist.x64.exe"
$VCRedistPath = Join-Path $OutputDir "vc_redist.x64.exe"
if (-not (Test-Path $VCRedistPath)) {
    Write-Info "Downloading VC++ Redistributable..."
    try {
        Invoke-WebRequest -Uri $VCRedistUrl -OutFile $VCRedistPath -UseBasicParsing
        Write-Success "Downloaded: vc_redist.x64.exe"
    } catch {
        Write-Warning "Failed to download VC++ Redistributable: $_"
        Write-Info "You may need to install it manually on target machines"
    }
}

# Copy VST3 plugin bundle
$Vst3Source = Join-Path $ScriptDir "build\SM5Plugin_artefacts\$BuildConfig\VST3\SAMPLAR.vst3"
if (Test-Path $Vst3Source) {
    $Vst3Dest = Join-Path $OutputDir "VST3"
    if (-not (Test-Path $Vst3Dest)) {
        New-Item -ItemType Directory -Path $Vst3Dest -Force | Out-Null
    }
    # Remove any stale VST3 in destination so we don't accumulate old bundles
    $Vst3DestBundle = Join-Path $Vst3Dest "SAMPLAR.vst3"
    if (Test-Path $Vst3DestBundle) {
        Remove-Item -Path $Vst3DestBundle -Recurse -Force -ErrorAction SilentlyContinue
    }
    Copy-Item -Path $Vst3Source -Destination $Vst3Dest -Recurse -Force
    Write-Success "Copied: SAMPLAR.vst3 bundle"
    $Vst3Size = Get-FolderSize (Join-Path $Vst3Dest "SAMPLAR.vst3")
    Write-Info "VST3 size: $Vst3Size"
} else {
    Write-Warning "VST3 plugin not found at: $Vst3Source"
    Write-Warning "Build with SM5Plugin_VST3 target to include VST3"
}

# Copy demucs.cpp CLI executables (CPU-based audio separation fallback)
# These are built by CMake and copied to the SM5App output directory
Write-Info "Checking for demucs.cpp CLI executables..."

$DemucsCLIs = @(
    @{ Name = "demucs_mt_openblas.exe"; Description = "OpenBLAS (universal)" },
    @{ Name = "demucs_mt_mkl.exe"; Description = "Intel MKL (Intel CPUs)" },
    @{ Name = "demucs_mt.cpp.main.exe"; Description = "Legacy name (fallback)" }
)

$demucsCopied = 0
foreach ($cli in $DemucsCLIs) {
    $CliPath = Join-Path $BuildDir $cli.Name
    if (Test-Path $CliPath) {
        Copy-Item -Path $CliPath -Destination $OutputDir -Force
        Write-Success "Copied: $($cli.Name) - $($cli.Description) ($(Get-FileSize $CliPath))"
        $demucsCopied++
    } else {
        Write-Info "Not found: $($cli.Name) (optional - $($cli.Description))"
    }
}

if ($demucsCopied -eq 0) {
    Write-Warning "No demucs CLI executables found - Full Song CPU fallback will not work!"
    Write-Warning "UVR/audio-separator (Python) will be the only separation backend"
} else {
    Write-Success "Copied $demucsCopied demucs CLI executable(s) for CPU separation fallback"
}

# ============================================================================
# STEP 4: COPY DATA FILES
# ============================================================================

Write-Step "STEP 4: COPYING DATA FILES"

$DataFiles = @(
    @{ Name = "patterns_with_rolls.json"; Required = $true },
    @{ Name = "latin_patterns.json"; Required = $true }
)

foreach ($DataFile in $DataFiles) {
    $SourcePath = Join-Path $DataDir $DataFile.Name
    $DestPath = Join-Path (Join-Path $OutputDir "data") $DataFile.Name

    if (Test-Path $SourcePath) {
        Copy-Item -Path $SourcePath -Destination $DestPath -Force
        Write-Success "Copied: $($DataFile.Name) ($(Get-FileSize $SourcePath))"
    } elseif ($DataFile.Required) {
        Write-Error "Required file not found: $SourcePath"
        exit 1
    } else {
        Write-Warning "Optional file not found: $($DataFile.Name)"
    }
}

# Copy REF.png asset
$RefPngSource = Join-Path $ScriptDir "plugins\REF.png"
if (Test-Path $RefPngSource) {
    Copy-Item -Path $RefPngSource -Destination $OutputDir -Force
    Write-Success "Copied: REF.png"
} else {
    Write-Warning "REF.png not found at: $RefPngSource"
}

# Copy BACKGROUNDS folder (large image assets: GIFs, PNGs for UI backgrounds)
$BackgroundsSource = Join-Path $ScriptDir "BACKGROUNDS"
$BackgroundsDest = Join-Path $OutputDir "BACKGROUNDS"
if (Test-Path $BackgroundsSource) {
    if (-not (Test-Path $BackgroundsDest)) {
        New-Item -ItemType Directory -Path $BackgroundsDest -Force | Out-Null
    }
    Copy-Item -Path "$BackgroundsSource\*" -Destination $BackgroundsDest -Force
    $BgCount = (Get-ChildItem $BackgroundsDest -File).Count
    $BgSize = Get-FolderSize $BackgroundsDest
    Write-Success "Copied: BACKGROUNDS/ ($BgCount files, $BgSize)"
} else {
    Write-Warning "BACKGROUNDS folder not found at: $BackgroundsSource"
    Write-Warning "UI backgrounds (GIFs, tileset, etc.) will not be available!"
}

# Copy Samples/ folder (808 preview kit + GM drum samples for pattern preview playback)
$SamplesSource = Join-Path $ScriptDir "Samples"
$SamplesDest = Join-Path $OutputDir "Samples"
if (Test-Path $SamplesSource) {
    if (-not (Test-Path $SamplesDest)) {
        New-Item -ItemType Directory -Path $SamplesDest -Force | Out-Null
    }
    Copy-Item -Path "$SamplesSource\*" -Destination $SamplesDest -Force -Recurse
    $SmpCount = (Get-ChildItem $SamplesDest -File).Count
    $SmpSize = Get-FolderSize $SamplesDest
    Write-Success "Copied: Samples/ ($SmpCount files, $SmpSize) - includes 808 preview kit"
} else {
    Write-Warning "Samples folder not found at: $SamplesSource"
    Write-Warning "Pattern preview playback (808 kit) will not be available!"
}

# ============================================================================
# STEP 5: SETUP EMBEDDED PYTHON (from local cache)
# ============================================================================

if (-not $SkipPythonSetup) {
    Write-Step "STEP 5: COPYING EMBEDDED PYTHON FROM CACHE"

    $PythonCacheDir = Join-Path $ScriptDir "portable_cache\python"
    $PythonDir = Join-Path $OutputDir "python"

    if (-not (Test-Path $PythonCacheDir)) {
        Write-Error "Python cache not found at: $PythonCacheDir"
        Write-Error "Please ensure portable_cache/python exists with pre-installed audio-separator"
        exit 1
    }

    Write-Info "Copying Python environment from cache..."
    Write-Info "Source: $PythonCacheDir"
    Write-Info "This contains Python + PyTorch + audio-separator (pre-installed)"

    # Copy entire python folder from cache
    if (Test-Path $PythonDir) {
        Write-Info "Removing existing python folder..."
        Remove-Item -Path $PythonDir -Recurse -Force
    }

    Copy-Item -Path $PythonCacheDir -Destination $PythonDir -Recurse -Force
    Write-Success "Copied Python environment"

    # Verify critical files exist
    $PythonExe = Join-Path $PythonDir "python.exe"
    $AudioSepExe = Join-Path $PythonDir "Scripts\audio-separator.exe"

    if (Test-Path $PythonExe) {
        Write-Success "Python interpreter: OK"
    } else {
        Write-Error "Python interpreter not found!"
        exit 1
    }

    if (Test-Path $AudioSepExe) {
        Write-Success "audio-separator CLI: OK"
    } else {
        Write-Error "audio-separator CLI not found!"
        exit 1
    }

    # Show size
    $PythonSize = Get-FolderSize $PythonDir
    Write-Info "Python environment size: $PythonSize"

    # FIX #4: Ensure __main__.py exists for 'python -m audio_separator'
    # The audio_separator package is missing __main__.py which prevents module execution
    $AudioSepMainPy = Join-Path $PythonDir "Lib\site-packages\audio_separator\__main__.py"
    if (-not (Test-Path $AudioSepMainPy)) {
        Write-Info "Creating __main__.py for audio_separator module execution..."
        $MainPyContent = @"
#!/usr/bin/env python
"""Entry point for running audio_separator as a module: python -m audio_separator"""
from audio_separator.utils.cli import main

if __name__ == "__main__":
    main()
"@
        Set-Content -Path $AudioSepMainPy -Value $MainPyContent -Encoding UTF8
        Write-Success "Created: audio_separator/__main__.py"
    } else {
        Write-Success "audio_separator/__main__.py: OK"
    }

} else {
    Write-Step "STEP 5: SKIPPING PYTHON SETUP (--SkipPythonSetup)"
}

# ============================================================================
# STEP 5B: BUNDLE FFMPEG (required by audio-separator)
# ============================================================================

Write-Step "STEP 5B: BUNDLING FFMPEG"

$FfmpegTarget = Join-Path $OutputDir "python\Scripts\ffmpeg.exe"
$FfmpegCachePath = Join-Path $ScriptDir "portable_cache\ffmpeg\ffmpeg.exe"

if (Test-Path $FfmpegTarget) {
    Write-Success "FFmpeg already present in python/Scripts/"
} elseif (Test-Path $FfmpegCachePath) {
    Write-Info "Copying FFmpeg from cache..."
    Copy-Item -Path $FfmpegCachePath -Destination $FfmpegTarget -Force
    Write-Success "Copied: ffmpeg.exe ($(Get-FileSize $FfmpegCachePath))"
} else {
    Write-Info "Downloading FFmpeg essentials build..."

    $FfmpegZipUrl = "https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip"
    $FfmpegZipPath = Join-Path $env:TEMP "ffmpeg-essentials.zip"
    $FfmpegExtractDir = Join-Path $env:TEMP "ffmpeg-extract"

    try {
        $ProgressPreference = 'SilentlyContinue'
        Invoke-WebRequest -Uri $FfmpegZipUrl -OutFile $FfmpegZipPath -UseBasicParsing
        $ProgressPreference = 'Continue'
        Write-Success "Downloaded FFmpeg archive"

        # Extract and find ffmpeg.exe
        if (Test-Path $FfmpegExtractDir) { Remove-Item $FfmpegExtractDir -Recurse -Force }
        Expand-Archive -Path $FfmpegZipPath -DestinationPath $FfmpegExtractDir -Force
        $FfmpegExe = Get-ChildItem -Path $FfmpegExtractDir -Filter "ffmpeg.exe" -Recurse | Select-Object -First 1

        if ($FfmpegExe) {
            # Copy to portable python/Scripts/
            Copy-Item -Path $FfmpegExe.FullName -Destination $FfmpegTarget -Force
            Write-Success "Installed: ffmpeg.exe ($(Get-FileSize $FfmpegExe.FullName))"

            # Cache it for future builds
            $FfmpegCacheDir = Join-Path $ScriptDir "portable_cache\ffmpeg"
            if (-not (Test-Path $FfmpegCacheDir)) {
                New-Item -ItemType Directory -Path $FfmpegCacheDir -Force | Out-Null
            }
            Copy-Item -Path $FfmpegExe.FullName -Destination $FfmpegCachePath -Force
            Write-Info "Cached FFmpeg for future builds"
        } else {
            Write-Error "Could not find ffmpeg.exe in downloaded archive!"
            exit 1
        }

        # Cleanup
        Remove-Item $FfmpegZipPath -Force -ErrorAction SilentlyContinue
        Remove-Item $FfmpegExtractDir -Recurse -Force -ErrorAction SilentlyContinue
    } catch {
        Write-Error "Failed to download FFmpeg: $_"
        Write-Info "Please manually place ffmpeg.exe in portable_cache/ffmpeg/ and re-run"
        exit 1
    }
}

# ============================================================================
# STEP 6: SETUP MODELS DIRECTORY
# ============================================================================

if (-not $SkipModelDownload) {
    Write-Step "STEP 6: SETTING UP MODELS DIRECTORY"

    $ModelsDir = Join-Path $OutputDir "models"

    # Create models directory if it doesn't exist
    if (-not (Test-Path $ModelsDir)) {
        New-Item -ItemType Directory -Path $ModelsDir -Force | Out-Null
        Write-Success "Created models directory"
    }

    # Check if there's a models cache to copy from
    $ModelsCacheDir = Join-Path $ScriptDir "portable_cache\models"
    if (Test-Path $ModelsCacheDir) {
        Write-Info "Copying pre-downloaded models from cache..."
        Copy-Item -Path "$ModelsCacheDir\*" -Destination $ModelsDir -Recurse -Force -ErrorAction SilentlyContinue
        $ModelsSize = Get-FolderSize $ModelsDir
        Write-Success "Copied models: $ModelsSize"
    } else {
        Write-Info "No models cache found - models will download on first use"
        Write-Info "First Full Song separation will download ~337 MB of model weights (htdemucs_ft, 4 fine-tuned stems)"
    }

} else {
    Write-Step "STEP 6: SKIPPING MODEL SETUP (--SkipModelDownload)"
}

# ============================================================================
# STEP 7: CREATE LAUNCHER SCRIPTS
# ============================================================================

Write-Step "STEP 7: CREATING LAUNCHER SCRIPTS"

# FIX #3: Create a batch file launcher with FULL environment isolation
$LauncherBat = @"
@echo off
REM SAMPLAR Portable Launcher
REM Sets up a fully isolated Python environment

set "SAMPLAR_ROOT=%~dp0"

REM === Python Isolation (prevents conflicts with system Python) ===
set "SAMPLAR_PYTHON=%SAMPLAR_ROOT%python"
set "PYTHONHOME=%SAMPLAR_PYTHON%"
set "PYTHONPATH="
set "PATH=%SAMPLAR_PYTHON%;%SAMPLAR_PYTHON%\Scripts;%PATH%"

REM === Model Cache Isolation (all models stay in portable folder) ===
set "TORCH_HOME=%SAMPLAR_ROOT%models"
set "XDG_CACHE_HOME=%SAMPLAR_ROOT%models"
set "HUGGINGFACE_HUB_CACHE=%SAMPLAR_ROOT%models\huggingface"
set "HF_HOME=%SAMPLAR_ROOT%models\huggingface"

REM === Launch Application ===
start "" "%SAMPLAR_ROOT%SAMPLAR.exe"
"@

$LauncherPath = Join-Path $OutputDir "Launch SAMPLAR.bat"
Set-Content -Path $LauncherPath -Value $LauncherBat -Encoding ASCII
Write-Success "Created: Launch SAMPLAR.bat"

# Create a config file for the application to find Python
$ConfigJson = @"
{
    "python_path": "python/python.exe",
    "models_path": "models",
    "data_path": "data",
    "portable": true,
    "environment": {
        "PYTHONHOME": "python",
        "TORCH_HOME": "models",
        "XDG_CACHE_HOME": "models",
        "HF_HOME": "models/huggingface"
    },
    "version": "1.0",
    "created": "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
}
"@

$ConfigPath = Join-Path $OutputDir "SAMPLAR_portable.json"
Set-Content -Path $ConfigPath -Value $ConfigJson -Encoding UTF8
Write-Success "Created: SAMPLAR_portable.json"

# Create README for portable users
$ReadmeContent = @"
================================================================================
                        SAMPLAR - PORTABLE VERSION
================================================================================

QUICK START:
------------
1. Double-click "SAMPLAR.exe" to launch the application

FIRST TIME SETUP (if app doesn't launch):
-----------------------------------------
If the application fails to start, you may need to install the
Visual C++ Runtime. Run "vc_redist.x64.exe" included in this folder.

FULL SONG FEATURE:
------------------
The Full Song audio separation feature requires the included Python
environment. Everything is pre-configured and should work automatically.

If models need to download on first use, ensure you have internet access.

FOLDER CONTENTS:
----------------
- SAMPLAR.exe       : Main application
- vc_redist.x64.exe       : Visual C++ Runtime installer (run if needed)
- python/                 : Embedded Python with audio-separator
- models/                 : ML models for audio separation
- data/                   : Pattern databases
- SAMPLAR_portable.json       : Portable configuration

TROUBLESHOOTING:
----------------
- App won't start: Run vc_redist.x64.exe first
- Full Song fails: Check python/Scripts/audio-separator.exe exists
- Slow separation: First run downloads ~320MB model (needs internet)

================================================================================
"@

$ReadmePath = Join-Path $OutputDir "README.txt"
Set-Content -Path $ReadmePath -Value $ReadmeContent -Encoding UTF8
Write-Success "Created: README.txt"

# ============================================================================
# STEP 8: FINAL VERIFICATION
# ============================================================================

Write-Step "STEP 8: FINAL VERIFICATION"

Write-Info "Checking all critical components..."

$VerificationErrors = @()

# Critical files that MUST exist
$CriticalFiles = @(
    @{ Path = "SAMPLAR.exe"; Description = "Main application" },
    @{ Path = "python\python.exe"; Description = "Python interpreter" },
    @{ Path = "python\Scripts\pip.exe"; Description = "pip package manager" },
    @{ Path = "python\Scripts\audio-separator.exe"; Description = "Audio separator CLI" },
    @{ Path = "python\Lib\site-packages\torch\__init__.py"; Description = "PyTorch package" },
    @{ Path = "python\Lib\site-packages\audio_separator\__init__.py"; Description = "audio-separator package" },
    @{ Path = "python\Lib\site-packages\audio_separator\__main__.py"; Description = "audio-separator module entry point" },
    @{ Path = "python\Scripts\ffmpeg.exe"; Description = "FFmpeg (required by audio-separator)" },
    @{ Path = "data\patterns_with_rolls.json"; Description = "Pattern database" },
    @{ Path = "data\latin_patterns.json"; Description = "Latin patterns" },
    @{ Path = "SAMPLAR_portable.json"; Description = "Portable config" },
    @{ Path = "Launch SAMPLAR.bat"; Description = "Launcher script" },
    @{ Path = "README.txt"; Description = "User instructions" }
)

# Recommended files (warn if missing)
$RecommendedFiles = @(
    @{ Path = "vc_redist.x64.exe"; Description = "VC++ Redistributable" },
    @{ Path = "demucs_mt_openblas.exe"; Description = "Demucs CLI (CPU fallback)" },
    @{ Path = "VST3\SAMPLAR.vst3\Contents\x86_64-win\SAMPLAR.vst3"; Description = "VST3 plugin" },
    @{ Path = "BACKGROUNDS\TILESET.png"; Description = "UI tileset (processing page border)" },
    @{ Path = "BACKGROUNDS\CAMPO_optimized.gif"; Description = "Campo GIF background" },
    @{ Path = "BACKGROUNDS\LAVA_optimized.gif"; Description = "Lava GIF background" }
)

Write-Host ""
Write-Host "  CRITICAL COMPONENTS:" -ForegroundColor White

foreach ($File in $CriticalFiles) {
    $FullPath = Join-Path $OutputDir $File.Path
    if (Test-Path $FullPath) {
        Write-Host "    [OK] $($File.Description)" -ForegroundColor Green
    } else {
        Write-Host "    [MISSING] $($File.Description)" -ForegroundColor Red
        Write-Host "              Expected: $($File.Path)" -ForegroundColor DarkRed
        $VerificationErrors += $File.Description
    }
}

Write-Host ""
Write-Host "  RECOMMENDED COMPONENTS:" -ForegroundColor White

foreach ($File in $RecommendedFiles) {
    $FullPath = Join-Path $OutputDir $File.Path
    if (Test-Path $FullPath) {
        Write-Host "    [OK] $($File.Description)" -ForegroundColor Green
    } else {
        Write-Host "    [WARN] $($File.Description) - not included" -ForegroundColor Yellow
    }
}

# Check models folder has content
Write-Host ""
Write-Host "  ML MODELS:" -ForegroundColor White
$ModelsDir = Join-Path $OutputDir "models"
$ModelFiles = Get-ChildItem -Path $ModelsDir -Recurse -File -ErrorAction SilentlyContinue
if ($ModelFiles.Count -gt 0) {
    $ModelsSize = Get-FolderSize $ModelsDir
    Write-Host "    [OK] Models folder: $($ModelFiles.Count) files ($ModelsSize)" -ForegroundColor Green
} else {
    Write-Host "    [WARN] Models folder is empty - will download on first use" -ForegroundColor Yellow
}

# Final verdict
Write-Host ""
if ($VerificationErrors.Count -eq 0) {
    Write-Host "  " -ForegroundColor Green
    Write-Host "           ALL CRITICAL COMPONENTS VERIFIED            " -ForegroundColor Green
    Write-Host "                PORTABLE IS READY                      " -ForegroundColor Green
    Write-Host "  " -ForegroundColor Green
} else {
    Write-Host "  " -ForegroundColor Red
    Write-Host "       VERIFICATION FAILED - MISSING COMPONENTS        " -ForegroundColor Red
    Write-Host "  " -ForegroundColor Red
    Write-Host ""
    Write-Host "  Missing: $($VerificationErrors -join ', ')" -ForegroundColor Red
    Write-Host ""
    exit 1
}

# ============================================================================
# STEP 9: FINAL SUMMARY
# ============================================================================

Write-Step "STEP 9: BUILD COMPLETE"

Write-Host ""
Write-Host "  Portable folder created at:" -ForegroundColor Green
Write-Host "  $((Resolve-Path $OutputDir).Path)" -ForegroundColor White
Write-Host ""

# Calculate sizes
$TotalSize = Get-FolderSize $OutputDir
$AppSize = Get-FileSize (Join-Path $OutputDir "SAMPLAR.exe")
$DataSize = Get-FolderSize (Join-Path $OutputDir "data")
$PythonSize = Get-FolderSize (Join-Path $OutputDir "python")
$ModelsSize = Get-FolderSize (Join-Path $OutputDir "models")
$BackgroundsSize = Get-FolderSize (Join-Path $OutputDir "BACKGROUNDS")

Write-Host "  " -ForegroundColor Cyan
Write-Host "   FOLDER SIZE BREAKDOWN                               " -ForegroundColor Cyan
Write-Host "  " -ForegroundColor Cyan
Write-Host "   Application (exe + dlls):    $($AppSize.PadLeft(15)) " -ForegroundColor White
Write-Host "   Data files:                  $($DataSize.PadLeft(15)) " -ForegroundColor White
Write-Host "   Python environment:          $($PythonSize.PadLeft(15)) " -ForegroundColor White
Write-Host "   ML Models:                   $($ModelsSize.PadLeft(15)) " -ForegroundColor White
Write-Host "   Backgrounds (UI assets):     $($BackgroundsSize.PadLeft(15)) " -ForegroundColor White
Write-Host "  " -ForegroundColor Cyan
Write-Host "   TOTAL:                       $($TotalSize.PadLeft(15)) " -ForegroundColor Green
Write-Host "  " -ForegroundColor Cyan
Write-Host ""

Write-Host "  Files created:" -ForegroundColor Gray
Get-ChildItem $OutputDir -File | ForEach-Object {
    Write-Host "    - $($_.Name)" -ForegroundColor White
}
Write-Host ""

Write-Host "  Folders created:" -ForegroundColor Gray
Get-ChildItem $OutputDir -Directory | ForEach-Object {
    $FolderCount = (Get-ChildItem $_.FullName -Recurse -File).Count
    Write-Host "    - $($_.Name)/ ($FolderCount files)" -ForegroundColor White
}
Write-Host ""

Write-Host @"
  
                                                                         
     NEXT STEPS:                                                         
                                                                         
     1. Test the portable version:                                       
        > cd $OutputDir
        > .\Launch SAMPLAR.bat                                     
                                                                         
     2. Test Full Song workflow with audio separation                    
                                                                         
     3. Create installer using 001installer.ps1 (coming next)            
                                                                         
  
"@ -ForegroundColor Yellow

Write-Host ""
Write-Success "Portable build completed successfully!"
Write-Host ""
