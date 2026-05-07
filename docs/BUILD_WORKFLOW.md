# SAMPLAR - Build Workflow Guide

## Quick Reference

### After Making Code Changes

```powershell
# Step 1: Build and create portable version
.\make_portable.ps1

# Step 2: Create installer
.\make_installer.ps1
```

That's it! 🚀

---

## Detailed Workflow

### 1. Development Workflow

**When you make code changes:**

```powershell
# Build and package portable version
.\make_portable.ps1
```

**What this does:**
- ✅ Rebuilds the application (Release mode)
- ✅ Creates `Samplar_Portable` folder
- ✅ Copies all dependencies (DLLs, models, assets)
- ✅ Ready to test or distribute as portable

**Options:**
```powershell
# Debug build (for testing)
.\make_portable.ps1 -Config Debug

# Also create ZIP file
.\make_portable.ps1 -Zip

# Skip rebuild, just re-package
.\make_portable.ps1 -SkipBuild
```

### 2. Create Installer

**After portable version is ready:**

```powershell
# Create installer from portable version
.\make_installer.ps1
```

**What this does:**
- ✅ Checks prerequisites (Inno Setup, portable folder, VC++ redist)
- ✅ Downloads VC++ redistributable if missing
- ✅ Creates icon from REF.png if missing
- ✅ Builds installer with Inno Setup
- ✅ Verifies output
- ✅ Opens output folder

**Output:**
- `installer_output\Samplar_Setup_v1.0.0.exe` (~108 MB)

---

## Complete Build Process

### From Scratch

```powershell
# 1. Build portable version
.\make_portable.ps1

# 2. Create installer
.\make_installer.ps1

# 3. Test installer
.\installer_output\Samplar_Setup_v1.0.0.exe
```

### Quick Rebuild (After Small Changes)

```powershell
# Just rebuild and package
.\make_portable.ps1

# Then create installer
.\make_installer.ps1
```

### Debug Build

```powershell
# Build debug version
.\make_portable.ps1 -Config Debug

# Note: Installer always uses Release build
# So run Release build before creating installer:
.\make_portable.ps1
.\make_installer.ps1
```

---

## File Structure

### Source Files
```
Sample_monsta5/
├── src/                          # Source code
├── plugins/
│   └── REF.png                   # Icon source image
├── CMakeLists.txt                # Build configuration
├── installer.iss                 # Installer script
├── make_portable.ps1             # Build portable version
├── make_installer.ps1            # Build installer
└── create_icon.ps1               # Create icon from REF.png
```

### Build Outputs
```
Sample_monsta5/
├── Samplar.ico             # Generated icon
├── Samplar_Portable/       # Portable version
│   ├── SAMPLAR.exe
│   ├── libopenblas.dll
│   ├── models/
│   ├── python_bridge/
│   ├── plugins/
│   ├── assets/
│   └── README.txt
├── installer_output/
│   └── Samplar_Setup_v1.0.0.exe
└── installer_dependencies/
    └── vc_redist.x64.exe
```

---

## Scripts Overview

### `make_portable.ps1`

**Purpose:** Build the app and create portable version

**Usage:**
```powershell
.\make_portable.ps1                 # Release build
.\make_portable.ps1 -Config Debug   # Debug build
.\make_portable.ps1 -Zip            # Also create ZIP
.\make_portable.ps1 -SkipBuild      # Just re-package
```

**What it does:**
1. Configures CMake (if needed)
2. Builds the application
3. Runs `build_portable.ps1` to package
4. Optionally creates ZIP file

**Output:**
- `Samplar_Portable/` folder
- `Samplar_Portable.zip` (if -Zip)

### `make_installer.ps1`

**Purpose:** Create Windows installer from portable version

**Usage:**
```powershell
.\make_installer.ps1
```

**What it does:**
1. Checks prerequisites:
   - Inno Setup installed
   - Portable folder exists
   - VC++ redistributable available
   - Icon file exists
2. Downloads VC++ redist if missing
3. Creates icon if missing
4. Compiles installer with Inno Setup
5. Verifies output
6. Opens output folder

**Output:**
- `installer_output\Samplar_Setup_v1.0.0.exe`

### `create_icon.ps1`

**Purpose:** Create Windows icon from REF.png

**Usage:**
```powershell
.\create_icon.ps1
```

**What it does:**
- Converts `plugins/REF.png` to `Samplar.ico`
- Uses ImageMagick if available (multi-resolution)
- Falls back to .NET (single resolution)

**Output:**
- `Samplar.ico`

### `build_portable.ps1`

**Purpose:** Package portable folder (called by make_portable.ps1)

