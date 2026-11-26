"""Verify the 32-bit target format and run an executable built by Thava."""
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile


compiler = Path(sys.argv[1]).resolve()
source = Path(__file__).with_name("i686.thv").resolve()
windows = os.name == "nt"
arm64 = "--windows-arm64" in sys.argv[2:]
target = "win-arm64" if arm64 else ("win-i686" if windows else "linux-i686")
success_banner = "[Thava Native] Build and run successful!"


def assert_native_output(output, expected_lines):
    assert output.splitlines() == [*expected_lines, "", success_banner], output


with tempfile.TemporaryDirectory() as tmp:
    output = Path(tmp) / ("smoke.exe" if windows else "smoke")
    build = subprocess.run([str(compiler), str(source), "--target", target, "--cpp",
                            "-O2", "-o", str(output)], cwd=tmp, check=True,
                           stdout=subprocess.PIPE, text=True)
    assert "Using detected C++ compiler" in build.stdout, build.stdout
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
            (["--target", target], "-O2"),
            (["--arch", "i686"], "-O0"),
            (["--target", target, "--native"], "-O0"),
            (["--backend", "pe-i686" if windows else "elf-i686"], "-O2"),
        ]:
            native = Path(tmp) / ("native" + opt + (".exe" if windows else ""))
            build = subprocess.run([str(compiler), str(fixture), *switches, opt, "--cxx",
                                    "missing-compiler", "-o", str(native)], cwd=tmp, env=env,
                                   check=True, stdout=subprocess.PIPE, text=True, timeout=30)
            assert "Success! Native i686" in build.stdout, build.stdout
            assert success_banner not in build.stdout, build.stdout
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
            assert_native_output(result.stdout, [
                "5000000008 -9223372036854775808 -15000000000 -555555555 -8 2 2000000000",
                "8 -1 1099511627776 -1 true false true trời đẹp",
            ])
        print(target, "native code generation and runtime output OK")

        cattach = Path(tmp) / "i686_cattach.thv"
        cattach.write_text('@cattach <stddef.h>\ncons.out("cattach")\n', encoding="utf-8")
        cattach_output = Path(tmp) / ("cattach.exe" if windows else "cattach")
        build = subprocess.run([str(compiler), str(cattach), "--target", target, "-o", str(cattach_output)],
                               cwd=tmp, check=True, capture_output=True, text=True, timeout=60)
        assert "Switching to C++ Transpiler" in build.stdout, build.stdout
        output = subprocess.check_output([str(cattach_output)], cwd=tmp, encoding="utf-8", timeout=10)
        assert_native_output(output, ["cattach"])
        rejected = subprocess.run([str(compiler), str(cattach), "--target", target, "--native",
                                   "-o", str(cattach_output)], cwd=tmp, capture_output=True, text=True, timeout=30)
        assert rejected.returncode != 0 and "Cannot compile directly via --backend native" in rejected.stderr

        fixture = source.with_name("optimizer.thv")
        native = Path(tmp) / ("optimizer.exe" if windows else "optimizer")
        for opt in ("-O1", "-O2"):
            subprocess.run([str(compiler), str(fixture), "--target", target, "--native",
                            opt, "-o", str(native)], cwd=tmp, env=env, check=True, timeout=30)
            dead_branch = b"optimizer should eliminate this branch entirely"
            assert (dead_branch in native.read_bytes()) == (opt == "-O1"), (target, opt)
            result = subprocess.run([str(native)], cwd=tmp, check=True,
                                    stdout=subprocess.PIPE, encoding="utf-8", timeout=10)
            assert_native_output(result.stdout, ["42", "19", "6", "9", "4"])

        unicode_input = "HÔM NAY TRỜI ĐẸP QUÁ\nhôm nay trời đẹp quá\n"
        for filename, expected, input_data in [
            ("gw.thv", ["4 3 AB new 9"], None),
            ("const_variables.thv", ["10 2.4 12 const 4 15"], None),
            ("multype_return.thv", [
                "[42, ok, true, 2.5]", "ok", "[wait, 3]", "50", "true 2.5 4",
                "[1, [nested, false], none]", "nested", "out of range",
            ], None),
            ("flr.thv", [
                "0.3 true 0",
                "246913578024691357802469135780.24",
                "2.25000000000000000001",
                "0.33333333333333333333333333333333 -0.66666666666666666666666666666666",
                "12.34000000000000000056 true true",
                "Invalid decimal value",
                "Decimal division by zero",
                "Decimal precision overflow",
                "Amount: 9007199254740992.99",
            ], "9007199254740993.99\n"),
            ("real_output.thv", [
                "1000000", "1000000.25", "999999999999.25", "-12345678.125",
                "1000001 -12345678.125", "1000000 999999999999.25", "2.5",
            ], None),
            ("interpolation.thv", [
                "Trần Minh có 42 điểm", "thứ 1", "2 / 3", "[đúng] true, 2.5",
                "@{name}", "Trần? Nhập: chào",
            ], "chào\n"),
            ("i686_class.thv", [
                "true 5000000021 boxed 3.75 5000000003", "Cannot access a member of none",
                "5000000012", "int string",
            ], None),
            ("i686_callbacks.thv", [
                "call-7 [11, changed]", "Wrong number of arguments for 'decorate'", "Cannot call a null function",
            ], None),
            ("i686_mixed.thv", [
                "1 8 bells 4.5 box true true",
                "[8, bells, [4.5, false], {name: box}, none]",
                "one 81 1 13", "[1, true, x]", "missing caught", "[rebound]",
            ], None),
            ("i686_struct.thv", [
                "ABD -25536 5000000017 7 true true true boxed",
                "ABDD -1 5000000000 box",
                "ABDD full true",
            ], None),
            ("i686_unwind.thv", [
                "5000000042 BA012I outer Division by zero",
                "file closed body|deferred", "replacement BA012IZ", "end", "program cleanup",
            ], None),
            ("os_time_arm64.thv", [
                ("windows" if windows else "linux") +
                " true [] true true true true true true 3 -1 true true true true 0",
            ], None),
            ("os_time_extended_arm64.thv", [
                "true true example true [] false 2 true true 0 true",
            ], None),
            ("i686_os_spawn.thv", ["true 0 true [] true"], None),
            ("file_arm64.thv", ["[alpha][17]", "18", "4 [2.5][true]"], None),
            ("i686_file_scope.thv", ["42 return|continue|break"], None),
            ("membership_rand.thv", ["true false true false true false true -4 true only"], None),
            ("weighted_arm64.thv", ["pick 7 fallback"], None),
            ("i686_collections.thv", [
                "[5, 21, 11] 3 11 37 9 true false",
                "2 8 99 true false b|c 13",
                "đẹp 2 4 một||hai|",
            ], None),
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
            assert_native_output(output.stdout, expected)
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
                assert_native_output(output.stdout, expected)
            print(target, filename, "OK")

        qemu = shutil.which("qemu-i386") if not windows else None
        if "--ish-syscalls" in sys.argv[2:] and qemu is None:
            raise RuntimeError("qemu-i386 is required for the iSH syscall check")
        if qemu:
            ish = Path(tmp) / "ish-regression"
            with (Path(tmp) / "large.bin").open("wb") as sparse:
                sparse.truncate(5_000_000_003)
            subprocess.run([str(compiler), str(source.with_name("i686_ish.thv")),
                            "--target", target, "--native", "-O2", "-o", str(ish)],
                           cwd=tmp, env=env, check=True, timeout=30)
            result = subprocess.run([qemu, "-r", "4.20.69-ish", "-E", "THAVA_TEST_SPARSE=large.bin",
                                     "-strace", str(ish)], cwd=tmp,
                                    check=True, capture_output=True, encoding="utf-8", timeout=10)
            assert_native_output(result.stdout, [
                "Chào mừng bạn đến với Thava trên iSH!", "[PASS] UTF-8", "[PASS] time",
                "[PASS] time.sleep / os.sleep", "[PASS] os.exists / os.is_file / os.is_dir",
                "[PASS] os.getsize", "[PASS] os.getsize > 4 GiB", "iSH smoke test OK",
            ])
            assert not list(Path(tmp).glob("thava-ish-test-*")), "iSH smoke test left a scratch directory"
            calls = set(re.findall(r"^\s*\d+\s+(\w+)\(", result.stderr, re.MULTILINE))
            assert {"uname", "gettimeofday", "nanosleep", "stat64"} <= calls, result.stderr
            assert not {"clock_gettime64", "clock_nanosleep_time64", "statx"} & calls, result.stderr
            assert not re.search(r"Unknown syscall (403|407)\b", result.stderr), result.stderr
            print(target, "iSH release detection and legacy syscalls OK")

        module_build = Path(tmp) / "module-build"
        module_run = Path(tmp) / "module-run"
        module_build.mkdir()
        module_run.mkdir()
        modules = [("thdl_arm64_library", "core_arm64.thdl", "bump"),
                   ("thdl_arm64_dependency", "dependent_arm64.thdl", "from_dependency")]
        for fixture, name, exported in modules:
            library = module_build / name
            subprocess.run([str(compiler), str(source.with_name(fixture + ".thv")),
                            "--target", target, "--native", "--lib", "-O2", "-o", str(library)],
                           cwd=module_build, env=env, check=True, timeout=30)
            info = subprocess.run([str(compiler), "--info", str(library)], cwd=module_build,
                                  env=env, check=True, stdout=subprocess.PIPE, encoding="utf-8", timeout=10)
            assert (b"machine:i686-pe-v2\n" if windows else b"machine:i686-elf-v2\n") in library.read_bytes()
            assert exported in info.stdout, info.stdout
        consumer = module_run / ("consumer.exe" if windows else "consumer")
        subprocess.run([str(compiler), str(source.with_name("thdl_arm64_consumer.thv")),
                        "--target", target, "--native", "-O2", "-o", str(consumer)],
                       cwd=module_build, env=env, check=True, timeout=30)
        for _, name, _ in modules:
            (module_build / name).replace(module_run / name)
        result = subprocess.run([str(consumer)], cwd=module_run, check=True,
                                stdout=subprocess.PIPE, encoding="utf-8", timeout=10)
        assert_native_output(result.stdout, ["42 43 44 5.5 thdl", "45 4.5 2 9"])
        print(target, "native libraries, dependencies and moved modules OK")

        objects = module_build / "i686_objects.thdl"
        subprocess.run([str(compiler), str(source.with_name("i686_objects_library.thv")),
                        "--target", target, "--native", "--lib", "-O2", "-o", str(objects)],
                       cwd=module_build, env=env, check=True, timeout=30)
        for opt in ("-O0", "-O2"):
            consumer = module_run / ("objects" + opt + (".exe" if windows else ""))
            subprocess.run([str(compiler), str(source.with_name("i686_objects_consumer.thv")),
                            "--target", target, "--native", opt, "-o", str(consumer)],
                           cwd=module_build, env=env, check=True, timeout=30)
            result = subprocess.run([str(consumer)], cwd=module_run, check=True,
                                    stdout=subprocess.PIPE, encoding="utf-8", timeout=10)
            assert_native_output(result.stdout, [
                "express box / express box / box / 5000000011 / true true",
                "Expected object of type 'Packet'", "library defer", "library exception",
                "program defer", "program exception",
            ])
        print(target, "objects, callbacks and deferred exceptions across .thdl OK")

        scalar_consumer = module_build / "scalar-consumer.thv"
        scalar_consumer.write_text('@dlattach <i686_objects.thdl>\ncons.out(moduleNumber())\n', encoding="utf-8")
        consumer = module_run / ("scalar.exe" if windows else "scalar")
        subprocess.run([str(compiler), str(scalar_consumer), "--target", target, "--native", "-o", str(consumer)],
                       cwd=module_build, env=env, check=True, timeout=30)
        result = subprocess.run([str(consumer)], cwd=module_run, check=True,
                                stdout=subprocess.PIPE, encoding="utf-8", timeout=10)
        assert_native_output(result.stdout, ["17"])

        incompatible = module_build / "incompatible.thv"
        different_type = source.with_name("i686_objects_types.thv").read_text(encoding="utf-8")
        incompatible.write_text(
            different_type.replace("<int64> count = 5000000000", "<int> count = 0") +
            '\n@dlattach <i686_objects.thdl>\n<Packet> result = updatePacket(Packet(), Parcel())\n',
            encoding="utf-8",
        )
        rejected = module_build / ("incompatible.exe" if windows else "incompatible")
        result = subprocess.run([str(compiler), str(incompatible), "--target", target, "--native", "-o", str(rejected)],
                                cwd=module_build, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                encoding="utf-8", timeout=30)
        assert result.returncode != 0 and "Object layout of 'Packet' differs" in result.stdout, result.stdout
        assert not rejected.exists()

        source = Path(tmp) / "unsupported.thv"
        source.write_text('file.open("sample.txt", "w") as opened {\n    <lst> collection = [opened]\n}\n',
                          encoding="utf-8")
        rejected = Path(tmp) / ("unsupported.exe" if windows else "unsupported")
        result = subprocess.run([str(compiler), str(source), "--target", target, "--native",
                                 "-o", str(rejected)], cwd=tmp, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
        assert result.returncode != 0 and "native i686 backend yet" in result.stdout, result.stdout
        assert not rejected.exists()

        uncaught_source = Path(tmp) / "uncaught.thv"
        uncaught_source.write_text('defer cons.out("cleanup")\nthrow "unhandled"\n', encoding="utf-8")
        uncaught = Path(tmp) / ("uncaught.exe" if windows else "uncaught")
        subprocess.run([str(compiler), str(uncaught_source), "--target", target, "--native",
                        "-o", str(uncaught)], cwd=tmp, env=env, check=True, timeout=30)
        result = subprocess.run([str(uncaught)], cwd=tmp, stdout=subprocess.PIPE,
                                encoding="utf-8", timeout=10)
        assert result.returncode == 1, result.returncode
        assert result.stdout.splitlines() == ["cleanup", "[Runtime Exception]: unhandled"]
