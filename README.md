# Thava Programming Language

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)]()
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20ARM64-blue.svg)]()
[![Standard](https://img.shields.io/badge/C%2B%2B-17-orange.svg)]()
[![License](https://img.shields.io/badge/license-MIT-green.svg)]()

A blazing-fast, dual-backend systems programming language featuring direct Native Machine Code emission (x86-64 & ARM64 PE/ELF) with zero toolchain dependencies, seamless C++ interop, embedded-metadata dynamic libraries (`.thdl`), and sub-microsecond AI inference.

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

1. **Direct Native Machine Code Backend (Default):** Directly emits Windows Portable Executable (PE32+ `.exe`, `.thdl`) and Linux ELF64 binary files in milliseconds. It requires **no external assembler or linker** (no GCC, Clang, NASM, or MSVC SDK needed).
2. **C++17 Transpiler Backend (`--cpp`):** Targets optimized standard C++17, enabling seamless interoperability with legacy C/C++ libraries, complex template headers, and full object-oriented classes.

---

## Key Features

- **Instant Compilation:** Emits complete standalone native executables in ~15 to 30 milliseconds.
- **Zero Toolchain Dependencies:** The native backend constructs PE and ELF headers, text/data sections, and import address tables from scratch.
- **Multi-Architecture Support:** Supports x86-64 (Windows PE, Linux ELF) and ARM64 (AArch64 Linux & Windows ARM experimental).
- **Embedded-Metadata Dynamic Libraries (`.thdl`):** Compile modular libraries with `--lib`. Exported function signatures and types are embedded directly inside binary metadata sections. Consumers import them via `@dlattach` with zero header files required.
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
                    [ AST Optimizaton ]
                              |
             +----------------+----------------+
             |                                 |
   [ Native Backend ]                  [ C++ Transpiler ]
   (Zero Dependencies)                    (With --cpp)
             |                                 |
  +----------+----------+                      |
  |                     |                      |
[x86-64 Emitter]  [ARM64 Emitter]       [g++ / clang++ / cl]
(Windows PE32+ /  (ELF64 / Win ARM)     (-O3 -march=native)
  Linux ELF64)          |                      |
  +----------+----------+                      |
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

### Linux / macOS

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

### 1. Hello World (`hello.thv`)

```thava
func main() {
    <str> target = "World"
    cons.out("Hello, @{target} from Thava!")
}

main()
```

Compile directly to a native binary:

```bash
thavac hello.thv -o hello.exe
./hello.exe
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

### 3. Dynamic Libraries (`.thdl`)

Create `math_lib.thv`:

```thava
<dbl> func square(<dbl> x) {
    return x * x
}
```

Build the dynamic library with embedded metadata:

```bash
thavac math_lib.thv -o math_lib.thdl --lib
```

Consume the library from another file without header files:

```thava
@dlattach "math_lib"

func main() {
    <dbl> result = square(9.0)
    cons.out("Square of 9.0 is: @{result}")
}

main()
```

Compile and run:

```bash
thavac main.thv -o main.exe
./main.exe
```

---

## CLI Reference

```
Usage: thavac <input.thv> [options]
```

| Flag | Description |
| :--- | :--- |
| `-o <filename>` | Specify the output executable or library filename. |
| `--native` | Enforce direct machine code generation (PE32+ / ELF64). |
| `--cpp` | Use the C++17 transpiler backend (invokes host C++ compiler). |
| `--lib` | Build as a shared dynamic library (`.thdl` on Windows, `.so` on Linux). |
| `--info <file.thdl>` | Inspect embedded metadata and exported function signatures. |
| `-O0`, `-O1`, `-O2`, `-O3` | Set optimization level (dead code elimination, register caching, peephole). |
| `-s` | Strip debug symbols from output binary. |
| `--arm64` | Target ARM64 architecture (AArch64). |
| `--version` | Display compiler version and target information. |
| `--help` | Display command-line usage information. |

---

## Project Structure

```
Thava_Complier/
|-- src/
|   |-- thavac.cpp             # Main compiler entry point, lexer, parser, type checker
|   |-- native_codegen.hpp     # Direct x86-64 machine code generator
|   |-- x86_encoder.hpp        # x86-64 instruction encoder and fixup resolver
|   |-- pe_emitter.hpp         # Windows PE32+ binary layout and IAT builder
|   |-- arm64_codegen.hpp      # ARM64 direct code generator
|   |-- arm64_encoder.hpp      # AArch64 instruction encoder
|   |-- elf_emitter.hpp        # Linux ELF64 binary layout builder
|   |-- elf_arm64_emitter.hpp  # Linux ELF ARM64 binary builder
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
|-- build.sh                   # Linux / macOS shell builder
|-- clbuild.bat                # Windows batch builder
|-- README.md                  # Project documentation
+-- LICENSE                    # MIT License
```

---

## Documentation

Full language documentation, syntax specifications, module attachments, and C interop guidelines are located in [docs/DOCUMENTATION.md](docs/DOCUMENTATION.md).

---

## Contributing

Contributions are welcome. Please check [open issues](https://github.com/) before submitting pull requests.

1. Fork the repository.
2. Create your feature branch (`git checkout -b feature/amazing-feature`).
3. Commit your changes (`git commit -m 'Add amazing feature'`).
4. Push to the branch (`git push origin feature/amazing-feature`).
5. Open a Pull Request.

---

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.
