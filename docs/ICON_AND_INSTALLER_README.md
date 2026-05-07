# SAMPLAR - Icon & Installer Setup

## Overview

This document explains the complete setup for creating a professional Windows installer for SAMPLAR with:

1. ✅ **Official Application Icon** - Created from `plugins/REF.png`
2. ✅ **Automatic VC++ Installation** - Bundled and installed silently
3. ✅ **Desktop Shortcuts** - With the application icon
4. ✅ **Professional Installer** - Using Inno Setup

---

## Quick Start (One Command)

```powershell
.\build_installer_complete.ps1
```

This will:
1. Create the icon from REF.png
2. Rebuild the app with the icon embedded
3. Create the portable version
4. Download VC++ redistributable (if needed)
5. Build the installer

**Output**: `installer_output\Samplar_Setup_v1.0.0.exe`

---

## What You Get

### Application Icon

The icon is created from `plugins/REF.png` (the SAMPLAR character):
- **Reddish-orange body**
- **Green spiky hair**
- **White eyes with black pupils**
- **Evil smile with sharp white teeth**

The icon appears in:
- ✅ Application window
- ✅ Taskbar
- ✅ Desktop shortcut
- ✅ Start Menu
- ✅ Installer
- ✅ File explorer

### Installer Features

- ✅ **Silent VC++ Installation**: Users don't need to install it manually
- ✅ **Desktop Shortcut**: Created by default (with icon)
- ✅ **Start Menu Integration**: App launcher + Uninstaller
- ✅ **All Dependencies Included**: OpenBLAS, AI models, game assets
- ✅ **Professional Uninstaller**: Clean removal
- ✅ **Windows 10/11 Compatible**: Modern installer UI

---

## File Structure

### Before Building

```
Sample_monsta5/
├── plugins/
│   └── REF.png                    # Source image for icon
├── installer_dependencies/
│   └── vc_redist.x64.exe          # VC++ redistributable (auto-downloaded)
├── CMakeLists.txt                 # Updated with icon settings
├── installer.iss                  # Inno Setup script
├── create_icon.ps1                # Icon creation script
├── build_installer_complete.ps1   # Complete build script
└── INSTALLER_BUILD_GUIDE.md       # Detailed guide
```

### After Building

```
Sample_monsta5/
├── Samplar.ico              # Generated icon file
├── Samplar_Portable/        # Portable version
│   ├── SAMPLAR.exe        # With embedded icon
│   ├── libopenblas.dll
│   ├── models/
│   ├── python_bridge/
│   ├── plugins/
│   ├── assets/
│   └── README.txt
└── installer_output/
    └── Samplar_Setup_v1.0.0.exe  # Final installer
```

---

## Prerequisites

### Required

1. **Visual Studio 2022** (for building the app)
   - Already installed if you can build the app

2. **Inno Setup 6** (for creating the installer)
   ```powershell
   winget install JRSoftware.InnoSetup
   ```
   Or download: https://jrsoftware.org/isdl.php

### Optional (Recommended)

3. **ImageMagick** (for high-quality multi-resolution icons)
   ```powershell
   winget install ImageMagick.ImageMagick
   ```
   
   Without ImageMagick, a basic icon will be created using .NET.

---

## Step-by-Step Guide

### Option 1: Automated Build (Recommended)

```powershell
# One command to do everything
.\build_installer_complete.ps1
```

### Option 2: Manual Steps

```powershell
# Step 1: Create the icon
.\create_icon.ps1

# Step 2: Build the app with icon
.\make_portable.ps1

# Step 3: Download VC++ redistributable
.\download_vcredist.ps1

# Step 4: Build the installer
.\build_installer.ps1
```

---

## Icon Details

### Source Image
- **File**: `plugins/REF.png`
- **Format**: PNG
- **Recommended Size**: 512x512 or larger (square)

### Generated Icon
- **File**: `Samplar.ico`
- **Format**: Windows ICO
- **Resolutions** (with ImageMagick):
  - 256x256 - Windows 10/11 taskbar, Start Menu
  - 128x128 - Large icons view
  - 64x64 - Medium icons view
  - 48x48 - Classic view
  - 32x32 - Small icons view
  - 16x16 - System tray, file explorer

### How It's Embedded

The icon is embedded in the executable through CMakeLists.txt:

```cmake
juce_add_gui_app(SM5App 
    PRODUCT_NAME "SAMPLAR"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Samplar.ico"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Samplar.ico"
    COMPANY_NAME "SAMPLAR"
)
```

---

## Installer Details

### What Gets Installed

