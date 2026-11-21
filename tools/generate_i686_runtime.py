"""Build the position-independent i686 runtime bytes with Linux GCC/binutils."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = root / "src" if (root / "src").is_dir() else root


def run(*args):
    return subprocess.check_output(args)


with tempfile.TemporaryDirectory() as tmp:
    temp = Path(tmp)
    obj = str(temp / "runtime.o")
    elf = str(temp / "runtime.elf")
    blob = str(temp / "runtime.bin")
    linker = temp / "runtime.ld"
    linker.write_text(
        "SECTIONS { . = 0; .runtime : { *(.text .text.*) "
        "*(.rodata .rodata.*) *(.got .got.*) } "
        "/DISCARD/ : { *(.comment .note* .eh_frame*) } }\n",
        encoding="ascii",
    )
    run("gcc", "-m32", "-march=i686", "-Os", "-std=c11", "-fPIC", "-ffreestanding",
        "-fno-stack-protector", "-fno-asynchronous-unwind-tables", "-fcf-protection=none",
        "-mno-sse", "-mno-mmx", "-mpreferred-stack-boundary=2",
        "-mincoming-stack-boundary=2", "-c", str(src / "i686_runtime.c"), "-o", obj)
    run("ld", "-m", "elf_i386", "-T", str(linker), "-e", "rt_len", obj, "-o", elf)
    if run("nm", "--undefined-only", elf).strip():
        raise RuntimeError("Unexpected runtime imports")
    run("objcopy", "-O", "binary", "-j", ".runtime", elf, blob)
    code = Path(blob).read_bytes()
    offsets = []
    for entry in run("nm", "--defined-only", elf).decode("ascii").splitlines():
        address, kind, name = entry.split()
        if kind.upper() == "T" and name.startswith("rt_"):
            offset = int(address, 16)
            if offset >= len(code):
                raise RuntimeError(f"Runtime entry outside the blob: {name}")
            offsets.append(f"inline constexpr uint32_t {name} = 0x{offset:x};")

header = [
    "#pragma once", "", "#include <cstdint>", "",
    "// Generated from i686_runtime.c by tools/generate_i686_runtime.py.",
    "namespace i686::runtime {", "inline constexpr uint8_t code[] = {",
]
for i in range(0, len(code), 16):
    header.append("    " + ", ".join(f"0x{byte:02x}" for byte in code[i:i + 16]) + ",")
header += ["};", "", *sorted(offsets), "", "} // namespace i686::runtime", ""]
(src / "i686_runtime_data.hpp").write_text("\n".join(header), encoding="ascii")
