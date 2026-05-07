Start-Sleep -Seconds 3
$src = "B:\SANTI\SAMPLE_MONSTA\sample_monsta4\Sample_monsta5\src\utils\UserPatternDatabase_NEW.h"
$dst = "B:\SANTI\SAMPLE_MONSTA\sample_monsta4\Sample_monsta5\src\utils\UserPatternDatabase.h"
try {
    Copy-Item -Path $src -Destination $dst -Force
    Write-Host "SUCCESS: File replaced"
} catch {
    Write-Host "FAILED: $($_.Exception.Message)"
}
