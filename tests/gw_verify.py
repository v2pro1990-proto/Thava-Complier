"""Run compiler regression fixtures across the supported backends."""
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

sys.stdout.reconfigure(encoding="utf-8")
fixtures = Path(__file__).resolve().parent
windows = os.name == "nt"
compiler = Path(sys.argv[1]).resolve()
run_windows_arm64 = "--run-windows-arm64" in sys.argv[2:]
targets = ("pe", "cpp", "pe-arm64") if windows else ("elf", "arm64")
if run_windows_arm64:
    targets = ("pe-arm64",)
expected = {"gw": "4 3 AB new 9", "gw_dict": "1 8", "strings_arm64": "[ABC-19][abc-19][Ab C][][][][][AbC-19]",
            "strings_search": "1 -1 3 0 [cXY-XY-XY][cab-ab-ab][c--][Alpha Beta][one||three|]",
            "membership_rand": "true false true false true false true -4 true only",
            "file_arm64": "[alpha][17]\n18\n4 [2.5][true]"}

unicode_output = (
    "hôm nay trời đẹp quá",
    "HÔM NAY TRỜI ĐẸP QUÁ",
    "Hôm Nay Trời Đẹp Quá",
    "SSTRAẞE\u00a0ÉLAN\tΩΜΈΓΑ 😀 𐐀",
    "ßtraße\u00a0élan\tωμέγα 😀 𐐨",
    "Sstraße\u00a0Élan\tΩμέγα 😀 𐐀",
    "A\u0302\u0301",
    "ab",
    "hôm nay trời đẹp quá",
    "HÔM NAY TRỜI ĐẸP QUÁ",
)
expected["unicode_case"] = "\n".join(unicode_output)
unicode_input = "HÔM NAY TRỜI ĐẸP QUÁ\nhôm nay trời đẹp quá\n"

if run_windows_arm64:
    expected["os_time_arm64"] = "windows true [] true true true true true true 3 -1 true true true true 0"
    expected["os_time_extended_arm64"] = "true true example true [] false 2 true true 0 true"

with tempfile.TemporaryDirectory() as tmp:
    if run_windows_arm64:
        subprocess.run(["cl.exe", "/nologo", "/LD", str(fixtures / "ffi_arm64.c"),
                        "/Fe:arm64-ci-fixture.dll"], cwd=tmp, check=True)
        expected["ffi_arm64"] = "true false 8.75 45 arm64 -12 2.5 9 5000000000 54321"
    if run_windows_arm64:
        for fixture, output in [
            ("thdl_arm64_library", "core_arm64.thdl"),
            ("thdl_arm64_dependency", "dependent_arm64.thdl"),
        ]:
            subprocess.run([str(compiler), str(fixtures / (fixture + ".thv")),
                            "--target", "win-arm64", "--lib", "-o", output], cwd=tmp, check=True)
            data = (Path(tmp) / output).read_bytes()
            header = struct.unpack_from("<I", data, 0x3C)[0]
            assert data[header:header + 6] == b"PE\0\0\x64\xaa"
            assert struct.unpack_from("<H", data, header + 22)[0] & 0x2000
            assert struct.unpack_from("<I", data, header + 24 + 112)[0]

        # A separate process releases the DLL before the temporary directory is removed.
        subprocess.run([sys.executable, "-c", """
import ctypes
from pathlib import Path
library = ctypes.WinDLL(str(Path('core_arm64.thdl').resolve()))
metadata = library._thava_get_metadata
metadata.restype = ctypes.c_char_p
assert b'machine:arm64-pe\\npe-dll:1\\n' in metadata()
"""], cwd=tmp, check=True)
        expected["thdl_arm64_consumer"] = "42 43 44 5.5 thdl\n45 4.5 2 9"
    for target in targets:
        cases = dict(expected)
        if target in ("arm64", "pe-arm64", "cpp"):
            cases["weighted_arm64"] = "pick 7 fallback"
        if target in ("arm64", "pe-arm64"):
            # Cross the previous 512-byte frame and restore SP after a throw.
            source = Path(tmp) / "stack_arm64.thv"
            variables = "".join(f"<int> frame_{i} = {i}\n" for i in range(72))
            source.write_text(variables + (fixtures / source.name).read_text() +
                              '\ntry { throw "stack" } except Exception as msg {\n'
                              '    cons.out("@{msg} @{frame_71}")\n}\n', encoding="utf-8")
            cases["stack_arm64"] = "19 29 38\nstack 71"
        for case, line in cases.items():
            output = Path(tmp) / (case + (".exe" if windows else ""))
            for opt in (["-O0", "-O3"] if case == "gw" and target != "cpp" else ["-O0"]):
                source = (Path(tmp) if case == "stack_arm64" else fixtures) / (case + ".thv")
                subprocess.run([str(compiler), str(source), "--backend",
                                target, opt, "-o", output.name], cwd=tmp, check=True)
                if target == "pe-arm64":
                    data = output.read_bytes()
                    header = struct.unpack_from("<I", data, 0x3C)[0]
                    assert data[header:header + 6] == b"PE\0\0\x64\xaa"
                    print(target, case, opt, "PE ARM64 header OK", flush=True)
                    if not run_windows_arm64:
                        continue
                runner = ["qemu-aarch64"] if target == "arm64" else []
                result = subprocess.run(runner + [str(output)], cwd=tmp,
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, encoding="utf-8",
                                        input=unicode_input if case == "unicode_case" else None)
                print(result.stdout, end="", flush=True)
                result.check_returncode()
                lines = [s for s in result.stdout.splitlines() if s and not s.startswith("[Thava Native]")]
                assert lines == line.splitlines(), (target, case, opt, lines)
                print(target, case, opt, line, flush=True)
                if windows and case == "unicode_case":
                    window = subprocess.STARTUPINFO()
                    window.dwFlags |= subprocess.STARTF_USESHOWWINDOW
                    window.wShowWindow = subprocess.SW_HIDE
                    console = subprocess.run(
                        [sys.executable, str(fixtures / "windows_console.py"), str(output)],
                        cwd=tmp, input=unicode_input, encoding="utf-8", stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NEW_CONSOLE,
                        startupinfo=window, timeout=30,
                    )
                    print(console.stdout, end="", flush=True)
                    console.check_returncode()
                    console_lines = [s for s in console.stdout.splitlines()
                                     if s and not s.startswith("[Thava Native]")]
                    assert console_lines == line.splitlines(), (target, "console", console_lines)
                    print(target, "Unicode console input OK", flush=True)

    for source in [
        "<int> x = 0\nfunc f() { x = 1 }",
        "<int> x = 0\nfunc f() { global <int64> x }",
        "<int> x = 0\nfunc f(<int> x) { global <int> x }",
        "<int> x = 0\nglobal <int> x",
        "func f() { global <int> missing }",
        "<int> x = 0\nfunc f() { global <int> x\n<int> x = 1 }",
        "<lst> xs = []\nfunc f() { <int> a = xs.addval(1) }",
        "<lst> xs = [1]\nfunc f() { xs[0] = 2 }",
        '<dict> d = {}\nfunc f() { d.del("key") }',
    ]:
        Path(tmp, "invalid.thv").write_text(source + "\n")
        result = subprocess.run([str(compiler), "invalid.thv", "-o", "invalid"], cwd=tmp,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        message = result.stdout.lower()
        assert result.returncode != 0 and ("global" in message or "already been declared" in message), result.stdout
    print("9 invalid declarations/writes rejected", flush=True)
