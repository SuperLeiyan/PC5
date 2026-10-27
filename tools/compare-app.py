#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Compares two builds of the app file by file: what changed, came or went between them.

    tools/compare-app.py OLD.zip NEW.zip [--markdown FILE]

Each file is compared by its size and CRC-32 in the zip. Prints one line a file that
differs, and a count; --markdown also writes them as a table (GitHub's job summary)."""
import argparse
import sys
import zipfile
from pathlib import Path


def entries(path: Path) -> dict:
    with zipfile.ZipFile(path) as archive:
        return {info.filename: (info.file_size, info.CRC) for info in archive.infolist() if not info.is_dir()}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("old", type=Path)
    parser.add_argument("new", type=Path)
    parser.add_argument("--markdown", type=Path)
    args = parser.parse_args()
    old, new = entries(args.old), entries(args.new)
    rows = []
    for name in sorted(old.keys() | new.keys()):
        if name not in new:
            rows.append(("gone", name, f"{old[name][0]} bytes"))
        elif name not in old:
            rows.append(("new", name, f"{new[name][0]} bytes"))
        elif old[name] != new[name]:
            rows.append(("changed", name, f"{old[name][0]} -> {new[name][0]} bytes"))
    same = len(old.keys() & new.keys()) - sum(1 for row in rows if row[0] == "changed")
    for what, name, sizes in rows:
        print(f"{what:8} {name}  ({sizes})")
    counts = {what: sum(1 for row in rows if row[0] == what) for what in ("changed", "new", "gone")}
    summary = (f"{same} files the same, {counts['changed']} changed, {counts['new']} new, "
               f"{counts['gone']} gone ({args.old.name} against {args.new.name})")
    print(summary)
    if args.markdown:
        with args.markdown.open("a") as out:
            out.write(f"### The app, file by file\n\n{summary}\n\n")
            if rows:
                out.write("| | File | Size |\n|---|---|---|\n")
                out.writelines(f"| {what} | `{name}` | {sizes} |\n" for what, name, sizes in rows)
                out.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
