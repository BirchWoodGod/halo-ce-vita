#!/usr/bin/env python3
"""The Halo 3-style menus' own art (port/vita/app0/menus/halo3): panels,
selection bars, strips, rules, the nameplate, the PlayStation button glyphs
and the HALO title, drawn here, not taken from any game. The colours are the
design's (docs/superpowers/specs/2026-10-18-halo3-menus-design.md), which
follows Halo 3's own screens; the title's font is Saira Semi Condensed (SIL
OFL, tools/fonts/OFL.txt). The same bytes each run.

    python3 tools/halo3_menu_art.py port/vita/app0/menus/halo3
"""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

FONTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fonts')


def lerp(a, b, t):
    return tuple(round(x + (y - x) * t) for x, y in zip(a, b))


def vertical_gradient(size, stops):
    """stops: [(position 0..1, (r, g, b, a)), ...]"""
    width, height = size
    image = Image.new('RGBA', size)
    pixels = image.load()
    for y in range(height):
        t = y / max(height - 1, 1)
        for (p0, c0), (p1, c1) in zip(stops, stops[1:]):
            if p0 <= t <= p1:
                colour = lerp(c0, c1, (t - p0) / max(p1 - p0, 1e-9))
                break
        else:
            colour = stops[-1][1]
        for x in range(width):
            pixels[x, y] = colour
    return image


def horizontal_gradient(size, stops):
    image = vertical_gradient((size[1], size[0]), stops)
    return image.transpose(Image.Transpose.TRANSPOSE)


def alpha(value):
    return round(value * 255)


def column():
    # Halo 3's main-menu column: navy, a little lighter at the top
    return vertical_gradient((256, 256), [
        (0.0, (20, 40, 78, alpha(.78))),
        (0.6, (14, 28, 58, alpha(.84))),
        (1.0, (8, 16, 36, alpha(.90))),
    ])


def panel():
    # the lobby and settings panel (Halo Online's window gradient)
    return vertical_gradient((256, 256), [
        (0.0, (27, 41, 70, alpha(.95))),
        (0.2, (24, 39, 69, alpha(.90))),
        (1.0, (6, 16, 37, alpha(.90))),
    ])


def item_on():
    # the main menu's selection: Halo 3's solid orange bar, a light edge on top
    image = horizontal_gradient((512, 32), [
        (0.0, (184, 116, 58, 255)),
        (0.6, (168, 104, 58, 255)),
        (1.0, (138, 90, 58, 255)),
    ])
    draw = ImageDraw.Draw(image)
    draw.line([(0, 0), (511, 0)], fill=(255, 220, 180, alpha(.35)))
    draw.line([(0, 31), (511, 31)], fill=(0, 0, 0, alpha(.35)))
    return image


def row_on():
    # a lobby or settings row's selection: orange fading out by two thirds
    return horizontal_gradient((512, 32), [
        (0.0, (200, 120, 40, alpha(.55))),
        (0.66, (200, 120, 40, 0)),
        (1.0, (200, 120, 40, 0)),
    ])


def strip():
    return Image.new('RGBA', (512, 32), (2, 5, 10, alpha(.88)))


def rule():
    image = horizontal_gradient((512, 2), [
        (0.0, (160, 180, 205, 0)),
        (0.2, (160, 180, 205, alpha(.55))),
        (0.8, (160, 180, 205, alpha(.55))),
        (1.0, (160, 180, 205, 0)),
    ])
    return image


def nameplate():
    # white, for the widget to tint with the profile's colour: the bar, the
    # emblem square at the left and the rank square at the right darker
    image = horizontal_gradient((512, 48), [
        (0.0, (238, 238, 238, 255)),
        (0.8, (255, 255, 255, 255)),
        (1.0, (205, 205, 205, 255)),
    ])
    draw = ImageDraw.Draw(image)
    draw.rectangle([0, 0, 47, 47], fill=(190, 190, 190, 255))
    draw.rectangle([464, 0, 511, 47], fill=(160, 160, 160, 255))
    draw.rectangle([0, 0, 511, 47], outline=(255, 255, 255, alpha(.6)))
    return image


GLYPH_COLOURS = {
    'cross': (156, 194, 255),
    'circle': (255, 122, 122),
    'triangle': (79, 224, 168),
    'square': (255, 155, 228),
    'start': (228, 232, 236),
}


