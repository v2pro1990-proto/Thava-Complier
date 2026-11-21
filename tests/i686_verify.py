"""Verify the 32-bit target format and run an executable built by Thava."""
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


compiler = Path(sys.argv[1]).resolve()
source = Path(__file__).with_name("i686.thv").resolve()
windows = os.name == "nt"
arm64 = "--windows-arm64" in sys.argv[2:]
target = "win-arm64" if arm64 else ("win-i686" if windows else "linux-i686")

with tempfile.TemporaryDirectory() as tmp:
    output = Path(tmp) / ("smoke.exe" if windows else "smoke")
    backend = ["--cpp"] if arm64 else []
    subprocess.run([str(compiler), str(source), "--target", target, *backend,
                    "-O2", "-o", str(output)], cwd=tmp, check=True)
    data = output.read_bytes()
    if windows:
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        assert data[:2] == b"MZ" and data[pe:pe + 4] == b"PE\0\0"
        assert struct.unpack_from("<H", data, pe + 4)[0] == (0xAA64 if arm64 else 0x14C)
        assert struct.unpack_from("<H", data, pe + 24)[0] == (0x20B if arm64 else 0x10B)
    else:
        assert data[:6] == b"\x7fELF\x01\x01"
        assert struct.unpack_from("<H", data, 18)[0] == 3
    result = subprocess.run([str(output)], cwd=tmp, check=True,
                            stdout=subprocess.PIPE, encoding="utf-8")
    lines = [line for line in result.stdout.splitlines() if line and not line.startswith("[Thava Native]")]
    assert lines == ["5000000006 2.5 TRỜI ĐẸP"], result.stdout
    print(target, "executable format and runtime output OK")

    if not arm64:
        result = subprocess.run([str(compiler), str(source), "--target", target, "--native",
                                 "-o", str(output)], cwd=tmp, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
        assert result.returncode != 0 and "i686 currently requires" in result.stdout, result.stdout
