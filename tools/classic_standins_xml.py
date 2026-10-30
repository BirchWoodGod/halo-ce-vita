#!/usr/bin/env python3
"""writes port/vita/app0/menus/classic/standins.xml and players.xml: Menu style Classic+'s screens
standing for the Halo 3 set's NETWORK picker, PLAYERS (and a player's options: players.xml, read
over a level too) and GAME TYPES (the list, a game
type's options, the delete question, the bases, the editor, a group of its settings, the question
before leaving it) - a cp/<name> widget stands for h3/<name> while the style is Classic+
(menu_tags.c, the classic set).

The look is the original menus': ui.map's gradient, the heading at 35,11 (the port's words drawn in
the look of the game's headings: tools/classic_menu_art.py), lists of list_item_bkd rows at 51 with
the description's list_field at 275,78 (the Xbox's Multiplayer and profile screens), settings on
Halo PC's option_bkds rows with the value between the spinners' arrows (its Server Setup), the
players on its server browser's rows, the button key at the foot as the Xbox's (the key, "=" and
the word in large_ui: A at 481 (the Xbox's at 471, moved for a translation's longer words), B at 371, the others at 196 and 40). Every widget a function of
menu_functions.c finds by its last name has it here; what each row runs (its <on> and <data>) is
copied from the Halo 3 set's own widget of the same name, so the two can't drift apart.

    python3 tools/classic_standins_xml.py port/vita/app0/menus/classic
"""
import os
import re
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
MENUS = os.path.join(HERE, '..', 'port', 'vita', 'app0', 'menus')
BLUE = '#FF2896FF'
WHITE = '#FFE0F0FF'
GREY = '#FFCCCCCC'
YELLOW = '#FFF0D090'
GRADIENT = r'ui\shell\bitmaps\gradient'
ITEM = r'ui\shell\bitmaps\list_item_bkd'
ITEM_TOP = r'ui\shell\bitmaps\list_item_bkd_top'
FIELD = r'ui\shell\bitmaps\list_field'
ROW_X, ROW_Y, ROW_STEP = 51, 78, 32
FIELD_X, FIELD_Y, FIELD_W, FIELD_H = 275, 78, 320, 322
IN_X, IN_W = 290, 290

# the Halo 3 set's widgets: what each runs
H3 = {}
for name in ('multiplayer.xml', 'players.xml', 'gametypes.xml'):
    for element in ET.parse(os.path.join(MENUS, 'halo3', name)).getroot().iter('widget'):
        H3[element.get('name')] = element

out = []
w = out.append


def esc(value):
    return value.replace('&', '&amp;').replace('"', '&quot;').replace('<', '&lt;')


def handlers(h3_name):
    """the Halo 3 widget's <on> and <data> lines, as they are"""
    lines = []
    for child in H3[h3_name]:
        if child.tag in ('on', 'data'):
            attributes = ' '.join(f'{k}="{esc(v)}"' for k, v in child.attrib.items())
            lines.append(f'\t\t<{child.tag} {attributes}/>')
    return lines


def text(name, l, t, wd, h, font='small', color=BLUE, extra=''):
    w(f'\t<widget name="{name}" type="text" left="{l}" top="{t}" width="{wd}" height="{h}" font="ui\\{font}_ui" color="{color}" text_flags="no_focus_test"{extra}/>')


def strings(name, items):
    w(f'\t<strings name="{name}">')
    for item in items:
        w(f'\t\t<string text="{esc(item)}"/>')
    w('\t</strings>')


def heading(name, png):
    w(f'\t<bitmap name="{name}_art">')
    w(f'\t\t<frame png="classic/{{lang}}/{png}.png" width="512" height="64"/>')
    w('\t</bitmap>')
    w(f'\t<widget name="{name}" left="35" top="11" width="512" height="64" bitmap="{name}_art"/>')


def keys(prefix, labels):
    """the button key: labels {key: (widget's last name, strings name, index)} for a, b, x, y"""
    places = {'a': 481, 'b': 371}
    further = [k for k in ('y', 'x') if k in labels]
    for key, left in zip(further, (40, 196) if len(further) == 2 else (196,)):
        places[key] = left
    names = []
    for key in ('y', 'x', 'b', 'a'):
        if key not in labels:
            continue
        left = places[key]
        label, string_list, index = labels[key]
        size = '21" height="26' if key in 'ab' else '20" height="25'
        w(f'\t<widget name="{prefix}/{key}_butn" left="{left}" top="414" width="{size}" bitmap="ui\\shell\\bitmaps\\{key}_butn"/>')
        room = {481: 134, 371: 86, 196: 150, 40: 132}[left]
        extra = f' string_list="{string_list}" string_index="{index}"' if string_list else ''
        text(f'{prefix}/{label}', left + 21, 414, room, 24, 'large', BLUE, extra)
        names += [f'{prefix}/{key}_butn', f'{prefix}/{label}']
    return names


