<#
.SYNOPSIS
  SAMPLAR release gate: build -> SamplarTests -> VST3SmokeHost -> pluginval -> release linter.

.DESCRIPTION
  Runs every automated check against ONE build directory and prints a summary table.
  Exit code 0 only if every step passed.

    1. build        cmake --build <BuildDir> --target SamplarPlugin_VST3 VST3SmokeHost SamplarTests
    2. unit tests   SamplarTests.exe (juce::UnitTest runner, tests/)
    3. smoke        VST3SmokeHost: single run, --two-instances x3, --ui-sweep
    4. pluginval    tools/run_pluginval.ps1 (Tracktion pluginval, strictness 10)
    5. linter       python tools/release_linter/run_all.py --bundle <BuildDir bundle>

  The build dir must be configured with tests:
    cmake -B build_qa -G "Visual Studio 17 2022" -A x64 -DSAMPLAR_BUILD_TESTS=ON
  Logs go to <BuildDir>/qa_logs/<timestamp>/.

.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_all_checks.ps1 -BuildDir build_qa
#>
param(
    [string]$BuildDir = "build_qa",
    [string]$Config = "Release",
    [switch]$SkipBuild,
    [switch]$SkipPluginval,
    [int]$Strictness = 10,
    [int]$TwoInstanceRuns = 3
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $RepoRoot $BuildDir }
if (-not (Test-Path -LiteralPath $BuildDir)) { Write-Host "[checks] build dir not found: $BuildDir (configure it first, see -? )"; exit 2 }
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path

$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$LogDir = Join-Path $BuildDir "qa_logs\$stamp"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$Bundle     = Join-Path $BuildDir "SamplarPlugin_artefacts\$Config\VST3\SAMPLAR.vst3"
$TestsExe   = Join-Path $BuildDir "SamplarTests_artefacts\$Config\SamplarTests.exe"
$SmokeExe   = Join-Path $BuildDir "VST3SmokeHost_artefacts\$Config\VST3SmokeHost.exe"
$results = New-Object System.Collections.Generic.List[object]

function Add-Result([string]$step, [bool]$ok, [double]$sec, [string]$detail) {
    $results.Add([pscustomobject]@{ Step = $step; Result = $(if ($ok) { 'PASS' } else { 'FAIL' }); Seconds = [int]$sec; Detail = $detail })
    $tag = if ($ok) { 'PASS' } else { 'FAIL' }
    Write-Host ("[checks] {0,-28} {1}  ({2}s) {3}" -f $step, $tag, [int]$sec, $detail)
}

