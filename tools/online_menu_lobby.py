#!/usr/bin/env python3
"""Menu style Online: writes port/vita/app0/menus/online/multiplayer.xml from the Halo 3 lobby's widgets
(halo3/multiplayer.xml and the set's other files), restyled, their handlers and data inputs as they are.

    python3 tools/online_menu_lobby.py port/vita/app0/menus/halo3/multiplayer.xml port/vita/app0/menus/online/multiplayer.xml
"""
import copy
import os
import re
import sys
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
OPENED = {'online/network/screen', 'online/map/screen', 'online/gametype/screen', 'online/forge/maps/screen'}
RENAME = ('h3/forge/', 'h3/mp/', 'h3/map/', 'h3/gametype/', 'h3/network/')

def ren(name):
    for p in RENAME:
        if name and name.startswith(p):
            return 'online/' + name[3:]
    return name

def take(name, **attrs):
    w = copy.deepcopy(widgets[name] if name in widgets else ALL[name])
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

# The layout, after the online lobby (the owner's screenshots at 1920x1080, 2.25 pixels a unit, the 640
# columns from x 107): the title over the column; the column (x -54 to 290) of rows - SWITCH LOBBY,
# HOST SETTINGS, GAME, MAP, a gap, START GAME, GAME OPTIONS - then ours (BROWSE GAMES, JOIN WITH CODE),
# the status, the map's card (picture, name, description), the caption, the buttons' strip; the
# players at the right edge (x 486 to 746).
L, R = -54, 290
W = R - L
RIGHT = 486
ROW = dict(bitmap='online/item', font='h3/font_item', color='#FFF0F4F8', text_x=8, text_y=2, width=W, height=18)
items = []
add = items.append
for name, png in (('online/panel', 'panel'), ('online/card', 'card'), ('online/backing', 'backing'),
                  ('online/dropdown', 'dropdown')):
    b = el('bitmap', name=name)
    b.append(el('frame', png='online/%s.png' % png, width=64, height=64))
    add(b)
plate = el('bitmap', name='online/plate')
plate.append(el('frame', png='online/plate.png', width=4, height=32))
for i in range(18):
    plate.append(el('frame', png='online/plate_%02d.png' % i, width=4, height=32))
add(plate)
add(el('strings', name='online/lobby_text'))
for text in ('SWITCH LOBBY', 'HOST SETTINGS', 'GAME OPTIONS', 'MULTIPLAYER', 'FORGE', 'CAMPAIGN', 'Settings',
             'Select', 'Leave'):
    items[-1].append(el('string', text=text))
add(el('strings', name='online/switch_text'))
for text in ('Take your party into a multiplayer\ngame - Slayer, Capture the Flag,\nKing of the Hill and the rest - on\n'
             'your network or online.',
             'Take your party into Forge: place,\nmove and change the objects of a\nmap together, then play it.',
             'Play the campaign with your party:\nco-op on your network or online.'):
    items[-1].append(el('string', text=text))

# (the party list at the right: the players' count over a dark strip, the nameplates slim flat
# bars in each player's colour - online/plate - with the name in the sharp font; the Halo 3 set's
# widgets copied, their functions the same)
MADE = {}
SHIFT = RIGHT - 380


def plates(widget):
    for e in widget.iter('widget'):
        if e.get('bitmap') == 'h3/nameplate':
            e.set('bitmap', 'online/plate')
            if e.get('height') == '16':
                e.set('height', '18')
        if e.get('name', '').endswith('/players'):
            e.set('font', 'h3/font_item')
        if e.get('font') == 'h3/font_small' and e.get('color') == '#FF10220A':
            e.set('font', 'h3/font_item')
            e.set('color', '#FFF4F6F8')
            e.set('text_y', '1')
            # (the name near the bar's left, as the online party list's: no emblem square)
            if e.get('text_x') == '22':
                e.set('text_x', '10')
            if e.get('left') and e.get('name', '').split('/')[-1].startswith('party_name_'):
                e.set('left', str(int(e.get('left')) - 12))


for w in ('h3/main/nameplate', 'h3/main/party_plates', 'h3/friends/lobby_panel', 'h3/main/players',
          'h3/main/servers'):
    copy_tree(w, SHIFT, MADE)