def screen(name, children, h3_name, background=GRADIENT):
    w(f'\t<widget name="{name}" flags="pass_unhandled_to_focused_child" bitmap="{background}">')
    for line in handlers(h3_name):
        w(line)
    for child in children:
        if isinstance(child, tuple):
            w(f'\t\t<child widget="{child[0]}" x="{child[1]}" y="{child[2]}"/>')
        else:
            w(f'\t\t<child widget="{child}"/>')
    w('\t</widget>')


def field(name):
    w(f'\t<widget name="{name}" left="{FIELD_X}" top="{FIELD_Y}" width="{FIELD_W}" height="{FIELD_H}" bitmap="{FIELD}"/>')


def tab_rows(prefix, h3_prefix, names, columns=(), string_list=None, first_index=0, indexes=None):
    """list_item_bkd rows (the first list_item_bkd_top), each a text row (no columns) or a row of the
    columns' text boxes; their <on> the Halo 3 rows'"""
    for i, row in enumerate(names):
        bitmap = ITEM_TOP if i == 0 else ITEM
        if columns:
            w(f'\t<widget name="{prefix}/{row}" width="226" height="32" bitmap="{bitmap}">')
        else:
            extra = ''
            if string_list:
                extra = f' string_list="{string_list}" string_index="{indexes[i] if indexes else first_index + i}"'
            w(f'\t<widget name="{prefix}/{row}" type="text" width="226" height="32" bitmap="{bitmap}" font="ui\\large_ui" color="{BLUE}" text_x="13" text_y="5"{extra}>')
        for line in handlers(f'{h3_prefix}/{row}'):
            w(line)
        for column in columns:
            w(f'\t\t<child widget="{prefix}/{column}"/>')
        w('\t</widget>')


def row_columns(prefix, tag='row_tag'):
    """a row's name in the large font, its tag in the small one at the right, ending well short of
    the description's field"""
    w(f'\t<widget name="{prefix}/row_name" type="text" left="13" top="5" width="199" height="24" font="ui\\large_ui" color="{BLUE}"/>')
    w(f'\t<widget name="{prefix}/{tag}" type="text" left="92" top="9" width="120" height="18" align="right" font="ui\\small_ui" color="{BLUE}"/>')


def column_list(name, h3_name, rows, x=0, y=0, step=ROW_STEP):
    w(f'\t<widget name="{name}" type="column_list" flags="pass_unhandled_to_focused_child up_down_tabs_items">')
    for line in handlers(h3_name):
        w(line)
    for i, row in enumerate(rows):
        w(f'\t\t<child widget="{row}" x="{x}" y="{y + step * i}"/>')
    w('\t</widget>')


def option_rows(prefix, h3_prefix, names):
    """the settings' rows as classic/customize.xml's (CUSTOMIZATION): Halo PC's server browser rows
    across the screen, the setting at the left and its value between the spinners' arrows, both in
    ui.map's small font (one size for every row: the translations' longest labels fit it)"""
    w(f'\t<widget name="{prefix}/label" type="text" left="0" top="0" width="270" height="17" font="ui\\small_ui" color="{BLUE}" text_x="44" text_y="1"/>')
    w(f'\t<widget name="{prefix}/value" type="text" left="291" top="0" width="220" height="17" align="center" font="ui\\small_ui" color="{BLUE}" text_y="1" text_flags="no_focus_test"/>')
    w(f'\t<widget name="{prefix}/arrow_left" left="279" top="1" width="12" height="16" bitmap="arrow_sm_left"/>')
    w(f'\t<widget name="{prefix}/arrow_right" left="511" top="1" width="12" height="16" bitmap="arrow_sm_right"/>')
    for row in names:
        h3_row = H3[f'{h3_prefix}/{row}']
        if h3_row.get('type') == 'text':
            extra = ''
            if h3_row.get('string_list'):
                extra = f' string_list="{h3_row.get("string_list")}" string_index="{h3_row.get("string_index")}"'
            w(f'\t<widget name="{prefix}/{row}" type="text" width="600" height="17" bitmap="server_item_bkds" font="ui\\small_ui" color="{BLUE}" text_x="44" text_y="1"{extra}>')
            for line in handlers(f'{h3_prefix}/{row}'):
                w(line)
            w('\t</widget>')
            continue
        w(f'\t<widget name="{prefix}/{row}" width="600" height="17" flags="pass_unhandled_to_focused_child" bitmap="server_item_bkds" color="{BLUE}">')
        for line in handlers(f'{h3_prefix}/{row}'):
            w(line)
        for child in [c.get('widget').split('/')[-1] for c in H3[f'{h3_prefix}/{row}'] if c.tag == 'child']:
            w(f'\t\t<child widget="{prefix}/{child}"/>')
        w('\t</widget>')


