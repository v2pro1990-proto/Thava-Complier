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
            "file_arm64": "[alpha][17]\n18\n4 [2.5][true]",
            "optimizer": "42\n19\n6\n9\n4",
            "const_variables": "10 2.4 12 const 4 15",
            "multype_return": "[42, ok, true, 2.5]\nok\n[wait, 3]\n50\ntrue 2.5 4\n"
                              "[1, [nested, false], none]\nnested\nout of range",
            "real_output": "1000000\n1000000.25\n999999999999.25\n-12345678.125\n"
                           "1000001 -12345678.125\n1000000 999999999999.25\n2.5",
            "interpolation": "Trần Minh có 42 điểm\nthứ 1\n2 / 3\n[đúng] true, 2.5\n@{name}\nTrần? Nhập: chào"}

expected["flr"] = "\n".join((
    "0.3 true 0",
    "246913578024691357802469135780.24",
    "2.25000000000000000001",
    "0.33333333333333333333333333333333 -0.66666666666666666666666666666666",
    "12.34000000000000000056 true true",
    "Invalid decimal value",
    "Decimal division by zero",
    "Decimal precision overflow",
    "Amount: 9007199254740992.99",
))

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
            levels = ["-O0"]
            if case == "gw" and target != "cpp":
                levels.append("-O3")
            if case == "optimizer":
                levels += ["-O1", "-O2", "-O3"]
            if case in ("const_variables", "real_output", "multype_return", "flr"):
                levels.append("-O2")
            for opt in levels:
                source = (Path(tmp) if case == "stack_arm64" else fixtures) / (case + ".thv")
                toolchain = ["--cxx", "missing-compiler"] if case in ("multype_return", "flr") and target != "cpp" else []
                subprocess.run([str(compiler), str(source), "--backend",
                                target, opt, *toolchain, "-o", output.name], cwd=tmp, check=True)
                if case == "optimizer" and target != "cpp":
                    dead_branch = b"optimizer should eliminate this branch entirely"
                    assert (dead_branch in output.read_bytes()) == (opt in ("-O0", "-O1")), (target, opt)
                if target == "pe-arm64":
                    data = output.read_bytes()
                    header = struct.unpack_from("<I", data, 0x3C)[0]
                    assert data[header:header + 6] == b"PE\0\0\x64\xaa"
                    print(target, case, opt, "PE ARM64 header OK", flush=True)
                    if not run_windows_arm64:
                        continue
                runner = ["qemu-aarch64"] if target == "arm64" else []
                input_data = {"unicode_case": unicode_input, "interpolation": "chào\n", "flr": "9007199254740993.99\n"}
                result = subprocess.run(runner + [str(output)], cwd=tmp,
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, encoding="utf-8",
                                        input=input_data.get(case))
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

        if target == "cpp":
            source = Path(tmp) / "optimizer_division.thv"
            source.write_text(
                'try {\n    <int> quotient = 0 * (1 / 0)\n    cons.out(quotient)\n'
                '} except Exception {\n    cons.out("division was evaluated")\n}\n',
                encoding="utf-8",
            )
            output = Path(tmp) / "optimizer_division.exe"
            subprocess.run([str(compiler), str(source), "--cpp", "-O2", "-o", str(output)],
                           cwd=tmp, check=True)
            result = subprocess.run([str(output)], cwd=tmp, check=True,
                                    stdout=subprocess.PIPE, encoding="utf-8")
            assert result.stdout.splitlines()[0] == "division was evaluated", result.stdout

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

    invalid_const_cases = [
        ("<int> const a\n", "requires an initializer"),
        ('<int> const a = "wrong"\n', "declared with type"),
        ("<int> const a = 1\na = 2\n", "cannot modify const variable 'a'"),
        ("func f() { <int> const a = 1\na += 2 }\n", "cannot modify const variable 'a'"),
        ("func f() { a = 2 }\n<int> const a = 1\n", "cannot modify const variable 'a'"),
        ("<int> const a = 1\nfunc f() { global <int> a }\n", "cannot modify const variable 'a'"),
        ("<int> const a = 1\n<int> a = 2\n", "already been declared"),
        ("<int> const a = 1\nif true { <int> a = 2 }\n", "already been declared"),
        ("<lst> const a = [1]\na[0] = 2\n", "cannot modify const variable 'a'"),
        ("<lst> const a = [1]\na.addval(2)\n", "cannot modify const variable 'a'"),
        ('<dict> const a = {"key": 1}\na.clear()\n', "cannot modify const variable 'a'"),
        ("struct Point { <int> x = 1 }\n<Point> const p = Point()\np.x = 2\n",
         "cannot modify const variable 'p'"),
        ("<int> const a = 1\nfor a in range(3) { cons.out(a) }\n", "cannot modify const variable 'a'"),
        ("<int> const a = 1\ndefer { a = 2 }\n", "cannot modify const variable 'a'"),
    ]
    for source, diagnostic in invalid_const_cases:
        Path(tmp, "invalid_const.thv").write_text(source, encoding="utf-8")
        result = subprocess.run([str(compiler), "invalid_const.thv", "-O2", "-o", "invalid_const"],
                                cwd=tmp, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        assert result.returncode != 0 and diagnostic in result.stdout.lower(), result.stdout
    print("Invalid const declarations and writes rejected", flush=True)

    for source, diagnostic in [
        ('<str> bad = "@{missing}"', "is not declared"),
        ('<str> bad = "@{1 2}"', "unexpected token in string interpolation"),
        ('<str> bad = "@{1"', "unterminated string interpolation"),
    ]:
        Path(tmp, "invalid_interpolation.thv").write_text(source + "\n", encoding="utf-8")
        result = subprocess.run([str(compiler), "invalid_interpolation.thv", "-o", "invalid_interpolation"],
                                cwd=tmp, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        assert result.returncode != 0 and diagnostic in result.stdout.lower(), result.stdout
    print("Invalid string interpolations rejected", flush=True)

    Path(tmp, "invalid_return.thv").write_text('<int> func f() { return 1, "two" }\n', encoding="utf-8")
    result = subprocess.run([str(compiler), "invalid_return.thv", "-o", "invalid_return"],
                            cwd=tmp, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    assert result.returncode != 0 and "requires a <multype> function" in result.stdout, result.stdout

    # Check that a consumer decodes the same tags across a native module boundary.
    Path(tmp, "mixed_library.thv").write_text(
        '<multype> func export_mix() { return 42, "library", true }\n', encoding="utf-8")
    Path(tmp, "mixed_consumer.thv").write_text(
        '@dlattach "mixed_library"\n<multype> value = export_mix()\ncons.out(value)\ncons.out(value[1])\n',
        encoding="utf-8")
    for target in targets:
        if target == "cpp" or (target == "pe-arm64" and not run_windows_arm64):
            continue
        switches = ["--backend", target, "--cxx", "missing-compiler"]
        library = "mixed_library.thdl" if windows else "mixed_library.so"
        binary = Path(tmp) / ("mixed_consumer.exe" if windows else "mixed_consumer")
        subprocess.run([str(compiler), "mixed_library.thv", *switches, "--lib", "-o", library],
                       cwd=tmp, check=True)
        subprocess.run([str(compiler), "mixed_consumer.thv", *switches, "-o", binary.name],
                       cwd=tmp, check=True)
        runner = ["qemu-aarch64"] if target == "arm64" else []
        result = subprocess.run(runner + [str(binary)], cwd=tmp, check=True,
                                capture_output=True, encoding="utf-8", timeout=15)
        assert result.stdout.splitlines()[:2] == ["[42, library, true]", "library"], (target, result.stdout)
