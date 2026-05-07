#Requires -RunAsAdministrator
<#
.SYNOPSIS
    Installs OpenClaw Gateway as a Windows Service using NSSM.

.DESCRIPTION
    This script downloads NSSM (Non-Sucking Service Manager) if not present,
    then configures the OpenClaw Gateway as a Windows Service with automatic
    restart on crash and boot-time startup.

.PARAMETER NssmPath
    Custom path to store NSSM. Defaults to ~/.openclaw/nssm

.PARAMETER ServiceName
    Name of the Windows Service. Defaults to OpenClawGateway

.EXAMPLE
    .\install.ps1
    
.EXAMPLE
    .\install.ps1 -ServiceName "MyOpenClawService"
#>

param(
    [string]$NssmPath = (Join-Path $env:USERPROFILE ".openclaw\nssm"),
    [string]$ServiceName = "OpenClawGateway",
    [int]$RestartDelayMs = 15000
)

$ErrorActionPreference = "Stop"

# NSSM download URL (2.24 is the latest stable)
$NssmVersion = "2.24"
$NssmUrl = "https://nssm.cc/release/nssm-$NssmVersion.zip"
$NssmZip = Join-Path $env:TEMP "nssm-$NssmVersion.zip"
$NssmExe = Join-Path $NssmPath "nssm.exe"

# Get the script directory (where supervisor.ps1 lives)
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$SupervisorScript = Join-Path $ScriptDir "supervisor.ps1"

Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "       OpenClaw Gateway Windows Service Installer           " -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host ""

# Check if service already exists
$existingService = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
if ($existingService) {
    Write-Host "[!] Service '$ServiceName' already exists." -ForegroundColor Yellow
    $response = Read-Host "Do you want to reinstall? (y/N)"
    if ($response -ne 'y' -and $response -ne 'Y') {
        Write-Host "Installation cancelled." -ForegroundColor Yellow
        exit 0
    }
    
    Write-Host "-> Stopping and removing existing service..." -ForegroundColor Gray
    if ($existingService.Status -eq 'Running') {
        Stop-Service -Name $ServiceName -Force
    }
    
    if (Test-Path $NssmExe) {
        & $NssmExe remove $ServiceName confirm 2>$null
    } else {
        sc.exe delete $ServiceName 2>$null
    }
    Start-Sleep -Seconds 2
}

# Step 1: Download NSSM if not present
Write-Host "Step 1: Checking NSSM..." -ForegroundColor White

if (-not (Test-Path $NssmExe)) {
    Write-Host "-> NSSM not found. Downloading..." -ForegroundColor Gray
    
    # Create directory
    New-Item -ItemType Directory -Path $NssmPath -Force | Out-Null
    
    # Download NSSM
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -Uri $NssmUrl -OutFile $NssmZip -UseBasicParsing
        Write-Host "  [OK] Downloaded NSSM $NssmVersion" -ForegroundColor Green
    }
    catch {
        Write-Host "  [FAIL] Failed to download NSSM: $_" -ForegroundColor Red
        exit 1
    }
    
    # Extract NSSM
    try {
        Expand-Archive -Path $NssmZip -DestinationPath $env:TEMP -Force
        
        # Determine architecture and copy correct binary
        $arch = if ([Environment]::Is64BitOperatingSystem) { "win64" } else { "win32" }
        $extractedNssm = Join-Path $env:TEMP "nssm-$NssmVersion\$arch\nssm.exe"
        
        Copy-Item -Path $extractedNssm -Destination $NssmExe -Force
        
        # Cleanup
        Remove-Item -Path $NssmZip -Force
        Remove-Item -Path (Join-Path $env:TEMP "nssm-$NssmVersion") -Recurse -Force
        
        Write-Host "  [OK] Extracted NSSM to $NssmExe" -ForegroundColor Green
    }
    catch {
        Write-Host "  [FAIL] Failed to extract NSSM: $_" -ForegroundColor Red
        exit 1
    }
}
else {
    Write-Host "  [OK] NSSM already installed at $NssmExe" -ForegroundColor Green
}

# Step 2: Verify openclaw is available
Write-Host "Step 2: Verifying OpenClaw installation..." -ForegroundColor White

$openclawPath = (Get-Command openclaw -ErrorAction SilentlyContinue).Source
if (-not $openclawPath) {
    Write-Host "  [FAIL] 'openclaw' command not found in PATH" -ForegroundColor Red
    Write-Host "  Please install OpenClaw first: npm install -g openclaw" -ForegroundColor Yellow
    exit 1
}
Write-Host "  [OK] Found openclaw at: $openclawPath" -ForegroundColor Green

