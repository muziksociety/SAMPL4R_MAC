#!/usr/bin/env pwsh
# rebuild_app.ps1 - Kill running app and rebuild SAMPLAR

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  SAMPLAR - Clean Rebuild      " -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Step 1: Kill any running instances
Write-Host "Step 1: Killing any running instances..." -ForegroundColor Yellow
Write-Host ""

$processNames = @(
    "SAMPLAR",
    "SM5App",
    "Samplar"
)

$killedAny = $false
foreach ($procName in $processNames) {
    $processes = Get-Process -Name $procName -ErrorAction SilentlyContinue
    if ($processes) {
        foreach ($proc in $processes) {
            Write-Host "  Killing process: $($proc.Name) (PID: $($proc.Id))" -ForegroundColor Red
            Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
            $killedAny = $true
        }
    }
}

if ($killedAny) {
    Write-Host "  ✅ Processes killed" -ForegroundColor Green
    Write-Host "  Waiting 2 seconds for cleanup..." -ForegroundColor Gray
    Start-Sleep -Seconds 2
} else {
    Write-Host "  ✅ No running instances found" -ForegroundColor Green
}

Write-Host ""

# Step 2: Clean build directory
Write-Host "Step 2: Cleaning build directory..." -ForegroundColor Yellow
Write-Host ""

if (Test-Path "build") {
    Write-Host "  Removing old build directory..." -ForegroundColor Gray
    Remove-Item -Recurse -Force "build" -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 1
}

Write-Host "  Creating fresh build directory..." -ForegroundColor Gray
New-Item -ItemType Directory -Path "build" -Force | Out-Null
Write-Host "  ✅ Build directory ready" -ForegroundColor Green
Write-Host ""

# Step 3: Configure with CMake
Write-Host "Step 3: Configuring with CMake..." -ForegroundColor Yellow
Write-Host ""

Set-Location "build"

$cmakeConfigCmd = 'cmake .. -G "Visual Studio 17 2022" -A x64'
Write-Host "  Running: $cmakeConfigCmd" -ForegroundColor Gray
Write-Host ""

Invoke-Expression $cmakeConfigCmd

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "❌ CMake configuration failed!" -ForegroundColor Red
    Set-Location ..
    exit 1
}

Write-Host ""
Write-Host "  ✅ CMake configuration successful" -ForegroundColor Green
Write-Host ""

# Step 4: Build Release
Write-Host "Step 4: Building Release configuration..." -ForegroundColor Yellow
Write-Host ""

$cores = (Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors
Write-Host "  Using $cores parallel jobs" -ForegroundColor Gray
Write-Host ""

$buildCmd = "cmake --build . --config Release -j $cores"
Write-Host "  Running: $buildCmd" -ForegroundColor Gray
Write-Host ""

Invoke-Expression $buildCmd

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "❌ Build failed!" -ForegroundColor Red
    Set-Location ..
    exit 1
}

Set-Location ..

Write-Host ""
Write-Host "  ✅ Build successful!" -ForegroundColor Green
Write-Host ""

# Step 5: Verify output
Write-Host "Step 5: Verifying build output..." -ForegroundColor Yellow
Write-Host ""

$exePath = "build\SM5App_artefacts\Release\SAMPLAR.exe"
if (Test-Path $exePath) {
    $exeSize = (Get-Item $exePath).Length / 1MB
    Write-Host "  ✅ Executable found: $exePath" -ForegroundColor Green
    Write-Host "  📦 Size: $("{0:N2}" -f $exeSize) MB" -ForegroundColor Cyan
} else {
    Write-Host "  ❌ Executable not found at: $exePath" -ForegroundColor Red
    exit 1
}

$dllPath = "build\SM5App_artefacts\Release\libopenblas.dll"
if (Test-Path $dllPath) {
    $dllSize = (Get-Item $dllPath).Length / 1MB
    Write-Host "  ✅ OpenBLAS DLL found" -ForegroundColor Green
    Write-Host "  📦 Size: $("{0:N2}" -f $dllSize) MB" -ForegroundColor Cyan
} else {
    Write-Host "  ⚠️  OpenBLAS DLL not found (may be optional)" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  ✅ REBUILD COMPLETE!                  " -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Yellow
Write-Host "  1. Test the app: .\build\SM5App_artefacts\Release\`"SAMPLAR.exe`"" -ForegroundColor White
Write-Host "  2. Build installer: .\build_installer.ps1 -SkipBuild" -ForegroundColor White
Write-Host ""

