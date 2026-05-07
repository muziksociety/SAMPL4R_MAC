# SAMPLAR - Production Deployment Guide

## 🎯 Overview
This guide covers everything needed to make SAMPLAR production-ready and distribute it securely.

---

## 🔐 1. CODE SIGNING (CRITICAL - DO THIS FIRST)

### Why Code Signing Matters
- **Security**: Proves the app hasn't been tampered with
- **Trust**: Shows users who published the app
- **Windows SmartScreen**: Prevents scary "Unknown Publisher" warnings
- **Antivirus**: Reduces false positives from antivirus software

### Getting a Code Signing Certificate

#### Option A: Standard EV Code Signing Certificate (Recommended)
**Cost**: $300-500/year  
**Providers**:
- DigiCert (most trusted)
- Sectigo (formerly Comodo)
- GlobalSign

**Process**:
1. Purchase certificate from provider
2. Complete identity verification (requires business documents)
3. Receive USB token or cloud-based certificate
4. Install certificate on your build machine

#### Option B: Self-Signed Certificate (Testing Only)
**Cost**: Free  
**Warning**: Only for internal testing - users will still see warnings

```powershell
# Create self-signed certificate (Windows)
New-SelfSignedCertificate -Type CodeSigningCert -Subject "CN=SAMPLAR" -CertStoreLocation Cert:\CurrentUser\My
```

### Signing Your Executable

#### Windows (using signtool.exe)
```powershell
# Sign the executable
signtool sign /f "YourCertificate.pfx" /p "password" /t http://timestamp.digicert.com "SAMPLAR.exe"

# Verify signature
signtool verify /pa "SAMPLAR.exe"
```

#### Automated Signing in CMake
Add to CMakeLists.txt after the executable is built:

```cmake
# Code signing (Windows only)
if(WIN32 AND DEFINED ENV{CODESIGN_CERTIFICATE})
  find_program(SIGNTOOL signtool.exe
    PATHS "C:/Program Files (x86)/Windows Kits/10/bin/*/x64"
  )
  
  if(SIGNTOOL)
    add_custom_command(TARGET SM5App POST_BUILD
      COMMAND ${SIGNTOOL} sign /f "$ENV{CODESIGN_CERTIFICATE}" 
              /p "$ENV{CODESIGN_PASSWORD}"
              /t http://timestamp.digicert.com
              /d "SAMPLAR"
              /du "https://yourwebsite.com"
              "$<TARGET_FILE:SM5App>"
      COMMENT "Code signing executable..."
    )
  endif()
endif()
```

---

## 📦 2. INSTALLER CREATION

### Why You Need an Installer
- Handles dependencies (Python, OpenBLAS DLL, models)
- Creates Start Menu shortcuts
- Manages uninstallation
- Can be code-signed for extra trust

### Option A: Inno Setup (Recommended - Free)

**Download**: https://jrsoftware.org/isinfo.php

**Sample Script** (`installer.iss`):

```inno
[Setup]
AppName=SAMPLAR
AppVersion=1.0.0
AppPublisher=Your Company Name
AppPublisherURL=https://yourwebsite.com
DefaultDirName={autopf}\SAMPLAR
DefaultGroupName=SAMPLAR
OutputDir=installer_output
OutputBaseFilename=Samplar_Setup_v1.0.0
Compression=lzma2/max
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64
SignTool=signtool
SignedUninstaller=yes

[Files]
Source: "build\SM5App_artefacts\Release\SAMPLAR.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "build\SM5App_artefacts\Release\libopenblas.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "build\SM5App_artefacts\Release\python_bridge\*"; DestDir: "{app}\python_bridge"; Flags: ignoreversion recursesubdirs
Source: "build\SM5App_artefacts\Release\models\*"; DestDir: "{app}\models"; Flags: ignoreversion recursesubdirs
Source: "assets\*"; DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs

[Icons]
Name: "{group}\SAMPLAR"; Filename: "{app}\SAMPLAR.exe"
Name: "{group}\Uninstall SAMPLAR"; Filename: "{uninstallexe}"
Name: "{autodesktop}\SAMPLAR"; Filename: "{app}\SAMPLAR.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop icon"; GroupDescription: "Additional icons:"

[Run]
Filename: "{app}\SAMPLAR.exe"; Description: "Launch SAMPLAR"; Flags: nowait postinstall skipifsilent
```

