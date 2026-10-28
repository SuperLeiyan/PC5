#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Makes the update zip: the files of a new app zip that differ from an older one's.

    tools/make-update.py OLD.zip NEW.zip OUT.zip [--markdown FILE]

OLD.zip is the last published release's PPSA11018.zip, NEW.zip the one being released.
OUT.zip (PPSA11018-update.zip) holds each file of NEW that OLD doesn't have, or has with
another size, CRC-32 or SHA-256 (from NEW's pc5/manifest.txt), and always that list
itself. PC5's updater installs from it when every other file of the new release is on
the console as the list says; otherwise it downloads the whole app (title/pc5_upgrade.c).

The files keep their names (under PPSA11018/), modes and dates, deflated as
tools/make-app.py does. Prints how many files went in and the zip's size against NEW's;
--markdown also adds that to a file (GitHub's job summary)."""
import argparse
import hashlib
import sys
import zipfile
from pathlib import Path

PREFIX = "PPSA11018/"
MANIFEST = PREFIX + "pc5/manifest.txt"


def manifest_of(archive: zipfile.ZipFile, where: str) -> dict:
    """NEW's list: name (under PREFIX) -> (size, sha256)."""
    try:
        text = archive.read(MANIFEST).decode("utf-8")
    except KeyError:
        sys.exit(f"make-update: {where} has no {MANIFEST} (made by an older tools/make-app.py?)")
    listed = {}
    for number, line in enumerate(text.split("\n"), 1):
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) != 3 or not parts[1].isdigit() or len(parts[2]) != 64:
            sys.exit(f"make-update: {where}'s {MANIFEST} line {number} isn't name, size and SHA-256")
        listed[PREFIX + parts[0]] = (int(parts[1]), parts[2])
    return listed


def sha256_in(archive: zipfile.ZipFile, name: str) -> str:
    digest = hashlib.sha256()
    with archive.open(name) as source:
        while chunk := source.read(1 << 20):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("old", type=Path)
    parser.add_argument("new", type=Path)
    parser.add_argument("out", type=Path)
    parser.add_argument("--markdown", type=Path)
    args = parser.parse_args()

    with zipfile.ZipFile(args.old) as old, zipfile.ZipFile(args.new) as new:
        before = {info.filename: info for info in old.infolist() if not info.is_dir()}
        files = [info for info in new.infolist() if not info.is_dir()]
        listed = manifest_of(new, args.new.name)
        # The list is the new release's files: PC5 trusts it for what it doesn't download.
        names = {info.filename for info in files} - {MANIFEST}
        if names != set(listed):
            odd = sorted(names ^ set(listed))
            sys.exit(f"make-update: {args.new.name}'s {MANIFEST} doesn't list its files ({len(odd)} differ, "
                     f"{odd[0]} first)")
        for info in files:
            if info.filename != MANIFEST and info.file_size != listed[info.filename][0]:
                sys.exit(f"make-update: {info.filename} isn't the size {MANIFEST} gives it")
        taken = []
        for info in files:
            was = before.get(info.filename)
            if info.filename == MANIFEST or was is None or (was.file_size, was.CRC) != (info.file_size, info.CRC):
                taken.append(info)
            elif sha256_in(old, info.filename) != listed[info.filename][1]:
                taken.append(info)   # the same size and CRC-32, other bytes
        part = args.out.with_suffix(".zip.part")
        with zipfile.ZipFile(part, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for info in taken:
                copy = zipfile.ZipInfo(info.filename, info.date_time)
                copy.create_system = info.create_system
                copy.external_attr = info.external_attr
                copy.compress_type = zipfile.ZIP_DEFLATED
                with new.open(info) as source, archive.open(copy, "w") as target:
                    while chunk := source.read(1 << 20):
                        target.write(chunk)
        part.replace(args.out)

    size, full = args.out.stat().st_size, args.new.stat().st_size
    summary = (f"{len(taken)} of {len(files)} files ({len(taken) - 1} changed or new, and the list): "
               f"{size / 1e6:.1f} MB against the full zip's {full / 1e6:.1f} MB ({size * 100 / max(full, 1):.1f}%)")
    print(f"make-update: {args.out}: {summary}")
    if args.markdown:
        with args.markdown.open("a") as out:
            out.write(f"### The update zip\n\n{args.out.name}: {summary}\n\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
