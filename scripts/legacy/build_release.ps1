# SAMPLAR - Release Build Script
# This script builds a production-ready release of SAMPLAR
# 
# Usage:
#   .\build_release.ps1                    # Build without code signing
#   .\build_release.ps1 -Sign              # Build with code signing (requires certificate)
#   .\build_release.ps1 -Sign -Installer   # Build + create installer

param(
    [switch]$Sign = $false,
    [switch]$Installer = $false,
    [switch]$Clean = $false,
    [string]$CertificatePath = "",
    [string]$CertificatePassword = ""
)

# Configuration
$ProjectRoot = $PSScriptRoot
$BuildDir = Join-Path $ProjectRoot "build"
$OutputDir = Join-Path $BuildDir "SM5App_artefacts\Release"
$InstallerDir = Join-Path $ProjectRoot "installer_output"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "SAMPLAR - Release Build Script" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Step 1: Clean build (optional)
if ($Clean) {
    Write-Host "[1/5] Cleaning previous build..." -ForegroundColor Yellow
    if (Test-Path $BuildDir) {
        Remove-Item -Recurse -Force $BuildDir
        Write-Host "  ✓ Build directory cleaned" -ForegroundColor Green
    }
} else {
    Write-Host "[1/5] Skipping clean (use -Clean to force clean build)" -ForegroundColor Gray
}

# Step 2: Configure CMake
Write-Host "[2/5] Configuring CMake..." -ForegroundColor Yellow
if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

Push-Location $BuildDir
try {
    cmake .. -G "Visual Studio 17 2022" -A x64
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configuration failed"
    }
    Write-Host "  ✓ CMake configured successfully" -ForegroundColor Green
} finally {
    Pop-Location
}

# Step 3: Build Release
Write-Host "[3/5] Building Release configuration..." -ForegroundColor Yellow
cmake --build $BuildDir --config Release --parallel
if ($LASTEXITCODE -ne 0) {
    Write-Host "  ✗ Build failed!" -ForegroundColor Red
    exit 1
}
Write-Host "  ✓ Build completed successfully" -ForegroundColor Green

# Step 4: Code Signing (optional)
if ($Sign) {
    Write-Host "[4/5] Code signing executables..." -ForegroundColor Yellow
    
    # Check for certificate
    if ($CertificatePath -eq "") {
        if ($env:CODESIGN_CERTIFICATE) {
            $CertificatePath = $env:CODESIGN_CERTIFICATE
        } else {
            Write-Host "  ✗ No certificate specified!" -ForegroundColor Red
            Write-Host "    Use -CertificatePath or set CODESIGN_CERTIFICATE environment variable" -ForegroundColor Red
            exit 1
        }
    }
    
    if ($CertificatePassword -eq "") {
        if ($env:CODESIGN_PASSWORD) {
            $CertificatePassword = $env:CODESIGN_PASSWORD
        } else {
            $CertificatePassword = Read-Host "Enter certificate password" -AsSecureString
            $CertificatePassword = [Runtime.InteropServices.Marshal]::PtrToStringAuto(
                [Runtime.InteropServices.Marshal]::SecureStringToBSTR($CertificatePassword))
        }
    }
    
    # Find signtool
    $SignTool = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin\*\x64\signtool.exe" -Recurse -ErrorAction SilentlyContinue | 
                Select-Object -First 1 -ExpandProperty FullName
    
    if (-not $SignTool) {
        Write-Host "  ✗ signtool.exe not found!" -ForegroundColor Red
        Write-Host "    Install Windows SDK from https://developer.microsoft.com/windows/downloads/windows-sdk/" -ForegroundColor Red
        exit 1
    }
    
    # Sign main executable
    $ExePath = Join-Path $OutputDir "SAMPLAR.exe"
    & $SignTool sign /f $CertificatePath /p $CertificatePassword /t http://timestamp.digicert.com /d "SAMPLAR" $ExePath
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  ✓ Executable signed successfully" -ForegroundColor Green
    } else {
        Write-Host "  ✗ Code signing failed!" -ForegroundColor Red
        exit 1
    }
} else {
    Write-Host "[4/5] Skipping code signing (use -Sign to enable)" -ForegroundColor Gray
}

