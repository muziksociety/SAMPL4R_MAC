# Replace all emojis with ASCII equivalents in SAMPLAR codebase
# Preserves special symbols: → ± ≈ °

$replacements = @{
    '🔥' = '[TRACE]'
    '🚨' = '[CRASH]'
    '💥' = '[ERROR]'
    '📍' = '[ADDR]'
    '🎵' = '[AUDIO]'
    '🔌' = '[DEVICE]'
    '✅' = '[OK]'
    '🔍' = '[SCAN]'
    '🔬' = '[ANALYZE]'
    '🎯' = '[TARGET]'
    '🛡️' = '[SHIELD]'
    '🔊' = '[VOLUME]'
    '🎨' = '[CREATIVE]'
    '⚠️' = '[WARNING]'
    '⚡' = '[FAST]'
    '❌' = '[FAILED]'
    '🐍' = '[PYTHON]'
    '🎬' = '[ANIM]'
    '🥁' = '[DRUM]'
    '🎧' = '[PLAY]'
    '📊' = '[CHART]'
    '🔄' = '[REFRESH]'
    'ℹ️' = '[INFO]'
    '📡' = '[SIGNAL]'
    '🔧' = '[TOOL]'
    '⭐' = '[STAR]'
    '🍎' = 'Apple'
    '🫐' = 'Berry'
    '🍒' = 'Cherry'
    '🍯' = 'Honey'
    '🚀' = '[SPEED]'
    '💰' = '[x2]'
    '💪' = '[GROWTH]'
    '🐍' = 'Snake'
    '🌱' = 'Garden'
    '🎉' = ''
    '💀' = ''
    '⏸' = '[PAUSED]'
    '⏳' = '[WAIT]'
}

$files = Get-ChildItem -Path src -Recurse -Include *.cpp,*.h

foreach ($file in $files) {
    $content = Get-Content $file.FullName -Raw -Encoding UTF8
    $modified = $false
    
    foreach ($emoji in $replacements.Keys) {
        if ($content -match [regex]::Escape($emoji)) {
            $content = $content -replace [regex]::Escape($emoji), $replacements[$emoji]
            $modified = $true
        }
    }
    
    if ($modified) {
        Write-Host "Replacing emojis in: $($file.FullName)"
        [System.IO.File]::WriteAllText($file.FullName, $content, [System.Text.UTF8Encoding]::new($false))
    }
}

Write-Host "Emoji replacement complete!"

