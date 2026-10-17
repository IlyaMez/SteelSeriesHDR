@echo off
if not exist "%~dp0build\ArenaHDR.exe" (
    echo Build Arena HDR first by running build.ps1 in PowerShell.
    pause
    exit /b 1
)
start "" "%~dp0build\ArenaHDR.exe"
