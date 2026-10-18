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
import struct
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
    return vertical_gradient((64, 64), [
        (0.0, (20, 40, 78, alpha(.78))),
        (0.6, (14, 28, 58, alpha(.84))),
        (1.0, (8, 16, 36, alpha(.90))),
    ])


def haze():
    # Halo 3's blue cast over the menu's 3D scene: full screen (a widget as
    # wide as the 640 columns stretches over the wide screen too), deeper at
    # the bottom
    return vertical_gradient((64, 64), [
        (0.0, (16, 34, 84, alpha(.50))),
        (1.0, (8, 18, 52, alpha(.64))),
    ])


def panel():
    # the lobby and settings panel (Halo Online's window gradient)
    return vertical_gradient((64, 64), [
        (0.0, (27, 41, 70, alpha(.95))),
        (0.2, (24, 39, 69, alpha(.90))),
        (1.0, (6, 16, 37, alpha(.90))),
    ])


def item_on():
    # the main menu's selection: Halo 3's solid orange bar, a light edge on top
    image = horizontal_gradient((128, 16), [
        (0.0, (184, 116, 58, 255)),
        (0.6, (168, 104, 58, 255)),
        (1.0, (138, 90, 58, 255)),
    ])
    draw = ImageDraw.Draw(image)
    draw.line([(0, 0), (127, 0)], fill=(255, 220, 180, alpha(.35)))
    draw.line([(0, 15), (127, 15)], fill=(0, 0, 0, alpha(.35)))
    return image


def item_off():
    # a list item's first frame (not focused): clear; the game shows the
    # second, item_on, on the focused one
    return Image.new('RGBA', (128, 16), (0, 0, 0, 0))


def row_on():
    # a lobby or settings row's selection: orange fading out by two thirds
    return horizontal_gradient((128, 16), [
        (0.0, (200, 120, 40, alpha(.55))),
        (0.66, (200, 120, 40, 0)),
        (1.0, (200, 120, 40, 0)),
    ])


def strip():
    return Image.new('RGBA', (64, 16), (2, 5, 10, alpha(.88)))


def rule():
    image = horizontal_gradient((128, 2), [
        (0.0, (160, 180, 205, 0)),
        (0.2, (160, 180, 205, alpha(.55))),
        (0.8, (160, 180, 205, alpha(.55))),
        (1.0, (160, 180, 205, 0)),
    ])
    return image


def nameplate():
    # Halo 3's lobby nameplate: a green bar (the default player colour), the
    # emblem square at the left lighter and the rank square at the right darker
    image = horizontal_gradient((256, 16), [
        (0.0, (169, 197, 58, 255)),
        (0.8, (185, 210, 74, 255)),
        (1.0, (143, 168, 58, 255)),
    ])
    draw = ImageDraw.Draw(image)
    draw.rectangle([0, 0, 15, 15], fill=(205, 222, 140, 255))
    draw.rectangle([240, 0, 255, 15], fill=(120, 140, 48, 255))
    draw.rectangle([0, 0, 255, 15], outline=(255, 255, 255, alpha(.35)))
    return image


def server_plate():
    # a live server's row: Halo 3's blue nameplate, fading out to the right
    image = horizontal_gradient((256, 16), [
        (0.0, (60, 110, 170, alpha(.85))),
        (1.0, (40, 80, 130, alpha(.45))),
    ])
    draw = ImageDraw.Draw(image)
    draw.rectangle([0, 0, 15, 15], fill=(25, 45, 75, alpha(.9)))
    draw.rectangle([0, 0, 255, 15], outline=(255, 255, 255, alpha(.25)))
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
    return image.resize((16, 16), Image.Resampling.LANCZOS)


