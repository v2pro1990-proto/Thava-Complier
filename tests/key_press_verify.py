"""Test blocking key input using a Windows console or Linux pseudoterminal."""
import os
from pathlib import Path
import queue
import subprocess
import sys
import threading

fixtures = Path(__file__).resolve().parent


def verify_key_press(compiler, work, backend, runner=()):
    work = Path(work)
    program = work / ("key-press.exe" if os.name == "nt" else "key-press")
    switches = ["--cxx", "missing-compiler"] if backend != "cpp" else []
    subprocess.run([str(compiler), str(fixtures / "key_press.thv"), "--backend", backend,
                    *switches, "-O2", "-o", str(program)], cwd=work, check=True, timeout=120)
    command = [*runner, str(program)]

    closed = subprocess.run(command, cwd=work, input=b"", capture_output=True, timeout=10)
    assert closed.returncode and b"requires interactive console input" in closed.stdout + closed.stderr, closed

    if os.name == "nt":
        import json
        # A key-up record and nonmatching keys must not stop the filtered calls.
        keys = [[0, "x", False], [0, "a"], [0, "q"], [13, "\r"], [0, "x"],
                [39, ""], [0, "ầ"], [27, ""], *[[13 if ch == "\r" else 0, ch] for ch in "Trần Minh\r"]]
        output = subprocess.check_output(
            [sys.executable, str(fixtures / "windows_console.py"), "--key-records", *command],
            cwd=work, input=json.dumps(keys).encode(), timeout=20,
        ).decode("utf-8")
    else:
        output = _verify_pty(command, work)

    assert output.splitlines() == [
        "ready", "KEY_A", "KEY_ENTER", "KEY_RIGHT", "KEY_ầ", "KEY_ESCAPE",
        "name: Trần Minh", "empty rejected", "unknown rejected", "",
        "[Thava Native] Build and run successful!",
    ], output
    print(backend, "key.press and subsequent key.input OK", flush=True)


def _verify_pty(command, work):
    import pty
    import termios

    master, slave = pty.openpty()
    original = termios.tcgetattr(slave)
    process = subprocess.Popen(command, cwd=work, stdin=slave, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT)
    parsed = queue.Queue()

    def read_output():
        for line in process.stdout:
            parsed.put(line.decode("utf-8").rstrip("\r\n"))

    reader = threading.Thread(target=read_output, daemon=True)
    reader.start()
    observed = []

    def expect(text):
        line = parsed.get(timeout=8)
        observed.append(line)
        assert line == text, observed

    try:
        expect("ready")
        # No characters yet. A successful key.press call cannot return.
        try:
            premature = parsed.get(timeout=0.15)
        except queue.Empty:
            pass
        else:
            raise AssertionError(("key.press returned before input", premature))

        for data, expected in [
            (b"a", "KEY_A"),
            (b"q\r", "KEY_ENTER"),
            (b"x\x1b[C", "KEY_RIGHT"),
            ("ầ".encode(), "KEY_ầ"),
            (b"\x1b", "KEY_ESCAPE"),
        ]:
            os.write(master, data)
            expect(expected)

        assert termios.tcgetattr(slave) == original, "terminal attributes were not restored"
        os.write(master, "Trần Minh\n".encode())
        process.wait(timeout=8)
        reader.join(timeout=2)
        assert process.returncode == 0, observed
        while not parsed.empty():
            observed.append(parsed.get_nowait())
        return "\n".join(observed)
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=5)
        process.stdout.close()
        os.close(master)
        os.close(slave)


if __name__ == "__main__":
    import tempfile
    with tempfile.TemporaryDirectory() as work:
        verify_key_press(Path(sys.argv[1]).resolve(), work, sys.argv[2], sys.argv[3:])
