# Build all BLAS-optimized versions of demucs_mt.cpp.main
# This script builds 3 versions:
# 1. demucs_mt_openblas.exe - Universal fallback (OpenBLAS)
# 2. demucs_mt_mkl.exe - Intel MKL optimized (~15% faster on Intel CPUs)
# 3. demucs_mt_blis.exe - AMD BLIS optimized (~10% faster on AMD CPUs)

param(
    [string]$BuildType = "Release"
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Building All BLAS-Optimized Demucs Versions" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

$rootDir = $PSScriptRoot
$buildDir = Join-Path $rootDir "build"
$outputDir = Join-Path $buildDir "SM5App_artefacts\$BuildType"
$demucsSource = Join-Path $rootDir "third_party\demucs.cpp"

# Create a temporary build directory for each BLAS version
$tempBuildBase = Join-Path $rootDir "build_temp_blas"

function Build-DemucsCLI {
    param(
        [string]$BlasName,
        [string]$OutputName,
        [hashtable]$CMakeOptions
    )
    
    Write-Host "----------------------------------------" -ForegroundColor Yellow
    Write-Host "Building $OutputName ($BlasName)" -ForegroundColor Yellow
    Write-Host "----------------------------------------" -ForegroundColor Yellow
    
    $tempBuildDir = Join-Path $tempBuildBase $BlasName
    
    # Clean previous build
    if (Test-Path $tempBuildDir) {
        Write-Host "Cleaning previous build..." -ForegroundColor Gray
        Remove-Item -Path $tempBuildDir -Recurse -Force
    }
    
    New-Item -ItemType Directory -Path $tempBuildDir -Force | Out-Null
    
    # Build CMake options string
    $cmakeArgs = @(
        "-S", $demucsSource,
        "-B", $tempBuildDir,
        "-DCMAKE_BUILD_TYPE=$BuildType"
    )
    
    foreach ($key in $CMakeOptions.Keys) {
        $cmakeArgs += "-D$key=$($CMakeOptions[$key])"
    }
    
    # Configure
    Write-Host "Configuring with CMake..." -ForegroundColor Gray
    & cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ERROR] CMake configuration failed for $BlasName" -ForegroundColor Red
        return $false
    }

    # Build
    Write-Host "Building..." -ForegroundColor Gray
    & cmake --build $tempBuildDir --config $BuildType --target demucs_mt.cpp.main -j 8
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ERROR] Build failed for $BlasName" -ForegroundColor Red
        return $false
    }

    # Copy to output directory
    $builtExe = Join-Path $tempBuildDir "$BuildType\demucs_mt.cpp.main.exe"
    if (-not (Test-Path $builtExe)) {
        Write-Host "[ERROR] Built executable not found: $builtExe" -ForegroundColor Red
        return $false
    }
    
    $destPath = Join-Path $outputDir $OutputName
    Copy-Item -Path $builtExe -Destination $destPath -Force
    
    $sizeKB = [math]::Round((Get-Item $destPath).Length / 1KB, 2)
    Write-Host "[OK] $OutputName built successfully ($sizeKB KB)" -ForegroundColor Green
    Write-Host ""

    return $true
}

# Ensure output directory exists
if (-not (Test-Path $outputDir)) {
    Write-Host "[ERROR] Output directory not found: $outputDir" -ForegroundColor Red
    Write-Host "Please build SM5App first with CMake" -ForegroundColor Yellow
    exit 1
}

Write-Host "Output directory: $outputDir" -ForegroundColor Gray
Write-Host ""

# Build 1: OpenBLAS version (universal fallback)
$openblasOptions = @{
    "USE_OPENBLAS" = "ON"
    "USE_INTEL_MKL" = "OFF"
    "USE_AMD_AOCL" = "OFF"
    "BLAS_LIBRARIES" = "$rootDir\third_party\openblas\lib\libopenblas.lib"
    "BLAS_INCLUDE_DIRS" = "$rootDir\third_party\openblas\include"
    "BLAS_FOUND" = "TRUE"
}

$success1 = Build-DemucsCLI -BlasName "OpenBLAS" -OutputName "demucs_mt_openblas.exe" -CMakeOptions $openblasOptions

# Build 2: Intel MKL version (Intel CPUs)
# Note: This requires Intel MKL to be installed
# Download from: https://www.intel.com/content/www/us/en/developer/tools/oneapi/onemkl-download.html

