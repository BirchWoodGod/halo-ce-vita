#!/usr/bin/env python3
"""Menu style Online: the last step of its XML (port/vita/app0/menus/online/*.xml), run by the
generators (tools/online_menu_*.py) and on the hand-written files: each online/ PNG frame's
width and height made the file's own (tools/online_menu_art.py draws them at about a 1080p
display's pixels), and the Halo 3 set's fonts (h3/font_title, h3/font_item, h3/font_small) made
the online set's, which have sharp variants (main.xml's <font sharp=...>).

    python3 tools/online_menu_finish.py port/vita/app0/menus/online/*.xml
"""
import os
import re
import struct
import sys

MENUS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'port', 'vita', 'app0', 'menus')


def png_size(path):
    with open(path, 'rb') as png:
        head = png.read(24)
    return struct.unpack('>II', head[16:24])


def finish_text(text):
    def frame(match):
        width, height = png_size(os.path.join(MENUS, match.group(1)))
        return '<frame png="%s" width="%d" height="%d"' % (match.group(1), width, height)
    text = re.sub(r'<frame png="(online/[^"]+)" width="\d+" height="\d+"', frame, text)
    return re.sub(r'font="h3/font_(title|item|small)"', r'font="online/font_\1"', text)


def finish(path):
    with open(path) as source:
        text = source.read()
    finished = finish_text(text)
    if finished != text:
        with open(path, 'w') as out:
            out.write(finished)


if __name__ == '__main__':
    for name in sys.argv[1:]:
        finish(name)