```
C:\Program Files\SAMPLAR\
├── SAMPLAR.exe          # Main app (with icon)
├── libopenblas.dll              # Math library
├── models\                      # AI models (~80 MB)
├── python_bridge\               # Python integration
├── plugins\                     # Logo and branding
├── assets\                      # Game assets
├── logs\                        # Created on first run
└── README.txt
```

### Installation Process

1. User runs `Samplar_Setup_v1.0.0.exe`
2. Installer shows welcome screen (with icon)
3. User selects installation directory
4. User chooses desktop shortcut option (checked by default)
5. Files are copied (~100 MB)
6. **VC++ Redistributable is installed silently**
7. Desktop shortcut is created (with icon)
8. Start Menu shortcuts are created
9. User can launch the app immediately

### Uninstallation

- Clean removal of all files
- Optional: Remove user data and settings
- Desktop and Start Menu shortcuts removed
- Registry entries cleaned up

---

## Testing

### Test Checklist

Before distributing the installer, test on a clean Windows machine:

- [ ] Icon appears in installer wizard
- [ ] Desktop shortcut has the correct icon
- [ ] Start Menu shortcut has the correct icon
- [ ] App launches successfully
- [ ] Icon appears in taskbar when app is running
- [ ] Icon appears in window title bar
- [ ] VC++ redistributable installs without errors
- [ ] No missing DLL errors
- [ ] Uninstaller works correctly

### Test Environments

1. **Virtual Machine** (recommended):
   - Fresh Windows 10/11 installation
   - No Visual Studio installed
   - No VC++ redistributables installed

2. **Physical Machine**:
   - Computer without development tools
   - Clean Windows installation

---

## Customization

### Change the Icon

1. Replace `plugins/REF.png` with your new image
   - Use PNG format
   - Square aspect ratio (1:1)
   - Minimum 256x256, recommended 512x512 or larger

2. Regenerate the icon:
   ```powershell
   .\create_icon.ps1
   ```

3. Rebuild everything:
   ```powershell
   .\build_installer_complete.ps1
   ```

### Change Installer Settings

Edit `installer.iss`:

```ini
#define MyAppName "SAMPLAR"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "Your Company Name"
#define MyAppURL "https://yourwebsite.com"
```

---

## Troubleshooting

### Icon Not Showing

**Problem**: Icon doesn't appear in the executable  
**Solution**:
1. Verify `Samplar.ico` exists
2. Rebuild: `.\make_portable.ps1`
3. Check CMakeLists.txt has icon settings

### Installer Build Fails

**Problem**: Inno Setup compilation error  
**Solution**:
1. Install Inno Setup 6
2. Verify `Samplar_Portable` folder exists
3. Check all files in `[Files]` section exist

### VC++ Redistributable Missing

**Problem**: `vc_redist.x64.exe` not found  
**Solution**:
```powershell
.\download_vcredist.ps1
```

### Low Quality Icon

**Problem**: Icon looks pixelated  
**Solution**:
1. Install ImageMagick: `winget install ImageMagick.ImageMagick`
2. Recreate icon: `.\create_icon.ps1`

---

## Distribution

### Installer Size

Typical size: **~100 MB**

Breakdown:
- Application: ~15 MB
- OpenBLAS DLL: ~5 MB
- AI Models: ~80 MB
- VC++ Redistributable: ~25 MB (embedded)
- Assets: ~5 MB

### Sharing Options

1. **Direct Download**: Host on your website
2. **Cloud Storage**: Google Drive, Dropbox, OneDrive
3. **GitHub Releases**: Attach to release tags
4. **File Sharing**: WeTransfer, SendSpace

### Code Signing (Optional)

For production releases, consider code signing to avoid Windows SmartScreen warnings.

---

## Summary

### What Was Changed

1. **CMakeLists.txt**: Added icon settings to `juce_add_gui_app`
2. **installer.iss**: Updated to use portable folder and include icon
3. **New Scripts**:
   - `create_icon.ps1` - Creates icon from REF.png
   - `build_installer_complete.ps1` - Complete automated build
   - `download_vcredist.ps1` - Downloads VC++ redistributable

### Key Features

✅ Professional application icon  
✅ Automatic VC++ installation  
✅ Desktop shortcut with icon  
✅ Start Menu integration  
✅ All dependencies included  
✅ Clean uninstaller  
✅ One-command build process  

### Quick Commands

```powershell
# Complete build (recommended)
.\build_installer_complete.ps1

# Test the installer
.\installer_output\Samplar_Setup_v1.0.0.exe
```

---

## Support

For detailed instructions, see:
- **INSTALLER_BUILD_GUIDE.md** - Complete build guide
- **installer.iss** - Installer configuration
- **CMakeLists.txt** - Icon embedding settings

**Happy building! 🎨🚀**

