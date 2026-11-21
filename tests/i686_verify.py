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

        unicode_input = "HÔM NAY TRỜI ĐẸP QUÁ\nhôm nay trời đẹp quá\n"
        for filename, expected, input_data in [
            ("i686_scalars.thv", [
                "3.75 4.5 -4 true -4.5",
                "hôm nay trời mát êêêßß true",
                "1 -1 3 true true 6",
                "Ờ TRỜI 81 5000000000 -0.125 true -12345",
            ], "5000000000\n-12.5e-2\nYES\n"),
            ("unicode_case.thv", [
                "hôm nay trời đẹp quá", "HÔM NAY TRỜI ĐẸP QUÁ", "Hôm Nay Trời Đẹp Quá",
                "SSTRAẞE\u00a0ÉLAN\tΩΜΈΓΑ 😀 𐐀", "ßtraße\u00a0élan\tωμέγα 😀 𐐨",
                "Sstraße\u00a0Élan\tΩμέγα 😀 𐐀", "A\u0302\u0301", "ab",
                "hôm nay trời đẹp quá", "HÔM NAY TRỜI ĐẸP QUÁ",
            ], unicode_input),
        ]:
            test_source = source.with_name(filename)
            native = Path(tmp) / (test_source.stem + (".exe" if windows else ""))
            subprocess.run([str(compiler), str(test_source), "--target", target, "--native",
                            "-O0", "-o", str(native)], cwd=tmp, env=env, check=True, timeout=30)
            output = subprocess.run([str(native)], cwd=tmp, input=input_data, check=True,
                                    stdout=subprocess.PIPE, encoding="utf-8", timeout=10)
            assert output.stdout.splitlines() == expected, output.stdout
            if windows and input_data is not None:
                window = subprocess.STARTUPINFO()
                window.dwFlags |= subprocess.STARTF_USESHOWWINDOW
                window.wShowWindow = subprocess.SW_HIDE
                output = subprocess.run(
                    [sys.executable, str(source.with_name("windows_console.py")), str(native)],
                    cwd=tmp, input=input_data, check=True, stdout=subprocess.PIPE,
                    encoding="utf-8", creationflags=subprocess.CREATE_NEW_CONSOLE,
                    startupinfo=window, timeout=30,
                )
                assert output.stdout.splitlines() == expected, output.stdout
            print(target, filename, "OK")

        source = Path(tmp) / "unsupported.thv"
        source.write_text('<lst> collection = [1, 2, 3]\n', encoding="utf-8")
        rejected = Path(tmp) / ("unsupported.exe" if windows else "unsupported")
        result = subprocess.run([str(compiler), str(source), "--target", target, "--native",
                                 "-o", str(rejected)], cwd=tmp, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
        assert result.returncode != 0 and "native i686 backend yet" in result.stdout, result.stdout
        assert not rejected.exists()
