<#
.SYNOPSIS
    Smart supervisor for OpenClaw Gateway with health monitoring.

.DESCRIPTION
    This script manages the OpenClaw Gateway process with intelligent health
    monitoring. It starts the gateway in foreground mode and monitors the
    /health/live endpoint. If the gateway becomes unhealthy for a sustained
    period, it will force-restart the process.
    
    This is designed to run as a Windows Service via NSSM, providing an
    additional layer of reliability on top of the gateway's internal watchdog.

.PARAMETER HealthCheckInterval
    Seconds between health checks. Default: 30

.PARAMETER UnhealthyThreshold
    Seconds of continuous unhealthy status before restart. Default: 60

.PARAMETER GatewayPort
    Port the gateway listens on. Default: 22140

.PARAMETER GracePeriod
    Seconds to wait after starting before health checks begin. Default: 30
#>

param(
    [int]$HealthCheckInterval = 30,
    [int]$UnhealthyThreshold = 60,
    [int]$GatewayPort = 22140,
    [int]$GracePeriod = 30
)

# Setup paths
$OpenClawHome = Join-Path $env:USERPROFILE ".openclaw"
$LogsDir = Join-Path $OpenClawHome "logs"
$LogFile = Join-Path $LogsDir "supervisor.log"

# Ensure logs directory exists
if (-not (Test-Path $LogsDir)) {
    New-Item -ItemType Directory -Path $LogsDir -Force | Out-Null
}

# Logging function
function Write-Log {
    param(
        [string]$Message,
        [string]$Level = "INFO"
    )
    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    $logLine = "[$timestamp] [$Level] $Message"
    
    # Write to file
    Add-Content -Path $LogFile -Value $logLine -ErrorAction SilentlyContinue
    
    # Also write to console (captured by NSSM)
    switch ($Level) {
        "ERROR" { Write-Host $logLine -ForegroundColor Red }
        "WARN"  { Write-Host $logLine -ForegroundColor Yellow }
        "INFO"  { Write-Host $logLine -ForegroundColor White }
        default { Write-Host $logLine }
    }
}

# Rotate log if too large (>10MB)
function Rotate-LogIfNeeded {
    if (Test-Path $LogFile) {
        $logSize = (Get-Item $LogFile).Length
        if ($logSize -gt 10MB) {
            $rotatedLog = $LogFile -replace "\.log$", ".$(Get-Date -Format 'yyyyMMdd-HHmmss').log"
            Move-Item -Path $LogFile -Destination $rotatedLog -Force
            Write-Log "Rotated log to $rotatedLog"
            
            # Keep only last 5 rotated logs
            Get-ChildItem -Path $LogsDir -Filter "supervisor.*.log" |
                Sort-Object CreationTime -Descending |
                Select-Object -Skip 5 |
                Remove-Item -Force -ErrorAction SilentlyContinue
        }
    }
}

