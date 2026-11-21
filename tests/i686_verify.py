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
        fixture = source.with_name("i686_native.thv")
        # Keep Windows PATH for thavac's own DLLs, but pass an invalid --cxx.
        env = dict(os.environ)
        if not windows:
            env["PATH"] = ""
        env.pop("CXX", None)
        for switches, opt in [
            (["--target", target, "--native"], "-O0"),
            (["--backend", "pe-i686" if windows else "elf-i686"], "-O2"),
        ]:
            native = Path(tmp) / ("native" + opt + (".exe" if windows else ""))
            build = subprocess.run([str(compiler), str(fixture), *switches, opt, "--cxx",
                                    "missing-compiler", "-o", str(native)], cwd=tmp, env=env,
                                   check=True, stdout=subprocess.PIPE, text=True, timeout=30)
            assert "Success! Native i686" in build.stdout, build.stdout
            data = native.read_bytes()
            if windows:
                pe = struct.unpack_from("<I", data, 0x3C)[0]
                assert data[:2] == b"MZ" and data[pe:pe + 6] == b"PE\0\0\x4c\x01"
                assert struct.unpack_from("<H", data, pe + 24)[0] == 0x10B
                assert b"kernel32.dll\0" in data and b"msvcrt.dll\0" not in data
            else:
                assert data[:6] == b"\x7fELF\x01\x01"
                assert struct.unpack_from("<H", data, 18)[0] == 3
                phoff = struct.unpack_from("<I", data, 28)[0]
                phsize, phcount = struct.unpack_from("<HH", data, 42)
                assert all(struct.unpack_from("<I", data, phoff + phsize * i)[0] not in (2, 3)
                           for i in range(phcount))  # No PT_DYNAMIC or PT_INTERP
            result = subprocess.run([str(native)], cwd=tmp, check=True,
                                    stdout=subprocess.PIPE, encoding="utf-8", timeout=10)
            assert result.stdout.splitlines() == [
                "5000000008 -9223372036854775808 -15000000000 -555555555 -8 2 2000000000",
                "8 -1 1099511627776 -1 true false true trời đẹp",
            ], result.stdout
        print(target, "native code generation and runtime output OK")

        rejected = Path(tmp) / ("unsupported.exe" if windows else "unsupported")
        result = subprocess.run([str(compiler), str(source), "--target", target, "--native",
                                 "-o", str(rejected)], cwd=tmp, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
        assert result.returncode != 0 and "native i686 backend yet" in result.stdout, result.stdout
        assert not rejected.exists()
