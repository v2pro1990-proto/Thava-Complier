"""Feed UTF-16 keyboard events to a child attached to this Windows console."""
import ctypes
from ctypes import wintypes
import msvcrt
import subprocess
import sys


class KeyEvent(ctypes.Structure):
    _fields_ = [
        ("down", wintypes.BOOL),
        ("repeat", wintypes.WORD),
        ("virtual_key", wintypes.WORD),
        ("scan_code", wintypes.WORD),
        ("character", wintypes.WORD),
        ("modifiers", wintypes.DWORD),
    ]


class InputRecord(ctypes.Structure):
    _fields_ = [("type", wintypes.WORD), ("key", KeyEvent)]


def main():
    args = sys.argv[1:]
    payload = sys.stdin.buffer.read().decode("utf-8")
    keys = []
    if args[0] == "--key-records":
        import json
        args.pop(0)
        for key, char, *flags in json.loads(payload):
            keys.append((key, ord(char) if char else 0, flags[0] if flags else True))
    else:
        encoded = payload.replace("\r\n", "\n").replace("\n", "\r").encode("utf-16-le")
        for offset in range(0, len(encoded), 2):
            char = int.from_bytes(encoded[offset:offset + 2], "little")
            keys.append((0x0D if char == 0x0D else 0, char, True))

    events = (InputRecord * len(keys))()
    for event, (key, char, down) in zip(events, keys):
        event.type = 1  # KEY_EVENT
        event.key = KeyEvent(down, 1, key, 0, char, 0)

    write_console_input = ctypes.WinDLL("kernel32", use_last_error=True).WriteConsoleInputW
    write_console_input.argtypes = [
        wintypes.HANDLE, ctypes.POINTER(InputRecord), wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)
    ]
    write_console_input.restype = wintypes.BOOL
    with open("CONIN$", "rb", buffering=0) as console:
        written = wintypes.DWORD()
        handle = msvcrt.get_osfhandle(console.fileno())
        if not write_console_input(handle, events, len(events), ctypes.byref(written)):
            raise ctypes.WinError(ctypes.get_last_error())
        if written.value != len(events):
            raise RuntimeError("The console received only part of the test input")
        result = subprocess.run(args, stdin=console, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=15)
    sys.stdout.buffer.write(result.stdout)
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
