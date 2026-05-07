#!/usr/bin/env pwsh
# Build SAMPLAR and create portable package

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  SAMPLAR - Build & Package" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Step 1: Build
Write-Host "[1/2] Building SAMPLAR..." -ForegroundColor Yellow
cd build
cmake --build . --config Release -j 16

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "Build failed!" -ForegroundColor Red
    cd ..
    exit 1
}

Write-Host ""
Write-Host "Build successful!" -ForegroundColor Green
cd ..

# Step 2: Create portable package
Write-Host ""
Write-Host "[2/2] Creating portable package..." -ForegroundColor Yellow
.\build_portable.ps1

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "  ALL DONE!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green