# Step 5: Create Installer (optional)
if ($Installer) {
    Write-Host "[5/5] Creating installer..." -ForegroundColor Yellow
    
    # Find Inno Setup
    $InnoSetup = "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
    if (-not (Test-Path $InnoSetup)) {
        Write-Host "  ✗ Inno Setup not found!" -ForegroundColor Red
        Write-Host "    Install from https://jrsoftware.org/isinfo.php" -ForegroundColor Red
        exit 1
    }
    
    # Build installer
    $InstallerScript = Join-Path $ProjectRoot "installer.iss"
    & $InnoSetup $InstallerScript
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  ✓ Installer created successfully" -ForegroundColor Green
        
        # Sign installer if code signing is enabled
        if ($Sign) {
            $InstallerPath = Get-ChildItem $InstallerDir -Filter "Samplar_Setup_*.exe" | 
                            Select-Object -First 1 -ExpandProperty FullName
            
            if ($InstallerPath) {
                Write-Host "  Signing installer..." -ForegroundColor Yellow
                & $SignTool sign /f $CertificatePath /p $CertificatePassword /t http://timestamp.digicert.com /d "SAMPLAR Setup" $InstallerPath
                
                if ($LASTEXITCODE -eq 0) {
                    Write-Host "  ✓ Installer signed successfully" -ForegroundColor Green
                } else {
                    Write-Host "  ✗ Installer signing failed!" -ForegroundColor Red
                }
            }
        }
    } else {
        Write-Host "  ✗ Installer creation failed!" -ForegroundColor Red
        exit 1
    }
} else {
    Write-Host "[5/5] Skipping installer creation (use -Installer to enable)" -ForegroundColor Gray
}

# Summary
Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Build Summary" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

$ExePath = Join-Path $OutputDir "SAMPLAR.exe"
if (Test-Path $ExePath) {
    $FileInfo = Get-Item $ExePath
    Write-Host "Executable: $ExePath" -ForegroundColor Green
    Write-Host "Size: $([math]::Round($FileInfo.Length / 1MB, 2)) MB" -ForegroundColor Green
    Write-Host "Modified: $($FileInfo.LastWriteTime)" -ForegroundColor Green
    
    # Check if signed
    $Signature = Get-AuthenticodeSignature $ExePath
    if ($Signature.Status -eq "Valid") {
        Write-Host "Code Signed: Yes ✓" -ForegroundColor Green
        Write-Host "Signer: $($Signature.SignerCertificate.Subject)" -ForegroundColor Green
    } else {
        Write-Host "Code Signed: No" -ForegroundColor Yellow
    }
}

if ($Installer -and (Test-Path $InstallerDir)) {
    $InstallerPath = Get-ChildItem $InstallerDir -Filter "Samplar_Setup_*.exe" | 
                    Select-Object -First 1
    if ($InstallerPath) {
        Write-Host ""
        Write-Host "Installer: $($InstallerPath.FullName)" -ForegroundColor Green
        Write-Host "Size: $([math]::Round($InstallerPath.Length / 1MB, 2)) MB" -ForegroundColor Green
    }
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Build completed successfully! 🎉" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Cyan

# Next steps
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Cyan
Write-Host "  1. Test the executable: $ExePath" -ForegroundColor White
if (-not $Sign) {
    Write-Host "  2. Get a code signing certificate for production release" -ForegroundColor White
}
if (-not $Installer) {
    Write-Host "  3. Create installer with: .\build_release.ps1 -Installer" -ForegroundColor White
}
Write-Host "  4. Test on a clean Windows machine" -ForegroundColor White
Write-Host "  5. Read docs/PRODUCTION_DEPLOYMENT_GUIDE.md for distribution info" -ForegroundColor White

