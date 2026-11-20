"""Run the global-write fixtures using the compiler built for the current host."""
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

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

if run_windows_arm64:
    expected["os_time_arm64"] = "windows true [] true true true true true true 3 -1 true true true true 0"
    expected["os_time_extended_arm64"] = "true true example true [] false 2 true true 0 true"

with tempfile.TemporaryDirectory() as tmp:
    if run_windows_arm64:
        subprocess.run(["cl.exe", "/nologo", "/LD", str(fixtures / "ffi_arm64.c"),
                        "/Fe:arm64-ci-fixture.dll"], cwd=tmp, check=True)
        expected["ffi_arm64"] = "true false 8.75 45 arm64 -12 2.5 9 5000000000"
    for target in targets:
        cases = dict(expected)
        if target in ("arm64", "pe-arm64", "cpp"):
            cases["weighted_arm64"] = "pick 7 fallback"
        for case, line in cases.items():
            output = Path(tmp) / (case + (".exe" if windows else ""))
            for opt in (["-O0", "-O3"] if case == "gw" and target != "cpp" else ["-O0"]):
                subprocess.run([str(compiler), str(fixtures / (case + ".thv")), "--backend",
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
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
                print(result.stdout, end="", flush=True)
                result.check_returncode()
                lines = [s for s in result.stdout.splitlines() if s and not s.startswith("[Thava Native]")]
                assert lines == line.splitlines(), (target, case, opt, lines)
                print(target, case, opt, line, flush=True)

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