w('<?xml version="1.0" encoding="UTF-8"?>')
w('<!-- Menu style Classic+: the NETWORK picker and GAME TYPES in the original menus\' look, standing\n'
  '     for the Halo 3 set\'s (a cp/<name> widget for h3/<name>: menu_tags.c, the classic set). Written by\n'
  '     tools/classic_standins_xml.py, which says how they are laid out. -->')
w('<menus>')
strings('cp/standin_keys', ['=BACK', '=SELECT', '=OPTIONS', '=CHANGE', '=DELETE', '=NEW'])
# (words of the Halo 3 set's broken into lines for its narrower font, broken here for ui.map's small_ui)
strings('cp/standin_text', ['The game type your new one starts as: its\\nrules, which you then change. The game\'s\\nown are never changed.'])
K = 'cp/standin_keys'
# (the button key's B and A for the classic set's other screens: forge.xml, pregame.xml, customize.xml)
w('\t<widget name="cp/key/b_butn" left="371" top="414" width="21" height="26" bitmap="ui\\shell\\bitmaps\\b_butn"/>')
text('cp/key/back', 392, 414, 86, 24, 'large', BLUE, f' string_list="{K}" string_index="0"')
w('\t<widget name="cp/key/a_butn" left="481" top="414" width="21" height="26" bitmap="ui\\shell\\bitmaps\\a_butn"/>')
text('cp/key/select', 502, 414, 134, 24, 'large', BLUE, f' string_list="{K}" string_index="1"')

# ---- NETWORK
w('\n\t<!-- NETWORK (the Forge lobby\'s and the Halo 3 lobby\'s NETWORK row): the networks at the left,\n\t     the focused one\'s words in the field -->')
heading('cp/network/header', 'header_select_network')
tab_rows('cp/network', 'h3/network', ['item_0', 'item_1', 'item_2', 'item_3'], string_list='h3/networks')
column_list('cp/network/list', 'h3/network/list', [f'cp/network/item_{i}' for i in range(4)])
field('cp/network/field')
# (the Halo 3 set's words, broken into lines for its own font: a little wider here for ui.map's)
text('cp/network/network_text', IN_X - 4, 90, IN_W + 10, 200, extra=' string_list="h3/network_text"')
footer = keys('cp/network', {'b': ('back', K, 0), 'a': ('select', K, 1)})
screen('cp/network/screen', ['cp/network/header', 'cp/network/field', 'cp/network/network_text'] + footer +
       [('cp/network/list', ROW_X, ROW_Y)], 'h3/network/screen')

# ---- GAME TYPES
w('\n\t<!-- GAME TYPES: the player\'s game types, then the game\'s own (CREATE NEW GAME TYPE first), each its\n\t     game at the right of its row; the chosen one\'s rules in the field -->')
heading('cp/gametypes/header', 'header_game_types')
heading('cp/gtbase/header', 'header_choose_a_base')
heading('cp/gtedit/header', 'header_edit_game_type')
row_columns('cp/gametypes', 'row_note')
tab_rows('cp/gametypes', 'h3/gametypes', [f'row_{i}' for i in range(1, 11)], columns=('row_name', 'row_note'))
column_list('cp/gametypes/list', 'h3/gametypes/list', [f'cp/gametypes/row_{i}' for i in range(1, 11)])
field('cp/gametypes/field')
text('cp/gametypes/info_name', IN_X, 88, IN_W, 26, 'large', WHITE)
text('cp/gametypes/info_name_small', IN_X, 92, IN_W, 22, 'small', WHITE)
text('cp/gametypes/info_kind', IN_X, 116, IN_W, 18)
text('cp/gametypes/info_text', IN_X, 138, IN_W, 200)
text('cp/gametypes/status', IN_X, 344, IN_W, 50, color=YELLOW)
footer = keys('cp/gametypes', {'y': ('new', K, 5), 'x': ('delete', K, 4), 'b': ('back', K, 0), 'a': ('select', K, 1)})
screen('cp/gametypes/screen', ['cp/gametypes/header', 'cp/gametypes/field', 'cp/gametypes/info_name', 'cp/gametypes/info_name_small',
                               'cp/gametypes/info_kind', 'cp/gametypes/info_text', 'cp/gametypes/status'] + footer +
       [('cp/gametypes/list', ROW_X, ROW_Y)], 'h3/gametypes/screen')


