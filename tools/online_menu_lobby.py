#!/usr/bin/env python3
"""Menu style Online: writes port/vita/app0/menus/online/multiplayer.xml from the Halo 3 lobby's widgets
(halo3/multiplayer.xml and the set's other files), restyled, their handlers and data inputs as they are.

    python3 tools/online_menu_lobby.py port/vita/app0/menus/halo3/multiplayer.xml port/vita/app0/menus/online/multiplayer.xml
"""
import copy, re, sys
import xml.etree.ElementTree as ET
src, out = sys.argv[1], sys.argv[2]
tree = ET.parse(src)
widgets = {w.get('name'): w for w in tree.getroot().iter('widget') if w.get('name')}
import glob, os
ALL = {}
for f in glob.glob(os.path.join(os.path.dirname(src), '*.xml')):
    try:
        for w in ET.parse(f).getroot().iter('widget'):
            if w.get('name'):
                ALL[w.get('name')] = w
    except ET.ParseError:
        pass

def copy_tree(name, shift, made):
    # a widget of the Halo 3 set and the named children under it, as online/lobby/..., moved
    # right by shift (the same leaves: the functions find them by them)
    new = 'online/lobby/' + name[3:]
    if new in made:
        return new
    w = copy.deepcopy(ALL[name])
    w.set('name', new)
    if w.get('left') is not None:
        w.set('left', str(int(w.get('left')) + shift))
    made[new] = w
    for c in w.findall('child'):
        if c.get('widget') in ALL:
            c.set('widget', copy_tree(c.get('widget'), shift, made))
    for e in list(w.iter('widget'))[1:]:
        if e.get('left') is not None:
            e.set('left', str(int(e.get('left')) + shift))
    return new
OPENED = {'online/network/screen', 'online/map/screen', 'online/gametype/screen'}
RENAME = ('h3/mp/', 'h3/map/', 'h3/gametype/', 'h3/network/')

def ren(name):
    for p in RENAME:
        if name and name.startswith(p):
            return 'online/' + name[3:]
    return name

def take(name, **attrs):
    w = copy.deepcopy(widgets[name])
    w.set('name', ren(name))
    for k, v in attrs.items():
        k = k.rstrip('_')
        if v is None:
            w.attrib.pop(k, None)
        else:
            w.set(k, str(v))
    for e in w.iter():
        if e.tag == 'child' and e.get('widget'):
            e.set('widget', ren(e.get('widget')))
        for a in ('open', 'otherwise'):
            if e.tag == 'on' and ren(e.get(a)) in OPENED:
                e.set(a, ren(e.get(a)))
    return w

def el(tag, **attrs):
    e = ET.Element(tag)
    for k, v in attrs.items():
        e.set(k.rstrip('_'), str(v))
    return e

def child_of(parent, widget, **attrs):
    parent.append(el('child', widget=widget, **attrs))

ROW = dict(bitmap='online/item', font='h3/font_item', color='#FFF0F4F8', text_x=8, text_y=2, width=292, height=18)
L = -60          # the lobby column's left (in the wide screen's margin)
items = []
add = items.append
for name, png in (('online/panel', 'panel'), ('online/card', 'card')):
    b = el('bitmap', name=name)
    b.append(el('frame', png='online/%s.png' % png, width=64, height=64))
    add(b)
# ---- the lobby
for n in ['network_row', 'game_row', 'map_row', 'start_online', 'start_link', 'start_adhoc', 'start_offline',
          'edit_row', 'browse_online', 'browse_link', 'browse_adhoc', 'code_row']:
    add(take('h3/mp/' + n, **ROW))
lst = take('h3/mp/list')
ys = {'network_row': 0, 'game_row': 36, 'map_row': 55, 'start_online': 80, 'start_link': 80, 'start_adhoc': 80,
      'start_offline': 80, 'edit_row': 99, 'browse_online': 118, 'browse_link': 118, 'browse_adhoc': 118, 'code_row': 137}
for c in lst.findall('child'):
    leaf = c.get('widget').rsplit('/', 1)[1]
    c.set('x', '0'); c.set('y', str(ys[leaf]))
add(lst)
add(take('h3/mp/network_status', left=L + 12, top=74, width=280))
add(el('widget', name='online/mp/panel', left=L - 6, top=50, width=304, height=398, bitmap='online/panel'))
add(el('widget', name='online/mp/band', left=L - 6, top=228, width=304, height=2, bitmap='online/rule'))
add(take('h3/mp/title', left=L - 2, top=12, width=400, height=34, font='online/font_header'))
add(take('h3/mp/ready', left=L + 4, top=234, width=290))
add(take('h3/mp/ready_sub', left=L + 4, top=252, width=290))
add(take('h3/mp/map_picture', left=L + 4, top=290, width=140, height=114))
add(take('h3/mp/caption', left=L + 4, top=418, width=290, height=24))
add(el('widget', name='online/mp/foot', left=L - 6, top=448, width=304, height=26, bitmap='online/footer'))
x = L + 4
for n, w in [('a_butn', 16), ('select', 60), ('b_butn', 16), ('back', 54), ('x_butn', 16), ('options', 60)]:
    if n.endswith('butn'):
        add(take('h3/mp/' + n, left=x, top=453)); x += 20
    else:
        add(take('h3/mp/' + n, left=x, top=452, width=w)); x += w