for w in MADE.values():
    plates(w)
add(el('widget', name='online/lobby/backing', left=RIGHT - 30, top=14, width=290, height=20, bitmap='online/backing'))

# SWITCH LOBBY: its list beside the row, in the lobby (port online switch: shown, given the focus),
# each row replacing the lobby with its own (the multiplayer lobby, the Forge lobby, the campaign's
# SELECT PROFILE then its lobby); B closes it
# (port online switch multiplayer, forge, campaign: menu_functions.c)
SWITCH_TARGETS = [('MULTIPLAYER', None), ('FORGE', None), ('CAMPAIGN', None)]


def switcher(kind):
    made = []
    for index, (label, target) in enumerate(SWITCH_TARGETS):
        row = el('widget', name='online/%s/switch_%d' % (kind, index), type='text', width=176, height=18,
                 bitmap='online/item', font='h3/font_item', color='#FFF0F4F8', text_x=8, text_y=2,
                 string_list='online/lobby_text', string_index=3 + index)
        if (kind == 'mp' and label == 'MULTIPLAYER') or (kind == 'forge' and label == 'FORGE'):
            row.append(el('on', event='a start', run='port online switch close'))
        else:
            row.append(el('on', event='a start', run='port online switch ' + label.lower()))
        made.append(row)
    made.append(el('widget', name='online/%s/switch_panel' % kind, left=L + 166, top=63, width=W - 166, height=96,
                   bitmap='online/dropdown'))
    made.append(el('widget', name='online/%s/switch_text' % kind, type='text', left=R + 10, top=66, width=300,
                   height=80, font='h3/font_item', color='#FFF0F4F8', string_list='online/switch_text'))
    return made


def switch_row(kind):
    row = el('widget', name='online/%s/switch_row' % kind, type='text', string_list='online/lobby_text',
             string_index=0, **ROW)
    row.append(el('on', event='a start', run='port online switch open'))
    return row


def host_row(kind):
    # (HOST SETTINGS: the NETWORK picker; named network_row - not a text box, so the lobby's NETWORK
    # words go to its value at the right instead: the network's status)
    row = el('widget', name='online/%s/network_row' % kind, width=W, height=18, bitmap='online/item',
             flags='pass_unhandled_to_focused_child')
    row.append(el('on', event='a', open='online/network/screen'))
    row.append(el('child', widget='online/%s/host_label' % kind))
    row.append(el('child', widget='online/%s/network_status' % kind))
    return row


def foot(kind, leave_list, leave_index):
    made = [el('widget', name='online/%s/foot' % kind, left=L, top=422, width=W, height=26, bitmap='online/footer')]
    x = L + 8
    for name, bitmap, text, list_, index, width in (
            ('start_butn', 'h3/glyph_start', None, None, 0, 0),
            ('settings', None, 'online/lobby_text', None, 6, 62),
            ('a_butn', 'h3/glyph_cross', None, None, 0, 0),
            ('select', None, 'online/lobby_text', None, 7, 50),
            ('b_butn', 'h3/glyph_circle', None, None, 0, 0),
            ('back', None, 'online/lobby_text', None, 8, 50)):
        if bitmap:
            made.append(el('widget', name='online/%s/%s' % (kind, name), left=x, top=427, width=16, height=16,
                           bitmap=bitmap))
            x += 19
        else:
            made.append(el('widget', name='online/%s/%s' % (kind, name), type='text', left=x, top=426, width=width,
                           height=18, font='h3/font_item', color='#FFF0F4F8', text_y=1, string_list=text,
                           string_index=index))
            x += width
    return made, x