def question(prefix, header, rows, title_extra='', question_widget=False):
    """a game type's options or a question: its rows at the left, its title and words in the field"""
    tab_rows(prefix, 'h3/' + prefix[3:], rows)
    # (the rows' words: the Halo 3 rows' strings)
    column_list(f'{prefix}/list', 'h3/' + prefix[3:] + '/list', [f'{prefix}/{r}' for r in rows])
    field(f'{prefix}/field')
    text(f'{prefix}/title', IN_X, 88, IN_W, 26, 'large', WHITE, title_extra)
    text(f'{prefix}/title_small', IN_X, 92, IN_W, 22, 'small', WHITE)
    names = [f'{prefix}/field', f'{prefix}/title', f'{prefix}/title_small']
    if question_widget:
        w(f'\t<widget name="{prefix}/question" type="text" left="{IN_X}" top="120" width="{IN_W}" height="120" font="ui\\small_ui" color="{BLUE}" text_flags="no_focus_test">')
        for line in handlers('h3/' + prefix[3:] + '/question'):
            w(line)
        w('\t</widget>')
        names.append(f'{prefix}/question')
    else:
        text(f'{prefix}/help', IN_X, 120, IN_W, 200)
        names.append(f'{prefix}/help')
    footer = keys(prefix, {'b': ('back', K, 0), 'a': ('select', K, 1)})
    screen(f'{prefix}/screen', [header] + names + footer + [(f'{prefix}/list', ROW_X, ROW_Y)], 'h3/' + prefix[3:] + '/screen')


def string_of(h3_name):
    element = H3[h3_name]
    return f' string_list="{element.get("string_list")}" string_index="{element.get("string_index")}"'


# (the rows' words are the Halo 3 rows' strings: tab_rows writes text rows without them, so they are
# written here with them)
def tab_text_rows(prefix, rows):
    for i, row in enumerate(rows):
        bitmap = ITEM_TOP if i == 0 else ITEM
        w(f'\t<widget name="{prefix}/{row}" type="text" width="226" height="32" bitmap="{bitmap}" font="ui\\large_ui" color="{BLUE}" text_x="13" text_y="5"{string_of("h3/" + prefix[3:] + "/" + row)}>')
        for line in handlers('h3/' + prefix[3:] + '/' + row):
            w(line)
        w('\t</widget>')


tab_rows = (lambda original: (lambda prefix, h3_prefix, names, **kw: tab_text_rows(prefix, names)
                              if not kw and prefix in ('cp/gtact', 'cp/gtdel', 'cp/gtquit') else original(prefix, h3_prefix, names, **kw)))(tab_rows)

w('\n\t<!-- a game type\'s options (EDIT, COPY, RENAME, DELETE: the game\'s own EDIT and COPY alone) -->')
question('cp/gtact', 'cp/gametypes/header', ['op_edit', 'op_copy', 'op_rename', 'op_delete'])
w('\n\t<!-- the question before a delete: CANCEL first, then DELETE GAME TYPE -->')
question('cp/gtdel', 'cp/gametypes/header', ['op_cancel', 'op_delete'], string_of('h3/gtdel/title'), question_widget=True)
w('\n\t<!-- leaving the editor with changes not saved: SAVE CHANGES, DISCARD CHANGES, CANCEL -->')
question('cp/gtquit', 'cp/gtedit/header', ['op_save', 'op_discard', 'op_cancel'], string_of('h3/gtquit/title'), question_widget=True)

w('\n\t<!-- CREATE NEW GAME TYPE: the bases (the game\'s own game types), ten shown, the chosen one\'s rules in the field -->')
for i in range(1, 11):
    w(f'\t<widget name="cp/gtbase/item_{i}" type="text" width="226" height="32" bitmap="{ITEM_TOP if i == 1 else ITEM}" font="ui\\large_ui" color="{BLUE}" text_x="13" text_y="5">')
    for line in handlers(f'h3/gtbase/item_{i}'):
        w(line)
    w('\t</widget>')