$mklRoot = $env:MKLROOT
if (-not $mklRoot) {
    # Try common installation paths
    $commonPaths = @(
        "C:\Program Files (x86)\Intel\oneAPI\mkl\latest",
        "C:\Program Files\Intel\oneAPI\mkl\latest",
        "$env:ProgramFiles\Intel\oneAPI\mkl\latest",
        "${env:ProgramFiles(x86)}\Intel\oneAPI\mkl\latest"
    )
    
    foreach ($path in $commonPaths) {
        if (Test-Path $path) {
            $mklRoot = $path
            break
        }
    }
}

if ($mklRoot -and (Test-Path $mklRoot)) {
    Write-Host "Found Intel MKL at: $mklRoot" -ForegroundColor Gray

    $mklOptions = @{
        "USE_OPENBLAS" = "OFF"
        "USE_INTEL_MKL" = "ON"
        "USE_AMD_AOCL" = "OFF"
        "INTEL_MKL_ROOT" = $mklRoot
    }

    $success2 = Build-DemucsCLI -BlasName "IntelMKL" -OutputName "demucs_mt_mkl.exe" -CMakeOptions $mklOptions
} else {
    Write-Host "[WARNING] Intel MKL not found - skipping demucs_mt_mkl.exe" -ForegroundColor Yellow
    Write-Host "  To build Intel MKL version, install from:" -ForegroundColor Gray
    Write-Host "  https://www.intel.com/content/www/us/en/developer/tools/oneapi/onemkl-download.html" -ForegroundColor Gray
    Write-Host ""
    $success2 = $false
}

# Build 3: AMD BLIS version (AMD CPUs)
# Note: This requires AMD AOCL to be installed
# Download from: https://www.amd.com/en/developer/aocl.html

$aoclRoot = $env:AOCL_ROOT
if (-not $aoclRoot) {
    # Try common installation paths
    $commonPaths = @(
        "C:\AMD\aocl",
        "$env:ProgramFiles\AMD\aocl",
        "C:\Program Files\AMD\aocl"
    )
    
    foreach ($path in $commonPaths) {
        if (Test-Path $path) {
            $aoclRoot = $path
            break
        }
    }
}

if ($aoclRoot -and (Test-Path $aoclRoot)) {
    Write-Host "Found AMD AOCL at: $aoclRoot" -ForegroundColor Gray
    
    $aoclOptions = @{
        "USE_OPENBLAS" = "OFF"
        "USE_INTEL_MKL" = "OFF"
        "USE_AMD_AOCL" = "ON"
        "AOCL_ROOT" = $aoclRoot
    }
    
    $success3 = Build-DemucsCLI -BlasName "AmdAoclBlis" -OutputName "demucs_mt_blis.exe" -CMakeOptions $aoclOptions
} else {
    Write-Host "[WARNING] AMD AOCL not found - skipping demucs_mt_blis.exe" -ForegroundColor Yellow
    Write-Host "  To build AMD BLIS version, install from:" -ForegroundColor Gray
    Write-Host "  https://www.amd.com/en/developer/aocl.html" -ForegroundColor Gray
    Write-Host ""
    $success3 = $false
}

# Clean up temporary build directories
Write-Host "Cleaning up temporary build directories..." -ForegroundColor Gray
if (Test-Path $tempBuildBase) {
    Remove-Item -Path $tempBuildBase -Recurse -Force
}

# Summary
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Build Summary" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

if ($success1) {
    Write-Host "[OK] demucs_mt_openblas.exe (OpenBLAS - universal)" -ForegroundColor Green
} else {
    Write-Host "[ERROR] demucs_mt_openblas.exe FAILED" -ForegroundColor Red
}

if ($success2) {
    Write-Host "[OK] demucs_mt_mkl.exe (Intel MKL - Intel CPUs)" -ForegroundColor Green
} else {
    Write-Host "[WARNING] demucs_mt_mkl.exe SKIPPED (Intel MKL not installed)" -ForegroundColor Yellow
}

if ($success3) {
    Write-Host "[OK] demucs_mt_blis.exe (AMD BLIS - AMD CPUs)" -ForegroundColor Green
} else {
    Write-Host "[WARNING] demucs_mt_blis.exe SKIPPED (AMD AOCL not installed)" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "All available versions have been built to:" -ForegroundColor Cyan
Write-Host "  $outputDir" -ForegroundColor White
Write-Host ""

if ($success1) {
    Write-Host "The application will automatically select the optimal version at runtime." -ForegroundColor Green
} else {
    Write-Host "[ERROR] OpenBLAS version failed - this is required!" -ForegroundColor Red
    exit 1
}

