#!/usr/bin/env python3
"""Menu style Online: writes port/vita/app0/menus/online/forge.xml, Forge's screens over the game
(the object browser, the Forge menu) from halo3/forge.xml's widgets - their handlers and data inputs as
they are, the same leaf names (forge.c and menu_functions.c find them by them) - in the online look: the
browser a slimmer navy panel at the wide screen's right with the orange bar, the menu a window across
the middle with its header band. The in-game set (ingame.txt) opens them in place of the Halo 3 ones
(menu_tags.c menu_tags_online).

    python3 tools/online_menu_forge.py port/vita/app0/menus/halo3/forge.xml port/vita/app0/menus/online/forge.xml
"""
import copy
import sys
import xml.etree.ElementTree as ET

src, out = sys.argv[1], sys.argv[2]
ALL = {w.get('name'): w for w in ET.parse(src).getroot().iter('widget') if w.get('name')}
BITMAPS = {'h3/panel': 'online/f_panel', 'h3/strip': 'online/f_foot', 'h3/browser_row': 'online/f_row',
           'h3/item': 'online/f_item', 'h3/column': 'online/f_panel', 'h3/rule': 'online/f_rule'}
made = {}


def ren(name):
    return 'online/' + name[3:] if name.startswith('h3/forge/') else name


def copy_tree(name, shift=0, top=0):
    new = ren(name)
    if new in made or name not in ALL:
        return new if name in ALL else name
    w = copy.deepcopy(ALL[name])
    made[new] = w
    w.set('name', new)
    for e in w.iter('widget'):
        if e.get('left') is not None:
            e.set('left', str(int(e.get('left')) + shift))
        if e.get('top') is not None:
            e.set('top', str(int(e.get('top')) + top))
        if e.get('bitmap') in BITMAPS:
            e.set('bitmap', BITMAPS[e.get('bitmap')])
        if e.get('font') == 'h3/font_title':
            e.set('font', 'h3/font_item')
    # (a list's rows are placed by it: their parts keep their places in them)
    inner = (0, 0) if w.get('type') == 'column_list' else (shift, top)
    for c in w.iter('child'):
        c.set('widget', copy_tree(c.get('widget'), *inner))
        shift, top = inner
        # (a child placed by its parent: moved as the widgets with places of their own)
        if c.get('x') is not None and int(c.get('x')) > 0:
            c.set('x', str(int(c.get('x')) + shift))
        if c.get('y') is not None and int(c.get('y')) > 0 and c.get('widget').endswith('/list'):
            c.set('y', str(int(c.get('y')) + top))
    return new


def el(tag, **attrs):
    e = ET.Element(tag)
    for k, v in attrs.items():
        e.set(k, str(v))
    return e


root = el('menus')
for name, png, w, h in (('online/f_panel', 'panel', 64, 64), ('online/f_foot', 'footer', 64, 16),
                        ('online/f_rule', 'rule', 128, 2), ('online/f_header', 'header', 64, 64)):
    b = el('bitmap', name=name)
    b.append(el('frame', png='online/%s.png' % png, width=w, height=h))
    root.append(b)
for name in ('online/f_row', 'online/f_item'):
    b = el('bitmap', name=name)
    b.append(el('frame', png='online/item_off.png', width=128, height=16))
    b.append(el('frame', png='online/item_on.png', width=128, height=16))
    root.append(b)
# the browser: 100 to the right (the wide screen's margin), as the online browser's panel
copy_tree('h3/forge/browser/screen', shift=100)
# the Forge menu: its window lower, under a header band across the screen
copy_tree('h3/forge/pause/screen', shift=-20, top=10)
pause = made['online/forge/pause/screen']
children = [c for c in pause if c.tag == 'child']
pause.insert(list(pause).index(children[0]) + 1, el('child', widget='online/forge/pause/band'))
root.append(el('widget', name='online/forge/pause/band', left=0, top=40, width=640, height=44, bitmap='online/f_header'))
for w in made.values():
    root.append(w)
ET.indent(root, '\t')
head = ('<?xml version="1.0" encoding="UTF-8"?>\n<!-- Menu style Online (a preview): Forge\'s object browser and '
        'Forge menu over the game, made from halo3/forge.xml by tools/online_menu_forge.py (see there). -->\n')
open(out, 'w').write(head + ET.tostring(root, encoding='unicode') + '\n')
print(len(made), 'widgets')