column_list('cp/gtbase/list', 'h3/gtbase/list', [f'cp/gtbase/item_{i}' for i in range(1, 11)])
field('cp/gtbase/field')
text('cp/gtbase/base_help', IN_X, 88, IN_W, 60, extra=' string_list="cp/standin_text" string_index="0"')
text('cp/gtbase/info_name', IN_X, 152, IN_W, 26, 'large', WHITE)
text('cp/gtbase/info_text', IN_X, 180, IN_W, 214)
footer = keys('cp/gtbase', {'b': ('back', K, 0), 'a': ('select', K, 1)})
screen('cp/gtbase/screen', ['cp/gtbase/header', 'cp/gtbase/field', 'cp/gtbase/base_help', 'cp/gtbase/info_name', 'cp/gtbase/info_text'] +
       footer + [('cp/gtbase/list', ROW_X, ROW_Y)], 'h3/gtbase/screen')

w('\n\t<!-- the editor: NAME, GAME, the groups of settings, SAVE on Halo PC\'s option rows; what the game type\n\t     is under the heading, the focused row\'s help and the outcome under the rows -->')
editor = ['op_name', 'op_game', 'op_general', 'op_respawn', 'op_player', 'op_items', 'op_team', 'op_rules', 'op_save']
option_rows('cp/gtedit', 'h3/gtedit', editor)
column_list('cp/gtedit/list', 'h3/gtedit/list', [f'cp/gtedit/{r}' for r in editor], step=19)
text('cp/gtedit/subtitle', 64, 80, 512, 18)
text('cp/gtedit/help', 64, 300, 512, 54, color=WHITE)
text('cp/gtedit/status', 64, 360, 512, 46, color=YELLOW)
footer = keys('cp/gtedit', {'b': ('back', K, 0), 'a': ('change', K, 3)})
screen('cp/gtedit/screen', ['cp/gtedit/header', 'cp/gtedit/subtitle', 'cp/gtedit/help', 'cp/gtedit/status'] + footer +
       [('cp/gtedit/list', 20, 100)], 'h3/gtedit/screen')

w('\n\t<!-- a group of settings: up to eight rows (port h3 gametype options names and fills them, the others\n\t     hidden), the group\'s name under the heading, the focused row\'s help and a note under the rows -->')
option_rows('cp/gtopt', 'h3/gtopt', [f'op_{i}' for i in range(1, 9)])
column_list('cp/gtopt/list', 'h3/gtopt/list', [f'cp/gtopt/op_{i}' for i in range(1, 9)], step=19)
text('cp/gtopt/title', 64, 74, 300, 24, 'large', WHITE)
text('cp/gtopt/subtitle', 370, 80, 206, 18, extra=' align="right"')
text('cp/gtopt/help', 64, 300, 512, 54, color=WHITE)
text('cp/gtopt/note', 64, 360, 512, 46)
footer = keys('cp/gtopt', {'b': ('back', K, 0), 'a': ('change', K, 3)})
screen('cp/gtopt/screen', ['cp/gtedit/header', 'cp/gtopt/title', 'cp/gtopt/subtitle', 'cp/gtopt/help', 'cp/gtopt/note'] + footer +
       [('cp/gtopt/list', 20, 100)], 'h3/gtopt/screen')
w('</menus>')
open(os.path.join(sys.argv[1], 'standins.xml'), 'w').write('\n'.join(out) + '\n')

# ---- classic/players.xml: PLAYERS, in the menus (menus.txt) and over a level (ingame.txt: a game's pause)
out.clear()
w('<?xml version="1.0" encoding="UTF-8"?>')
w('<!-- Menu style Classic+: PLAYERS (a game\'s pause, the pregame\'s LOBBY OPTIONS) in the original menus\' look,\n'
  '     standing for the Halo 3 set\'s (a cp/<name> widget for h3/<name>: menu_tags.c, the classic set); read with\n'
  '     the menus and with a level\'s (ingame.txt). Written by tools/classic_standins_xml.py, which says how it is\n'
  '     laid out. -->')
