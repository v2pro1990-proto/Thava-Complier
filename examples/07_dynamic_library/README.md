# Dynamic Library Example

The consumer imports `math_plugin` by name. Add the compiler to your `PATH`, choose a target for both builds, and run the commands from this directory.

## Windows x86-64

Build a `.thdl` and keep it beside the executable.

```powershell
thavac.exe math_plugin.thv --target win-x64 --lib -o math_plugin.thdl
thavac.exe --info math_plugin.thdl
thavac.exe use_plugin.thv --target win-x64 -o use_plugin.exe
.\use_plugin.exe
```

## Linux x86-64

Build a `.so` and leave the library available at runtime.

```sh
thavac math_plugin.thv --target linux-x64 --lib -o math_plugin.so
thavac --info math_plugin.so
thavac use_plugin.thv --target linux-x64 -o use_plugin
./use_plugin
```

For other architectures, use the same `--target` on both build commands and the library extension for that OS. Add `--native` on both commands for i686.