add(take('h3/mp/players', left=470, top=14, width=270))
add(take('h3/mp/l_butn', left=600, top=452))
add(take('h3/mp/invite', left=620, top=451))
screen = take('h3/mp/screen')
SHIFT = 100
MADE = {}      # the players, the party and the friends at the wide screen's right
for c in list(screen.findall('child')):
    w = c.get('widget')
    if w == 'h3/main/haze':
        c.set('widget', 'online/main/haze')
    elif w in ('online/mp/column', 'online/mp/rule1', 'online/mp/rule2', 'online/mp/rule3'):
        screen.remove(c)
    elif w.startswith('h3/pregame/row_'):
        c.set('x', str(int(c.get('x')) + SHIFT))
    elif w in ('h3/main/nameplate', 'h3/main/party_plates', 'h3/friends/lobby_panel'):
        c.set('widget', copy_tree(w, SHIFT, MADE))
    elif w == 'online/mp/list':
        c.set('x', str(L)); c.set('y', '54')
# (the panel first, behind)
idx = [i for i, c in enumerate(screen) if c.tag == 'child'][0]
for i, w in enumerate(['online/mp/panel', 'online/mp/band', 'online/mp/foot']):
    screen.insert(idx + 1 + i, el('child', widget=w))
for w in MADE.values():
    add(w)
add(screen)

# ---- the pickers: a band across the top with the title, the list at the left, the card at the right
P = -40
add(el('widget', name='online/pick/header', left=0, top=0, width=640, height=64, bitmap='online/header'))
add(el('widget', name='online/pick/panel', left=P - 8, top=72, width=330, height=370, bitmap='online/panel'))
add(el('widget', name='online/pick/card', left=318, top=72, width=290, height=300, bitmap='online/card'))
add(el('widget', name='online/pick/foot', left=0, top=446, width=640, height=34, bitmap='online/footer'))
add(el('widget', name='online/pick/a_butn', left=P, top=455, width=16, height=16, bitmap='h3/glyph_cross'))
add(el('widget', name='online/pick/select', type='text', left=P + 20, top=454, width=70, height=18, font='h3/font_item',
       color='#FFF0F4F8', text_y=1, string_list='h3/campaign_text', string_index=5))
add(el('widget', name='online/pick/b_butn', left=P + 90, top=455, width=16, height=16, bitmap='h3/glyph_circle'))
add(el('widget', name='online/pick/back', type='text', left=P + 110, top=454, width=70, height=18, font='h3/font_item',
       color='#FFF0F4F8', text_y=1, string_list='h3/main_items', string_index=10))
PROW = dict(bitmap='online/item', font='h3/font_item', color='#FFF0F4F8', text_x=8, text_y=2, width=310, height=18)
for kind, rows, extra in [('network', ['item_%d' % i for i in range(4)], []),
                          ('map', ['item_%d' % i for i in range(13)], ['map_picture', 'map_text', 'map_why']),
                          ('gametype', ['item_%d' % i for i in range(1, 13)], ['gametype_text'])]:
    for r in rows:
        add(take('h3/%s/%s' % (kind, r), **PROW))
    add(take('h3/%s/list' % kind))
    add(take('h3/%s/title' % kind, left=P, top=14, width=400, height=34, font='online/font_header'))
    for e in extra:
        if e == 'map_picture':
            add(take('h3/map/map_picture', left=330, top=84, width=140, height=114))
        else:
            add(take('h3/%s/%s' % (kind, e), left=330, top=84 if kind != 'map' else 208, width=266, height=190))
    if kind == 'network':
        add(take('h3/network/network_text', left=330, top=84, width=266, height=190))
    scr = take('h3/%s/screen' % kind)
    for c in list(scr.findall('child')):
        w = c.get('widget')
        if w == 'h3/main/haze':
            c.set('widget', 'online/main/haze')
        elif w.startswith('h3/pick/'):
            if w.endswith(('/rule', '/strip')):
                scr.remove(c)
            else:
                c.set('widget', 'online/pick/' + w.rsplit('/', 1)[1])
        elif w.endswith('/list'):
            c.set('x', str(P)); c.set('y', '80')
    first = [i for i, c in enumerate(scr) if c.tag == 'child'][1]
    for i, w in enumerate(['online/pick/header', 'online/pick/card', 'online/pick/foot']):
        scr.insert(first + i, el('child', widget=w))
    add(scr)

root = el('menus')
root.text = '\n\t'
for i, w in enumerate(items):
    root.append(w)
ET.indent(root, '\t')
body = ET.tostring(root, encoding='unicode')
head = '''<?xml version="1.0" encoding="UTF-8"?>
<!-- Menu style Online (a preview): the multiplayer lobby and its pickers (SELECT NETWORK, SELECT MAP,
SELECT GAME TYPE). They open in place of the Halo 3 lobby's (h3/mp/screen and the others:
menu_tags.c menu_tags_online) and are the same widgets - every row, handler and data input of
halo3/multiplayer.xml, the same names (made from it by a script) - laid out the online way: the
title over a see-through navy column at the left (the rows, the network's status, Ready, the map's
picture and "<game> on <map>", the buttons at its foot), the players, the party and the friends at
the wide screen's right; the pickers a band with the title, the list at the left on the orange bar
and the map's picture and description in a card at the right. The art is this folder's own
(tools/online_menu_art.py) and the Halo 3 set's. -->
'''
open(out, 'w').write(head + body + '\n')
print(len(items), 'widgets')