**Usage:**
```powershell
.\build_portable.ps1
```

**What it does:**
- Creates `Samplar_Portable` folder
- Copies executable, DLLs, models, assets
- Creates README.txt

---

## Common Scenarios

### Scenario 1: Daily Development

```powershell
# Make code changes...

# Test locally
.\make_portable.ps1 -Config Debug
# Run: Samplar_Portable\SAMPLAR.exe

# When ready for release
.\make_portable.ps1
.\make_installer.ps1
```

### Scenario 2: Quick Fix

```powershell
# Fix bug in code...

# Rebuild
.\make_portable.ps1

# Create new installer
.\make_installer.ps1

# Test
.\installer_output\Samplar_Setup_v1.0.0.exe
```

### Scenario 3: Update Icon

```powershell
# Replace plugins/REF.png with new image

# Recreate icon
.\create_icon.ps1

# Rebuild portable (to embed icon in exe)
.\make_portable.ps1

# Create installer (with new icon)
.\make_installer.ps1
```

### Scenario 4: Update Assets/Models

```powershell
# Update files in assets/ or models/

# Just re-package (no rebuild needed)
.\make_portable.ps1 -SkipBuild

# Create installer
.\make_installer.ps1
```

### Scenario 5: Release Build

```powershell
# 1. Ensure Release mode
.\make_portable.ps1

# 2. Create installer
.\make_installer.ps1

# 3. Test on clean VM
# Copy installer_output\Samplar_Setup_v1.0.0.exe to VM
# Run installer
# Verify app works

# 4. Distribute
# Upload to website, GitHub releases, etc.
```

---

## Prerequisites

### Required

1. **Visual Studio 2022** (Build Tools or full IDE)
   - Already installed if you can build the app

2. **CMake**
   - Already installed if you can build the app

3. **Inno Setup 6** (for installer creation)
   ```powershell
   winget install JRSoftware.InnoSetup
   ```

### Optional

4. **ImageMagick** (for high-quality multi-resolution icons)
   ```powershell
   winget install ImageMagick.ImageMagick
   ```

---

## Troubleshooting

### "Inno Setup not found"

**Solution:**
```powershell
winget install JRSoftware.InnoSetup
```

### "Portable folder not found"

**Solution:**
```powershell
# Run make_portable.ps1 first
.\make_portable.ps1
```

### "VC++ Redistributable not found"

**Solution:**
- `make_installer.ps1` will download it automatically
- Or download manually: https://aka.ms/vs/17/release/vc_redist.x64.exe
- Save to: `installer_dependencies\vc_redist.x64.exe`

### "Icon file not found"

**Solution:**
```powershell
# Create icon from REF.png
.\create_icon.ps1
```

### Build fails

**Solution:**
```powershell
# Clean build
Remove-Item -Recurse -Force ..\build\sm5
.\make_portable.ps1
```

---

## Testing Checklist

### Before Distribution

- [ ] Run `.\make_portable.ps1` successfully
- [ ] Run `.\make_installer.ps1` successfully
- [ ] Installer created: `installer_output\Samplar_Setup_v1.0.0.exe`
- [ ] Test portable version works
- [ ] Test installer on clean Windows VM
- [ ] Verify icon appears everywhere
- [ ] Verify VC++ installs automatically
- [ ] Verify app launches without errors
- [ ] Test all features work

---

## Quick Commands

```powershell
# Build everything
.\make_portable.ps1 && .\make_installer.ps1

# Build with ZIP
.\make_portable.ps1 -Zip && .\make_installer.ps1

# Debug build + installer
.\make_portable.ps1 -Config Debug
.\make_portable.ps1  # Switch back to Release
.\make_installer.ps1

# Test VC++ detection
.\test_vcredist_detection.ps1

# Open output folder
explorer.exe installer_output
```

---

## Summary

### Two-Step Process

1. **Build Portable:**
   ```powershell
   .\make_portable.ps1
   ```

2. **Create Installer:**
   ```powershell
   .\make_installer.ps1
   ```

### What You Get

- ✅ Portable folder (ready to distribute as-is)
- ✅ Professional Windows installer
- ✅ Icon embedded everywhere
- ✅ VC++ redistributable auto-install
- ✅ Desktop shortcuts
- ✅ Start Menu integration
- ✅ Clean uninstaller

### Distribution

**Portable Version:**
- Share `Samplar_Portable` folder
- Or create ZIP: `.\make_portable.ps1 -Zip`

**Installer:**
- Share `installer_output\Samplar_Setup_v1.0.0.exe`
- Upload to website, GitHub, cloud storage

---

**Happy building! 🚀**

