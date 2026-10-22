---
name: Bug Report
about: Create a report to help us improve the Thava compiler, runtime, or tools.
title: "[BUG]: "
labels: bug
assignees: ''

---

## Bug Description
A clear and concise description of what the bug is.

## Minimal Reproducible Example (Thava Code)
Please provide a minimal, self-contained `.thv` code snippet that triggers the issue:

```thava
func main() {
    // Put minimal code reproducing the bug here
}
```

## Compilation Command Used
Which command and compiler flags were used to reproduce the issue?
```bash
# Example:
thavac main.thv -o main.exe
# Or with C++ transpiler fallback:
thavac main.thv --cpp -o main.exe
```

## Expected Behavior
A clear and concise description of what you expected to happen.

## Actual Behavior & Error Output
What actually happened? Please paste compiler error output, panic trace, or runtime logs:
```text
[Paste logs/errors here]
```

## Environment Information
- **OS & Architecture**: [e.g. Windows 11 x64 / Windows ARM64 / Ubuntu 22.04 LTS]
- **Compiler Backend**: [e.g. Native PE x86-64 / ARM64 / C++ Transpiler]
- **Thava Compiler Version**: [e.g. Run `thavac --version` or git commit SHA]
- **Toolchain / Dependencies (if any)**: [e.g. SDL3 3.4.18, GCC 13.2, MSVC 2022]

## Additional Context
Add any other context, screenshots, or memory dumps about the problem here.
