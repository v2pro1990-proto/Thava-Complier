@echo off
echo [Thava] Compiling thavac with g++ (C++17, -O3)...
g++ -O3 -std=c++17 src\thavac.cpp -Isrc -o thavac.exe
if %ERRORLEVEL% EQU 0 (
    echo [Thava] Build successful: thavac.exe
) else (
    echo [Thava] Build failed.
)
