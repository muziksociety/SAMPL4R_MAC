<#
.SYNOPSIS
  Run Tracktion pluginval against a SAMPLAR VST3 bundle and keep the log.

.DESCRIPTION
  Uses tools/pluginval/pluginval.exe (git-ignored binary; fetched by this script from
  github.com/Tracktion/pluginval releases if missing). Default strictness 10.
  -Snapshot copies the bundle to a temp folder first, so the build tree's DLL is never
  locked while pluginval runs (a concurrent link into that build dir keeps working).

  Exit code: pluginval's (0 = all tests passed), 2 = setup error, 3 = wall-clock timeout.

.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_pluginval.ps1 `
      -Plugin build/SamplarPlugin_artefacts/Release/VST3/SAMPLAR.vst3 -Snapshot
#>
param(
    [string]$Plugin = "",
    [int]$Strictness = 10,
    [int]$TimeoutMs = 120000,          # pluginval: max silence between test outputs
    [int]$WallTimeoutMin = 45,         # this script: hard stop for the whole run
    [string]$OutDir = "",
    [switch]$Snapshot,
    [switch]$SkipGui,
    [string]$Version = "v1.0.4"
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$PvDir = Join-Path $PSScriptRoot 'pluginval'
$PvExe = Join-Path $PvDir 'pluginval.exe'

if (-not $Plugin) { $Plugin = Join-Path $RepoRoot 'build/SamplarPlugin_artefacts/Release/VST3/SAMPLAR.vst3' }
if (-not (Test-Path -LiteralPath $Plugin)) { Write-Host "[pluginval] plugin not found: $Plugin"; exit 2 }
$Plugin = (Resolve-Path -LiteralPath $Plugin).Path

if (-not $OutDir) { $OutDir = Join-Path $PvDir 'logs' }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# --- pluginval binary ---------------------------------------------------------------
if (-not (Test-Path -LiteralPath $PvExe)) {
    Write-Host "[pluginval] downloading pluginval $Version ..."
    New-Item -ItemType Directory -Force -Path $PvDir | Out-Null
    $zip = Join-Path $PvDir 'pluginval_Windows.zip'
    $ProgressPreference = 'SilentlyContinue'
    try {
        Invoke-WebRequest -Uri "https://github.com/Tracktion/pluginval/releases/download/$Version/pluginval_Windows.zip" -OutFile $zip -TimeoutSec 300
        Expand-Archive -Force -LiteralPath $zip -DestinationPath $PvDir
    } catch {
        Write-Host "[pluginval] download failed: $($_.Exception.Message)"
        Write-Host "[pluginval] fallback: gh release download $Version --repo Tracktion/pluginval -p pluginval_Windows.zip -D tools/pluginval"
        exit 2
    }
    if (-not (Test-Path -LiteralPath $PvExe)) { Write-Host "[pluginval] pluginval.exe missing after extract"; exit 2 }
}

# --- optional snapshot of the bundle --------------------------------------------------
$target = $Plugin
$snapDir = $null
if ($Snapshot) {
    $snapDir = Join-Path ([System.IO.Path]::GetTempPath()) ("samplar_pluginval_" + [guid]::NewGuid().ToString('N').Substring(0, 8))
    New-Item -ItemType Directory -Force -Path $snapDir | Out-Null
    Write-Host "[pluginval] snapshot -> $snapDir"
    # robocopy: skip the debug map / pdb / dev logs (not part of the shipped bundle)
    & robocopy $Plugin (Join-Path $snapDir (Split-Path -Leaf $Plugin)) /E /NFL /NDL /NJH /NJS /NP /XF *.map *.pdb /XD logs | Out-Null
    if ($LASTEXITCODE -ge 8) { Write-Host "[pluginval] snapshot copy failed (robocopy $LASTEXITCODE)"; exit 2 }
    $target = Join-Path $snapDir (Split-Path -Leaf $Plugin)
}

$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$logName = "pluginval_SAMPLAR_s$Strictness`_$stamp.txt"
$logPath = Join-Path $OutDir $logName

$pvArgs = @('--strictness-level', "$Strictness", '--timeout-ms', "$TimeoutMs",
            '--output-dir', "`"$OutDir`"", '--output-filename', "pluginval_raw_$stamp.txt", '--verbose')
if ($SkipGui) { $pvArgs += '--skip-gui-tests' }
$pvArgs += @('--validate', "`"$target`"")

Write-Host "[pluginval] $PvExe $($pvArgs -join ' ')"
Write-Host "[pluginval] console log: $logPath"

$stdoutFile = Join-Path $OutDir "pluginval_stdout_$stamp.tmp"
$stderrFile = Join-Path $OutDir "pluginval_stderr_$stamp.tmp"
$t0 = Get-Date
$proc = Start-Process -FilePath $PvExe -ArgumentList ($pvArgs -join ' ') -NoNewWindow -PassThru `
                      -RedirectStandardOutput $stdoutFile -RedirectStandardError $stderrFile
$null = $proc.Handle   # cache the handle now, or ExitCode reads back empty later
$timedOut = $false
try { Wait-Process -Id $proc.Id -Timeout ($WallTimeoutMin * 60) -ErrorAction Stop } catch { $timedOut = -not $proc.HasExited }
if ($timedOut) {
    Write-Host "[pluginval] WALL TIMEOUT after $WallTimeoutMin min - killing"
    try { $proc.Kill() } catch {}
    Start-Sleep -Seconds 2
}
$proc.WaitForExit()
$elapsed = [int]((Get-Date) - $t0).TotalSeconds
$text = ''
if (Test-Path -LiteralPath $stdoutFile) { $text += (Get-Content -LiteralPath $stdoutFile -Raw) }
if (Test-Path -LiteralPath $stderrFile) { $err = Get-Content -LiteralPath $stderrFile -Raw; if ($err) { $text += "`n--- stderr ---`n" + $err } }
Set-Content -LiteralPath $logPath -Value $text -Encoding utf8
Remove-Item -Force -LiteralPath $stdoutFile, $stderrFile -ErrorAction SilentlyContinue

$exit = if ($timedOut) { 3 } else { $proc.ExitCode }

# --- summary: every "!!! Test N failed" with its test name ---------------------------
$lines = $text -split "`r?`n"
$current = ''
$fails = New-Object System.Collections.Generic.List[string]
foreach ($l in $lines) {
    if ($l -match '^Starting tests? in: (.+)$' -or $l -match '^Starting test: (.+)$') { $current = $Matches[1].Trim() }
    if ($l -match '!!! Test \d+ failed: (.+)$') { $fails.Add("[$current] $($Matches[1].Trim())") }
}
Write-Host ""
Write-Host "[pluginval] exit=$exit  elapsed=${elapsed}s  failures=$($fails.Count)"
foreach ($f in ($fails | Select-Object -Unique)) { Write-Host "  FAIL $f" }
if ($lines | Where-Object { $_ -match 'SUCCESS' }) { Write-Host "[pluginval] SUCCESS line present" }
Write-Host "[pluginval] log: $logPath"

if ($snapDir) { try { Remove-Item -Recurse -Force -LiteralPath $snapDir } catch { Write-Host "[pluginval] (snapshot not removed: $snapDir)" } }
exit $exit
