# SAMPLAR - Installer Testing Guide

## 🚀 Quick Start: Build the Installer

### Step 1: Install Inno Setup
1. Download from: https://jrsoftware.org/isinfo.php
2. Install with default settings
3. Restart PowerShell if needed

### Step 2: Build the Installer
```powershell
# Simple - just run this:
.\build_installer.ps1

# Or if you want to rebuild everything first:
.\build_installer.ps1 -Clean
```

The script will:
- ✅ Check for Inno Setup
- ✅ Verify Release build exists (or build it)
- ✅ Check all required files (exe, DLLs, models, assets)
- ✅ Build the installer
- ✅ Show you the output file

### Step 3: Find Your Installer
The installer will be in: `installer_output\Samplar_Setup_v1.0.0.exe`

Expected size: **~5-6 GB** (includes the 5GB Demucs model)

---

## 🧪 Testing on a Fresh Computer

### What You Need:
- A clean Windows 10/11 machine (VM or physical)
- **NO** Python installed (we want to test without it)
- **NO** Visual C++ Redistributables (installer should handle this)
- At least **10 GB** free disk space

### Testing Checklist:

#### ✅ Installation
- [ ] Installer runs without errors
- [ ] No Windows SmartScreen warnings (expected - we're not signed yet)
- [ ] Installation completes successfully
- [ ] Desktop icon created (if selected)
- [ ] Start menu shortcuts created

#### ✅ First Launch
- [ ] App launches without errors
- [ ] No missing DLL errors
- [ ] License screen appears
- [ ] Can enter license credentials

#### ✅ Core Features
- [ ] Can load an audio file (WAV/MP3)
- [ ] Demucs model loads successfully
- [ ] Audio separation works (drums/vocals/bass/other)
- [ ] Sample detection works
- [ ] Variation engine generates samples
- [ ] Can export samples

#### ✅ Wave Editor
- [ ] Can open wave editor
- [ ] Effects work (EQ, reverb, delay, etc.)
- [ ] Waveform displays correctly
- [ ] Can save edited audio

#### ✅ Mini-Games
- [ ] Snake game loads and plays
- [ ] Flappy Bird loads and plays
- [ ] Space Shooter loads and plays
- [ ] Game assets display correctly

#### ✅ Backgrounds
- [ ] Background images load
- [ ] Can switch between backgrounds
- [ ] No missing image errors

#### ✅ Performance
- [ ] Audio processing completes in reasonable time
- [ ] No crashes during heavy processing
- [ ] Memory usage is reasonable
- [ ] GPU detection works (if available)

---

## 🐛 Common Issues & Solutions

### Issue: "Inno Setup not found"
**Solution**: Install Inno Setup from https://jrsoftware.org/isinfo.php

### Issue: "Release build not found"
**Solution**: Run `.\build_release.ps1` first

### Issue: "Demucs model not found"
**Solution**: Make sure `models\ggml-model-htdemucs-4s-f16.bin` exists (5GB file)

### Issue: Installer is too large (>10 GB)
**Solution**: This is normal - the Demucs model is 5GB. Compressed installer should be ~5-6 GB.

### Issue: Windows SmartScreen blocks installer
**Solution**: Click "More info" → "Run anyway" (expected without code signing)

### Issue: App won't launch - missing DLL
**Solution**: 
1. Check if `libopenblas.dll` is in the installation folder
2. Install Visual C++ Redistributable 2015-2022 (x64)

### Issue: Demucs fails with "model not found"
**Solution**: Check that `models\ggml-model-htdemucs-4s-f16.bin` exists in installation folder

### Issue: Python bridge fails
**Solution**: 
1. Check if `python_bridge\demucs_separator.py` exists
2. Install Python 3.8+ (optional - C++ backend should work)

---

## 📊 What's Included in the Installer

### Core Application (~50 MB)
- `SAMPLAR.exe` - Main application
- `libopenblas.dll` - Math library for audio processing

### Demucs Model (~5 GB)
- `models\ggml-model-htdemucs-4s-f16.bin` - Neural network for audio separation

### Python Bridge (~1 MB)
- `python_bridge\demucs_separator.py` - Python fallback for Demucs

### Game Assets (~10 MB)
- `assets\flappy\*` - Flappy Bird game
- `assets\gamesnake\*` - Snake game
- `assets\shooter\*` - Space Shooter game

### Backgrounds (~5 MB)
- `BACKGROUNDS\*.png` - Background images

### Documentation
- `LICENSE.txt` - Software license
- `docs\INSTALLATION_NOTES.txt` - Installation guide

---

## 🔧 Advanced: Manual Installer Build

If you want to build the installer manually:

```powershell
# 1. Build Release version
cmake --build build --config Release

# 2. Run Inno Setup compiler
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer.iss

# 3. Find installer in installer_output\
```

---

## 📝 Notes for Production

### Before Public Release:
1. **Code Signing** - Get an EV code signing certificate ($300-500/year)
2. **Update Version** - Change version in `installer.iss` and `CMakeLists.txt`
3. **Update Publisher** - Change "Your Company Name" in `installer.iss`
4. **Update URLs** - Change website URLs in `installer.iss`
5. **Add Icons** - Create proper installer icons (optional)
6. **Test on Multiple Machines** - Windows 10, Windows 11, different hardware

### Current Limitations (v1.0 Testing):
- ❌ Not code signed (Windows SmartScreen warnings expected)
- ⚠️ Large installer size (~5-6 GB due to model)
- ⚠️ No auto-update mechanism
- ⚠️ No crash reporting/telemetry

---

## 🎯 Success Criteria

Your installer is ready for testing if:
- ✅ Builds without errors
- ✅ Size is ~5-6 GB
- ✅ Includes all required files
- ✅ Runs on a fresh Windows machine
- ✅ App launches and core features work

---

## 📞 Support

If you encounter issues:
1. Check the logs in `C:\Program Files\SAMPLAR\logs\`
2. Check Windows Event Viewer for crash logs
3. Try running as Administrator
4. Check antivirus isn't blocking the app

---

## 🚀 Next Steps After Testing

Once testing is successful:
1. Get code signing certificate
2. Sign the installer
3. Test signed installer on fresh machine
4. Distribute to beta testers
5. Collect feedback
6. Iterate and improve
7. Public release!

Good luck! 🎉

