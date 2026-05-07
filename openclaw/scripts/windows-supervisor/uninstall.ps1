#Requires -RunAsAdministrator
<#
.SYNOPSIS
    Uninstalls the OpenClaw Gateway Windows Service.

.DESCRIPTION
    This script stops and removes the OpenClaw Gateway Windows Service.
    Optionally removes NSSM and logs.

.PARAMETER ServiceName
    Name of the Windows Service. Defaults to OpenClawGateway

.PARAMETER RemoveNssm
    Also remove the NSSM installation

.PARAMETER RemoveLogs
    Also remove the supervisor logs

.EXAMPLE
    .\uninstall.ps1
    
.EXAMPLE
    .\uninstall.ps1 -RemoveNssm -RemoveLogs
#>

param(
    [string]$ServiceName = "OpenClawGateway",
    [string]$NssmPath = (Join-Path $env:USERPROFILE ".openclaw\nssm"),
    [switch]$RemoveNssm,
    [switch]$RemoveLogs
)

$ErrorActionPreference = "Stop"
$NssmExe = Join-Path $NssmPath "nssm.exe"

Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "       OpenClaw Gateway Windows Service Uninstaller         " -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host ""

# Step 1: Check if service exists
Write-Host "Step 1: Checking for service..." -ForegroundColor White

$service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
if (-not $service) {
    Write-Host "  [!] Service '$ServiceName' not found" -ForegroundColor Yellow
    
    if (-not $RemoveNssm -and -not $RemoveLogs) {
        Write-Host "Nothing to uninstall." -ForegroundColor Gray
        exit 0
    }
}
else {
    Write-Host "  [OK] Found service: $ServiceName (Status: $($service.Status))" -ForegroundColor Green
}

# Step 2: Stop the service if running
if ($service -and $service.Status -eq 'Running') {
    Write-Host "Step 2: Stopping service..." -ForegroundColor White
    
    try {
        Stop-Service -Name $ServiceName -Force
        Start-Sleep -Seconds 2
        Write-Host "  [OK] Service stopped" -ForegroundColor Green
    }
    catch {
        Write-Host "  [!] Could not stop gracefully, forcing..." -ForegroundColor Yellow
        
        # Try to kill associated processes
        $processName = "powershell"
        Get-Process -Name $processName -ErrorAction SilentlyContinue | 
            Where-Object { $_.MainWindowTitle -match "supervisor" -or $_.CommandLine -match "supervisor.ps1" } |
            Stop-Process -Force -ErrorAction SilentlyContinue
        
        Start-Sleep -Seconds 2
    }
}
elseif ($service) {
    Write-Host "Step 2: Service already stopped" -ForegroundColor White
}

# Step 3: Remove the service
if ($service) {
    Write-Host "Step 3: Removing service..." -ForegroundColor White
    
    try {
        if (Test-Path $NssmExe) {
            & $NssmExe remove $ServiceName confirm
        }
        else {
            # Fallback to sc.exe if NSSM not available
            sc.exe delete $ServiceName
        }
        
        Start-Sleep -Seconds 2
        
        # Verify removal
        $checkService = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
        if (-not $checkService) {
            Write-Host "  [OK] Service removed successfully" -ForegroundColor Green
        }
        else {
            Write-Host "  [!] Service may still exist (might need reboot)" -ForegroundColor Yellow
        }
    }
    catch {
        Write-Host "  [FAIL] Failed to remove service: $_" -ForegroundColor Red
    }
}

# Step 4: Remove NSSM if requested
if ($RemoveNssm) {
    Write-Host "Step 4: Removing NSSM..." -ForegroundColor White
    
    if (Test-Path $NssmPath) {
        try {
            Remove-Item -Path $NssmPath -Recurse -Force
            Write-Host "  [OK] NSSM removed from $NssmPath" -ForegroundColor Green
        }
        catch {
            Write-Host "  [FAIL] Failed to remove NSSM: $_" -ForegroundColor Red
        }
    }
    else {
        Write-Host "  [!] NSSM not found at $NssmPath" -ForegroundColor Yellow
    }
}
else {
    Write-Host "Step 4: Keeping NSSM (use -RemoveNssm to remove)" -ForegroundColor Gray
}

# Step 5: Remove logs if requested
if ($RemoveLogs) {
    Write-Host "Step 5: Removing logs..." -ForegroundColor White
    
    $LogsDir = Join-Path $env:USERPROFILE ".openclaw\logs"
    $LogFiles = @(
        "supervisor.log",
        "service-stdout.log",
        "service-stderr.log"
    )
    
    foreach ($logFile in $LogFiles) {
        $logPath = Join-Path $LogsDir $logFile
        if (Test-Path $logPath) {
            try {
                Remove-Item -Path $logPath -Force
                Write-Host "  [OK] Removed $logFile" -ForegroundColor Green
            }
            catch {
                Write-Host "  [!] Could not remove $logFile (may be in use)" -ForegroundColor Yellow
            }
        }
    }
}
else {
    Write-Host "Step 5: Keeping logs (use -RemoveLogs to remove)" -ForegroundColor Gray
}

Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "Uninstallation Complete!" -ForegroundColor Green
Write-Host "============================================================" -ForegroundColor Cyan
