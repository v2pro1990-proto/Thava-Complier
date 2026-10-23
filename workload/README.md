# Thava Visual Studio Workload & SDK

Official Visual Studio IDE integration and MSBuild SDK for the **Thava Programming Language** (`.thv`, `.thvh`, `.thdl`). Supports Visual Studio 2022 (`17.x`) and Visual Studio 2026 (`18.x`).

---

## Features

1. **Syntax Highlighting & Editor Support (`.thv`, `.thvh`):**
   - Full TextMate grammar (`source.thava`) highlighting directives (`@attach`, `@cattach`, `@dlattach`), static type tags (`<int>`, `<fl>`, `<dbl>`, `<str>`, `<lst>`, `<dict>`, `<bl>`, `<void>`), control flow keywords, built-in namespaces (`cons.out`, `key.input`, `file.open`, `os`, `time`, `clib`), and string interpolation (`{...}`).
   - Automatic bracket, quote, and type-tag (`<>`) closing, block comment toggling (`Ctrl+K, Ctrl+C`), and smart indentation.

2. **Code Snippets & IntelliSense:**
   - Built-in TextMate `.tmSnippet` expansions (`func`, `class`, `for`, `try`, `raise`, `fopen`, `cout`) and Visual Studio Code Snippets (`tfunc`, `tclass`, `tfor`, `ttry`, `traise`, `tfopen`).

3. **Visual Studio Project & Item Templates:**
   - **Thava Console Application:** Pre-configured `.vcxproj` for `x64` and `ARM64` (`Debug` and `Release`) that builds standalone native executables (`.exe`).
   - **Thava Dynamic Library (`.thdl`):** Pre-configured project that builds `.thdl` dynamic libraries with embedded symbol metadata for `@dlattach`.
   - **Item Templates:** Add new `Thava Source File (.thv)` and `Thava Header Module (.thvh)` items directly from `Add -> New Item`.

4. **MSBuild Build Customization (`Thava.props`, `Thava.targets`, `Thava.xml`):**
   - Press `Ctrl+Shift+B` to compile via `thavac.exe` and `F5` / `Ctrl+F5` to launch and debug.
   - Dedicated **Project Properties -> Configuration Properties -> Thava Compiler** UI page to switch between:
     - **Target Backend:** `Native Machine Code (--native)`, `C++17 Transpiler (--cpp)`, `Windows ARM64 (--target win-arm64)`, `Linux ELF64 (--backend elf)`, `Linux ARM64 (--arm64)`.
     - **Optimization Level:** `-O0`, `-O1`, `-O2`, `-O3`.
     - **Output Artifact Type:** `Executable Application (.exe)` or `Thava Dynamic Library (.thdl)`.
     - **Host C++ Compiler:** `Auto-Detect`, `g++`, `clang++`, `cl.exe`.
   - Automatic **Error List** integration: Lexer, Parser, and Static Type errors (`THV1001`, `THV2001`, `THV3001`, `THV4001`) are mapped to clickable file and line locations in Visual Studio's Error List window.

---

## Installation

### Option 1: One-Click Workload & SDK Installer (Recommended)

Run `Install-ThavaWorkload.bat` (or `Install-ThavaWorkload.ps1` in PowerShell):

```cmd
cd workload
Install-ThavaWorkload.bat
```

This installer automatically:
- Installs `thavac.exe` and MSBuild targets into `%LOCALAPPDATA%\Programs\ThavaSDK`.
- Sets `THAVA_SDK_DIR` and adds `%LOCALAPPDATA%\Programs\ThavaSDK\bin` to the user `PATH`.
- Installs TextMate syntax highlighting and snippets into `%USERPROFILE%\.vs\Extensions\thava`.
- Installs Project Templates and Item Templates into Visual Studio 2022 and Visual Studio 2026 (`18.0`).
- Packages `dist\Thava.VisualStudio.vsix`.

### Option 2: VSIX Extension Package

After running `Install-ThavaWorkload.bat` (or `powershell -File Install-ThavaWorkload.ps1 -PackageOnly`), double-click:

```text
workload\dist\Thava.VisualStudio.vsix
```

### Uninstallation

Run `Uninstall-ThavaWorkload.bat` to remove the SDK, templates, and editor extensions.
