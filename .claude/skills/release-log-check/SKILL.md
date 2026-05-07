---
name: release-log-check
description: Check SAMPLAR release/installed version logs for errors, separation status, crashes, or runtime issues. Use when the user says "check logs", "check release logs", "what's in the logs", "is separation working", "verify the installed version", "check if SAMPLAR is running correctly", or reports any runtime error from the installed (non-dev) build. Always look at the installed version's log, not the dev build log.
---

# Release Log Check

The **installed** (release) version of SAMPLAR logs to:

```
C:\Users\Muzik\AppData\Local\Programs\SAMPLAR\logs\SAMPLAR_console.log
```

**This is the ONLY log location for the release build.** Do NOT check:
- `build/SM5App_artefacts/Release/logs/` (dev build only)
- `%APPDATA%\Samplar\logs/` (legacy/VST3 path, may be stale)

## Quick commands

```bash
# Tail the live log
tail -50 "/c/Users/Muzik/AppData/Local/Programs/SAMPLAR/logs/SAMPLAR_console.log"

# Search for errors
grep -E "FAILED|ERROR|error|crash" "/c/Users/Muzik/AppData/Local/Programs/SAMPLAR/logs/SAMPLAR_console.log" | tail -20

# Check separation status
grep -E "FullSong|UVR|separator|separation" "/c/Users/Muzik/AppData/Local/Programs/SAMPLAR/logs/SAMPLAR_console.log" | tail -20

# Check what exe is actually running
wmic process where "name='SAMPLAR.exe'" get ExecutablePath

# Verify it's the release build (NOT dev, NOT ToneAndBeats)
# Expected: C:\Users\Muzik\AppData\Local\Programs\SAMPLAR\SAMPLAR.exe
```

## Common log patterns

| Pattern | Meaning |
|---|---|
| `[OK] [FullSongService]` | Separation step succeeded |
| `[FAILED] [FullSongService]` | Separation step failed — read the message |
| `[UVR_CLI] ... ERROR` | audio-separator Python error |
| `[UVR_CLI] ... Format not recognised` | Invalid input file (corrupted/wrong format) |
| `[UVR_CLI] 100%\|##########\|` | Separation pass completed (one per stem) |
| `[OAuth] URL:` | OAuth login URL (check for `app_version=` param) |
| `[App] shutdown()` | App closed normally |
| `[LicenseSystem]` | License validation events |

## First thing to check on any runtime bug report

1. `wmic process where "name='SAMPLAR.exe'" get ExecutablePath` — verify it's the release build, not dev or ToneAndBeats ghost
2. `tail -50` the log — see what happened last
3. `grep FAILED` — find any error messages