**Build Installer**:
```powershell
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer.iss
```

### Option B: NSIS (Free, more complex)
### Option C: WiX Toolset (Free, MSI format)
### Option D: Advanced Installer (Paid, GUI-based)

---

## 🔒 3. SECURITY HARDENING

### A. Disable JUCE Telemetry & Splash Screen

Add to CMakeLists.txt (line ~216):

```cmake
target_compile_definitions(SM5App
  PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0        # Remove JUCE splash
    JUCE_REPORT_APP_USAGE=0             # Disable analytics
    JUCE_CHECK_MEMORY_LEAKS=0           # Disable in Release
    JUCE_STRICT_REFCOUNTEDPOINTER=1     # Better memory safety
)
```

### B. Strip Debug Symbols (Release Build)

Already configured via JUCE, but verify:
```cmake
# In Release builds, strip symbols
if(MSVC)
  target_compile_options(SM5App PRIVATE $<$<CONFIG:Release>:/Zi>)
  target_link_options(SM5App PRIVATE $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF>)
endif()
```

### C. Enable Security Features

```cmake
if(MSVC)
  # Enable security features
  target_compile_options(SM5App PRIVATE
    /GS        # Buffer security check
    /guard:cf  # Control Flow Guard
    /sdl       # Additional security checks
  )
  target_link_options(SM5App PRIVATE
    /DYNAMICBASE  # ASLR
    /NXCOMPAT     # DEP
    /GUARD:CF     # Control Flow Guard
  )
endif()
```

### D. Obfuscation (Optional)
For extra protection against reverse engineering:
- **VMProtect** (paid, very strong)
- **Themida** (paid)
- **Enigma Protector** (paid)

**Warning**: Can cause false positives with antivirus!

---

## 🏗️ 4. BUILD CONFIGURATION

### Update Version Number

Edit `CMakeLists.txt` line 3:
```cmake
project(Samplar VERSION 1.0.0 LANGUAGES CXX)
```

### Release Build Optimizations

```bash
# Clean build
cd build
cmake --build . --config Release --clean-first

# Or rebuild from scratch
rm -rf build
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

### Verify Release Build
```powershell
# Check file size (should be smaller than Debug)
dir "build\SM5App_artefacts\Release\SAMPLAR.exe"

