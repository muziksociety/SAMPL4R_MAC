# SAMPLAR - Quick Start to Production

**TL;DR**: Your app needs code signing before you can distribute it. Everything else is optional but recommended.

---

## ⚡ Fastest Path to Distribution (1-2 weeks)

### Step 1: Get a Code Signing Certificate (1-3 days)
**Cost**: $300-500/year

1. Go to https://www.digicert.com/ or https://sectigo.com/
2. Purchase "EV Code Signing Certificate for Windows"
3. Complete identity verification (requires business documents)
4. Receive certificate (USB token or file)

**Alternative (Testing Only)**: Create self-signed certificate
```powershell
New-SelfSignedCertificate -Type CodeSigningCert -Subject "CN=SAMPLAR" -CertStoreLocation Cert:\CurrentUser\My
```
⚠️ **Warning**: Self-signed certificates still show warnings to users!

---

### Step 2: Update Version Number (5 minutes)

Edit `CMakeLists.txt` line 3:
```cmake
project(Samplar VERSION 1.0.0 LANGUAGES CXX)
```

---

### Step 3: Build Release (10 minutes)

```powershell
# Option A: Use the automated script
.\build_release.ps1 -Sign -Installer

# Option B: Manual build
cd build
cmake --build . --config Release
```

Your executable will be at:
```
build\SM5App_artefacts\Release\SAMPLAR.exe
```

---

### Step 4: Sign the Executable (2 minutes)

```powershell
# Set environment variables
$env:CODESIGN_CERTIFICATE = "C:\Path\To\YourCertificate.pfx"
$env:CODESIGN_PASSWORD = "YourPassword"

# Rebuild (will auto-sign)
cmake --build build --config Release

# Or sign manually
signtool sign /f "YourCertificate.pfx" /p "password" /t http://timestamp.digicert.com "build\SM5App_artefacts\Release\SAMPLAR.exe"
```

---

### Step 5: Create Installer (15 minutes)

1. **Install Inno Setup**: https://jrsoftware.org/isinfo.php

2. **Edit installer.iss** (update company name, website)

3. **Build installer**:
```powershell
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer.iss
```

4. **Sign installer**:
```powershell
signtool sign /f "YourCertificate.pfx" /p "password" /t http://timestamp.digicert.com "installer_output\Samplar_Setup_v1.0.0.exe"
```

---

### Step 6: Test on Clean Machine (30 minutes)

1. Get a clean Windows 10/11 machine (or VM)
2. Run the installer
3. Check for SmartScreen warnings (should be minimal with EV cert)
4. Test all features
5. Test uninstaller

---

### Step 7: Distribute! 🎉

Upload to:
- Your website
- Gumroad / Itch.io
- Steam (requires $100 fee + approval)

---

## 🎯 What You Get

### ✅ With Code Signing
- No "Unknown Publisher" warnings
- Antivirus won't flag it
- Users can verify authenticity
- Professional appearance

### ❌ Without Code Signing
- Windows SmartScreen blocks it
- "This app might harm your PC" warnings
- Users won't trust it
- Antivirus false positives

---

## 💰 Cost Breakdown

### Minimum (DIY Distribution)
| Item | Cost | Frequency |
|------|------|-----------|
| Code Signing Certificate | $300-500 | Yearly |
| Domain Name | $10-20 | Yearly |
| Web Hosting | $5-10/month | Monthly |
| **Total Year 1** | **~$400** | - |

### Recommended (Professional)
| Item | Cost | Frequency |
|------|------|-----------|
| EV Code Signing | $400 | Yearly |
| Domain + Hosting | $100 | Yearly |
| Steam Fee | $100 | One-time |
| Email Service | $50 | Yearly |
| **Total Year 1** | **~$650** | - |
| **Total Year 2+** | **~$550** | - |

---

## 🔒 Security Status

### Current (After Following This Guide)
- ✅ Code signed executable
- ✅ Code signed installer
- ✅ Security compiler flags enabled
- ✅ JUCE telemetry disabled
- ✅ License system with hardware fingerprinting
- ⚠️ No server-side license validation
- ⚠️ No auto-update system
- ⚠️ No file integrity checks

### This is GOOD ENOUGH for v1.0 release!

---

## 📋 Pre-Release Checklist

### Must Have (Before Any Release)
- [ ] Version updated to 1.0.0
- [ ] Code signing certificate purchased
- [ ] Executable signed
- [ ] Installer created and signed
- [ ] Tested on clean Windows 10
- [ ] Tested on clean Windows 11
- [ ] All features working
- [ ] License activation working
- [ ] No crashes on startup

