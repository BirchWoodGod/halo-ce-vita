#!/usr/bin/env python3
"""The Halo CE menus' additions' own art (port/vita/app0/menus/classic): the
multiplayer pause's box for its four rows (pausebox.png), drawn here in the
look of the game's own pause box - its blue border, dark blue glass and the
line over the button key - not taken from any game. The same bytes each run.

    python3 tools/classic_menu_art.py port/vita/app0/menus/classic
"""
import os
import sys

from PIL import Image, ImageDraw

# (the widget it is drawn into, fit="widget": 226 x 229; the PNG a power of
# two, so drawn at that scale and stretched back by the game)
WIDGET = (226, 229)
SIZE = (512, 512)
BORDER = (40, 150, 255, 255)
SEPARATOR = (32, 94, 154, 255)
# (where the line over the button key falls, in the widget's pixels)
SEPARATOR_Y = 200


def pausebox():
    sx, sy = SIZE[0] / WIDGET[0], SIZE[1] / WIDGET[1]
    image = Image.new('RGBA', SIZE, (0, 0, 0, 0))
    # the glass: lighter at the top, darkest under the line
    glass = Image.new('RGBA', SIZE, (0, 0, 0, 0))
    pixels = glass.load()
    for y in range(SIZE[1]):
        t = y / sy / WIDGET[1]
        if y / sy < SEPARATOR_Y:
            colour = tuple(round(a + (b - a) * (y / sy / SEPARATOR_Y)) for a, b in zip((20, 40, 66), (10, 20, 36)))
            alpha = 200
        else:
            colour, alpha = (6, 10, 16), 215
        for x in range(SIZE[0]):
            pixels[x, y] = colour + (alpha,)
    mask = Image.new('L', SIZE, 0)
    radius = (round(9 * sx), round(9 * sy))
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, SIZE[0] - 1, SIZE[1] - 1), radius=min(radius), fill=255)
    image.paste(glass, (0, 0), mask)
    draw = ImageDraw.Draw(image)
    width = round(2 * sx)
    draw.rounded_rectangle((width // 2, width // 2, SIZE[0] - 1 - width // 2, SIZE[1] - 1 - width // 2),
                           radius=min(radius), outline=BORDER, width=width)
    line_y = round(SEPARATOR_Y * sy)
    draw.rectangle((width, line_y, SIZE[0] - 1 - width, line_y + round(sy) - 1), fill=SEPARATOR)
    return image


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else 'port/vita/app0/menus/classic'
    pausebox().save(os.path.join(folder, 'pausebox.png'), optimize=True)


if __name__ == '__main__':
    main()
