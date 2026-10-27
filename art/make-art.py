#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Makes PC5's tile icon and its Home-screen background from the pictures beside this script.

    art/make-art.py [OUT_DIR]        (default: art/)

icon0.png  512x512    from icon-source.png: a gaming PC tower lit in blue (no lettering:
                      Home shows the app's name under it)
pic0.png   1920x1080  from background-source.png: a PS5 and a PC at night, a game's world
                      between them

Each picture is scaled to cover its size and cut to the middle (the background's sides lose a
pixel at most). The output depends only on the two pictures, this script and Pillow.
"""
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent


def cover(source: Path, width: int, height: int) -> Image.Image:
    """The picture scaled to cover width x height, cut to the middle."""
    with Image.open(source) as picture:
        picture = picture.convert("RGB")
        scale = max(width / picture.width, height / picture.height)
        size = (max(width, round(picture.width * scale)), max(height, round(picture.height * scale)))
        picture = picture.resize(size, Image.LANCZOS)
        left, top = (size[0] - width) // 2, (size[1] - height) // 2
        return picture.crop((left, top, left + width, top + height))


def main() -> int:
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else HERE
    out.mkdir(parents=True, exist_ok=True)
    cover(HERE / "icon-source.png", 512, 512).save(out / "icon0.png", optimize=True)
    cover(HERE / "background-source.png", 1920, 1080).save(out / "pic0.png", optimize=True)
    print(f"make-art: {out / 'icon0.png'} and {out / 'pic0.png'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