def lobby(kind, h3_screen, rows, extra_rows, title_widget):
    # the column's rows: SWITCH LOBBY, HOST SETTINGS, the lobby's own (GAME, MAP / MAP, EXTRA
    # OBJECTS), a gap, START, GAME OPTIONS, then ours
    for w in switcher(kind):
        add(w)
    add(switch_row(kind))
    add(el('widget', name='online/%s/host_label' % kind, type='text', left=0, top=0, width=160, height=18,
           font='h3/font_item', color='#FFF0F4F8', text_x=8, text_y=2, string_list='online/lobby_text',
           string_index=1))
    add(host_row(kind))
    add(take('h3/%s/network_status' % kind, left=150, top=3, width=W - 158, height=14, align='right'))
    ys = {'switch_row': 0, 'network_row': 19}
    y = 38
    for n in rows:
        add(take('h3/%s/%s' % (kind, n), **ROW))
        if n.startswith('start_'):
            continue
        ys[n] = y
        y += 19
    y += 5
    for n in rows:
        if n.startswith('start_'):
            ys[n] = y
    y += 19
    for n, label in extra_rows:
        w = take('h3/%s/%s' % (kind, n), **ROW)
        if label is not None:
            w.set('string_list', 'online/lobby_text')
            w.set('string_index', str(label))
        add(w)
        if n in ('browse_online', 'browse_link', 'browse_adhoc') and 'browse' in ys:
            ys[n] = ys['browse']
            continue
        ys[n] = y
        if n.startswith('browse'):
            ys['browse'] = y
        y += 19
    lst = take('h3/%s/list' % kind)
    for c in list(lst.findall('child')):
        lst.remove(c)
    # (the switcher's rows first, beside SWITCH LOBBY, shown while it is open: port online switch)
    lst.append(el('data', input='port online switch'))
    for index in range(len(SWITCH_TARGETS)):
        lst.append(el('child', widget='online/%s/switch_%d' % (kind, index), x=168, y=index * 19))
    for n in ['switch_row', 'network_row'] + [r for r in rows] + [r for r, _ in extra_rows]:
        lst.append(el('child', widget='online/%s/%s' % (kind, n), x=0, y=ys[n]))
    add(lst)
    status = 66 + y + 6
    add(el('widget', name='online/%s/panel' % kind, left=L, top=62, width=W, height=386, bitmap='online/panel'))
    add(el('widget', name='online/%s/band' % kind, left=L, top=status - 4, width=W, height=1, bitmap='online/rule'))
    add(take(title_widget, left=L + 2, top=26, width=400, height=34, font='online/font_header'))
    add(take('h3/%s/ready' % kind, left=L + 8, top=status, width=W - 16))
    add(take('h3/%s/ready_sub' % kind, left=L + 8, top=status + 17, width=W - 16, height=34))
    card = status + 52
    add(el('widget', name='online/%s/card' % kind, left=L, top=card, width=W, height=116, bitmap='online/card'))
    return card


# ---- the multiplayer lobby
card = lobby('mp', 'h3/mp/screen',
             ['game_row', 'map_row', 'start_online', 'start_link', 'start_adhoc', 'start_offline'],
             [('edit_row', 2), ('browse_online', None), ('browse_link', None), ('browse_adhoc', None),
              ('code_row', None)],
             'h3/mp/title')
add(take('h3/mp/map_picture', left=L + 6, top=card + 5, width=140, height=114))
add(el('widget', name='online/mp/map_value', type='text', left=L + 156, top=card + 6, width=W - 162, height=20,
       font='h3/font_title', color='#FFF2F6FA'))
add(el('widget', name='online/mp/map_text', type='text', left=L + 156, top=card + 30, width=W - 162, height=90,
       font='h3/font_small', color='#FFC4D2E2',
       string_list='ui\\shell\\main_menu\\multiplayer_type_select\\mp_map_select\\map_data'))
add(take('h3/mp/caption', left=L + 8, top=card + 119, width=W - 16, height=20))
made, x = foot('mp', None, 0)
for w in made:
    add(w)
add(take('h3/mp/x_butn', left=x, top=427))
add(take('h3/mp/options', left=x + 19, top=426, width=60))
add(take('h3/mp/players', left=RIGHT - 24, top=16, width=270, font='h3/font_item'))
add(take('h3/mp/l_butn', left=RIGHT + 110, top=427))
add(take('h3/mp/invite', left=RIGHT + 129, top=426, width=120))
screen = take('h3/mp/screen')


