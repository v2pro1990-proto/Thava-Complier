#!/bin/sh
# ==============================================================================
# Thava Compiler (thavac) - Linux / macOS Build Script
# ==============================================================================
set -e

echo "[Thava] Building thavac with g++ (C++17, -O3)..."

if ! command -v g++ >/dev/null 2>&1; then
    echo "[Error] g++ not found on the system."
    echo "Ubuntu/Debian: sudo apt update && sudo apt install -y build-essential"
    echo "Fedora:        sudo dnf groupinstall \"Development Tools\""
    echo "Arch Linux:    sudo pacman -S base-devel"
    exit 1
fi

g++ -O3 -std=c++17 src/thavac.cpp -Isrc -o thavac
chmod +x thavac

echo "[Thava] Build completed successfully: ./thavac"
