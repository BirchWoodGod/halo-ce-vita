#!/usr/bin/env python3
"""The Online menu style's own art (port/vita/app0/menus/online): the deep
navy see-through panels, the orange selection bars, the header band and its
tabs, the colour grid's tiles, the boxes and rules, the title, and the fonts -
drawn here from rectangles, gradients and lines, not taken from any game.
The fonts are Saira Semi Condensed (SIL OFL, tools/fonts/OFL.txt), as the
Halo 3 menus' (tools/halo3_menu_art.py, whose helpers this uses); the title
is the owner's HALO CE VITA logo (tools/art). The same bytes each run.

Sharp: the pictures are drawn for a 1920x1080 display (2.25 pixels a unit of
the 640x480 interface the widgets are laid out in), so that they are not
scaled up there and only a little down elsewhere: a bar 226 units wide is
512 texels, its top edge's highlight two texels; panels are flat (a short
gradient at the top only), so stretching them blurs nothing. The fonts come
in variants at the displays' pixels per unit (the Vita's 1.133, 1280x720's
1.5, 1920x1080's 2.25, 2560x1440's 3), each glyph hinted at that pixel size:
menu_tags.c picks the one for the display and draw_string.c draws its
texels pixel for pixel (<font sharp=...>).

    python3 tools/online_menu_art.py port/vita/app0/menus/online
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from halo3_menu_art import (ART_SOURCES, PLAYER_COLOURS, alpha, font_file,  # noqa: E402
                            horizontal_gradient, vertical_gradient)

# the palette: deep navy, ice-white text (the XML's), the orange of the bars
NAVY = (14, 27, 52)
NAVY_TOP = (24, 44, 80)
ORANGE = (196, 112, 58)
ORANGE_DARK = (150, 82, 44)
LINE = (150, 178, 214)
# (the pixels a unit is on a 1920x1080 display: the pictures' scale)
S = 2.25


def haze_light():
    # CUSTOMIZATION's: the Spartan at the right stays bright, the rows' side dark
    return horizontal_gradient((128, 4), [
        (0.0, (6, 14, 34, alpha(.84))),
        (0.5, (6, 14, 34, alpha(.62))),
        (0.75, (6, 14, 34, alpha(.18))),
        (1.0, (6, 14, 34, alpha(.30))),
    ])


def haze():
    # over the live scene: a deep, calm blue cast, darker toward the bottom
    # and the sides (a widget as wide as the 640 columns stretches over the
    # wide screen)
    width, height = 256, 128
    image = vertical_gradient((width, height), [
        (0.0, (8, 18, 44, alpha(.76))),
        (0.55, (6, 14, 36, alpha(.80))),
        (1.0, (2, 6, 18, alpha(.88))),
    ])
    shade = Image.new('L', (width, height), 0)
    ImageDraw.Draw(shade).ellipse([-width * .25, -height * .35, width * 1.25, height * 1.3], fill=255)
    shade = shade.filter(ImageFilter.GaussianBlur(28))
    pixels, mask = image.load(), shade.load()
    for y in range(height):
        for x in range(width):
            r, g, b, a = pixels[x, y]
            edge = 1.0 - mask[x, y] / 255.0
            pixels[x, y] = (r, g, b, min(255, round(a + (250 - a) * edge * .55)))
    return image


def flat_panel(top, body, opacity, height=256, lip=.06):
    # a see-through block: one flat navy, a short lighter gradient at its top
    return vertical_gradient((4, height), [
        (0.0, top + (alpha(opacity),)),
        (lip, body + (alpha(opacity),)),
        (1.0, body + (alpha(opacity + .04),)),
    ])


def main_panel():
    return flat_panel(NAVY_TOP, NAVY, .80)


def panel():
    return flat_panel(NAVY_TOP, (16, 30, 58), .82, lip=.04)


def footer():
    return Image.new('RGBA', (4, 4), (4, 9, 22, alpha(.86)))


def item_on():
    # the selection: an orange bar with hard ends, a little deeper toward the
    # right, a light edge on top and a dark one under it, about the 1080p
    # display's pixels (226 units: 508 pixels, 512 texels; 18: 40, 64 texels -
    # a power of two's, as the menus' textures are)
    width, height = 512, 64
    image = horizontal_gradient((width, height), [
        (0.0, ORANGE + (255,)),
        (0.75, (176, 98, 52, 255)),
        (1.0, ORANGE_DARK + (255,)),
    ])
    draw = ImageDraw.Draw(image)
    draw.rectangle([0, 0, width - 1, 2], fill=(255, 210, 160, alpha(.55)))
    draw.rectangle([0, height - 3, width - 1, height - 1], fill=(60, 26, 10, alpha(.55)))
    return image


def item_off():
    return Image.new('RGBA', (4, 4), (0, 0, 0, 0))


def row_dim_on():
    image = item_on()
    image.putalpha(image.getchannel('A').point(lambda value: round(value * .45)))
    return image


def rule():
    # a thin line: flat, see-through (its widget one unit tall)
    return Image.new('RGBA', (4, 4), LINE + (alpha(.42),))


def header():
    # the header band (a screen's title and its tabs), full width: dark for
    # the title, a navy strip for the tabs, a hard edge between
    return vertical_gradient((4, 256), [
        (0.0, (4, 9, 20, alpha(.84))),
        (0.62, (6, 12, 26, alpha(.84))),
        (0.625, (22, 40, 72, alpha(.88))),
        (1.0, (18, 34, 62, alpha(.88))),
    ])


def tab(on):
    # a tab of the header: the chosen one a lighter navy block with an
    # orange line under it, each ending in a slanted edge
    width, height = 256, 64
    image = Image.new('RGBA', (width, height), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    slant = 24
    if on:
        draw.polygon([(0, 0), (width - slant - 1, 0), (width - 1, height - 1), (0, height - 1)],
                     fill=(44, 70, 112, alpha(.95)))
        draw.rectangle([0, height - 5, width - 1, height - 1], fill=ORANGE + (255,))
    draw.line([(width - slant - 1, 0), (width - 1, height - 1)], fill=LINE + (alpha(.6),), width=2)
    return image


def card():
    return vertical_gradient((4, 256), [
        (0.0, (40, 64, 104, alpha(.72))),
        (0.04, (28, 48, 84, alpha(.64))),
        (0.8, (28, 48, 84, alpha(.58))),
        (1.0, (28, 48, 84, 0)),
    ])


def box():
    # a value's box (the colour's hex): dark, a hard orange rim of 2 texels
    width, height = 256, 32
    image = Image.new('RGBA', (width, height), (6, 12, 28, alpha(.9)))
    ImageDraw.Draw(image).rectangle([0, 0, width - 1, height - 1], outline=ORANGE + (255,), width=2)
    return image


def tile(on):
    width = height = 128
    image = Image.new('RGBA', (width, height), (24, 42, 74, alpha(.8)))
    draw = ImageDraw.Draw(image)
    if on:
        draw.rectangle([0, 0, width - 1, height - 1], outline=ORANGE + (255,), width=3)
    else:
        draw.rectangle([0, 0, width - 1, height - 1], outline=LINE + (alpha(.3),), width=2)
    return image


def select_frame():
    # the colour grid's chosen tile: an orange frame, open in the middle
    width, height = 64, 32
    image = Image.new('RGBA', (width, height), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.rectangle([0, 0, width - 1, height - 1], outline=ORANGE + (255,), width=3)
    draw.rectangle([3, 3, width - 4, height - 4], outline=(255, 240, 220, alpha(.85)), width=1)
    return image


def colour_tile(colour):
    width, height = 64, 32
    if colour is None:
        image = Image.new('RGBA', (width, height))
        draw = ImageDraw.Draw(image)
        quarters = [(0xFE, 0x00, 0x00), (0x02, 0x01, 0xE3), (0xFF, 0xFF, 0x01), (0x00, 0xFF, 0x01)]
        for index, fill in enumerate(quarters):
            draw.rectangle([index * 16, 0, index * 16 + 15, height - 1], fill=fill + (255,))
        return image
    rgb = ((colour >> 16) & 0xFF, (colour >> 8) & 0xFF, colour & 0xFF)
    return Image.new('RGBA', (width, height), rgb + (255,))


def name_bar():
    return horizontal_gradient((256, 4), [
        (0.0, ORANGE + (255,)),
        (1.0, ORANGE + (0,)),
    ])


def title():
    # the owner's HALO CE VITA logo at 1024x256 (400x100 units: 900x225
    # pixels on a 1080p display), a narrow glow only, so its edges stay hard
    width, height, margin_x = 1024, 256, 24
    logo = Image.open(os.path.join(ART_SOURCES, 'halo_ce_vita_logo.png')).convert('RGBA')
    box_ = logo.getbbox()
    if box_:
        logo = logo.crop(box_)
    fit = min((width - 2 * margin_x) / logo.width, (height - 24) / logo.height)
    size = (max(1, round(logo.width * fit)), max(1, round(logo.height * fit)))
    logo = logo.resize(size, Image.Resampling.LANCZOS)
    at = ((width - size[0]) // 2, (height - size[1]) // 2)
    shape = Image.new('L', (width, height), 0)
    shape.paste(logo.getchannel('A'), at)
    glow = Image.new('RGBA', (width, height), (110, 170, 240, 0))
    glow.putalpha(shape.filter(ImageFilter.GaussianBlur(10)).point(lambda value: round(value * .35)))
    layer = Image.new('RGBA', (width, height), (0, 0, 0, 0))
    layer.paste(logo, at)
    return Image.alpha_composite(glow, layer)


ART = {
    'haze.png': haze,
    'haze_light.png': haze_light,
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
    'title.png': title,
}
for _index, _colour in enumerate(PLAYER_COLOURS):
    ART['colour_%02d.png' % _index] = (lambda colour: lambda: colour_tile(colour))(_colour)

# the fonts: (face, size in units, tracking in units); each also drawn at the
# displays' pixels per unit (file_<scale>.bin: <font sharp=...> lists them)
FONTS = {
    'font_header': ('SairaSemiCondensed-Medium.ttf', 30, 1),
    'font_title': ('SairaSemiCondensed-Medium.ttf', 22, 1),
    'font_item': ('SairaSemiCondensed-Regular.ttf', 15, 0),
    'font_small': ('SairaSemiCondensed-Regular.ttf', 12, 0),
    'font_tab': ('SairaSemiCondensed-Regular.ttf', 14, 1),
}
SCALES = (1.133, 1.5, 2.25, 3.0)


def scale_name(scale):
    return '%04d' % round(scale * 1000)


def main(folder):
    os.makedirs(folder, exist_ok=True)
    for name, make in ART.items():
        make().save(os.path.join(folder, name), format='PNG', optimize=True)
    for name, (face, size, tracking) in FONTS.items():
        with open(os.path.join(folder, name + '.bin'), 'wb') as out:
            out.write(font_file(face, size, tracking, False))
        for scale in SCALES:
            with open(os.path.join(folder, '%s_%s.bin' % (name, scale_name(scale))), 'wb') as out:
                out.write(font_file(face, round(size * scale), round(tracking * scale), False))


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
