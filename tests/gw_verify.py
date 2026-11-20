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
            "strings_search": "1 -1 3 0 [cXY-XY-XY][cab-ab-ab][c--][Alpha Beta][one||three|]"}

with tempfile.TemporaryDirectory() as tmp:
    for target in targets:
        for case, line in expected.items():
            if target == "pe-arm64" and case == "gw_dict":
                continue  # This target rejects dictionaries at compile time.
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
                assert lines == [line], (target, case, opt, lines)
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
