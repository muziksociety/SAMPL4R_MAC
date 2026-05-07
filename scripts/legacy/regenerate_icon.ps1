# Regenerate samplar.ico from REF.png
# This creates a proper multi-resolution Windows icon file

$ErrorActionPreference = "Stop"

$sourcePng = "plugins\REF.png"
$outputIco = "samplar.ico"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  SAMPLAR - Icon Regenerator" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Check if source image exists
if (-not (Test-Path $sourcePng)) {
    Write-Host "[ERROR] Source image not found: $sourcePng" -ForegroundColor Red
    exit 1
}

Write-Host "[OK] Found source image: $sourcePng" -ForegroundColor Green

# Load System.Drawing assembly
Add-Type -AssemblyName System.Drawing

# Load the source image
$sourceImage = [System.Drawing.Image]::FromFile((Resolve-Path $sourcePng).Path)
Write-Host "[OK] Loaded source image: $($sourceImage.Width)x$($sourceImage.Height)" -ForegroundColor Green

# Create icon with multiple resolutions
$sizes = @(256, 128, 64, 48, 32, 16)
$iconStream = New-Object System.IO.MemoryStream

# Write ICO header
$iconWriter = New-Object System.IO.BinaryWriter($iconStream)
$iconWriter.Write([UInt16]0)  # Reserved (must be 0)
$iconWriter.Write([UInt16]1)  # Type (1 = ICO)
$iconWriter.Write([UInt16]$sizes.Count)  # Number of images

# Calculate offsets
$headerSize = 6 + ($sizes.Count * 16)  # ICO header + directory entries
$offset = $headerSize

# Write directory entries
$imageDataList = @()
foreach ($size in $sizes) {
    # Create resized bitmap
    $bitmap = New-Object System.Drawing.Bitmap($size, $size)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.DrawImage($sourceImage, 0, 0, $size, $size)
    $graphics.Dispose()
    
    # Convert to PNG
    $pngStream = New-Object System.IO.MemoryStream
    $bitmap.Save($pngStream, [System.Drawing.Imaging.ImageFormat]::Png)
    $pngData = $pngStream.ToArray()
    $pngStream.Dispose()
    $bitmap.Dispose()
    
    # Store image data
    $imageDataList += $pngData
    
    # Write directory entry
    # In ICO format, 256 is represented as 0
    $width = if ($size -eq 256) { 0 } else { $size }
    $height = if ($size -eq 256) { 0 } else { $size }
    $iconWriter.Write([Byte]$width)  # Width (0 = 256)
    $iconWriter.Write([Byte]$height)  # Height (0 = 256)
    $iconWriter.Write([Byte]0)      # Color palette
    $iconWriter.Write([Byte]0)      # Reserved
    $iconWriter.Write([UInt16]1)    # Color planes
    $iconWriter.Write([UInt16]32)   # Bits per pixel
    $iconWriter.Write([UInt32]$pngData.Length)  # Size of image data
    $iconWriter.Write([UInt32]$offset)  # Offset to image data
    
    $offset += $pngData.Length
}

# Write image data
foreach ($imageData in $imageDataList) {
    $iconWriter.Write($imageData)
}

# Save to file
$iconWriter.Flush()
$iconBytes = $iconStream.ToArray()
[System.IO.File]::WriteAllBytes((Resolve-Path ".").Path + "\$outputIco", $iconBytes)

# Cleanup
$iconWriter.Dispose()
$iconStream.Dispose()
$sourceImage.Dispose()

$iconSize = (Get-Item $outputIco).Length / 1KB
Write-Host "[OK] Icon created: $outputIco ($([math]::Round($iconSize, 2)) KB)" -ForegroundColor Green
Write-Host "  Resolutions: 256x256, 128x128, 64x64, 48x48, 32x32, 16x16" -ForegroundColor Gray
Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "  Icon regeneration complete!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green

