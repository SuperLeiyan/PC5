#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Checks that the app's lapy.elf is one PC5 can make home screen tiles from.

A tile is an app of its own under its own title ID (title/pc5_tile.h). Its Lapy
helper grants /data only to the title ID built into it, so PC5 changes that ID
in the tile's copy on the console (pc5_tile_lapy_patch). That needs PC5's ID in
the helper exactly where the C code looks: two inlined compares (a movabs of the
first eight characters, then an xor of the ninth) and one string. A Lapy built
differently fails the build here rather than a tile on the console.

    tools/lapy-title.py <lapy.elf> [<title id>]

With a title ID, also patches a copy the way the console does and checks it.
"""
import re
import sys

OLD = b"PPSA11018"


def sites(data: bytes):
    compares, strings = [], []
    for m in re.finditer(b"\x48[\xb8-\xbf]" + re.escape(OLD[:8]), data):
        imm = m.start() + 2
        j = data.find(b"\x48\x83", imm + 8, imm + 48)
        if j < 0 or j + 3 >= len(data) or data[j + 2] & 0xf8 != 0xf0 or data[j + 3] != OLD[8]:
            raise SystemExit("lapy.elf: PC5's ID is there, but not its ninth character's compare")
        compares.append((imm, j + 3))
    for m in re.finditer(re.escape(OLD) + b"\x00", data):
        strings.append(m.start())
    return compares, strings


def main(argv):
    if len(argv) not in (2, 3):
        raise SystemExit(__doc__)
    data = open(argv[1], "rb").read()
    if data[:4] != b"\x7fELF":
        raise SystemExit(f"lapy.elf: {argv[1]} is not an ELF file")
    compares, strings = sites(data)
    if len(compares) != 2 or len(strings) != 1:
        raise SystemExit(f"lapy.elf: {len(compares)} compares and {len(strings)} names of {OLD.decode()}, "
                         "where PC5 makes tiles from 2 and 1 (title/pc5_tile.c)")
    if len(argv) == 3:
        new = argv[2].encode()
        if not re.fullmatch(rb"PPSA[0-9]{5}", new):
            raise SystemExit("a tile's ID is PPSA and five digits")
        patched = bytearray(data)
        for imm, ninth in compares:
            patched[imm:imm + 8] = new[:8]
            patched[ninth] = new[8]
        patched[strings[0]:strings[0] + 9] = new
        if OLD[:8] in patched or len(patched) != len(data):
            raise SystemExit("lapy.elf: PC5's ID is still in the patched copy")
    print(f"lapy.elf: PC5 can make tiles from it ({len(compares)} compares, {len(strings)} name)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
