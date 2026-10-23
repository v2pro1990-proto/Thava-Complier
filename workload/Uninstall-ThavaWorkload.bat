@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-ThavaWorkload.ps1" -Uninstall
if "%1"=="" (
    echo.
    pause
)