def title_halo():
    # HALO in a soft blue glow, COMBAT EVOLVED under it (Halo 3's title look)
    # (drawn at four times and scaled down: the menus draw a picture 1:1,
    # 256x64 in the 640x480 screen)
    scale = 4
    image = Image.new('RGBA', (256 * scale, 64 * scale), (0, 0, 0, 0))
    big = ImageFont.truetype(os.path.join(FONTS, 'SairaSemiCondensed-Medium.ttf'), 42 * scale)
    small = ImageFont.truetype(os.path.join(FONTS, 'SairaSemiCondensed-Regular.ttf'), 9 * scale)
    text = Image.new('RGBA', image.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(text)
    spacing = 11 * scale
    width = 256 * scale
    widths = [draw.textlength(c, font=big) for c in 'HALO']
    total = sum(widths) + spacing * 3
    x = (width - total) / 2
    for c, w in zip('HALO', widths):
        draw.text((x, -4 * scale), c, font=big, fill=(207, 227, 245, 255))
        x += w + spacing
    sub = 'COMBAT EVOLVED'
    sub_spacing = 4.5 * scale
    sub_widths = [draw.textlength(c, font=small) for c in sub]
    sub_total = sum(sub_widths) + sub_spacing * (len(sub) - 1)
    x = (width - sub_total) / 2
    for c, w in zip(sub, sub_widths):
        draw.text((x, 49 * scale), c, font=small, fill=(159, 182, 204, 255))
        x += w + sub_spacing
    glow = text.split()[3].filter(ImageFilter.GaussianBlur(3.5 * scale))
    halo = Image.new('RGBA', image.size, (140, 200, 255, 0))
    halo.putalpha(glow.point(lambda value: round(value * .55)))
    image = Image.alpha_composite(image, halo)
    image = Image.alpha_composite(image, text)
    return image.resize((256, 64), Image.Resampling.LANCZOS)


# the menus' fonts (menu_tags.c makes each a font tag of the game's): the
# characters of English and Spanish (32 to 126, 160 to 255), each its advance,
# its pixels' box and where that box sits from the pen on the baseline, and its
# pixels, 8 bits each, anti-aliased. The file: "H3FN", version 1, ascending,
# descending, leading height and width, the count; then per character its code,
# advance, box width and height, origin x and y (the pen minus the box's left,
# the baseline minus its top); then the pixels, character after character
FONT_FILES = {
    'font_title.bin': ('SairaSemiCondensed-Medium.ttf', 22, 1),
    'font_item.bin': ('SairaSemiCondensed-Regular.ttf', 15, 0),
    'font_small.bin': ('SairaSemiCondensed-Regular.ttf', 12, 0),
}


def font_file(face, size, tracking):
    font = ImageFont.truetype(os.path.join(FONTS, face), size)
    ascent, descent = font.getmetrics()
    codes = list(range(32, 127)) + list(range(160, 256))
    records = []
    pixels = bytearray()
    for code in codes:
        char = chr(code)
        advance = round(font.getlength(char)) + tracking
        x0, y0, x1, y1 = font.getbbox(char, anchor='ls')
        width, height = max(x1 - x0, 0), max(y1 - y0, 0)
        if width and height:
            image = Image.new('L', (width, height), 0)
            ImageDraw.Draw(image).text((-x0, -y0), char, font=font, fill=255, anchor='ls')
            pixels += image.tobytes()
        records.append(struct.pack('<Hhhhhh', code, advance, width, height, -x0, -y0))
    # (the font's own ascent and descent leave room for accents above the
    # capitals and more: a widget draws a line only when its height fits,
    # so the heights are the characters' own, the highest above the baseline
    # and the lowest below it)
    tops = [struct.unpack('<Hhhhhh', r)[5] for r in records]
    bottoms = [struct.unpack('<Hhhhhh', r)[3] - struct.unpack('<Hhhhhh', r)[5] for r in records]
    ascent, descent = max(tops), max(max(bottoms), 0)
    header = b'H3FN' + struct.pack('<Hhhhhh', 1, ascent, descent, 0, 0, len(codes))
    return header + b''.join(records) + bytes(pixels)


ART = {
    'column.png': column,
    'haze.png': haze,
    'panel.png': panel,
    'item_on.png': item_on,
    'item_off.png': item_off,
    'row_on.png': row_on,
    'strip.png': strip,
    'rule.png': rule,
    'nameplate.png': nameplate,
    'server_plate.png': server_plate,
    'glyph_cross.png': lambda: glyph('cross'),
    'glyph_circle.png': lambda: glyph('circle'),
    'glyph_triangle.png': lambda: glyph('triangle'),
    'glyph_square.png': lambda: glyph('square'),
    'glyph_start.png': lambda: glyph('start'),
    'title_halo.png': title_halo,
}


def main(folder):
    os.makedirs(folder, exist_ok=True)
    for name, make in ART.items():
        # (no time or software stamp: the same bytes each run)
        make().save(os.path.join(folder, name), format='PNG', optimize=True)
    for name, (face, size, tracking) in FONT_FILES.items():
        with open(os.path.join(folder, name), 'wb') as out:
            out.write(font_file(face, size, tracking))


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