# Check dependencies
dumpbin /dependents "build\SM5App_artefacts\Release\SAMPLAR.exe"
```

---

## 📤 5. DISTRIBUTION METHODS

### A. Direct Download (Simple)
1. Upload installer to your website
2. Provide SHA-256 checksum for verification
3. Include installation instructions

```powershell
# Generate checksum
certutil -hashfile Samplar_Setup_v1.0.0.exe SHA256
```

### B. Steam (Recommended for Games/Audio Software)
- Built-in DRM
- Automatic updates
- Payment processing
- Large user base
- **Cost**: $100 one-time fee + 30% revenue share

### C. Gumroad / Itch.io (Indie-Friendly)
- Easy setup
- Lower fees (10% or less)
- Good for niche audio software

### D. Your Own Website + License System
You already have a license system implemented! (`src/licensing/`, `src/security/`)

---

## 🛡️ 6. SECURITY ASSESSMENT

### Current Security Status

✅ **Good**:
- License system with hardware fingerprinting
- Secure session management
- No web browser component (can't be exploited)
- No curl (no network vulnerabilities)
- Cryptography module for license validation

⚠️ **Needs Attention**:
- No code signing (CRITICAL)
- Python bridge could be attack vector
- Large model files could be replaced
- No integrity checking of assets

❌ **Missing**:
- Code signing certificate
- Installer package
- Update mechanism
- Crash reporting system

### Against Common Attacks

| Attack Type | Current Protection | Recommendation |
|-------------|-------------------|----------------|
| **Tampering** | ❌ None | ✅ Code signing required |
| **Reverse Engineering** | ⚠️ Basic (C++ compiled) | Consider obfuscation |
| **License Cracking** | ✅ Hardware fingerprint | Good, but add server validation |
| **DLL Hijacking** | ⚠️ Partial | Sign all DLLs, use absolute paths |
| **Asset Replacement** | ❌ None | Add checksum validation |
| **Memory Dumping** | ⚠️ Basic | Consider anti-debugging |

---

## 🚀 7. PRE-RELEASE CHECKLIST

### Before First Release

- [ ] Update version to 1.0.0
- [ ] Purchase code signing certificate
- [ ] Sign executable and installer
- [ ] Create installer with Inno Setup
- [ ] Test installer on clean Windows machine
- [ ] Verify all dependencies are included
- [ ] Test license activation system
- [ ] Add JUCE_DISPLAY_SPLASH_SCREEN=0
- [ ] Add JUCE_REPORT_APP_USAGE=0
- [ ] Enable compiler security flags (/GS, /GUARD:CF)
- [ ] Create user manual / documentation
- [ ] Set up crash reporting (optional)
- [ ] Create website / landing page
- [ ] Prepare marketing materials
- [ ] Test on multiple Windows versions (10, 11)
- [ ] Test with different audio interfaces
- [ ] Verify Python bridge works on target machines

### Testing Checklist

- [ ] Install on Windows 10 (clean machine)
- [ ] Install on Windows 11 (clean machine)
- [ ] Test without Python installed
- [ ] Test with antivirus enabled
- [ ] Test license activation
- [ ] Test offline mode
- [ ] Test all audio effects
- [ ] Test all mini-games
- [ ] Test sample browser
- [ ] Verify no crashes on startup
- [ ] Check memory leaks (long sessions)
- [ ] Test uninstaller

---

## 📊 8. DISTRIBUTION COSTS

### Minimum (DIY)
- Code signing certificate: **$300-500/year**
- Domain + hosting: **$50-100/year**
- **Total**: ~$400/year

### Recommended (Professional)
- EV Code signing: **$400/year**
- Domain + hosting: **$100/year**
- Steam fee: **$100 one-time**
- Marketing: **$500-1000**
- **Total**: ~$1100 first year, ~$500/year after

---

## 🔧 9. NEXT STEPS

### Immediate (This Week)
1. Update version to 1.0.0
2. Add JUCE splash screen disable flags
3. Enable security compiler flags
4. Create Release build

### Short Term (This Month)
1. Purchase code signing certificate
2. Create Inno Setup installer script
3. Test on clean machines
4. Set up distribution website

### Long Term (Before Launch)
1. Implement auto-update system
2. Add crash reporting
3. Create comprehensive documentation
4. Plan marketing strategy

---

## 📞 SUPPORT

For questions about:
- **Code signing**: Contact your certificate provider
- **JUCE licensing**: https://juce.com/
- **Inno Setup**: https://jrsoftware.org/
- **Steam**: https://partner.steamgames.com/

---

## ⚖️ LEGAL CONSIDERATIONS

### You Need
- [ ] End User License Agreement (EULA)
- [ ] Privacy Policy (if collecting any data)
- [ ] Terms of Service
- [ ] Copyright notices
- [ ] Third-party license compliance (JUCE, SoundTouch, Demucs, etc.)

### JUCE Licensing
Check your JUCE license type:
- **GPL**: Must open-source your app OR
- **Commercial**: Need JUCE license (~$40/month or $800/year)

Your app appears to be closed-source commercial, so you need a JUCE commercial license!

---

**Last Updated**: 2025-10-09  
**Version**: 1.0