def lobby_children(screen, kind, own):
    # the screen's children: the haze, the panel and band and card, the lobby's own, the right side,
    # the switcher, the lists
    # (B: port online lobby leave, which closes the switcher when it is open, in place of the Halo 3
    # lobby's port h3 live leave and back)
    keep = [c for c in screen if c.tag != 'child' and not (c.tag == 'on' and c.get('event') == 'b back')]
    keep.append(el('on', event='b back', run='port online lobby leave', back='true'))
    for c in list(screen):
        screen.remove(c)
    for c in keep:
        screen.append(c)
    screen.append(el('on', event='created', run='port online switch close'))
    screen.append(el('on', event='start', run='port settings panel'))
    screen.append(el('data', input='port online lobby'))
    for w in ['online/main/haze', 'online/%s/panel' % kind, 'online/%s/band' % kind, 'online/%s/card' % kind,
              'online/%s/foot' % kind, 'online/lobby/backing'] + own + [
              'online/lobby/main/nameplate', 'online/lobby/main/party_plates', 'online/lobby/friends/lobby_panel',
              'online/%s/switch_panel' % kind, 'online/%s/switch_text' % kind]:
        screen.append(el('child', widget=w))
    for i in range(1, 9):
        screen.append(el('child', widget='h3/pregame/row_%d' % i, x=RIGHT, y=48 + (i - 1) * 18))
    screen.append(el('child', widget='online/%s/list' % kind, x=L, y=66))


lobby_children(screen, 'mp', ['online/mp/title', 'online/mp/ready', 'online/mp/ready_sub', 'online/mp/map_picture',
                              'online/mp/map_value', 'online/mp/map_text', 'online/mp/caption',
                              'online/mp/start_butn', 'online/mp/settings', 'online/mp/a_butn', 'online/mp/select',
                              'online/mp/b_butn', 'online/mp/back', 'online/mp/x_butn', 'online/mp/options',
                              'online/mp/players', 'online/mp/l_butn', 'online/mp/invite'])
for w in MADE.values():
    add(w)
add(screen)

# ---- the pickers (SELECT NETWORK, SELECT MAP, SELECT GAME TYPE): as the online lobby's, a band across
# the screen over the lobby's scene - the title, the list at the left on the orange bar, the card at the
# right with the map's picture and description, Select and Back at the band's foot
P = 6
add(el('widget', name='online/pick/dim', left=0, top=0, width=640, height=480, bitmap='online/haze'))
add(el('widget', name='online/pick/header', left=0, top=72, width=640, height=278, bitmap='online/panel'))
add(el('widget', name='online/pick/card', left=216, top=80, width=290, height=236, bitmap='online/card'))
add(el('widget', name='online/pick/foot', left=0, top=326, width=640, height=24, bitmap='online/footer'))
add(el('widget', name='online/pick/a_butn', left=P, top=330, width=16, height=16, bitmap='h3/glyph_cross'))
add(el('widget', name='online/pick/select', type='text', left=P + 20, top=329, width=70, height=18, font='h3/font_item',
       color='#FFF0F4F8', text_y=1, string_list='h3/campaign_text', string_index=5))
add(el('widget', name='online/pick/b_butn', left=P + 90, top=330, width=16, height=16, bitmap='h3/glyph_circle'))
add(el('widget', name='online/pick/back', type='text', left=P + 110, top=329, width=70, height=18, font='h3/font_item',
       color='#FFF0F4F8', text_y=1, string_list='h3/main_items', string_index=10))
PROW = dict(bitmap='online/item', font='h3/font_item', color='#FFF0F4F8', text_x=8, text_y=1, width=150, height=17)
for kind, rows, extra in [('network', ['item_%d' % i for i in range(4)], []),
                          ('map', ['item_%d' % i for i in range(13)], ['map_picture', 'map_text', 'map_why']),
                          ('gametype', ['item_%d' % i for i in range(1, 13)], ['gametype_text'])]:
    for r in rows:
        add(take('h3/%s/%s' % (kind, r), **PROW))
    lst = take('h3/%s/list' % kind)
    for i, c in enumerate(lst.findall('child')):
        c.set('y', str(i * 18))
    add(lst)
    add(take('h3/%s/title' % kind, left=P - 4, top=36, width=400, height=34, font='online/font_header'))
    for e in extra:
        if e == 'map_picture':
            add(take('h3/map/map_picture', left=226, top=88, width=140, height=114))
        else:
            add(take('h3/%s/%s' % (kind, e), left=226, top=88 if kind != 'map' else 208, width=272, height=110))
    if kind == 'network':
        add(take('h3/network/network_text', left=226, top=88, width=272, height=190))
    scr = take('h3/%s/screen' % kind)
    for c in list(scr.findall('child')):
        w = c.get('widget')
        if w == 'h3/main/haze':
            c.set('widget', 'online/pick/dim')
        elif w.startswith('h3/pick/'):
            if w.endswith(('/rule', '/strip', '/panel')):
                scr.remove(c)
            else:
                c.set('widget', 'online/pick/' + w.rsplit('/', 1)[1])
        elif w.endswith('/list'):
            c.set('x', str(P)); c.set('y', '86')
    first = [i for i, c in enumerate(scr) if c.tag == 'child'][1]
    for i, w in enumerate(['online/pick/header'] + (['online/pick/card'] if kind != 'network' else []) +
                          ['online/pick/foot']):
        scr.insert(first + i, el('child', widget=w))
    add(scr)