# Runs an exe with a hard timeout; stdout+stderr -> $log. Returns the exit code (124 = timeout).
function Invoke-Logged([string]$exe, [string[]]$exeArgs, [string]$log, [int]$timeoutSec) {
    $out = "$log.out"; $err = "$log.err"
    $quoted = $exeArgs | ForEach-Object { if ($_ -match '\s') { "`"$_`"" } else { $_ } }
    $sp = @{ FilePath = $exe; NoNewWindow = $true; PassThru = $true; RedirectStandardOutput = $out
             RedirectStandardError = $err; WorkingDirectory = $RepoRoot }
    if ($quoted) { $sp.ArgumentList = ($quoted -join ' ') }   # Start-Process rejects an empty list
    $p = Start-Process @sp
    $null = $p.Handle
    $timedOut = $false
    try { Wait-Process -Id $p.Id -Timeout $timeoutSec -ErrorAction Stop } catch { $timedOut = -not $p.HasExited }
    if ($timedOut) { try { $p.Kill() } catch {}; Start-Sleep -Seconds 1 }
    $p.WaitForExit()
    $text = ''
    if (Test-Path $out) { $text += Get-Content -LiteralPath $out -Raw }
    if (Test-Path $err) { $e = Get-Content -LiteralPath $err -Raw; if ($e) { $text += "`n--- stderr ---`n$e" } }
    Set-Content -LiteralPath $log -Value $text -Encoding utf8
    Remove-Item -Force -LiteralPath $out, $err -ErrorAction SilentlyContinue
    if ($timedOut) { Add-Content -LiteralPath $log -Value "`n[checks] TIMEOUT after $timeoutSec s"; return 124 }
    return $p.ExitCode
}

function Tail([string]$log, [int]$n = 3) {
    if (-not (Test-Path $log)) { return '' }
    ((Get-Content -LiteralPath $log | Where-Object { $_.Trim() } | Select-Object -Last $n) -join ' | ')
}

# ---- 1. build ------------------------------------------------------------------------
if (-not $SkipBuild) {
    $t0 = Get-Date
    $log = Join-Path $LogDir '1_build.log'
    $targets = @('SamplarPlugin_VST3', 'VST3SmokeHost')
    if (Test-Path (Join-Path $BuildDir 'SamplarTests.vcxproj')) { $targets += 'SamplarTests' }
    else { Write-Host "[checks] WARNING: $BuildDir was not configured with -DSAMPLAR_BUILD_TESTS=ON" }
    $cmakeArgs = @('--build', $BuildDir, '--config', $Config, '--parallel', '--target') + $targets
    $code = Invoke-Logged 'cmake' $cmakeArgs $log 5400
    $errs = @(Select-String -LiteralPath $log -Pattern ': (fatal )?error [A-Z]+\d+' -ErrorAction SilentlyContinue)
    Add-Result 'build' ($code -eq 0) ((Get-Date) - $t0).TotalSeconds ($(if ($code -eq 0) { $targets -join ', ' } else { "exit $code, $($errs.Count) error line(s), see $log" }))
    if ($code -ne 0) { Write-Host "[checks] build failed - later steps use whatever binaries exist" }
}

# ---- 2. unit tests -------------------------------------------------------------------
$t0 = Get-Date
$log = Join-Path $LogDir '2_SamplarTests.log'
if (Test-Path -LiteralPath $TestsExe) {
    $code = Invoke-Logged $TestsExe @() $log 900
    $sum = (Select-String -LiteralPath $log -Pattern 'checks passed' | Select-Object -Last 1).Line
    Add-Result 'unit tests (SamplarTests)' ($code -eq 0) ((Get-Date) - $t0).TotalSeconds ("exit $code; " + ($sum -replace '^\s+', ''))
} else {
    Add-Result 'unit tests (SamplarTests)' $false 0 "not built: $TestsExe"
}

# ---- 3. smoke host -------------------------------------------------------------------
if ((Test-Path -LiteralPath $SmokeExe) -and (Test-Path -LiteralPath $Bundle)) {
    $t0 = Get-Date
    $log = Join-Path $LogDir '3a_smoke_single.log'
    $code = Invoke-Logged $SmokeExe @('--plugin', $Bundle, '--show-ms', '2500') $log 300
    Add-Result 'smoke: single instance' ($code -eq 0) ((Get-Date) - $t0).TotalSeconds ("exit $code; " + (Tail $log 1))

    $fails = 0
    $t0 = Get-Date
    for ($i = 1; $i -le $TwoInstanceRuns; $i++) {
        $log = Join-Path $LogDir "3b_smoke_two_instances_$i.log"
        $code = Invoke-Logged $SmokeExe @('--plugin', $Bundle, '--two-instances') $log 300
        if ($code -ne 0) { $fails++ }
    }
    Add-Result "smoke: --two-instances x$TwoInstanceRuns" ($fails -eq 0) ((Get-Date) - $t0).TotalSeconds "$fails/$TwoInstanceRuns run(s) failed"

    $t0 = Get-Date
    $sweepDir = Join-Path $LogDir 'ui_sweep'
    New-Item -ItemType Directory -Force -Path $sweepDir | Out-Null
    $log = Join-Path $LogDir '3c_smoke_ui_sweep.log'
    $code = Invoke-Logged $SmokeExe @('--plugin', $Bundle, '--ui-sweep', $sweepDir) $log 600
    Add-Result 'smoke: --ui-sweep' ($code -eq 0) ((Get-Date) - $t0).TotalSeconds ("exit $code; " + (Tail $log 1))
} else {
    Add-Result 'smoke (VST3SmokeHost)' $false 0 "missing: $(if (-not (Test-Path $SmokeExe)) { $SmokeExe } else { $Bundle })"
}

# ---- 4. pluginval --------------------------------------------------------------------
if (-not $SkipPluginval) {
    $t0 = Get-Date
    $log = Join-Path $LogDir '4_pluginval.log'
    $pvArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'run_pluginval.ps1'),
                '-Plugin', $Bundle, '-Strictness', "$Strictness", '-OutDir', (Join-Path $LogDir 'pluginval'))
    $code = Invoke-Logged 'powershell' $pvArgs $log 3600
    $fl = (Select-String -LiteralPath $log -Pattern 'failures=' | Select-Object -Last 1).Line
    Add-Result "pluginval (strictness $Strictness)" ($code -eq 0) ((Get-Date) - $t0).TotalSeconds ("exit $code; " + ($fl -replace '^\[pluginval\]\s*', ''))
}

# ---- 5. release linter ---------------------------------------------------------------
$t0 = Get-Date
$log = Join-Path $LogDir '5_release_linter.log'
$py = if (Get-Command python -ErrorAction SilentlyContinue) { 'python' } else { 'py' }
$code = Invoke-Logged $py @((Join-Path $RepoRoot 'tools\release_linter\run_all.py'), '--bundle', $Bundle,
                            '--out-dir', (Join-Path $LogDir 'release_linter')) $log 600
$ov = (Select-String -LiteralPath $log -Pattern '^OVERALL' | Select-Object -Last 1).Line
Add-Result 'release linter' ($code -eq 0) ((Get-Date) - $t0).TotalSeconds ("exit $code; $ov")

# ---- summary -------------------------------------------------------------------------
Write-Host ""
Write-Host "==================== SAMPLAR release gate ===================="
$results | Format-Table -AutoSize Step, Result, Seconds, Detail | Out-String -Width 220 | Write-Host
$failed = @($results | Where-Object { $_.Result -ne 'PASS' }).Count
Write-Host ("{0} step(s), {1} failed. Logs: {2}" -f $results.Count, $failed, $LogDir)
Write-Host "=============================================================="
if ($failed -gt 0) { exit 1 }
exit 0
