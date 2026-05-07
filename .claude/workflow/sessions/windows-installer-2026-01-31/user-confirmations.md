# User Confirmations

## Workflow Approach
- Step 1: Use `000portable.ps1` to create SAMPLAR_Portable/ folder ✅ COMPLETE
- Step 2: Create `001installer.ps1` that packages SAMPLAR_Portable/ into Windows installer
- Step 3: Installer installs portable version to Program Files and creates desktop shortcut

## Specification Details

**Publisher/Company Name:** SAMPLAR

**EULA Content:** Create basic proprietary software license

**Version Number:** 1.0.0

**Desktop Shortcut:** Yes, include by default (checked)

**Installer Technology:** Inno Setup (recommended for large files)

**Installation Path:** C:\Program Files\SAMPLAR\

**Package Size:** ~1.86 GB (compresses to ~800MB-1GB)

## User Approval
Status: ✅ APPROVED - proceed to investigation phase
