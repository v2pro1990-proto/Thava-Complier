import ctypes
from pathlib import Path
import struct
import subprocess
import tempfile

compiler = Path('thavac-windows-arm64.exe').resolve()
source = Path('examples/01_hello_world.thv').resolve()

def probe(path):
    try:
        result = subprocess.run([str(path)], capture_output=True, timeout=10)
        print(path.name, hex(result.returncode & 0xffffffff), result.stdout, result.stderr, flush=True)
    except (OSError, subprocess.TimeoutExpired) as error:
        print(path.name, error, flush=True)

for dll in ('kernel32.dll', 'msvcrt.dll', 'user32.dll'):
    try:
        ctypes.WinDLL(dll)
        print('LoadLibrary succeeded:', dll, flush=True)
    except OSError as error:
        print('LoadLibrary failed:', dll, error, flush=True)

with tempfile.TemporaryDirectory() as folder:
    folder = Path(folder)
    image = folder / 'hello.exe'
    subprocess.run([str(compiler), str(source), '--target', 'win-arm64', '-o', str(image)], check=True)
    probe(image)
    data = bytearray(image.read_bytes())
    pe = struct.unpack_from('<I', data, 60)[0]
    optional = pe + 24
    sections = [struct.unpack_from('<IIII', data, optional + 240 + i*40 + 8)
                for i in range(struct.unpack_from('<H', data, pe+6)[0])]
    def offset(rva):
        return next(raw + rva - base for size, base, rawsize, raw in sections if base <= rva < base+rawsize)
    entry = offset(struct.unpack_from('<I', data, optional+16)[0])
    data[entry:entry+8] = bytes.fromhex('000080d2 c0035fd6')
    desc_rva, desc_size = struct.unpack_from('<II', data, optional+120)
    start = offset(desc_rva)
    descriptors = [bytes(data[x:x+20]) for x in range(start, start+desc_size-20, 20)]
    def dll_name(desc):
        off = offset(struct.unpack_from('<I', desc, 12)[0])
        return data[off:data.index(0, off)].decode()
    for label, selected in [('no-imports', [])] + [(dll_name(d), [d]) for d in descriptors] + [('all-imports', descriptors)]:
        variant = bytearray(data)
        if not selected:
            struct.pack_into('<II', variant, optional+120, 0, 0)
            struct.pack_into('<II', variant, optional+208, 0, 0)
        else:
            struct.pack_into('<I', variant, optional+124, 20*(len(selected)+1))
        variant[start:start+desc_size] = b''.join(selected).ljust(desc_size, b'\0')
        target = folder / (label + '.exe')
        target.write_bytes(variant)
        probe(target)
