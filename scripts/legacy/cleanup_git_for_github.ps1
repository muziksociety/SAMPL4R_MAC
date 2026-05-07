# Git Cleanup Script for GitHub Push
# This script removes large files from git tracking while keeping them locally

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Git Cleanup for GitHub Push" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Remove entire build directory from git tracking
Write-Host "Removing build/ directory from git tracking..." -ForegroundColor Yellow
git rm -r --cached build/ 2>$null
if ($LASTEXITCODE -eq 0) {
    Write-Host "✓ build/ removed from git tracking" -ForegroundColor Green
} else {
    Write-Host "⚠ build/ not tracked or already removed" -ForegroundColor Gray
}

# Remove portable folder from git tracking
Write-Host "Removing Samplar_Portable/ from git tracking..." -ForegroundColor Yellow
git rm -r --cached Samplar_Portable/ 2>$null
if ($LASTEXITCODE -eq 0) {
    Write-Host "✓ Samplar_Portable/ removed from git tracking" -ForegroundColor Green
} else {
    Write-Host "⚠ Samplar_Portable/ not tracked or already removed" -ForegroundColor Gray
}

# Remove installer output from git tracking
Write-Host "Removing installer_output/ from git tracking..." -ForegroundColor Yellow
git rm -r --cached installer_output/ 2>$null
if ($LASTEXITCODE -eq 0) {
    Write-Host "✓ installer_output/ removed from git tracking" -ForegroundColor Green
} else {
    Write-Host "⚠ installer_output/ not tracked or already removed" -ForegroundColor Gray
}

# Remove specific large files
Write-Host "Removing large binary files from git tracking..." -ForegroundColor Yellow

git rm --cached "Samplar_Portable.rar" 2>$null
git rm --cached "installer_output/Samplar_Setup_v1.0.0.exe" 2>$null
git rm --cached "samplar.ico" 2>$null

# Remove log files
Write-Host "Removing log files from git tracking..." -ForegroundColor Yellow
git rm --cached logs/SAMPLAR_console.log 2>$null
git rm --cached SAMPLAR_debug.log 2>$null

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Cleanup Complete!" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Yellow
Write-Host "1. Review changes: git status" -ForegroundColor White
Write-Host "2. Commit changes: git commit -m 'Remove large files from git tracking'" -ForegroundColor White
Write-Host "3. Push to GitHub: git push" -ForegroundColor White
Write-Host ""
Write-Host "Note: Files are still on your disk, just not tracked by git anymore." -ForegroundColor Gray

