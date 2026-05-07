# Remove all juce::Logger::writeToLog lines from game files

$gameFiles = @(
    "src\ui\Games\BlockPuzzleGame.cpp",
    "src\ui\Games\MiniGameHost.cpp",
    "src\ui\Games\SnakeGame.cpp",
    "src\ui\Games\FlappyGame.cpp",
    "src\ui\Games\Game2048.cpp",
    "src\ui\Games\BreakoutGame.cpp",
    "src\ui\Games\SpaceShooterGame.cpp",
    "src\ui\Games\DummyGame.cpp"
)

$totalRemoved = 0

foreach ($file in $gameFiles) {
    if (Test-Path $file) {
        $content = Get-Content $file -Raw
        $lines = Get-Content $file
        
        # Count lines with logging
        $logLines = $lines | Where-Object { $_ -match 'juce::Logger::writeToLog' }
        $count = $logLines.Count
        
        if ($count -gt 0) {
            # Remove lines containing juce::Logger::writeToLog
            $newLines = $lines | Where-Object { $_ -notmatch 'juce::Logger::writeToLog' }
            $newLines | Set-Content $file -Encoding UTF8
            
            Write-Host "✓ $file : Removed $count log lines" -ForegroundColor Green
            $totalRemoved += $count
        } else {
            Write-Host "  $file : No log lines found" -ForegroundColor Gray
        }
    } else {
        Write-Host "  $file : File not found" -ForegroundColor Yellow
    }
}

Write-Host "`n✅ Total: Removed $totalRemoved log lines from game files" -ForegroundColor Cyan

