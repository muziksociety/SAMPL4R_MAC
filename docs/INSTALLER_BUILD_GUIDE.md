# SAMPLAR - Installer Build Guide

This guide explains how to create a professional Windows installer for SAMPLAR with an embedded icon and automatic VC++ redistributable installation.

## Quick Start

### One-Command Build (Recommended)

```powershell
.\build_installer_complete.ps1
```

This single script will:
1. ✅ Create the application icon from `plugins/REF.png`
2. ✅ Rebuild the application with the icon embedded
3. ✅ Create the portable version
4. ✅ Download VC++ redistributable (if needed)
5. ✅ Build the installer with Inno Setup

**Output:** `installer_output\Samplar_Setup_v1.0.0.exe`

---

## Prerequisites

### Required Software

1. **Inno Setup 6** (for creating the installer)
   ```powershell
   winget install JRSoftware.InnoSetup
   ```
   Or download from: https://jrsoftware.org/isdl.php

2. **Visual Studio 2022** (already installed for building the app)

### Optional Software

3. **ImageMagick** (for high-quality multi-resolution icons)
   ```powershell
   winget install ImageMagick.ImageMagick
   ```
   
   Without ImageMagick, the script will create a basic icon using .NET (single resolution).

---

## Step-by-Step Manual Build

If you prefer to run each step manually:

### Step 1: Create the Icon

```powershell
.\create_icon.ps1
```

This creates `Samplar.ico` from `plugins/REF.png`.

**With ImageMagick:** Multi-resolution icon (256, 128, 64, 48, 32, 16)  
**Without ImageMagick:** Single resolution icon (256x256)

### Step 2: Build the Application

```powershell
.\make_portable.ps1
```

This rebuilds the app with the icon embedded and creates the portable version.

### Step 3: Download VC++ Redistributable

```powershell
.\download_vcredist.ps1
```

Or download manually:
- URL: https://aka.ms/vs/17/release/vc_redist.x64.exe
- Save to: `installer_dependencies\vc_redist.x64.exe`

### Step 4: Build the Installer

```powershell
.\build_installer.ps1
```

Or manually:
```powershell
& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer.iss
```

---

## Installer Features

### What's Included

- ✅ **Application Icon**: Embedded in the .exe and shown in shortcuts
- ✅ **VC++ Redistributable**: Automatically installed during setup
- ✅ **Desktop Shortcut**: Created by default (user can uncheck)
- ✅ **Start Menu Shortcuts**: App launcher + Uninstaller
- ✅ **All Dependencies**: OpenBLAS DLL, AI models, game assets
- ✅ **Professional Uninstaller**: Clean removal with optional data deletion

### Installation Process

1. User runs `Samplar_Setup_v1.0.0.exe`
2. Installer checks Windows version (requires Windows 10+)
3. User selects installation directory (default: `C:\Program Files\SAMPLAR`)
4. User chooses whether to create desktop shortcut (checked by default)
5. Installer copies all files (~100 MB)
6. **VC++ Redistributable is installed silently** (if not already installed)
7. User can optionally launch the app immediately

### What Gets Installed

```
C:\Program Files\SAMPLAR\
├── SAMPLAR.exe          (Main application with icon)
├── libopenblas.dll              (Math library)
├── models\                      (AI models for vocal separation)
│   └── ggml-model-htdemucs-4s-f16.bin
├── python_bridge\               (Python integration)
├── plugins\                     (Logo and branding)
│   └── REF.png
├── assets\                      (Game assets)
│   ├── flappy\
│   ├── gamesnake\
│   └── shooter\
├── logs\                        (Created on first run)
└── README.txt
```

---

## Icon Details

### Source Image
- **File**: `plugins/REF.png`
- **Description**: SAMPLAR character (reddish-orange body, green spiky hair)

### Generated Icon
- **File**: `Samplar.ico`
- **Resolutions** (with ImageMagick):
  - 256x256 (Windows 10/11 taskbar, Start Menu)
  - 128x128 (Large icons view)
  - 64x64 (Medium icons view)
  - 48x48 (Classic view)
  - 32x32 (Small icons view)
  - 16x16 (System tray, file explorer)

### Where the Icon Appears
- ✅ Application window title bar
- ✅ Taskbar when app is running
- ✅ Desktop shortcut
- ✅ Start Menu shortcut
- ✅ Installer icon
- ✅ Uninstaller icon
- ✅ File explorer (when viewing the .exe)

---

## Customization

### Change Icon

1. Replace `plugins/REF.png` with your new image (PNG format, square, 512x512+ recommended)
2. Run `.\create_icon.ps1` to regenerate the icon
3. Rebuild: `.\build_installer_complete.ps1`