# Step 3: Verify supervisor script exists
Write-Host "Step 3: Verifying supervisor script..." -ForegroundColor White

if (-not (Test-Path $SupervisorScript)) {
    Write-Host "  [FAIL] Supervisor script not found at: $SupervisorScript" -ForegroundColor Red
    exit 1
}
Write-Host "  [OK] Found supervisor script" -ForegroundColor Green

# Step 4: Create logs directory
Write-Host "Step 4: Creating logs directory..." -ForegroundColor White

$LogsDir = Join-Path $env:USERPROFILE ".openclaw\logs"
if (-not (Test-Path $LogsDir)) {
    New-Item -ItemType Directory -Path $LogsDir -Force | Out-Null
}
Write-Host "  [OK] Logs directory ready: $LogsDir" -ForegroundColor Green

# Step 5: Install the service using NSSM
Write-Host "Step 5: Installing Windows Service..." -ForegroundColor White

$PowerShellPath = (Get-Command powershell.exe).Source

try {
    # Install the service
    & $NssmExe install $ServiceName $PowerShellPath "-ExecutionPolicy Bypass -NoProfile -File `"$SupervisorScript`""
    
    # Configure service display name and description
    & $NssmExe set $ServiceName DisplayName "OpenClaw Gateway"
    & $NssmExe set $ServiceName Description "OpenClaw Gateway service with intelligent health monitoring and automatic restart"
    
    # Configure startup type (automatic)
    & $NssmExe set $ServiceName Start SERVICE_AUTO_START
    
    # Configure restart on crash
    & $NssmExe set $ServiceName AppExit Default Restart
    & $NssmExe set $ServiceName AppRestartDelay $RestartDelayMs
    
    # Configure crash throttling (don't restart more than 3 times in 60 seconds)
    & $NssmExe set $ServiceName AppThrottle 60000
    
    # Configure stdout/stderr logging
    $StdoutLog = Join-Path $LogsDir "service-stdout.log"
    $StderrLog = Join-Path $LogsDir "service-stderr.log"
    & $NssmExe set $ServiceName AppStdout $StdoutLog
    & $NssmExe set $ServiceName AppStderr $StderrLog
    & $NssmExe set $ServiceName AppStdoutCreationDisposition 4
    & $NssmExe set $ServiceName AppStderrCreationDisposition 4
    & $NssmExe set $ServiceName AppRotateFiles 1
    & $NssmExe set $ServiceName AppRotateBytes 10485760
    
    # Set working directory to user's home
    & $NssmExe set $ServiceName AppDirectory $env:USERPROFILE
    
    Write-Host "  [OK] Service installed successfully" -ForegroundColor Green
}
catch {
    Write-Host "  [FAIL] Failed to install service: $_" -ForegroundColor Red
    exit 1
}

# Step 6: Start the service
Write-Host "Step 6: Starting service..." -ForegroundColor White

try {
    Start-Service -Name $ServiceName
    Start-Sleep -Seconds 3
    
    $service = Get-Service -Name $ServiceName
    if ($service.Status -eq 'Running') {
        Write-Host "  [OK] Service started successfully" -ForegroundColor Green
    }
    else {
        Write-Host "  [!] Service installed but not running (Status: $($service.Status))" -ForegroundColor Yellow
        Write-Host "  Check logs at: $LogsDir" -ForegroundColor Yellow
    }
}
catch {
    Write-Host "  [!] Could not start service: $_" -ForegroundColor Yellow
    Write-Host "  Try starting manually: Start-Service $ServiceName" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "Installation Complete!" -ForegroundColor Green
Write-Host ""
Write-Host "Service Name:     $ServiceName" -ForegroundColor White
Write-Host "NSSM Location:    $NssmExe" -ForegroundColor White
Write-Host "Supervisor:       $SupervisorScript" -ForegroundColor White
Write-Host "Logs:             $LogsDir" -ForegroundColor White
Write-Host ""
Write-Host "Useful commands:" -ForegroundColor Yellow
Write-Host "  Start:   Start-Service $ServiceName" -ForegroundColor Gray
Write-Host "  Stop:    Stop-Service $ServiceName" -ForegroundColor Gray
Write-Host "  Status:  Get-Service $ServiceName" -ForegroundColor Gray
Write-Host "  Logs:    Get-Content $LogsDir\supervisor.log -Tail 50" -ForegroundColor Gray
Write-Host "============================================================" -ForegroundColor Cyan
