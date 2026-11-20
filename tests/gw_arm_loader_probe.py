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

    (folder / 'reference.c').write_text(
        'int entry(void) { return 0; }\n', encoding='ascii')
    subprocess.run(['cl.exe', '/nologo', '/c', 'reference.c'], cwd=folder, check=True)
    subprocess.run(['link.exe', '/nologo', '/nodefaultlib', '/entry:entry',
                    '/subsystem:console', '/out:reference.exe', 'reference.obj'],
                   cwd=folder, check=True)
    reference = folder / 'reference.exe'
    probe(reference)
    for image in (folder / 'no-imports.exe', reference):
        print(image.name, flush=True)
        subprocess.run(['dumpbin.exe', '/headers', str(image)], check=True)

    bare = bytearray((folder / 'no-imports.exe').read_bytes())
    reloc = offset(struct.unpack_from('<I', bare, optional+152)[0])
    text_raw = sections[0][3]
    # Put a real absolute image VA in the last aligned word of .text.
    text_slot = sections[0][2] - 8
    struct.pack_into('<Q', bare, text_raw+text_slot, struct.unpack_from('<Q', bare, optional+24)[0])
    struct.pack_into('<I', bare, optional+240+8, sections[0][2])
    struct.pack_into('<H', bare, reloc+8, 0xa000 | text_slot)
    probe_path = folder / 'dir64.exe'
    probe_path.write_bytes(bare)
    probe(probe_path)

    ref_data = reference.read_bytes()
    ref_opt = struct.unpack_from('<I', ref_data, 60)[0] + 24
    for label, field, width in [('dll-characteristics', 70, 2), ('versions', 40, 12),
                                ('linker-version', 2, 2), ('image-base', 24, 8)]:
        variant = bytearray(bare)
        variant[optional+field:optional+field+width] = ref_data[ref_opt+field:ref_opt+field+width]
        path = folder / (label + '.exe')
        path.write_bytes(variant)
        probe(path)