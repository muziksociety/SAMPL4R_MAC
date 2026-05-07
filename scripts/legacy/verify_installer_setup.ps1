Write-Host 'Installer Setup Verification' -ForegroundColor Cyan
Write-Host ''

if (Test-Path 'plugins\REF.png') { Write-Host '? Source image found' -ForegroundColor Green } else { Write-Host '? Source image NOT found' -ForegroundColor Red }
if (Test-Path 'Samplar.ico') { Write-Host '? Icon file found' -ForegroundColor Green } else { Write-Host '? Icon will be created' -ForegroundColor Yellow }
if (Test-Path 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe') { Write-Host '? Inno Setup installed' -ForegroundColor Green } else { Write-Host '? Inno Setup NOT installed' -ForegroundColor Red }
if (Test-Path 'installer_dependencies\vc_redist.x64.exe') { Write-Host '? VC++ Redistributable found' -ForegroundColor Green } else { Write-Host '? VC++ will be downloaded' -ForegroundColor Yellow }
Write-Host ''
Write-Host 'Run: .\build_installer_complete.ps1' -ForegroundColor Cyan