w('<menus>')
PK = 'cp/players_keys'
strings(PK, ['=BACK', '=SELECT', '=OPTIONS'])
# ---- PLAYERS
w('\n\t<!-- PLAYERS (a game\'s pause, the pregame\'s LOBBY OPTIONS): the players on the server browser\'s rows\n\t     (name, team, what they are to this PS Vita), the chosen one\'s words under them -->')
heading('cp/players/header', 'header_players')
text('cp/players/players_status', 24, 336, 592, 20)
text('cp/players/header_name', 32, 82, 250, 16, color=GREY, extra=' string_list="h3/players_text" string_index="1"')
text('cp/players/header_team', 330, 82, 120, 16, color=GREY, extra=' string_list="h3/players_text" string_index="2"')
text('cp/players/header_friend', 470, 82, 140, 16, color=GREY, extra=' string_list="h3/players_text" string_index="3"')
text('cp/players/row_name', 12, 1, 296, 16)
text('cp/players/row_team', 310, 1, 136, 16)
text('cp/players/row_state', 450, 1, 146, 16)
for i in range(1, 11):
    w(f'\t<widget name="cp/players/row_{i}" width="600" height="17" bitmap="server_item_bkds">')
    for line in handlers(f'h3/players/row_{i}'):
        w(line)
    for column in ('row_name', 'row_team', 'row_state'):
        w(f'\t\t<child widget="cp/players/{column}"/>')
    w('\t</widget>')
column_list('cp/players/list', 'h3/players/list', [f'cp/players/row_{i}' for i in range(1, 11)], step=18)
w(f'\t<widget name="cp/players/blueline" left="24" top="286" width="592" height="2" bitmap="ui\\shell\\bitmaps\\blue"/>')
text('cp/players/detail', 24, 294, 592, 40)
text('cp/players/players_message', 24, 358, 592, 46, color=YELLOW)
footer = keys('cp/players', {'b': ('back', PK, 0), 'a': ('a_label', PK, 2)})
# (over a level: the game dimmed under the gradient, as the Xbox's questions dim the screen)
w(f'\t<widget name="cp/players/backdrop" left="0" top="0" width="640" height="480" bitmap="{GRADIENT}"/>')
screen('cp/players/screen', ['cp/players/backdrop', 'cp/players/header', 'cp/players/players_status', 'cp/players/header_name', 'cp/players/header_team',
                             'cp/players/header_friend', ('cp/players/list', 20, 100), 'cp/players/blueline', 'cp/players/detail',
                             'cp/players/players_message'] + footer, 'h3/players/screen', background=r'ui\shell\bitmaps\semi_transparent_grey')
w('\n\t<!-- a player\'s options: the rows at the left (BACK first; those that do not apply hidden), the player\n\t     and what they are in the field -->')
options = ['option_back', 'option_add', 'option_block', 'option_mute', 'option_party', 'option_kick', 'option_ban']
tab_rows('cp/players', 'h3/players', options, string_list='h3/players_text', indexes=[13, 6, 7, 12, 9, 10, 11])
column_list('cp/players/options_list', 'h3/players/options_list', [f'cp/players/{o}' for o in options])
field('cp/players/options_field')
text('cp/players/options_title', IN_X, 88, IN_W, 26, 'large', WHITE)
text('cp/players/options_text', IN_X, 118, IN_W, 270)
footer = keys('cp/players/options', {'b': ('options_back', PK, 0), 'a': ('options_a_label', PK, 1)})
screen('cp/players/options', ['cp/players/backdrop', 'cp/players/header', 'cp/players/options_field', 'cp/players/options_title', 'cp/players/options_text'] +
       footer + [('cp/players/options_list', ROW_X, ROW_Y)], 'h3/players/options', background=r'ui\shell\bitmaps\semi_transparent_grey')

w('</menus>')
# (over a level its map has none of ui.map's shell pictures: each read from Halo PC's bitmaps.map by
# its name, as art.xml reads them - art.xml itself is the menus' alone)
text_out = '\n'.join(out) + '\n'
pictures = []
for picture in ('gradient', 'semi_transparent_grey', 'list_item_bkd', 'list_item_bkd_top', 'list_field', 'server_item_bkds', 'blue', 'a_butn', 'b_butn'):
    local = f'cp/players/art_{picture}'
    text_out = text_out.replace(f'bitmap="ui\\shell\\bitmaps\\{picture}"', f'bitmap="{local}"')
    text_out = text_out.replace(f'bitmap="{picture}"', f'bitmap="{local}"')
    pictures.append(f'\t<bitmap name="{local}" resource="ui\\shell\\bitmaps\\{picture}"/>')
text_out = text_out.replace('<menus>\n', '<menus>\n' + '\n'.join(pictures) + '\n', 1)
open(os.path.join(sys.argv[1], 'players.xml'), 'w').write(text_out)
