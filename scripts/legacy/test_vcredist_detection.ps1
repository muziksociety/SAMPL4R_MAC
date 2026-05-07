#!/usr/bin/env pwsh
# Test VC++ Redistributable Detection
# This script checks if VC++ 2015-2022 Redistributable is installed

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  VC++ Redistributable Detection Test" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Check registry for VC++ 2015-2022 x64
$regPath = "HKLM:\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\X64"

Write-Host "Checking registry path:" -ForegroundColor Yellow
Write-Host "  $regPath" -ForegroundColor Gray
Write-Host ""

if (Test-Path $regPath) {
    Write-Host "✓ Registry key found!" -ForegroundColor Green
    Write-Host ""
    
    $props = Get-ItemProperty -Path $regPath
    
    Write-Host "Registry values:" -ForegroundColor Cyan
    Write-Host "  Installed: $($props.Installed)" -ForegroundColor White
    Write-Host "  Version: $($props.Version)" -ForegroundColor White
    Write-Host "  Major: $($props.Major)" -ForegroundColor White
    Write-Host "  Minor: $($props.Minor)" -ForegroundColor White
    Write-Host "  Bld: $($props.Bld)" -ForegroundColor White
    Write-Host ""
    
    if ($props.Installed -eq 1) {
        Write-Host "✓ VC++ Redistributable IS INSTALLED" -ForegroundColor Green
        Write-Host "  Version: $($props.Version)" -ForegroundColor Green
    } else {
        Write-Host "✗ VC++ Redistributable NOT INSTALLED (Installed flag = 0)" -ForegroundColor Red
    }
} else {
    Write-Host "✗ Registry key NOT found" -ForegroundColor Red
    Write-Host ""
    Write-Host "VC++ Redistributable is NOT installed" -ForegroundColor Red
    Write-Host ""
    Write-Host "The installer will automatically install it." -ForegroundColor Yellow
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Also check for the actual DLL files
Write-Host "Checking for VC++ DLL files:" -ForegroundColor Yellow
Write-Host ""

$dllPaths = @(
    "$env:SystemRoot\System32\msvcp140.dll",
    "$env:SystemRoot\System32\vcruntime140.dll",
    "$env:SystemRoot\System32\vcruntime140_1.dll"
)

$allFound = $true
foreach ($dll in $dllPaths) {
    if (Test-Path $dll) {
        $version = (Get-Item $dll).VersionInfo.FileVersion
        Write-Host "  ✓ Found: $(Split-Path $dll -Leaf) (v$version)" -ForegroundColor Green
    } else {
        Write-Host "  ✗ Missing: $(Split-Path $dll -Leaf)" -ForegroundColor Red
        $allFound = $false
    }
}

Write-Host ""
if ($allFound) {
    Write-Host "✓ All required VC++ DLL files are present" -ForegroundColor Green
} else {
    Write-Host "✗ Some VC++ DLL files are missing" -ForegroundColor Red
    Write-Host "  The installer will install them automatically" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Summary
Write-Host "SUMMARY:" -ForegroundColor Cyan
Write-Host ""

if ((Test-Path $regPath) -and ((Get-ItemProperty -Path $regPath).Installed -eq 1) -and $allFound) {
    Write-Host "  ✓ VC++ Redistributable is fully installed and working" -ForegroundColor Green
    Write-Host "  ✓ The installer will SKIP VC++ installation" -ForegroundColor Green
} else {
    Write-Host "  ⚠ VC++ Redistributable is missing or incomplete" -ForegroundColor Yellow
    Write-Host "  ✓ The installer WILL install VC++ automatically" -ForegroundColor Green
}

Write-Host ""

