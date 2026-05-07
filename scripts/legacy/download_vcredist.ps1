#!/usr/bin/env pwsh
# Download Visual C++ 2022 Redistributable (x64)

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  VC++ 2022 Redistributable Downloader" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Create installer_dependencies folder
$depsDir = "installer_dependencies"
if (-not (Test-Path $depsDir)) {
    Write-Host "Creating $depsDir folder..." -ForegroundColor Yellow
    New-Item -ItemType Directory -Path $depsDir | Out-Null
}

# VC++ Redistributable download URL (official Microsoft link)
$vcRedistUrl = "https://aka.ms/vs/17/release/vc_redist.x64.exe"
$vcRedistPath = Join-Path $depsDir "vc_redist.x64.exe"

# Check if already downloaded
if (Test-Path $vcRedistPath) {
    $size = (Get-Item $vcRedistPath).Length / 1MB
    Write-Host "VC++ Redistributable already exists: $([math]::Round($size, 2)) MB" -ForegroundColor Green
    Write-Host ""
    
    $response = Read-Host "Re-download? (y/N)"
    if ($response -ne "y" -and $response -ne "Y") {
        Write-Host "Using existing file." -ForegroundColor Green
        exit 0
    }
    
    Remove-Item $vcRedistPath -Force
}

# Download VC++ Redistributable
Write-Host "Downloading VC++ 2022 Redistributable (x64)..." -ForegroundColor Yellow
Write-Host "URL: $vcRedistUrl" -ForegroundColor Gray
Write-Host ""

try {
    # Use Invoke-WebRequest (simpler, works in all PowerShell versions)
    Invoke-WebRequest -Uri $vcRedistUrl -OutFile $vcRedistPath -UseBasicParsing
    
    if (Test-Path $vcRedistPath) {
        $size = (Get-Item $vcRedistPath).Length / 1MB
        Write-Host ""
        Write-Host "✓ Download successful: $([math]::Round($size, 2)) MB" -ForegroundColor Green
        Write-Host "✓ Saved to: $vcRedistPath" -ForegroundColor Green
        Write-Host ""
        Write-Host "You can now build the installer with VC++ Runtime included!" -ForegroundColor Cyan
    } else {
        throw "Download failed - file not found"
    }
}
catch {
    Write-Host ""
    Write-Host "✗ Download failed: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host ""
    Write-Host "Please download manually from:" -ForegroundColor Yellow
    Write-Host "  $vcRedistUrl" -ForegroundColor White
    Write-Host "And save it to:" -ForegroundColor Yellow
    Write-Host "  $vcRedistPath" -ForegroundColor White
    exit 1
}

