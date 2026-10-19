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


def custom_panel():
    # CUSTOMIZATION's editor: the panel (as panel(), over the editor's 560x400
    # at 40,40, stretched) with a window at the right where the scene packs'
    # Spartan stands (the preview, 396,98 to 592,304 on the screen), rimmed
    width, height = 256, 256
    image = vertical_gradient((width, height), [
        (0.0, (27, 41, 70, alpha(.95))),
        (0.2, (24, 39, 69, alpha(.90))),
        (1.0, (6, 16, 37, alpha(.90))),
    ])
    x0, x1 = round(356 / 560 * width), round(552 / 560 * width)
    y0, y1 = round(58 / 400 * height), round(264 / 400 * height)
    draw = ImageDraw.Draw(image)
    draw.rectangle([x0, y0, x1, y1], fill=(0, 0, 0, 0))
    draw.rectangle([x0, y0, x1, y1], outline=(150, 176, 206, alpha(.45)))
    return image


def profiles_panel():
    # SELECT PROFILE's (and CUSTOMIZATION's list's) panel: as custom_panel(),
    # its window at the card's lower right, where the focused profile's
    # Spartan stands (486,146 to 592,334 on the screen), rimmed
    width, height = 256, 256
    image = vertical_gradient((width, height), [
        (0.0, (27, 41, 70, alpha(.95))),
        (0.2, (24, 39, 69, alpha(.90))),
        (1.0, (6, 16, 37, alpha(.90))),
    ])
    x0, x1 = round(446 / 560 * width), round(552 / 560 * width)
    y0, y1 = round(106 / 400 * height), round(294 / 400 * height)
    draw = ImageDraw.Draw(image)
    draw.rectangle([x0, y0, x1, y1], fill=(0, 0, 0, 0))
    draw.rectangle([x0, y0, x1, y1], outline=(150, 176, 206, alpha(.45)))
    return image


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


def nameplate_colour(colour):
    # the nameplate in a profile's primary colour (the main menu's, the
    # lobbies' and the pause screen's: menu_functions.c h3_nameplate_update
    # picks the frame): the bar the colour made light enough that the dark
    # name on it reads on every colour, the emblem square at the left the
    # colour itself, the rank square at the right darker, as the green one
    rgb = ((colour >> 16) & 0xFF, (colour >> 8) & 0xFF, colour & 0xFF)
    luminance = (0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2]) / 255
    t = 0.62 if luminance < 0.2 else 0.5 if luminance < 0.6 else 0.25
    bar = tuple(round(c + (255 - c) * t) for c in rgb)
    image = horizontal_gradient((256, 16), [
        (0.0, tuple(round(c * .94) for c in bar) + (255,)),
        (0.8, bar + (255,)),
        (1.0, tuple(round(c * .82) for c in bar) + (255,)),
    ])
    draw = ImageDraw.Draw(image)
    draw.rectangle([0, 0, 15, 15], fill=rgb + (255,))
    draw.rectangle([240, 0, 255, 15], fill=tuple(round(c * .62) for c in bar) + (255,))
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
    # (SELECT and the D-pad: START's)
    'select': (228, 232, 236),
    'up': (228, 232, 236),
    'down': (228, 232, 236),
    'left': (228, 232, 236),
    'right': (228, 232, 236),
}