### Should Have (For Professional Release)
- [ ] User manual / documentation
- [ ] Website / landing page
- [ ] Support email set up
- [ ] Privacy policy (if collecting any data)
- [ ] EULA (End User License Agreement)
- [ ] Tested with antivirus enabled
- [ ] Tested with different audio interfaces
- [ ] Marketing materials ready

### Nice to Have (Can Add Later)
- [ ] Auto-update system
- [ ] Crash reporting
- [ ] Server-side license validation
- [ ] File integrity checks
- [ ] Obfuscation (if worried about piracy)

---

## 🚨 Common Mistakes to Avoid

### 1. Forgetting to Update Version
```cmake
# ❌ DON'T ship with this
project(Samplar VERSION 0.1.0 LANGUAGES CXX)

# ✅ DO update to
project(Samplar VERSION 1.0.0 LANGUAGES CXX)
```

### 2. Building Debug Instead of Release
```powershell
# ❌ DON'T
cmake --build build --config Debug

# ✅ DO
cmake --build build --config Release
```

### 3. Not Testing on Clean Machine
- Your dev machine has all dependencies installed
- Users' machines don't!
- Always test on a clean VM

### 4. Forgetting to Sign the Installer
- Signing the .exe is not enough
- The installer also needs to be signed
- Otherwise users still see warnings

### 5. Using Self-Signed Certificate for Production
- Self-signed = still shows warnings
- Only use for internal testing
- Spend the $300 for a real certificate

---

## 🎓 Learning Resources

### Code Signing
- https://docs.microsoft.com/en-us/windows/win32/seccrypto/cryptography-tools
- https://www.digicert.com/signing/code-signing-certificate

### Inno Setup
- https://jrsoftware.org/ishelp/
- https://www.youtube.com/results?search_query=inno+setup+tutorial

### JUCE
- https://juce.com/learn/documentation
- https://forum.juce.com/

### Windows App Distribution
- https://docs.microsoft.com/en-us/windows/apps/get-started/

---

## 🆘 Troubleshooting

### "SmartScreen blocked this app"
- **Cause**: Not code signed, or new certificate
- **Fix**: Sign with EV certificate, build reputation over time
- **Workaround**: Users can click "More info" → "Run anyway"

### "Windows Defender flagged my app"
- **Cause**: New executable, no reputation
- **Fix**: Submit to Microsoft for analysis: https://www.microsoft.com/en-us/wdsi/filesubmission
- **Prevention**: Code signing helps

### "Installer fails on clean machine"
- **Cause**: Missing dependencies (Python, Visual C++ Runtime)
- **Fix**: Include dependencies in installer or check in installer script
- **Check**: Test on VM without dev tools

### "License activation doesn't work"
- **Cause**: Hardware fingerprint mismatch
- **Fix**: Check `src/security/HardwareFingerprintGenerator.cpp`
- **Debug**: Enable logging in license system

### "App crashes on startup"
- **Cause**: Missing DLLs or assets
- **Fix**: Check all files copied to output directory
- **Debug**: Run with Dependency Walker

---

## 📞 Next Steps

1. **Read the full guide**: `docs/PRODUCTION_DEPLOYMENT_GUIDE.md`
2. **Check security**: `docs/SECURITY_ASSESSMENT.md`
3. **Get certificate**: Start the process today (takes 1-3 days)
4. **Build release**: Use `build_release.ps1`
5. **Test thoroughly**: On clean machines
6. **Launch!** 🚀

---

## 💡 Pro Tips

### Tip 1: Start Certificate Process Early
Code signing certificates require identity verification. Start the process 1-2 weeks before you want to release.

### Tip 2: Use Environment Variables for Secrets
Never commit certificates or passwords to git!
```powershell
# .env file (add to .gitignore)
CODESIGN_CERTIFICATE=C:\Certificates\MyCompany.pfx
CODESIGN_PASSWORD=MySecurePassword
```

### Tip 3: Automate Everything
Use the `build_release.ps1` script for consistent builds:
```powershell
.\build_release.ps1 -Sign -Installer -Clean
```

### Tip 4: Keep a Changelog
Document what's new in each version. Users appreciate it!

### Tip 5: Plan for Updates
You WILL need to release updates. Plan for it from day 1.

---

## ✅ You're Ready When...

- [x] Executable is code signed
- [x] Installer is code signed
- [x] Tested on 2+ clean machines
- [x] No SmartScreen warnings (or minimal)
- [x] All features work
- [x] Documentation is ready
- [x] Support system is set up
- [x] You're confident in the product

**Then hit that publish button!** 🎉

---

**Good luck with your launch!** 🚀

If you have questions, check:
- Full guide: `docs/PRODUCTION_DEPLOYMENT_GUIDE.md`
- Security: `docs/SECURITY_ASSESSMENT.md`
- JUCE forums: https://forum.juce.com/

