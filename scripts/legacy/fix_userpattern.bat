@echo off
setlocal enabledelayedexpansion

set src=B:\SANTI\SAMPLE_MONSTA\sample_monsta4\Sample_monsta5\src\utils\UserPatternDatabase_fixed.h
set dst=B:\SANTI\SAMPLE_MONSTA\sample_monsta4\Sample_monsta5\src\utils\UserPatternDatabase.h

echo Attempting to copy fixed header...
for /L %%i in (1,1,20) do (
    copy /Y "%src%" "%dst%" >nul 2>&1
    if !errorlevel! equ 0 (
        echo SUCCESS: File copied on attempt %%i
        goto :done
    )
    echo Attempt %%i failed, waiting...
    timeout /t 1 /nobreak >nul
)
echo FAILED: Could not copy file after 20 attempts
:done
