#!/usr/bin/env python3
"""The Online menu style's own art (port/vita/app0/menus/online): the deep
navy see-through panels, the orange selection bars, the header band and its
tabs, the colour grid's tiles, the boxes and rules, and the header font -
drawn here from rectangles, gradients and lines, not taken from any game.
The fonts are Saira Semi Condensed (SIL OFL, tools/fonts/OFL.txt), as the
Halo 3 menus' (tools/halo3_menu_art.py, whose helpers this uses). The same
bytes each run.

    python3 tools/online_menu_art.py port/vita/app0/menus/online
"""
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from halo3_menu_art import (PLAYER_COLOURS, alpha, font_file, horizontal_gradient,  # noqa: E402
                            vertical_gradient)

# the palette: deep navy, ice-white text (the XML's), the orange of the bars
NAVY_TOP = (26, 46, 84)
NAVY_BOTTOM = (8, 18, 40)
ORANGE = (186, 108, 62)
ORANGE_DARK = (122, 68, 42)
LINE = (150, 176, 210)


def haze():
    # over the live scene behind the menus: a deep blue cast, darker at the
    # bottom (a widget as wide as the 640 columns stretches over the wide screen)
    return vertical_gradient((64, 64), [
        (0.0, (18, 38, 86, alpha(.58))),
        (0.7, (12, 28, 68, alpha(.64))),
        (1.0, (6, 14, 38, alpha(.76))),
    ])


def main_panel():
    # the main menu's list: a see-through navy block, lighter at its top edge
    return vertical_gradient((64, 64), [
        (0.0, NAVY_TOP + (alpha(.86),)),
        (0.12, (20, 38, 72, alpha(.80))),
        (1.0, NAVY_BOTTOM + (alpha(.82),)),
    ])


def footer():
    # a list's foot (the version, the Settings button): darker
    return vertical_gradient((64, 16), [
        (0.0, (6, 12, 28, alpha(.78))),
        (1.0, (4, 9, 22, alpha(.86))),
    ])


def item_on():
    # the selection: an orange bar, deeper toward the right, a light top edge
    image = horizontal_gradient((128, 16), [
        (0.0, ORANGE + (255,)),
        (0.7, (160, 92, 56, 255)),
        (1.0, ORANGE_DARK + (235,)),
    ])
    draw = ImageDraw.Draw(image)
    draw.line([(0, 0), (127, 0)], fill=(255, 214, 170, alpha(.45)))
    draw.line([(0, 15), (127, 15)], fill=(40, 18, 8, alpha(.45)))
    return image


def item_off():
    return Image.new('RGBA', (128, 16), (0, 0, 0, 0))


def row_dim_on():
    # a greyed row's selection ("not yet"): the bar, dimmer
    image = item_on()
    image.putalpha(image.getchannel('A').point(lambda value: round(value * .5)))
    return image


def rule():
    return horizontal_gradient((128, 2), [
        (0.0, LINE + (alpha(.55),)),
        (0.85, LINE + (alpha(.40),)),
        (1.0, LINE + (0,)),
    ])


def header():
    # the header band (a screen's title and its tabs), full width
    return vertical_gradient((64, 64), [
        (0.0, (6, 12, 26, alpha(.82))),
        (0.62, (10, 20, 42, alpha(.78))),
        (0.63, (20, 36, 66, alpha(.86))),
        (1.0, (16, 30, 58, alpha(.86))),
    ])


