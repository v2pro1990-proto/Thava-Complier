"""Generate the packed UTF-8 mappings from a fixed Unicode Character Database."""
from pathlib import Path
from urllib.request import urlopen

VERSION = "17.0.0"
BASE = f"https://www.unicode.org/Public/{VERSION}/ucd"


def records(name):
    with urlopen(f"{BASE}/{name}", timeout=30) as response:
        for line in response.read().decode("utf-8").splitlines():
            row = line.partition("#")[0].strip()
            if row:
                yield [column.strip() for column in row.split(";")]


def packed(chars):
    encoded = "".join(chr(char) for char in chars).encode("utf-8")
    assert 0 < len(encoded) <= 8 and b"\0" not in encoded
    return int.from_bytes(encoded, "little")


mappings = {}
for fields in records("UnicodeData.txt"):
    code = int(fields[0], 16)
    upper, lower, title = fields[12:15]
    if upper or lower or title:
        mappings[code] = tuple((int(value, 16),) if value else (code,)
                               for value in (lower, upper, title or upper))

for code, lower, title, upper, *condition in records("SpecialCasing.txt"):
    if not any(condition):
        mappings[int(code, 16)] = tuple(tuple(int(value, 16) for value in field.split())
                                        for field in (lower, upper, title))

spaces = set()
for span, name in records("PropList.txt"):
    if name == "White_Space":
        first, _, last = span.partition("..")
        spaces.update(range(int(first, 16), int(last or first, 16) + 1))

rows = []
for code in set(mappings) | spaces:
    key = packed((code,))
    fields = mappings.get(code, ((code,),) * 3)
    values = tuple(packed(value) for value in fields)
    assert all(len(bytes(chr(code), "utf-8")) * 4 >= (value.bit_length() + 7) // 8
               for value in values)
    rows.append((key, values[0], values[1], 0 if code in spaces else values[2]))

header = [
    "#pragma once",
    "",
    "#include <cstddef>",
    "#include <cstdint>",
    "",
    f"// Generated from Unicode {VERSION} UnicodeData.txt, SpecialCasing.txt and PropList.txt.",
    "// Copyright (c) 2025 Unicode, Inc. See UNICODE-LICENSE.txt.",
    "// All fields are UTF-8 bytes packed least-significant byte first.",
    "// A zero title mapping marks Unicode White_Space.",
    "namespace thava_unicode {",
    "inline constexpr uint64_t caseMappings[][4] = {",
]
for row in sorted(rows):
    header.append("    {" + ", ".join(f"0x{value:x}ULL" for value in row) + "},")
header.extend([
    "};",
    "inline constexpr size_t caseMappingCount = sizeof(caseMappings) / sizeof(caseMappings[0]);",
    "} // namespace thava_unicode",
])
root = Path(__file__).resolve().parents[1]
source = root / "src" if (root / "src").is_dir() else root
(source / "unicode_case_data.hpp").write_text("\n".join(header) + "\n", encoding="ascii")
