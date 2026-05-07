# Remove large files that are still in git history
# This uses BFG Repo-Cleaner for fast history rewriting

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "REMOVING LARGE FILES FROM GIT HISTORY" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# First, let's check what large files are in the repo
Write-Host "Checking for large files in git history..." -ForegroundColor Yellow
git rev-list --objects --all | git cat-file --batch-check='%(objecttype) %(objectname) %(objectsize) %(rest)' | Where-Object { $_ -match '^blob' } | Sort-Object { [int]($_ -split '\s+')[2] } -Descending | Select-Object -First 20

Write-Host ""
Write-Host "========================================" -ForegroundColor Yellow
Write-Host "SOLUTION: Start with a fresh repository" -ForegroundColor Yellow
Write-Host "========================================" -ForegroundColor Yellow
Write-Host ""
Write-Host "The easiest solution is to create a fresh repository with only the current state:" -ForegroundColor Cyan
Write-Host ""
Write-Host "1. Remove the .git folder:" -ForegroundColor White
Write-Host "   Remove-Item -Recurse -Force .git" -ForegroundColor Gray
Write-Host ""
Write-Host "2. Initialize a new repository:" -ForegroundColor White
Write-Host "   git init" -ForegroundColor Gray
Write-Host "   git add ." -ForegroundColor Gray
Write-Host "   git commit -m 'Initial commit - SAMPLAR v2.0.0'" -ForegroundColor Gray
Write-Host ""
Write-Host "3. Force push to GitHub:" -ForegroundColor White
Write-Host "   git remote add origin https://github.com/Muziksocietyorg/Sample_monsta5.git" -ForegroundColor Gray
Write-Host "   git push --force --set-upstream origin master" -ForegroundColor Gray
Write-Host ""
Write-Host "This will create a clean repository without any large files in history." -ForegroundColor Green
Write-Host ""
Write-Host "Do you want to proceed? (This will delete git history)" -ForegroundColor Yellow
Write-Host ""