def tab(on):
    # a tab of the header: the chosen one a lighter navy block, each ending
    # in a slanted edge, as a folder's tab
    width, height = 128, 16
    image = Image.new('RGBA', (width * 4, height * 4), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    w, h = width * 4, height * 4
    slant = 40
    if on:
        draw.polygon([(0, 0), (w - slant - 1, 0), (w - 1, h - 1), (0, h - 1)], fill=(40, 64, 104, alpha(.92)))
        draw.line([(0, h - 4), (w - 1, h - 4)], fill=ORANGE + (255,), width=6)
    draw.line([(w - slant - 1, 0), (w - 1, h - 1)], fill=LINE + (alpha(.55),), width=4)
    return image.resize((width, height), Image.Resampling.LANCZOS)


def panel():
    # the lobbies' and pickers' panel
    return vertical_gradient((64, 64), [
        (0.0, (30, 52, 92, alpha(.84))),
        (0.15, (22, 42, 78, alpha(.80))),
        (1.0, (10, 22, 46, alpha(.84))),
    ])


def card():
    # a description card (a map's, a game type's): lighter at the top, fading
    # out at its foot
    return vertical_gradient((64, 64), [
        (0.0, (44, 68, 108, alpha(.70))),
        (0.75, (30, 50, 86, alpha(.55))),
        (1.0, (30, 50, 86, 0)),
    ])


def box():
    # a value's box (the colour's hex, an option's tile): dark, an orange rim
    image = Image.new('RGBA', (64, 16), (8, 16, 34, alpha(.88)))
    draw = ImageDraw.Draw(image)
    draw.rectangle([0, 0, 63, 15], outline=ORANGE + (255,))
    return image


def tile(on):
    # an option's tile: navy, the chosen one rimmed in orange and white
    image = vertical_gradient((64, 64), [
        (0.0, (34, 56, 94, alpha(.80))),
        (1.0, (18, 34, 64, alpha(.80))),
    ])
    draw = ImageDraw.Draw(image)
    if on:
        draw.rectangle([0, 0, 63, 63], outline=ORANGE + (255,), width=2)
        draw.rectangle([2, 2, 61, 61], outline=(230, 236, 244, alpha(.55)))
    else:
        draw.rectangle([0, 0, 63, 63], outline=LINE + (alpha(.25),))
    return image


def select_frame():
    # the colour grid's chosen tile: an orange frame, open in the middle
    image = Image.new('RGBA', (32, 16), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.rectangle([0, 0, 31, 15], outline=ORANGE + (255,), width=2)
    draw.rectangle([2, 2, 29, 13], outline=(255, 240, 220, alpha(.8)))
    return image


def colour_tile(colour):
    # a colour of the grid: flat, a touch lighter at the top
    if colour is None:
        image = Image.new('RGBA', (32, 16))
        draw = ImageDraw.Draw(image)
        quarters = [(0xFE, 0x00, 0x00), (0x02, 0x01, 0xE3), (0xFF, 0xFF, 0x01), (0x00, 0xFF, 0x01)]
        for index, fill in enumerate(quarters):
            draw.rectangle([index * 8, 0, index * 8 + 7, 15], fill=fill + (255,))
        return image
    rgb = ((colour >> 16) & 0xFF, (colour >> 8) & 0xFF, colour & 0xFF)
    top = tuple(min(255, round(c + (255 - c) * .10)) for c in rgb)
    bottom = tuple(round(c * .88) for c in rgb)
    return vertical_gradient((32, 16), [(0.0, top + (255,)), (1.0, bottom + (255,))])


def name_bar():
    # the lobby's player bar: the profile's colour comes from the Halo 3
    # nameplate's frames; this is the count line's underline
    return horizontal_gradient((128, 2), [
        (0.0, ORANGE + (255,)),
        (1.0, ORANGE + (0,)),
    ])


ART = {
    'haze.png': haze,
    'main_panel.png': main_panel,
    'footer.png': footer,
    'item_on.png': item_on,
    'item_off.png': item_off,
    'row_dim_on.png': row_dim_on,
    'rule.png': rule,
    'header.png': header,
    'tab_on.png': lambda: tab(True),
    'tab_off.png': lambda: tab(False),
    'panel.png': panel,
    'card.png': card,
    'box.png': box,
    'tile_on.png': lambda: tile(True),
    'tile_off.png': lambda: tile(False),
    'select_frame.png': select_frame,
    'colour_random.png': lambda: colour_tile(None),
    'name_bar.png': name_bar,
}
for _index, _colour in enumerate(PLAYER_COLOURS):
    ART['colour_%02d.png' % _index] = (lambda colour: lambda: colour_tile(colour))(_colour)

FONT_FILES = {
    # (the screens' titles: CUSTOMIZATION, MULTIPLAYER LOBBY)
    'font_header.bin': ('SairaSemiCondensed-Medium.ttf', 30, 1, False),
    # (the tabs and the lobby's rows)
    'font_tab.bin': ('SairaSemiCondensed-Regular.ttf', 14, 1, False),
}


def main(folder):
    os.makedirs(folder, exist_ok=True)
    for name, make in ART.items():
        make().save(os.path.join(folder, name), format='PNG', optimize=True)
    for name, (face, size, tracking, symbols) in FONT_FILES.items():
        with open(os.path.join(folder, name), 'wb') as out:
            out.write(font_file(face, size, tracking, symbols))


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
