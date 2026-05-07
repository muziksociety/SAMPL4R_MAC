# Build script for SAMPLAR
Write-Host "Building SAMPLAR..." -ForegroundColor Cyan

$buildCommand = "cmake --build build --config Release -j 16"
Write-Host "Running: $buildCommand" -ForegroundColor Yellow

# Execute the build command
& cmake --build build --config Release -j 16

# Check the exit code
if ($LASTEXITCODE -eq 0) {
    Write-Host "`nBuild completed successfully!" -ForegroundColor Green
} else {
    Write-Host "`nBuild failed with exit code: $LASTEXITCODE" -ForegroundColor Red
}

exit $LASTEXITCODE

