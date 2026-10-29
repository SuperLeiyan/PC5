#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Patch a copy of PC5's one-shot Lapy helper (lapy.elf) to a different PS5 app id.

This is a throwaway tool for the home-screen-tile console test
(docs/plans/standalone-tile-launchers.md). The real feature will do the same
patch on the console from an offsets table the build writes; this script finds
the offsets itself so the test needs no build.

PC5's lapy.elf grants /data only to the app id compiled into it (PPSA11018).
The id appears in five places: two inlined title checks (each an 8-byte
`movabs` of the first eight characters plus a 1-byte `xor $imm8` on the ninth)
and one plain .rodata log string. Every tile id is "PPSA" and five digits, the
same length, so the bytes are replaced in place and the file keeps its size.

Usage:
    python3 patch-lapy-title.py <lapy.elf> <PPSA99xxx> [<out.elf>]

lapy.elf is inside the built release (PPSA11018.zip), not in the source tree.
With no output path, writes lapy-<ID>.elf beside this script's working dir.
"""
import hashlib
import os
import re
import sys

OLD = b"PPSA11018"


def patch(src_path: str, new_id: bytes) -> bytes:
    if not re.fullmatch(rb"PPSA[0-9]{5}", new_id):
        raise SystemExit(f"app id must be PPSA and five digits, not {new_id!r}")
    data = bytearray(open(src_path, "rb").read())
    if data[:4] != b"\x7fELF":
        raise SystemExit(f"{src_path} is not an ELF file")

    sites = []  # (offset, expected_old_bytes, new_bytes)
    # The two inlined checks: `48 b8/b9 <PPSA1101>` (movabs) then a nearby
    # `48 83 f?/xor $imm8` whose immediate is the ninth character.
    for m in re.finditer(b"\x48[\xb8-\xbf]" + re.escape(OLD[:8]), data):
        imm = m.start() + 2
        tail = data.find(b"\x48\x83", imm + 8, imm + 48)
        if tail < 0 or data[tail + 2] & 0xf8 != 0xf0 or data[tail + 3] != OLD[8]:
            raise SystemExit("found the id constant but not its ninth-char compare; "
                             "this lapy.elf is built differently and needs a fresh look")
        sites += [(imm, OLD[:8], new_id[:8]), (tail + 3, OLD[8:9], new_id[8:9])]
    # The plain NUL-terminated string.
    for m in re.finditer(re.escape(OLD) + b"\x00", data):
        sites.append((m.start(), OLD, new_id))

    if len(sites) != 5:
        raise SystemExit(f"expected 5 id sites, found {len(sites)}: "
                         f"{[hex(o) for o, _, _ in sites]}")
    for off, want, repl in sites:
        if data[off:off + len(want)] != want:
            raise SystemExit(f"byte mismatch at {hex(off)}")
        data[off:off + len(repl)] = repl
    if data.count(OLD) or data.count(OLD[:8]):
        raise SystemExit("the old id is still present after patching")
    print("patched offsets:", ", ".join(hex(o) for o, _, _ in sites))
    return bytes(data)


def main(argv):
    if len(argv) not in (3, 4):
        raise SystemExit(__doc__)
    src, new_id = argv[1], argv[2].encode()
    out = argv[3] if len(argv) == 4 else f"lapy-{argv[2]}.elf"
    orig = open(src, "rb").read()
    patched = patch(src, new_id)
    if len(patched) != len(orig):
        raise SystemExit("size changed: refusing to write")
    open(out, "wb").write(patched)
    changed = sum(a != b for a, b in zip(orig, patched))
    print(f"wrote {out} ({len(patched)} bytes, {changed} changed)")
    print(f"source sha256: {hashlib.sha256(orig).hexdigest()}")
    print(f"patched sha256: {hashlib.sha256(patched).hexdigest()}")


if __name__ == "__main__":
    main(sys.argv)