# ---- the Forge lobby (halo3/forge_lobby.xml), the same: SWITCH LOBBY, HOST SETTINGS, MAP, EXTRA
# OBJECTS, START FORGE
card = lobby('forge', 'h3/forge/lobby', ['map_row', 'packs_row', 'start_row', 'start_live'], [], 'h3/forge/title')
add(take('h3/forge/map_picture', left=L + 6, top=card + 5))
add(take('h3/forge/emblem', left=L + 12, top=card + 11))
add(take('h3/forge/note', left=L + 156, top=card + 6, width=W - 162, height=110))
add(take('h3/forge/caption', left=L + 8, top=card + 119, width=W - 16, height=20))
made, x = foot('forge', None, 0)
for w in made:
    add(w)
screen = take('h3/forge/lobby')
lobby_children(screen, 'forge', ['online/forge/title', 'online/forge/ready', 'online/forge/ready_sub',
                                 'online/forge/map_picture', 'online/forge/emblem', 'online/forge/note',
                                 'online/forge/caption', 'online/forge/start_butn', 'online/forge/settings',
                                 'online/forge/a_butn', 'online/forge/select', 'online/forge/b_butn',
                                 'online/forge/back', 'online/mp/players', 'online/mp/l_butn', 'online/mp/invite'])
# (its leave, as the Halo 3 lobby's: the live game closed, then back)
add(screen)
for i in range(13):
    add(take('h3/forge/maps/item_%d' % i, **PROW))
lst = take('h3/forge/maps/list')
for i, c in enumerate(lst.findall('child')):
    c.set('y', str(i * 18))
add(lst)
add(take('h3/forge/maps/title', left=P - 4, top=36, width=400, height=34, font='online/font_header'))
add(take('h3/forge/maps/maps_picture', left=226, top=88))
add(take('h3/forge/maps/maps_text', left=226, top=208, width=272, height=110))
add(take('h3/forge/maps/variant_name', left=226, top=208, width=272))
add(take('h3/forge/maps/variant_text', left=226, top=230, width=272, height=90))
scr = take('h3/forge/maps/screen')
for c in list(scr.findall('child')):
    w = c.get('widget')
    if w == 'h3/main/haze':
        c.set('widget', 'online/pick/dim')
    elif w.startswith('h3/pick/'):
        if w.endswith(('/rule', '/strip', '/panel')):
            scr.remove(c)
        else:
            c.set('widget', 'online/pick/' + w.rsplit('/', 1)[1])
    elif w.endswith('/list'):
        c.set('x', str(P)); c.set('y', '86')
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
SELECT GAME TYPE), and the Forge lobby and its map picker (halo3/forge_lobby.xml's). They open in place of the Halo 3 lobby's (h3/mp/screen and the others:
menu_tags.c menu_tags_online) and are the same widgets - every row, handler and data input of
halo3/multiplayer.xml, the same names (made from it by a script) - laid out the online way: the
title over a see-through navy column at the left (the rows, the network's status, Ready, the map's
picture and "<game> on <map>", the buttons at its foot), the players, the party and the friends at
the wide screen's right; the pickers a band with the title, the list at the left on the orange bar
and the map's picture and description in a card at the right. The art is this folder's own
(tools/online_menu_art.py) and the Halo 3 set's. -->
'''
open(out, 'w').write(head + body + '\n')
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from online_menu_finish import finish  # noqa: E402
finish(out)
print(len(items), 'widgets')