def glyph(kind):
    # a PlayStation button: a dark round button, its symbol in its colour
    scale = 4
    size = 64 * scale
    image = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    centre = size / 2
    radius = 29 * scale
    for step in range(radius, 0, -1):
        t = step / radius
        shade = lerp((74, 79, 87), (27, 30, 35), 1 - t * t)
        draw.ellipse([centre - step, centre - step, centre + step, centre + step], fill=shade + (255,))
    draw.ellipse([centre - radius, centre - radius, centre + radius, centre + radius],
                 outline=(255, 255, 255, alpha(.3)), width=2 * scale)
    colour = GLYPH_COLOURS[kind] + (255,)
    width = 5 * scale
    s = 12 * scale
    if kind == 'cross':
        draw.line([(centre - s, centre - s), (centre + s, centre + s)], fill=colour, width=width)
        draw.line([(centre - s, centre + s), (centre + s, centre - s)], fill=colour, width=width)
    elif kind == 'circle':
        draw.ellipse([centre - s, centre - s, centre + s, centre + s], outline=colour, width=width)
    elif kind == 'triangle':
        h = s * math.sqrt(3) / 2
        points = [(centre, centre - s), (centre + s, centre + h * .9), (centre - s, centre + h * .9)]
        draw.line(points + [points[0]], fill=colour, width=width, joint='curve')
    elif kind == 'square':
        draw.rectangle([centre - s * .85, centre - s * .85, centre + s * .85, centre + s * .85], outline=colour, width=width)
    else:  # start: the Vita's START, a small play triangle
        draw.polygon([(centre - s * .55, centre - s * .8), (centre + s * .85, centre), (centre - s * .55, centre + s * .8)],
                     fill=colour)
    return image.resize((64, 64), Image.Resampling.LANCZOS)


def glyphs():
    sheet = Image.new('RGBA', (320, 64), (0, 0, 0, 0))
    for index, kind in enumerate(('cross', 'circle', 'triangle', 'square', 'start')):
        sheet.paste(glyph(kind), (index * 64, 0))
    return sheet


def title_halo():
    # HALO in a soft blue glow, COMBAT EVOLVED under it (Halo 3's title look)
    image = Image.new('RGBA', (512, 128), (0, 0, 0, 0))
    big = ImageFont.truetype(os.path.join(FONTS, 'SairaSemiCondensed-Medium.ttf'), 84)
    small = ImageFont.truetype(os.path.join(FONTS, 'SairaSemiCondensed-Regular.ttf'), 17)
    text = Image.new('RGBA', image.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(text)
    spacing = 22
    x = 512
    widths = [draw.textlength(c, font=big) for c in 'HALO']
    total = sum(widths) + spacing * 3
    x = (512 - total) / 2
    for c, w in zip('HALO', widths):
        draw.text((x, -6), c, font=big, fill=(207, 227, 245, 255))
        x += w + spacing
    sub = 'COMBAT EVOLVED'
    sub_spacing = 9
    sub_widths = [draw.textlength(c, font=small) for c in sub]
    sub_total = sum(sub_widths) + sub_spacing * (len(sub) - 1)
    x = (512 - sub_total) / 2
    for c, w in zip(sub, sub_widths):
        draw.text((x, 98), c, font=small, fill=(159, 182, 204, 255))
        x += w + sub_spacing
    glow = text.split()[3].filter(ImageFilter.GaussianBlur(7))
    halo = Image.new('RGBA', image.size, (140, 200, 255, 0))
    halo.putalpha(glow.point(lambda value: round(value * .55)))
    image = Image.alpha_composite(image, halo)
    return Image.alpha_composite(image, text)


ART = {
    'column.png': column,
    'panel.png': panel,
    'item_on.png': item_on,
    'row_on.png': row_on,
    'strip.png': strip,
    'rule.png': rule,
    'nameplate.png': nameplate,
    'glyphs.png': glyphs,
    'title_halo.png': title_halo,
}


def main(folder):
    os.makedirs(folder, exist_ok=True)
    for name, make in ART.items():
        # (no time or software stamp: the same bytes each run)
        make().save(os.path.join(folder, name), format='PNG', optimize=True)


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
