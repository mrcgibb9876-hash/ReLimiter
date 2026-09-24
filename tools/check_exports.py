#!/usr/bin/env python3
"""Assert a PE image exports the given names. Prints the full export table on failure.

Reads the export directory itself rather than shelling out to dumpbin. dumpbin needs the VC
environment loaded to run at all -- without it it launches and produces nothing, which is how the
first version of this check "failed" on a DLL whose exports were fine. Parsing the file needs nothing
but Python, works the same on a runner and a laptop, and can show what IS exported when a name is
missing.

    python tools/check_exports.py <image> <name> [<name> ...]
"""
import struct
import sys


def exports(path):
    d = open(path, "rb").read()
    if d[:2] != b"MZ":
        raise SystemExit(f"{path}: not a PE image")
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    if d[pe:pe + 4] != b"PE\0\0":
        raise SystemExit(f"{path}: no PE signature")
    opt = pe + 24
    magic = struct.unpack_from("<H", d, opt)[0]          # 0x10b PE32, 0x20b PE32+
    dirs = opt + (112 if magic == 0x20B else 96)
    edir_rva, _edir_size = struct.unpack_from("<II", d, dirs)
    if not edir_rva:
        return []

    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    sec0 = opt + struct.unpack_from("<H", d, pe + 20)[0]
    secs = []
    for i in range(nsec):
        o = sec0 + 40 * i
        vsize, vaddr, rsize, raw = struct.unpack_from("<IIII", d, o + 8)
        secs.append((vaddr, max(vsize, rsize), raw))

    def offset(rva):
        for vaddr, size, raw in secs:
            if vaddr <= rva < vaddr + size:
                return raw + (rva - vaddr)
        raise SystemExit(f"{path}: RVA {rva:#x} is in no section")

    base = offset(edir_rva)
    count = struct.unpack_from("<I", d, base + 24)[0]
    names_rva = struct.unpack_from("<I", d, base + 32)[0]
    if not count:
        return []
    table = offset(names_rva)
    found = []
    for i in range(count):
        nrva = struct.unpack_from("<I", d, table + 4 * i)[0]
        o = offset(nrva)
        found.append(d[o:d.index(b"\0", o)].decode("latin1"))
    return found


if len(sys.argv) < 3:
    raise SystemExit(__doc__)

image, wanted = sys.argv[1], sys.argv[2:]
have = exports(image)
missing = [n for n in wanted if n not in have]
if missing:
    print(f"{image} does not export: {', '.join(missing)}")
    print(f"it exports {len(have)}: {', '.join(have) if have else '(nothing)'}")
    raise SystemExit(1)
print(f"{image} exports {', '.join(wanted)} ({len(have)} export(s) total)")
