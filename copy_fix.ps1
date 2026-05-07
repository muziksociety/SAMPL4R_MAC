$src = "B:\SANTI\SAMPLE_MONSTA\sample_monsta4\Sample_monsta5\src\utils\UserPatternDatabase_fixed.h"
$dst = "B:\SANTI\SAMPLE_MONSTA\sample_monsta4\Sample_monsta5\src\utils\UserPatternDatabase.h"
try {
    [System.IO.File]::Copy($src, $dst, $true)
    Write-Host "SUCCESS: File copied. Size:" (Get-Item $dst).Length
} catch {
    Write-Host "FAILED:" $_.Exception.Message
}