### Change Installer Settings

Edit `installer.iss`:

```ini
#define MyAppName "SAMPLAR"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "Your Company Name"
#define MyAppURL "https://yourwebsite.com"
```

### Add/Remove Files

Edit the `[Files]` section in `installer.iss`:

```ini
[Files]
Source: "Samplar_Portable\YourFile.dll"; DestDir: "{app}"; Flags: ignoreversion
```

---

## Testing the Installer

### Test on Clean Machine

1. **Virtual Machine** (recommended):
   - Create a fresh Windows 10/11 VM
   - Install the app using the installer
   - Verify icon appears correctly
   - Check desktop shortcut works
   - Confirm VC++ redist installed silently

2. **Physical Machine**:
   - Test on a computer without Visual Studio installed
   - Verify the app runs without errors

### What to Check

- [ ] Icon appears in installer wizard
- [ ] Desktop shortcut has the correct icon
- [ ] Start Menu shortcut has the correct icon
- [ ] App launches successfully
- [ ] VC++ redistributable installs without user interaction
- [ ] Uninstaller works correctly
- [ ] No missing DLL errors

---

## Troubleshooting

### Icon Not Showing

**Problem**: Icon doesn't appear in the built executable  
**Solution**: 
1. Verify `Samplar.ico` exists in the project root
2. Rebuild the project: `.\make_portable.ps1`
3. Check CMakeLists.txt has `ICON_BIG` and `ICON_SMALL` set

### Installer Build Fails

**Problem**: Inno Setup compilation error  
**Solution**:
1. Check Inno Setup is installed: `C:\Program Files (x86)\Inno Setup 6\ISCC.exe`
2. Verify all source files exist (check `[Files]` section in `installer.iss`)
3. Ensure `Samplar_Portable` folder exists and is complete

### VC++ Redistributable Missing

**Problem**: `installer_dependencies\vc_redist.x64.exe` not found  
**Solution**:
```powershell
.\download_vcredist.ps1
```
Or download manually from: https://aka.ms/vs/17/release/vc_redist.x64.exe

### ImageMagick Not Found

**Problem**: Icon creation uses .NET fallback (lower quality)  
**Solution**:
```powershell
winget install ImageMagick.ImageMagick
```
Then re-run: `.\create_icon.ps1`

---

## Advanced Options

### Build Without Rebuilding

If you've already built the app and just want to create the installer:

```powershell
.\build_installer_complete.ps1 -SkipBuild
```

### Build Without Recreating Icon

If the icon already exists:

```powershell
.\build_installer_complete.ps1 -SkipIcon
```

### Debug Build Installer

To create an installer with debug binaries:

```powershell
.\build_installer_complete.ps1 -Config Debug
```

---

## Distribution

### Installer Size

Typical installer size: **~100 MB**

Breakdown:
- Application: ~15 MB
- OpenBLAS DLL: ~5 MB
- AI Models: ~80 MB
- VC++ Redistributable: ~25 MB (embedded)
- Assets: ~5 MB

### Sharing the Installer

1. **Upload to website**: Host `Samplar_Setup_v1.0.0.exe`
2. **Cloud storage**: Google Drive, Dropbox, OneDrive
3. **GitHub Releases**: Attach to a release tag
4. **Direct download**: Provide direct link to users

### Code Signing (Optional)

For production releases, consider code signing:

1. Obtain a code signing certificate
2. Edit `installer.iss`:
   ```ini
   SignTool=signtool /f "YourCertificate.pfx" /p "YourPassword" /t http://timestamp.digicert.com /d $q{#MyAppName}$q $f
   SignedUninstaller=yes
   ```

---

## Summary

### Quick Commands

```powershell
# Complete build (recommended)
.\build_installer_complete.ps1

# Manual steps
.\create_icon.ps1
.\make_portable.ps1
.\download_vcredist.ps1
.\build_installer.ps1
```

### Output Files

- **Icon**: `Samplar.ico`
- **Portable**: `Samplar_Portable\`
- **Installer**: `installer_output\Samplar_Setup_v1.0.0.exe`

### Key Features

✅ Professional Windows installer  
✅ Embedded application icon  
✅ Automatic VC++ redistributable installation  
✅ Desktop shortcut with icon  
✅ Start Menu integration  
✅ Clean uninstaller  
✅ All dependencies included  

---

## Support

For issues or questions:
1. Check the troubleshooting section above
2. Review the build logs
3. Verify all prerequisites are installed
4. Test on a clean Windows installation

**Happy building! 🚀**

