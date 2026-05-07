# Remote Debugging Setup for SAMPLAR

## Quick Setup (5 minutes)

### On the CRASHING Computer:

1. **Install Visual Studio Remote Tools** (free, no VS needed):
   - Download: https://aka.ms/vs/17/release/RemoteTools.amd64ret.enu.exe
   - Run installer
   - Launch "Remote Debugger" from Start Menu
   - Note the computer name shown (e.g., "DESKTOP-ABC123")

2. **Copy Debug Build** from dev computer:
   - Copy `build\SM5App_artefacts\Debug\SAMPLAR.exe`
   - Copy `build\SM5App_artefacts\Debug\SAMPLAR.pdb` (debug symbols)
   - Copy all DLLs from Debug folder

### On YOUR Computer (Dev Machine):

1. **Open Visual Studio 2022**
2. **Debug → Attach to Process**
3. **Connection Type**: "Remote (no authentication)"
4. **Connection Target**: Enter the computer name from step 1
5. **Find** "SAMPLAR.exe" in the process list
6. **Attach**

### Result:
When the app crashes, Visual Studio will show you the **exact line of code** that's crashing!

---

## Alternative: Crash Dump Analysis

If remote debugging isn't possible, we can analyze crash dumps:

### On Crashing Computer:

1. Enable crash dumps:
   ```powershell
   # Run as Administrator
   New-Item -Path "HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps" -Force
   New-ItemProperty -Path "HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps" -Name "DumpFolder" -Value "C:\CrashDumps" -PropertyType ExpandString -Force
   New-ItemProperty -Path "HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps" -Name "DumpType" -Value 2 -PropertyType DWord -Force
   New-Item -ItemType Directory -Path "C:\CrashDumps" -Force
   ```

2. Run the app until it crashes
3. Copy `C:\CrashDumps\SAMPLAR.exe.*.dmp` to dev computer
4. Open in Visual Studio: File → Open → File → select .dmp file
5. Click "Debug with Native Only"

This will show the exact crash location!

