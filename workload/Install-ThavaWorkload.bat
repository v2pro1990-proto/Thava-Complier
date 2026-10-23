@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-ThavaWorkload.ps1" %*
if "%1"=="" (
    echo.
    pause
)
