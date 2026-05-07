# Create a fresh git repository without large files in history
# This is the cleanest solution for pushing to GitHub

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "CREATING FRESH GIT REPOSITORY" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Backup the current .git folder (just in case)
Write-Host "Backing up current .git folder..." -ForegroundColor Yellow
if (Test-Path ".git") {
    if (Test-Path ".git_backup") {
        Remove-Item -Recurse -Force ".git_backup"
    }
    Copy-Item -Recurse ".git" ".git_backup"
    Write-Host "  ✓ Backed up to .git_backup" -ForegroundColor Green
}
Write-Host ""

# Remove the .git folder
Write-Host "Removing .git folder..." -ForegroundColor Yellow
if (Test-Path ".git") {
    Remove-Item -Recurse -Force ".git"
    Write-Host "  ✓ Removed .git folder" -ForegroundColor Green
}
Write-Host ""

# Initialize a new repository
Write-Host "Initializing new git repository..." -ForegroundColor Yellow
git init
git branch -M master
Write-Host "  ✓ Initialized new repository" -ForegroundColor Green
Write-Host ""

# Add all files (respecting .gitignore)
Write-Host "Adding files (respecting .gitignore)..." -ForegroundColor Yellow
git add .
Write-Host "  ✓ Files added" -ForegroundColor Green
Write-Host ""

# Show what will be committed
Write-Host "Files to be committed:" -ForegroundColor Cyan
git status --short
Write-Host ""

# Commit
Write-Host "Creating initial commit..." -ForegroundColor Yellow
git commit -m "Initial commit - SAMPLAR v2.0.0

- Complete SAMPLAR application source code
- UVR-optimized demucs.cpp (3x faster processing)
- Fixed 'Select All' bug in sample browser
- Full JUCE-based UI with step sequencer, wave editor, and mini-games
- Documentation and build scripts included

Version: 2.0.0
Build Date: October 2025"

Write-Host "  ✓ Initial commit created" -ForegroundColor Green
Write-Host ""

# Add remote
Write-Host "Adding GitHub remote..." -ForegroundColor Yellow
git remote add origin https://github.com/Muziksocietyorg/Sample_monsta5.git
Write-Host "  ✓ Remote added" -ForegroundColor Green
Write-Host ""

Write-Host "========================================" -ForegroundColor Green
Write-Host "FRESH REPOSITORY CREATED!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "Now push to GitHub with:" -ForegroundColor Yellow
Write-Host "  git push --force --set-upstream origin master" -ForegroundColor Cyan
Write-Host ""
Write-Host "Note: This will overwrite the remote repository!" -ForegroundColor Red
Write-Host ""

