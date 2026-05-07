# OpenClaw Gateway Windows Supervisor

A robust Windows service setup for running the OpenClaw Gateway with intelligent health monitoring and automatic recovery.

## Overview

This supervisor system provides **two layers of reliability**:

1. **NSSM (Service Layer)**: Monitors the supervisor process itself. If PowerShell crashes, NSSM restarts it.
2. **Supervisor Script (Health Layer)**: Monitors the gateway's `/health/live` endpoint. If the gateway becomes unresponsive, the supervisor force-restarts it.

This is **additive** to the gateway's internal watchdog—it provides external oversight without interfering with normal operation.

## Quick Start

### Install

```powershell
# Run as Administrator
cd scripts\windows-supervisor
.\install.ps1
```

### Uninstall

```powershell
# Run as Administrator
.\uninstall.ps1

# To also remove NSSM and logs:
.\uninstall.ps1 -RemoveNssm -RemoveLogs
```

## How It Works

```
┌─────────────────────────────────────────────────────────────┐
│                    Windows Service (NSSM)                    │
│  - Starts on boot                                           │
│  - Restarts on crash (15s delay)                            │
│  - Manages supervisor.ps1 lifecycle                         │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    supervisor.ps1                            │
│  - Runs `openclaw gateway start --foreground`               │
│  - Monitors /health/live every 30s                          │
│  - Force-restarts if unhealthy for 60s                      │
│  - Logs all actions                                         │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    OpenClaw Gateway                          │
│  - Internal watchdog (self-monitors)                        │
│  - Handles most restarts internally                         │
│  - External supervisor only intervenes when truly stuck     │
└─────────────────────────────────────────────────────────────┘
```

### Health Check Flow

1. Gateway starts, supervisor waits 30s (grace period)
2. Every 30 seconds, supervisor pings `http://127.0.0.1:22140/health/live`
3. If healthy: reset unhealthy timer
4. If unhealthy: start/continue unhealthy timer
5. If unhealthy for 60s continuous: force-kill and restart
6. After restart, wait 15s (respecting internal protocols) + 30s grace period

## Configuration

### Install Script Options

```powershell
.\install.ps1 -ServiceName "MyGateway" -RestartDelayMs 30000
```

| Parameter         | Default            | Description                                |
| ----------------- | ------------------ | ------------------------------------------ |
| `-ServiceName`    | `OpenClawGateway`  | Windows service name                       |
| `-NssmPath`       | `~/.openclaw/nssm` | Where to store NSSM                        |
| `-RestartDelayMs` | `15000`            | Delay before NSSM restarts crashed service |

### Supervisor Script Options

The supervisor script accepts these parameters (modify the service if needed):

| Parameter              | Default | Description                                 |
| ---------------------- | ------- | ------------------------------------------- |
| `-HealthCheckInterval` | `30`    | Seconds between health checks               |
| `-UnhealthyThreshold`  | `60`    | Seconds of unhealthy before restart         |
| `-GatewayPort`         | `22140` | Port to check for health                    |
| `-GracePeriod`         | `30`    | Seconds to wait after start before checking |

To modify parameters after install:

```powershell
# Edit the service arguments
~\.openclaw\nssm\nssm.exe edit OpenClawGateway
```

## Log Files

All logs are stored in `~/.openclaw/logs/`:

| File                 | Contents                                    |
| -------------------- | ------------------------------------------- |
| `supervisor.log`     | Supervisor actions, health checks, restarts |
| `service-stdout.log` | NSSM captured stdout                        |
| `service-stderr.log` | NSSM captured stderr                        |
| `gateway-stdout.log` | Gateway process stdout                      |
| `gateway-stderr.log` | Gateway process stderr                      |

### View Logs

```powershell
# Tail the supervisor log
Get-Content ~/.openclaw/logs/supervisor.log -Tail 50 -Wait

# View all logs
Get-ChildItem ~/.openclaw/logs/*.log | ForEach-Object { Write-Host "`n=== $_ ==="; Get-Content $_ -Tail 20 }
```

### Log Rotation

The supervisor automatically rotates `supervisor.log` when it exceeds 10MB, keeping the last 5 rotated logs.

## Managing the Service

### Common Commands

```powershell
# Check status
Get-Service OpenClawGateway

# Start/Stop/Restart
Start-Service OpenClawGateway
Stop-Service OpenClawGateway
Restart-Service OpenClawGateway

# View detailed status
sc.exe query OpenClawGateway
```

### Using NSSM Directly

```powershell
$nssm = "$env:USERPROFILE\.openclaw\nssm\nssm.exe"

# Edit service configuration (GUI)
& $nssm edit OpenClawGateway

# View all service parameters
& $nssm dump OpenClawGateway

# Rotate logs now
& $nssm rotate OpenClawGateway
```

## Troubleshooting

### Service Won't Start

1. **Check if OpenClaw is installed:**

   ```powershell
   openclaw --version
   ```

2. **Check logs:**

   ```powershell
   Get-Content ~/.openclaw/logs/service-stderr.log -Tail 50
   ```

3. **Try running supervisor manually:**
   ```powershell
   .\supervisor.ps1
   ```

### Gateway Keeps Restarting

1. **Check gateway logs:**

   ```powershell
   Get-Content ~/.openclaw/logs/gateway-stderr.log -Tail 100
   ```

2. **Check if port is in use:**

   ```powershell
   netstat -ano | findstr :22140
   ```

3. **Try running gateway directly:**
   ```powershell
   openclaw gateway start --foreground
   ```

### Health Checks Failing

1. **Verify endpoint is accessible:**

   ```powershell
   Invoke-WebRequest http://127.0.0.1:22140/health/live
   ```

2. **Check if gateway is listening on correct port:**
   ```powershell
   openclaw gateway status
   ```

### NSSM Download Failed

If automatic download fails:

1. Download manually from https://nssm.cc/download
2. Extract to `~/.openclaw/nssm/nssm.exe`
3. Run install script again

### Removing Stuck Service

If normal uninstall fails:

```powershell
# Force stop
taskkill /F /IM powershell.exe /FI "WINDOWTITLE eq *supervisor*"

# Delete service directly
sc.exe delete OpenClawGateway

# Reboot if still stuck
```

## Integration with Internal Watchdog

The gateway has its own internal watchdog that handles most recovery scenarios. This external supervisor is designed to be **non-intrusive**:

- **60-second threshold**: Gives the internal watchdog time to recover
- **15-second restart delay**: Respects internal restart protocols
- **30-second grace period**: Allows full initialization before monitoring

The supervisor only intervenes when the gateway is **truly stuck**—not responding to health checks for a full minute despite internal recovery attempts.

## Security Notes

- The service runs as `Local System` by default
- NSSM is downloaded from the official source (nssm.cc) over HTTPS
- No credentials are stored; uses existing OpenClaw configuration
- Logs may contain operational data; restrict access to `~/.openclaw/logs` if needed

## Comparison with Linux systemd

| Feature             | Windows (NSSM)      | Linux (systemd) |
| ------------------- | ------------------- | --------------- |
| Process supervision | ✅                  | ✅              |
| Restart on crash    | ✅                  | ✅              |
| Boot start          | ✅                  | ✅              |
| Health checks       | ✅ (supervisor.ps1) | ✅ (sd_notify)  |
| Log rotation        | ✅                  | ✅ (journald)   |
| Resource limits     | Via NSSM            | Via cgroups     |

## Files

```
scripts/windows-supervisor/
├── install.ps1      # Service installation
├── uninstall.ps1    # Service removal
├── supervisor.ps1   # Health-monitoring supervisor
└── README.md        # This file
```
