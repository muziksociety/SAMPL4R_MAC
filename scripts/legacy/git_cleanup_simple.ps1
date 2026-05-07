# Simple Git Cleanup Script
Write-Host "Removing large files from git tracking..." -ForegroundColor Cyan

# Remove build directory
Write-Host "Removing build/..." -ForegroundColor Yellow
git rm -r --cached build/

# Remove portable folder
Write-Host "Removing Samplar_Portable/..." -ForegroundColor Yellow
git rm -r --cached Samplar_Portable/

# Remove installer output
Write-Host "Removing installer_output/..." -ForegroundColor Yellow
git rm -r --cached installer_output/

# Remove large files
Write-Host "Removing large files..." -ForegroundColor Yellow
git rm --cached Samplar_Portable.rar
git rm --cached samplar.ico
git rm --cached logs/SAMPLAR_console.log
git rm --cached SAMPLAR_debug.log

Write-Host ""
Write-Host "Done! Now run:" -ForegroundColor Green
Write-Host "  git add .gitignore" -ForegroundColor White
Write-Host "  git commit -m 'Add .gitignore and remove large files'" -ForegroundColor White
Write-Host "  git push" -ForegroundColor White