# Health check function
function Test-GatewayHealth {
    param([int]$Port)
    
    try {
        $response = Invoke-WebRequest -Uri "http://127.0.0.1:$Port/health/live" `
            -TimeoutSec 10 `
            -UseBasicParsing `
            -ErrorAction Stop
        
        return $response.StatusCode -eq 200
    } catch {
        return $false
    }
}

# Start the gateway process
function Start-GatewayProcess {
    Write-Log "Starting OpenClaw Gateway..."
    
    $processInfo = New-Object System.Diagnostics.ProcessStartInfo
    $processInfo.FileName = "openclaw"
    $processInfo.Arguments = "gateway start --foreground"
    $processInfo.UseShellExecute = $false
    $processInfo.RedirectStandardOutput = $true
    $processInfo.RedirectStandardError = $true
    $processInfo.CreateNoWindow = $true
    $processInfo.WorkingDirectory = $env:USERPROFILE
    
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $processInfo
    
    # Setup async output handling
    $outputHandler = {
        if (-not [String]::IsNullOrEmpty($EventArgs.Data)) {
            Add-Content -Path "$($Event.MessageData.LogsDir)\gateway-stdout.log" -Value $EventArgs.Data -ErrorAction SilentlyContinue
        }
    }
    $errorHandler = {
        if (-not [String]::IsNullOrEmpty($EventArgs.Data)) {
            Add-Content -Path "$($Event.MessageData.LogsDir)\gateway-stderr.log" -Value $EventArgs.Data -ErrorAction SilentlyContinue
        }
    }
    
    Register-ObjectEvent -InputObject $process -EventName OutputDataReceived -Action $outputHandler -MessageData @{LogsDir=$LogsDir} | Out-Null
    Register-ObjectEvent -InputObject $process -EventName ErrorDataReceived -Action $errorHandler -MessageData @{LogsDir=$LogsDir} | Out-Null
    
    try {
        $process.Start() | Out-Null
        $process.BeginOutputReadLine()
        $process.BeginErrorReadLine()
        
        Write-Log "Gateway process started (PID: $($process.Id))"
        return $process
    } catch {
        Write-Log "Failed to start gateway: $_" "ERROR"
        return $null
    }
}

# Stop the gateway process
function Stop-GatewayProcess {
    param([System.Diagnostics.Process]$Process)
    
    if ($Process -and -not $Process.HasExited) {
        Write-Log "Stopping gateway process (PID: $($Process.Id))..."
        
        # Try graceful shutdown first
        try {
            # Send Ctrl+C equivalent
            $Process.Kill()
            $Process.WaitForExit(5000)
        } catch {
            Write-Log "Could not stop gracefully: $_" "WARN"
        }
        
        # Force kill if still running
        if (-not $Process.HasExited) {
            Write-Log "Force killing process..." "WARN"
            $Process.Kill()
            $Process.WaitForExit(3000)
        }
        
        Write-Log "Gateway process stopped"
    }
}

# Cleanup function for graceful shutdown
function Cleanup {
    Write-Log "Supervisor shutting down..."
    if ($script:gatewayProcess) {
        Stop-GatewayProcess -Process $script:gatewayProcess
    }
    Get-EventSubscriber | Unregister-Event -ErrorAction SilentlyContinue
    Write-Log "Supervisor shutdown complete"
}

# Register cleanup on exit
$null = Register-EngineEvent -SourceIdentifier PowerShell.Exiting -Action { Cleanup }

# Handle Ctrl+C
[Console]::TreatControlCAsInput = $false
$null = [Console]::CancelKeyPress.Add({
    param($sender, $e)
    $e.Cancel = $true
    $script:shouldExit = $true
})

# Main supervisor loop
Write-Log "============================================================"
Write-Log "OpenClaw Gateway Supervisor Starting"
Write-Log "============================================================"
Write-Log "Configuration:"
Write-Log "  Health Check Interval: ${HealthCheckInterval}s"
Write-Log "  Unhealthy Threshold:   ${UnhealthyThreshold}s"
Write-Log "  Gateway Port:          $GatewayPort"
Write-Log "  Grace Period:          ${GracePeriod}s"
Write-Log "  Log File:              $LogFile"
Write-Log "============================================================"

$script:shouldExit = $false
$unhealthyStartTime = $null
$restartCount = 0
$lastRotateCheck = Get-Date

try {
    while (-not $script:shouldExit) {
        # Rotate log if needed (check once per hour)
        if ((Get-Date) - $lastRotateCheck -gt [TimeSpan]::FromHours(1)) {
            Rotate-LogIfNeeded
            $lastRotateCheck = Get-Date
        }
        
        # Start gateway if not running
        if (-not $script:gatewayProcess -or $script:gatewayProcess.HasExited) {
            if ($script:gatewayProcess -and $script:gatewayProcess.HasExited) {
                $exitCode = $script:gatewayProcess.ExitCode
                Write-Log "Gateway process exited (Exit Code: $exitCode)" "WARN"
                
                # Respect internal restart protocols - wait before intervening
                Write-Log "Waiting 15s before restart (respecting internal protocols)..."
                Start-Sleep -Seconds 15
            }
            
            $script:gatewayProcess = Start-GatewayProcess
            $restartCount++
            $unhealthyStartTime = $null
            
            if (-not $script:gatewayProcess) {
                Write-Log "Failed to start gateway, waiting 30s before retry..." "ERROR"
                Start-Sleep -Seconds 30
                continue
            }
            
            # Grace period before health checks
            Write-Log "Waiting ${GracePeriod}s grace period for gateway to initialize..."
            Start-Sleep -Seconds $GracePeriod
        }
        
        # Perform health check
        $isHealthy = Test-GatewayHealth -Port $GatewayPort
        
        if ($isHealthy) {
            if ($unhealthyStartTime) {
                Write-Log "Gateway recovered, health check passed" "INFO"
                $unhealthyStartTime = $null
            }
        } else {
            if (-not $unhealthyStartTime) {
                $unhealthyStartTime = Get-Date
                Write-Log "Health check failed, starting unhealthy timer..." "WARN"
            } else {
                $unhealthyDuration = (Get-Date) - $unhealthyStartTime
                $remainingSeconds = $UnhealthyThreshold - $unhealthyDuration.TotalSeconds
                
                Write-Log "Health check failed (unhealthy for $([int]$unhealthyDuration.TotalSeconds)s, threshold: ${UnhealthyThreshold}s)" "WARN"
                
                if ($unhealthyDuration.TotalSeconds -ge $UnhealthyThreshold) {
                    Write-Log "UNHEALTHY THRESHOLD EXCEEDED - Forcing restart!" "ERROR"
                    Stop-GatewayProcess -Process $script:gatewayProcess
                    $script:gatewayProcess = $null
                    $unhealthyStartTime = $null
                    
                    # Brief pause before restart
                    Start-Sleep -Seconds 5
                    continue
                }
            }
        }
        
        # Wait until next health check
        Start-Sleep -Seconds $HealthCheckInterval
    }
} catch {
    Write-Log "Supervisor error: $_" "ERROR"
    Write-Log $_.ScriptStackTrace "ERROR"
} finally {
    Cleanup
}

Write-Log "Supervisor exited"
exit 0
