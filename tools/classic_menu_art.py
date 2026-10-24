#!/usr/bin/env python3
"""The Halo CE menus' additions' own art (port/vita/app0/menus/classic), drawn
here, not taken from any game (the font is Saira Semi Condensed, SIL OFL,
tools/fonts/OFL.txt). The same bytes each run.

- pausebox.png: the multiplayer pause's box for its four rows, in the look of
  the game's own pause box (its blue border, dark blue glass and the line
  over the button key).
- <lang>/main_<row>_0.png and _1.png: the main menu's new rows (FORGE,
  FRIENDS, FILE SHARE, MOD MANAGER) as words in the look of the game's own
  CAMPAIGN, MULTIPLAYER and SETTINGS (a picture 256 x 64, capitals 20
  pixels high from its 6th row, about 17.5 pixels a letter): 0 not focused
  (a solid mid blue), 1 focused (white with a blue glow), the two frames the
  game's column list shows by focus. In English they are only the fallback:
  the game makes them of the player's ui.map's own letters as the menus load
  (port/linux/game/menu_words.c).
- <lang>/header_lobby_options.png, header_file_share.png,
  header_mod_manager.png: LOBBY OPTIONS's, FILE SHARE's and MOD MANAGER's
  headings in the look of the Multiplayer screens' (512 x 64 at 35,11: blue
  capitals 23 pixels high).
- In English (en/) and in each language of port/vita/app0/lang (es/), the
  words its file gives them; menu_tags.c picks the language's ({lang}).

    python3 tools/classic_menu_art.py port/vita/app0/menus/classic
"""
import os
import sys

import re

from PIL import Image, ImageDraw, ImageFilter, ImageFont

FONT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fonts', 'SairaSemiCondensed-Medium.ttf')
LANG = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'port', 'vita', 'app0', 'lang')
# the main menu's rows (their files' names) and the heading, in English
ROWS = (('forge', 'FORGE'), ('friends', 'FRIENDS'), ('fileshare', 'FILE SHARE'), ('mods', 'MOD MANAGER'))
HEADINGS = (('header_lobby_options', 'LOBBY OPTIONS'),
            # (Classic+'s screens for the port's features: classic/forge.xml, customize.xml)
            ('header_forge', 'FORGE'), ('header_extra_objects', 'EXTRA OBJECTS'),
            ('header_customization', 'CUSTOMIZATION'),
            # (classic/fileshare.xml, mods.xml)
            ('header_file_share', 'FILE SHARE'), ('header_mod_manager', 'MOD MANAGER'))

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


def letters(text, cap, scale=4):
    """the text's shape, white on black, capitals cap pixels high (drawn at
    scale times and left so), its top row the capitals' top"""
    probe = ImageFont.truetype(FONT, 100)
    box = probe.getbbox('H')
    font = ImageFont.truetype(FONT, round(100 * cap * scale / (box[3] - box[1])))
    stroke = scale
    bounds = font.getbbox(text, stroke_width=stroke)
    top = font.getbbox('H')[1]
    image = Image.new('L', (bounds[2] - bounds[0] + 2 * stroke, round(cap * scale) + 4 * stroke), 0)
    ImageDraw.Draw(image).text((stroke - bounds[0], stroke - top), text, font=font, fill=255, stroke_width=stroke,
                               stroke_fill=255)
    return image, stroke


def word_mask(text, cap, per_letter, most):
    """the text's shape at its size: capitals cap high, per_letter wide each
    (a space under half), no wider than most"""
    scale = 4
    image, stroke = letters(text, cap, scale)
    want = per_letter * len(text.replace(' ', '')) + per_letter * 0.45 * text.count(' ')
    width = max(1, round(min(want, most)))
    height = round(image.size[1] / scale)
    return image.resize((width, height), Image.LANCZOS), round(stroke / scale)


def main_row(text, focused):
    """a main menu row: 256 x 64 as the game's own rows' pictures, the word
    centred, its capitals 20 high from the 6th row"""
    width, height = 256, 64
    mask, margin = word_mask(text, 20, 17.5, width - 12)
    shape = Image.new('L', (width, height), 0)
    shape.paste(mask, ((width - mask.size[0]) // 2, 6 - margin))
    image = Image.new('RGBA', (width, height), (0, 0, 0, 0))
    if focused:
        glow = shape.filter(ImageFilter.GaussianBlur(2.2)).point(lambda v: min(255, int(v * 2.2)))
        image.paste(Image.new('RGBA', (width, height), (40, 150, 255, 255)), (0, 0), glow)
        image.paste(Image.new('RGBA', (width, height), (250, 252, 255, 255)), (0, 0), shape)
    else:
        image.paste(Image.new('RGBA', (width, height), (48, 128, 205, 255)), (0, 0),
                    shape.point(lambda v: int(v * 0.8)))
    return image


def heading(text):
    """a screen's heading: 512 x 64, blue capitals 23 high from the 33rd row,
    35 pixels in, as the Multiplayer screens' headings"""
    width, height = 512, 64
    mask, margin = word_mask(text, 23, 23, width - 40)
    image = Image.new('RGBA', (width, height), (0, 0, 0, 0))
    shape = Image.new('L', (width, height), 0)
    shape.paste(mask, (35, 33 - margin))
    image.paste(Image.new('RGBA', (width, height), (40, 150, 255, 255)), (0, 0), shape)
    return image


def translations(code):
    """the language file's words for the English (port/vita/app0/lang/<code>.txt)"""
    words = {}
    with open(os.path.join(LANG, code + '.txt'), encoding='utf-8') as file:
        for line in file:
            match = re.match(r'^"((?:[^"\\]|\\.)*)" = "((?:[^"\\]|\\.)*)"', line)
            if match:
                words[match.group(1)] = match.group(2)
    return words


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else 'port/vita/app0/menus/classic'
    pausebox().save(os.path.join(folder, 'pausebox.png'), optimize=True)
    languages = {'en': {}}
    for name in sorted(os.listdir(LANG)):
        if name.endswith('.txt'):
            languages[name[:-4]] = translations(name[:-4])
    for code, words in languages.items():
        os.makedirs(os.path.join(folder, code), exist_ok=True)
        for name, english in ROWS:
            text = words.get(english, english)
            for focused in (0, 1):
                main_row(text, focused).save(os.path.join(folder, code, 'main_%s_%d.png' % (name, focused)),
                                             optimize=True)
        for name, english in HEADINGS:
            heading(words.get(english, english)).save(os.path.join(folder, code, name + '.png'), optimize=True)


if __name__ == '__main__':
    main()