def glyph(kind, size=16):
    # a PlayStation button: a dark round button, its symbol in its colour
    # (16x16 for the menus; the in-game atlas below draws the same at 32)
    scale = 4
    canvas = 64 * scale
    image = Image.new('RGBA', (canvas, canvas), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    centre = canvas / 2
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
    elif kind == 'select':
        # the Vita's SELECT: a small pill, beside START's play triangle
        draw.rounded_rectangle([centre - s * .95, centre - s * .42, centre + s * .95, centre + s * .42],
                               radius=s * .42, fill=colour)
    elif kind in DPAD_ARMS:
        # a D-pad direction: the pad's cross, dim, and the arm pressed lit,
        # wider and pointed (readable at the HUD's size)
        arm, half = s * 1.1, s * .3
        dim = (84, 90, 100, 255)
        for dx, dy in DPAD_ARMS.values():
            draw.rectangle(dpad_arm(centre, dx, dy, arm * .9, half), fill=dim)
        draw.rectangle([centre - half, centre - half, centre + half, centre + half], fill=dim)
        dx, dy = DPAD_ARMS[kind]
        wide, base, tip = s * .5, s * .05, arm
        # (an arrow along the arm: its square base, then its point)
        px, py = -dy, dx
        mid = arm * .55
        points = [(centre + dx * base + px * wide, centre + dy * base + py * wide),
                  (centre + dx * mid + px * wide, centre + dy * mid + py * wide),
                  (centre + dx * tip, centre + dy * tip),
                  (centre + dx * mid - px * wide, centre + dy * mid - py * wide),
                  (centre + dx * base - px * wide, centre + dy * base - py * wide)]
        draw.polygon(points, fill=colour)
    else:  # start: the Vita's START, a small play triangle
        draw.polygon([(centre - s * .55, centre - s * .8), (centre + s * .85, centre), (centre - s * .55, centre + s * .8)],
                     fill=colour)
    return image.resize((size, size), Image.Resampling.LANCZOS)


# the D-pad's arms: the direction each points (the screen's y down)
DPAD_ARMS = {'up': (0, -1), 'down': (0, 1), 'left': (-1, 0), 'right': (1, 0)}


def dpad_arm(centre, dx, dy, arm, half):
    # one arm of the D-pad's cross: from the middle square out to `arm`
    if dx:
        x0, x1 = sorted((centre + dx * half, centre + dx * arm))
        return [x0, centre - half, x1, centre + half]
    y0, y1 = sorted((centre + dy * half, centre + dy * arm))
    return [centre - half, y0, centre + half, y1]


def dpad_left_right(size=16):
    # the D-pad's left and right, a list's two ways (the browsers' filter):
    # two pointed arms, back to back, in the START glyph's colour on a dark
    # rounded tab as the shoulders' (readable at 16)
    scale = 4
    canvas = 64 * scale
    image = Image.new('RGBA', (canvas, canvas), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle([2 * scale, 14 * scale, 62 * scale, 50 * scale], radius=14 * scale, fill=(44, 48, 55, 255),
                           outline=(255, 255, 255, alpha(.3)), width=2 * scale)
    colour = GLYPH_COLOURS['start'] + (255,)
    centre, half = canvas / 2, 11 * scale
    for dx in (-1, 1):
        draw.polygon([(centre + dx * 4 * scale, centre - half), (centre + dx * 26 * scale, centre),
                      (centre + dx * 4 * scale, centre + half)], fill=colour)
    return image.resize((size, size), Image.Resampling.LANCZOS)


def shoulder(letter, size=16):
    # the Vita's L or R: a dark rounded tab with its letter (as the round
    # buttons' glyphs, its colour the START glyph's)
    scale = 4
    canvas = 64 * scale
    image = Image.new('RGBA', (canvas, canvas), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    box = [6 * scale, 14 * scale, 58 * scale, 50 * scale]
    draw.rounded_rectangle(box, radius=14 * scale, fill=(44, 48, 55, 255),
                           outline=(255, 255, 255, alpha(.3)), width=2 * scale)
    font = ImageFont.truetype(os.path.join(FONTS, 'SairaSemiCondensed-Medium.ttf'), 34 * scale)
    width = draw.textlength(letter, font=font)
    draw.text(((canvas - width) / 2, 6 * scale), letter, font=font, fill=GLYPH_COLOURS['start'] + (255,))
    return image.resize((size, size), Image.Resampling.LANCZOS)


# the in-game button icons (Button icons PlayStation: source/interface/hud_draw.c
# draws the HUD's prompts, the Halo CE menus' text and button hints and the
# pause menu with these): the same glyphs as the menus', at twice their size,
# GLYPH_CELL square each, four to a row, in the order of the game's
# VITA_GLYPH_CROSS ... VITA_GLYPH_SELECT (port/vita/include/vita_controls.h);
# the cells after them are empty
GLYPH_CELL = 32
ATLAS_GLYPHS = ('cross', 'circle', 'square', 'triangle', 'L', 'R', 'up', 'down', 'left', 'right', 'start', 'select')


def glyph_atlas():
    image = Image.new('RGBA', (GLYPH_CELL * 4, GLYPH_CELL * 4), (0, 0, 0, 0))
    for index, kind in enumerate(ATLAS_GLYPHS):
        cell = shoulder(kind, GLYPH_CELL) if kind in ('L', 'R') else glyph(kind, GLYPH_CELL)
        image.paste(cell, ((index % 4) * GLYPH_CELL, (index // 4) * GLYPH_CELL))
    return image


def forge_emblem():
    # Forge's emblem over the lobby's map picture (where Halo 3 puts its
    # hammer): a monitor's eye of our own - a pale blue ring round a glowing
    # core, on a dark disc, 44x48 as the difficulty emblems
    scale = 4
    width, height = 44 * scale, 48 * scale
    image = Image.new('RGBA', (width, height), (0, 0, 0, 0))
    glow = Image.new('RGBA', image.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(glow)
    cx, cy = width / 2, height / 2
    draw.ellipse([cx - 19 * scale, cy - 19 * scale, cx + 19 * scale, cy + 19 * scale], fill=(10, 20, 38, alpha(.82)))
    draw.ellipse([cx - 15 * scale, cy - 15 * scale, cx + 15 * scale, cy + 15 * scale],
                 outline=(150, 205, 255, 255), width=3 * scale)
    draw.ellipse([cx - 7 * scale, cy - 7 * scale, cx + 7 * scale, cy + 7 * scale], fill=(120, 200, 255, 255))
    blurred = glow.filter(ImageFilter.GaussianBlur(2.5 * scale))
    image = Image.alpha_composite(image, blurred)
    image = Image.alpha_composite(image, glow)
    core = ImageDraw.Draw(image)
    core.ellipse([cx - 3 * scale, cy - 3 * scale, cx + 3 * scale, cy + 3 * scale], fill=(235, 248, 255, 255))
    # (in a 64x64 picture, at its top left: the menus' pictures are powers
    # of two, menu_tags.c bitmap_build)
    square = Image.new('RGBA', (64, 64), (0, 0, 0, 0))
    square.paste(image.resize((44, 48), Image.Resampling.LANCZOS), (0, 0))
    return square


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
# (the overlay's are the Vita settings panel's, drawn at the screen's 960x544
# rather than the menus' 640x480: vita_overlay_font.c; its item font carries
# the PlayStation buttons' symbols as characters 1 to 5)
FONT_FILES = {
    'font_title.bin': ('SairaSemiCondensed-Medium.ttf', 22, 1, False),
    'font_item.bin': ('SairaSemiCondensed-Regular.ttf', 15, 0, False),
    'font_small.bin': ('SairaSemiCondensed-Regular.ttf', 12, 0, False),
    'overlay_title.bin': ('SairaSemiCondensed-Medium.ttf', 34, 1, False),
    'overlay_item.bin': ('SairaSemiCondensed-Regular.ttf', 19, 0, True),
    'overlay_small.bin': ('SairaSemiCondensed-Regular.ttf', 15, 0, False),
}
SYMBOLS = {1: 'cross', 2: 'circle', 3: 'triangle', 4: 'square', 5: 'disc'}


def symbol(kind, diameter):
    # a button's symbol, coverage only (the overlay colours it), or the
    # button's round disc behind it; drawn large, then made small
    scale = 4
    size = diameter * scale
    image = Image.new('L', (size, size), 0)
    draw = ImageDraw.Draw(image)
    centre = size / 2
    width = max(round(diameter * .1), 2) * scale
    s = size * .22
    if kind == 'disc':
        draw.ellipse([0, 0, size - 1, size - 1], fill=255)
    elif kind == 'cross':
        draw.line([(centre - s, centre - s), (centre + s, centre + s)], fill=255, width=width)
        draw.line([(centre - s, centre + s), (centre + s, centre - s)], fill=255, width=width)
    elif kind == 'circle':
        draw.ellipse([centre - s, centre - s, centre + s, centre + s], outline=255, width=width)
    elif kind == 'triangle':
        h = s * math.sqrt(3) / 2
        points = [(centre, centre - s), (centre + s, centre + h * .9), (centre - s, centre + h * .9)]
        draw.line(points + [points[0]], fill=255, width=width, joint='curve')
    else:
        draw.rectangle([centre - s * .85, centre - s * .85, centre + s * .85, centre + s * .85], outline=255, width=width)
    return image.resize((diameter, diameter), Image.Resampling.LANCZOS)


def font_file(face, size, tracking, symbols):
    font = ImageFont.truetype(os.path.join(FONTS, face), size)
    ascent, descent = font.getmetrics()
    codes = list(range(32, 127)) + list(range(160, 256))
    records = []
    pixels = bytearray()
    if symbols:
        # (a little taller than a capital, centred on the capitals' middle)
        capital = -font.getbbox('H', anchor='ls')[1]
        diameter = round(capital * 1.6)
        for code, kind in SYMBOLS.items():
            pixels += symbol(kind, diameter).tobytes()
            records.append(struct.pack('<Hhhhhh', code, diameter + round(size * .3), diameter, diameter, 0,
                                       round(capital / 2 + diameter / 2)))
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
    header = b'H3FN' + struct.pack('<Hhhhhh', 1, ascent, descent, 0, 0, len(records))
    return header + b''.join(records) + bytes(pixels)


def lock_glyph(scale):
    # a game with a password: a small padlock, light on the dark rows (16x16
    # at scale 1)
    image = Image.new('RGBA', (16 * scale, 16 * scale), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    colour = (222, 228, 236, 255)
    width = round(1.6 * scale)
    draw.arc([4.5 * scale, 1.5 * scale, 11.5 * scale, 10 * scale], 180, 360, fill=colour, width=width)
    draw.line([(4.5 * scale + width / 2, 5.5 * scale), (4.5 * scale + width / 2, 8 * scale)], fill=colour, width=width)
    draw.line([(11.5 * scale - width / 2, 5.5 * scale), (11.5 * scale - width / 2, 8 * scale)], fill=colour, width=width)
    draw.rounded_rectangle([3 * scale, 7.5 * scale, 13 * scale, 14.5 * scale], radius=scale, fill=colour)
    draw.ellipse([7.2 * scale, 9.6 * scale, 8.8 * scale, 11.2 * scale], fill=(40, 50, 64, 255))
    return image


def badge(text, fill, locked):
    # the server browser's row badge (multiplayer.xml row_badge): the game's
    # platform, a small rounded plate with the word in the menus' font (our
    # own drawing, no maker's logo), and the padlock beside it when the game
    # has a password; 64x16, the size of its widget (one widget for both,
    # which the menus' widget memory has room for)
    scale = 4
    image = Image.new('RGBA', (64 * scale, 16 * scale), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle([1 * scale, 2 * scale, 31 * scale - 1, 14 * scale - 1], radius=3 * scale,
                           fill=fill + (235,), outline=(255, 255, 255, alpha(.45)), width=scale)
    font = ImageFont.truetype(os.path.join(FONTS, 'SairaSemiCondensed-Medium.ttf'), 10 * scale)
    width = draw.textlength(text, font=font)
    draw.text(((32 * scale - width) / 2, 8 * scale), text, font=font, fill=(246, 248, 250, 255), anchor='lm')
    if locked:
        image.alpha_composite(lock_glyph(scale), (35 * scale, 0))
    return image.resize((64, 16), Image.Resampling.LANCZOS)


def arrow(pointing):
    # a value's ‹ and › on Create Game's rows (the spinners' header and
    # footer), ice blue as the values: 8x16
    scale = 4
    image = Image.new('RGBA', (8 * scale, 16 * scale), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    points = [(5.5 * scale, 4 * scale), (2.5 * scale, 8 * scale), (5.5 * scale, 12 * scale)]
    if pointing == 'right':
        points = [(8 * scale - x, y) for x, y in points]
    draw.line(points, fill=(170, 190, 214, 255), width=round(1.6 * scale), joint='curve')
    return image.resize((8, 16), Image.Resampling.LANCZOS)


# the player colours a profile may have, in the game's order
# (source/saved games/player_profile.c profile_color_table): white, black,
# red, blue, gray, yellow, green, pink, purple, cyan, cobalt, orange, teal,
# sage, brown, tan, maroon, salmon
PLAYER_COLOURS = [
    0xFFFFFF, 0x000000, 0xFE0000, 0x0201E3, 0x707E71, 0xFFFF01, 0x00FF01, 0xFF56B9, 0xAB10F4,
    0x01FFFF, 0x6493ED, 0xFF7F00, 0x1ECC91, 0x006401, 0x603814, 0xC69C6C, 0x9D0B0E, 0xF5999E,
]


def swatch(colour):
    # a profile's colour on the profile screens (the list's rows, the
    # chosen one's and CUSTOMIZATION's larger swatch, stretched): the colour,
    # a little lighter at the top, with a thin rim; colour None is a profile
    # with none, which the host gives a colour (four quarters of colours)
    size = 16
    if colour is None:
        image = Image.new('RGBA', (size, size))
        draw = ImageDraw.Draw(image)
        quarters = [(0xFE, 0x00, 0x00), (0x02, 0x01, 0xE3), (0xFF, 0xFF, 0x01), (0x00, 0xFF, 0x01)]
        for index, fill in enumerate(quarters):
            x, y = (index % 2) * 8, (index // 2) * 8
            draw.rectangle([x, y, x + 7, y + 7], fill=fill + (255,))
    else:
        rgb = ((colour >> 16) & 0xFF, (colour >> 8) & 0xFF, colour & 0xFF)
        top = tuple(min(255, round(c + (255 - c) * .18)) for c in rgb)
        bottom = tuple(round(c * .82) for c in rgb)
        image = vertical_gradient((size, size), [(0.0, top + (255,)), (1.0, bottom + (255,))])
        draw = ImageDraw.Draw(image)
    draw.rectangle([0, 0, size - 1, size - 1], outline=(200, 214, 230, alpha(.65)))
    return image


ART = {
    'column.png': column,
    'haze.png': haze,
    'panel.png': panel,
    'custom_panel.png': custom_panel,
    'profiles_panel.png': profiles_panel,
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
    'glyph_dpad_lr.png': dpad_left_right,
    'glyph_l.png': lambda: shoulder('L'),
    'glyph_r.png': lambda: shoulder('R'),
    'glyph_atlas.png': glyph_atlas,
    'forge_emblem.png': forge_emblem,
    'title_halo.png': title_halo,
    'badge_vita.png': lambda: badge('VITA', (38, 104, 178), False),
    'badge_pc.png': lambda: badge('PC', (92, 102, 116), False),
    'badge_vita_locked.png': lambda: badge('VITA', (38, 104, 178), True),
    'badge_pc_locked.png': lambda: badge('PC', (92, 102, 116), True),
    'arrow_left.png': lambda: arrow('left'),
    'arrow_right.png': lambda: arrow('right'),
    'swatch_random.png': lambda: swatch(None),
}
for _index, _colour in enumerate(PLAYER_COLOURS):
    ART['swatch_%02d.png' % _index] = (lambda colour: lambda: swatch(colour))(_colour)
    ART['nameplate_%02d.png' % _index] = (lambda colour: lambda: nameplate_colour(colour))(_colour)


def main(folder):
    os.makedirs(folder, exist_ok=True)
    for name, make in ART.items():
        # (no time or software stamp: the same bytes each run)
        make().save(os.path.join(folder, name), format='PNG', optimize=True)
    for name, (face, size, tracking, symbols) in FONT_FILES.items():
        with open(os.path.join(folder, name), 'wb') as out:
            out.write(font_file(face, size, tracking, symbols))


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
