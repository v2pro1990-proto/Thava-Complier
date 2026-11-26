# Thava Programming Language

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)]()
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20ARM64-blue.svg)]()
[![Standard](https://img.shields.io/badge/C%2B%2B-17-orange.svg)]()
[![License](https://img.shields.io/badge/license-MIT-green.svg)]()

A systems programming language with direct PE and ELF machine code generation for x86-64, ARM64, and i686, a C++17 transpiler, and embedded-metadata dynamic libraries (`.thdl`). Native builds require no external assembler or linker.

---

## Table of Contents

- [Overview](#overview)
- [Key Features](#key-features)
- [Architecture & Dual-Backend](#architecture--dual-backend)
- [Compilation Benchmarks](#compilation-benchmarks)
- [Building the Compiler](#building-the-compiler)
- [Quick Start](#quick-start)
- [CLI Reference](#cli-reference)
- [Project Structure](#project-structure)
- [Documentation](#documentation)
- [Contributing](#contributing)
- [License](#license)

---

## Overview

Thava is designed to bridge the gap between high-level expressive syntax and low-level bare-metal execution speed. It offers a dual-backend architecture:

1. **Direct Native Machine Code Backend** emits Windows PE32/PE32+ and Linux ELF32/ELF64 executables and libraries without an external assembler or linker. A plain compiler invocation targets x86-64 on the current OS. Select `--target win-i686` or `--target linux-i686` for native i686.
2. **C++17 Transpiler Backend (`--cpp`):** Targets optimized standard C++17, enabling seamless interoperability with legacy C/C++ libraries, complex template headers, and full object-oriented classes.

---

## Key Features

- **Instant Compilation:** Emits complete standalone native executables in ~15 to 30 milliseconds.
- **Zero Toolchain Dependencies:** The native backend constructs PE and ELF headers, text/data sections, and import address tables from scratch.
- **Multi-Architecture Support** includes x86-64, ARM64, and i686 on Windows and Linux. The native i686 backend does not support C interop.
- **Dynamic Libraries** use `--lib` to build Windows `.thdl` or Linux `.so` modules. `@dlattach` reads exported signatures from embedded metadata. For native i686 exports that share classes or structs, include the same declarations on both sides with `@attach`.
- **Constants** use `<type> const name = value` for read-only variables in programs and functions.
- **C/C++ Interoperability:** Attach C headers via `@cattach <header.h>`, declare signatures with `cfunc`, or call routines dynamically using `c.function()`.
- **Object-Oriented Programming:** Classes, constructors (`func init`), encapsulated fields, `locvar` (self reference), and `parvar` (base class reference).
- **Sub-Microsecond AI Inference:** Optimized matrix operations and multi-layer neural network forward passes running purely in native machine code without Python/PyTorch runtime overhead.
- **Native Game Engine Support:** Hardware-accelerated 2D graphics and input processing through native SDL3 integration.

---

## Architecture & Dual-Backend

```
                    +--------------------+
                    |  Thava Source Code |
                    |      (*.thv)       |
                    +---------+----------+
                              |
                     [ Lexer & Parser ]
                              |
                    [ AST & Type Checker ]
                              |
                    [ AST Optimization ]
                              |
             +----------------+----------------+
             |                                 |
   [ Native Backend ]                  [ C++ Transpiler ]
   (Zero Dependencies)                    (With --cpp)
             |                                 |
   x86-64 / ARM64 / i686                g++ / clang++ / cl
       PE and ELF                             |
             |                                 |
             +----------------+----------------+
                              |
                    +---------v----------+
                    | Standalone Binary  |
                    | (.exe / ELF / .so) |
                    +--------------------+
```

---

## Compilation Benchmarks

Measured on an Intel Core i7 system compiling a standard 500-line systems utility:

| Compiler / Toolchain | Build Mode | Compilation Time | External Dependencies | Output Binary Size |
| :--- | :--- | :--- | :--- | :--- |
| **Thava Native Backend** | Direct PE Machine Code | **18 ms** | **None (Zero)** | **~8 KB** |
| Thava C++ Transpiler | g++ -O0 | 480 ms | MinGW-w64 | ~120 KB |
| Thava C++ Transpiler | g++ -O3 -march=native -s | 1,420 ms | MinGW-w64 | ~45 KB |
| Standard Clang++ | clang++ -O2 | 1,650 ms | LLVM Toolchain | ~140 KB |

---

## Building the Compiler

### Prerequisites

A modern C++17 compiler (GCC 9+, Clang 10+, or MSVC 2019+).

### Windows (MinGW-w64)

```cmd
clbuild.bat
```

Or manually:

```cmd
g++ -O3 -std=c++17 src\thavac.cpp -Isrc -o thavac.exe
```

### Linux

```bash
make
sudo make install
```

Or using the build script:

```bash
chmod +x build.sh
./build.sh
```

---

## Quick Start

Download the compiler for your operating system and CPU from [GitHub Releases](https://github.com/v2pro1990-proto/Thava-Complier/releases).

### 1. Hello World (`hello.thv`)

```thava
func main() {
    <str> target = "World"
    cons.out("Hello, @{target} from Thava!")
}

main()
```

Compile and run on Windows x86-64.

```powershell
.\thavac.exe hello.thv --target win-x64 -o hello.exe
.\hello.exe
```

Compile and run on Linux x86-64.

```sh
./thavac hello.thv --target linux-x64 -o hello
./hello
```

### 2. Math & Control Flow

```thava
<int> func factorial(<int> n) {
    if n <= 1 {
        return 1
    }
    return n * factorial(n - 1)
}

func main() {
    for <int> i in range(6) {
        cons.out("factorial(@{i}) = @{factorial(i)}")
    }
}

main()
```

### 3. Dynamic Libraries (`.thdl` and `.so`)

Create `math_lib.thv`:

```thava
<dbl> func square(<dbl> x) {
    return x * x
}
```

Save this consumer as `main.thv`. The extensionless name lets the compiler select the library for the target OS.

```thava
@dlattach "math_lib"

func main() {
    <dbl> result = square(9.0)
    cons.out("Square of 9.0 is: @{result}")
}

main()
```

Build the library and consumer for the same target. On Windows x86-64, keep the library beside the executable.

```powershell
.\thavac.exe math_lib.thv --target win-x64 --lib -o math_lib.thdl
.\thavac.exe main.thv --target win-x64 -o main.exe
.\main.exe
```

On Linux x86-64, run the following commands from the directory containing both sources.

```sh
./thavac math_lib.thv --target linux-x64 --lib -o math_lib.so
./thavac main.thv --target linux-x64 -o main
./main
```

### 4. Linux i686 on iSH

Extract the Linux i686 archive and keep `build-ish.sh` next to `thavac-linux-i686`. Run the included smoke test from that directory.

```sh
sh ./build-ish.sh --i686_ish.thv -o test-ish
./test-ish
```

---

## CLI Reference

```
Usage: thavac <input.thv> [options]
```

| Flag | Description |
| :--- | :--- |
| `-o <filename>` | Specify the output executable or library filename. |
| `--native` | Enforce direct machine code generation. |
| `--cpp` | Use the C++17 transpiler backend (auto-detects g++, clang++, or cl). |
| `--cxx <compiler>` | Specify custom host C++ compiler command or path (also honors CXX env var). |
| `--lib` | Build as a shared dynamic library (`.thdl` on Windows, `.so` on Linux). |
| `--info <file>` | Inspect metadata and exported signatures in a `.thdl` or `.so` file. |
| `-O0`, `-O1`, `-O2`, `-O3` | Select an [optimization level](docs/DOCUMENTATION.md#optimization-levels--o0-to--o3). |
| `--target win-x64`, `--target linux-x64` | Select Windows or Linux x86-64. |
| `--target win-arm64`, `--target linux-arm64` | Select Windows or Linux ARM64. |
| `--target win-i686`, `--target linux-i686` | Select native Windows or Linux i686. Add `--cpp` to use the C++ toolchain. |

---

## Project Structure

```
Thava_Complier/
|-- src/
|   |-- thavac.cpp             # Main compiler entry point, lexer, parser, type checker
|   |-- native_codegen.hpp     # Direct x86-64 machine code generator
|   |-- x86_encoder.hpp        # x86-64 instruction encoder and fixup resolver
|   |-- pe_emitter.hpp         # Windows PE layout, imports and exports
|   |-- arm64_codegen.hpp      # ARM64 direct code generator
|   |-- arm64_encoder.hpp      # AArch64 instruction encoder
|   |-- elf_emitter.hpp        # Linux ELF64 binary layout builder
|   |-- elf_arm64_emitter.hpp  # Linux ELF ARM64 binary builder
|   |-- i686_codegen.hpp       # Native Windows and Linux i686 code generation
|   |-- elf_i686_emitter.hpp   # Linux ELF32 binary builder
|   |-- ast_optimizer.hpp      # Intermediate AST optimization passes
|   |-- linux_runtime.hpp      # Bare-metal Linux runtime routines
|-- docs/
|   +-- DOCUMENTATION.md       # Comprehensive language reference manual
|-- examples/
|   |-- 01_hello_world.thv     # Basic I/O and string interpolation
|   |-- 02_types_and_operators.thv # Arithmetic, logic, compound assignments
|   |-- 03_control_flow.thv    # If-elif-else, while, range-for loops
|   |-- 04_collections.thv     # Lists, tuples, indexing, and slicing
|   |-- 05_functions.thv       # Recursion and typed function signatures
|   |-- 06_oop_classes.thv     # Classes, fields, constructors, locvar
|   +-- 07_dynamic_library/    # Self-contained dynamic library demo (.thdl)
|-- Makefile                   # Unix build configuration
|-- build.sh                   # Linux compiler build script
|-- build-ish.sh               # Linux i686 compiler launcher for iSH
|-- clbuild.bat                # Windows batch builder
|-- README.md                  # Project documentation
+-- LICENSE                    # MIT License
```

---

## Documentation

Full language documentation, syntax specifications, module attachments, and C interop guidelines are located in [docs/DOCUMENTATION.md](docs/DOCUMENTATION.md).

---

## Contributing

Contributions are welcome. Please check [open issues](https://github.com/v2pro1990-proto/Thava-Complier/issues) before submitting pull requests.

1. Fork the repository.
2. Create your feature branch (`git checkout -b feature/amazing-feature`).
3. Commit your changes (`git commit -m 'Add amazing feature'`).
4. Push to the branch (`git push origin feature/amazing-feature`).
5. Open a Pull Request.

---

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.
