@echo off
:: Self-elevating batch file — re-launches as Administrator if not already.
net session >nul 2>&1
if %errorLevel% == 0 goto :run

echo Requesting Administrator privileges...
powershell -Command "Start-Process cmd -ArgumentList '/c \"%~f0\"' -Verb RunAs"
exit /b

:run
powershell -ExecutionPolicy Bypass -File "%~dp0apply_patch.ps1"
pause
