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
    input_text = sys.stdin.buffer.read().decode("utf-8").replace("\r\n", "\n").replace("\n", "\r")
    encoded = input_text.encode("utf-16-le")
    events = (InputRecord * (len(encoded) // 2))()
    for event, offset in zip(events, range(0, len(encoded), 2)):
        char = int.from_bytes(encoded[offset:offset + 2], "little")
        event.type = 1  # KEY_EVENT
        event.key = KeyEvent(True, 1, 0x0D if char == 0x0D else 0, 0, char, 0)

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
        result = subprocess.run(sys.argv[1:], stdin=console, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=15)
    sys.stdout.buffer.write(result.stdout)
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
