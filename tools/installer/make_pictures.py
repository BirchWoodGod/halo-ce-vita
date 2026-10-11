#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
MAKE_PICTURES.PY

The install helper's pictures (tools/installer/pictures/*.png): the project's
own screenshots of the port running on a PS Vita (docs/screenshots, the ones
README.md shows), cropped, shrunk to 76x43 pixels and cut down to a few
colours, for the window's retro look: the window zooms them up by whole
pixels (tkinter's PhotoImage.zoom), so each one shows as big square pixels.
Only these four screenshots are used: no box art, logos or other artwork.

Run by hand when the screenshots or the crops change, then commit the PNGs
(a few KB each; the window and the Windows program need only those):

    python -m pip install pillow
    python tools/installer/make_pictures.py
"""

import os
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SCREENSHOTS = os.path.join(HERE, "..", "..", "docs", "screenshots")
PICTURES_FOLDER = os.path.join(HERE, "pictures")

SIZE = (76, 43)  # 16:9, zoomed x3 to the sidebar's 228 pixels at 100%
COLOURS = 24

# picture: (screenshot, crop box in its 960x544 pixels: 16:9 inside the
# letterboxed ones' picture)
PICTURES = {
    "welcome.png": ("pelicans.png", (180, 68, 905, 476)),
    "xbox.png": ("beach-landing.png", (120, 68, 845, 476)),
    "movies.png": ("pelicans.png", (466, 262, 806, 453)),
    "pc.png": ("blood-gulch.png", (0, 0, 960, 544)),
    "copy.png": ("beach-landing.png", (352, 68, 832, 338)),
    "vpk.png": ("blood-gulch.png", (190, 60, 670, 330)),
    "done.png": ("warthog-beach.png", (0, 0, 960, 544)),
}


def make(source: str, box, path: str) -> None:
    with Image.open(source) as image:
        picture = image.convert("RGB").crop(box).resize(SIZE, Image.Resampling.BOX)
    picture = picture.quantize(COLOURS, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    picture.save(path, optimize=True)


def main() -> int:
    os.makedirs(PICTURES_FOLDER, exist_ok=True)
    for name, (screenshot, box) in PICTURES.items():
        path = os.path.join(PICTURES_FOLDER, name)
        make(os.path.join(SCREENSHOTS, screenshot), box, path)
        print("%s: %s %r, %d bytes" % (path, screenshot, box, os.path.getsize(path)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
