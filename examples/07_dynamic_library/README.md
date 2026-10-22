# Dynamic Library Example (.thdl)

This example demonstrates how to build and link dynamic libraries in Thava.

## 1. Build the Dynamic Library

Compile `math_plugin.thv` into a `.thdl` (Windows dynamic library with embedded symbol metadata):

```bash
thavac math_plugin.thv -o math_plugin.thdl --lib
```

To inspect exported function signatures embedded in the library:

```bash
thavac --info math_plugin.thdl
```

## 2. Compile and Run the Consumer Application

Compile `use_plugin.thv` which links to `math_plugin.thdl` via `@dlattach "math_plugin"`:

```bash
thavac use_plugin.thv -o use_plugin.exe
./use_plugin.exe
```
