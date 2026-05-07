# Pristine VM Test — The Ultimate Runtime Gate

Static linters catch what's *knowable from source and binary inspection*. They cannot catch:

- **Windows Defender SmartScreen blocking** the unsigned installer
- **Missing VC++ Redistributable install failure** (installer ships it, but the install might fail silently)
- **`%APPDATA%\Samplar\` directory creation** on first launch
- **License server connectivity** from a new machine (firewall, DNS, TLS)
- **Python embedded environment bootstrap** — `python.exe` launching under a constrained user account
- **audio-separator first-run model download** — ~500 MB pull over the user's network
- **First-play audio device acquisition** on unfamiliar hardware
- **High-DPI scaling, multi-monitor, dark mode** rendering bugs
- **SAMPLAR_Portable path resolution** when `{app}` contains spaces or non-ASCII characters

The only way to verify these is to run the installer on a **machine that has never had SAMPLAR or its dependencies installed**.

## Setup (one-time)

1. **Create a Windows 10 Pro VM** in Hyper-V, VMware, or VirtualBox
   - Minimum: 4 CPU, 8 GB RAM, 80 GB disk, audio device passthrough
   - Use a stock Windows ISO — don't mount a prepared SAMPLAR dev image
2. **Install from ISO and activate** (trial mode is fine for testing)
3. **Windows Update once**, then **disable automatic updates** so the test state stays reproducible
4. **Take a baseline snapshot** named `fresh-win10-pro`. This is your reset point.

## Test protocol (every release)

```
1. Revert VM to `fresh-win10-pro` snapshot
2. Copy the release installer (.exe) into the VM
3. Run the installer as a standard user (NOT as admin). If the installer
   needs elevation, it should prompt via UAC; click through normally.
4. Launch SAMPLAR.exe from the Start Menu shortcut
5. Check each of the acceptance criteria below
6. If all pass, take a snapshot named `samplar-v<version>-installed`
7. If any fail, create a GitHub issue with logs from %APPDATA%\Samplar\logs\
```

## Acceptance criteria

| # | Test | Expected |
|---|---|---|
| 1 | Installer runs without errors | Success dialog appears |
| 2 | SmartScreen warning acknowledged | User can click "Run anyway" |
| 3 | `vc_redist.x64.exe` step | Completes silently or prompts once |
| 4 | Launch exe from Start Menu | Main window appears within 10s |
| 5 | `%APPDATA%\Samplar\` | Created automatically |
| 6 | License activation dialog | Appears and can reach the license server |
| 7 | Load a drum sample | Waveform renders, preview plays |
| 8 | Generate a variation | All 8 engines selectable, variation generates, preview plays |
| 9 | Full song separation (UVR path) | audio-separator launches, drums stem returns |
| 10 | First-run model download | Progress visible, completes (takes minutes) |
| 11 | Close and relaunch | State persists (no crash, settings remembered) |
| 12 | Audio device switch | Preview works through system default → other device |

## Common failure modes and fixes

| Symptom | Likely cause | Fix |
|---|---|---|
| "The code execution cannot proceed because X.dll was not found" | Check #6 missed a DLL | Add DLL to installer, rebuild, re-run Check #6 |
| SmartScreen blocks install | Installer unsigned | Sign installer with Authenticode cert (EV for fewer warnings) |
| "VCRUNTIME140.dll not found" | VC++ redist not installed | Installer must run `vc_redist.x64.exe /install /quiet /norestart` |
| App crashes on first launch | `%APPDATA%` dir not created | Fix `DataPaths::getAppDataDirectory()` to `createDirectory()` |
| "Could not reach license server" | VM network / firewall | Confirm HTTPS outbound allowed; test license endpoint with curl |
| UVR separation hangs forever | audio-separator Python env broken | Check VM Python PATH; verify `python\Scripts\audio-separator.exe` exists |
| Audio plays garbled | Wrong sample rate for VM device | AudioDeviceService should negotiate; check `initialise()` logs |

## VM recycling

After each test, **always revert to the `fresh-win10-pro` snapshot** before the next run. Residual state from a previous test can hide a new bug.

Suggested cadence:
- **Every release candidate** — full run of all 12 criteria
- **Every major refactor** — at least criteria 1, 4, 9 (the flagship paths)
- **Monthly sanity check** — random spot checks on latest dev build
