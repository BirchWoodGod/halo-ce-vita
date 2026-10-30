#!/usr/bin/env python3
"""writes port/vita/app0/menus/classic/friends.xml (the rows are many and alike):

    python3 tools/classic_friends_xml.py port/vita/app0/menus/classic/friends.xml
"""
import sys
out = []
w = out.append
BLUE = '#FF2896FF'
WHITE = '#FFE0F0FF'
YELLOW = '#FFF0D090'
FIELD = r'ui\shell\bitmaps\list_field'
# (the Xbox's list screens' places: the rows at 51 (list_item_bkd, its tab 232 wide), the
# description's field at 275,78 320 x 322, its words inside it from 290; the button key's keys at
# 40, 196 (the further ones), 371 (B) and 481 (A), each key's words after it: "=" and the word)
ROW_X = 51
# (the lists' rows: list_item_bkd's tab, 24 high for the small font)
ROW_H = 24
FIELD_X, FIELD_Y, FIELD_W, FIELD_H = 275, 78, 320, 322
# (the field's words: 15 in from its left, 25 short of its right - at least 16 pixels inside its
# border at 960x544)
IN_X, IN_W = 290, 280
ITEM = r'ui\shell\bitmaps\list_item_bkd'
ITEM_TOP = r'ui\shell\bitmaps\list_item_bkd_top'
GRADIENT = r'ui\shell\bitmaps\gradient'

def text(name, l, t, wd, h, font='small', color=BLUE, extra=''):
    return f'\t<widget name="{name}" type="text" left="{l}" top="{t}" width="{wd}" height="{h}" font="ui\\{font}_ui" color="{color}" text_flags="no_focus_test"{extra}/>'

def keys(prefix, a=True, x=False, y=False, ylist=None, yindex=None):
    """the button key along the foot, as the Xbox screens' (A at 481 (the Xbox's at 471, moved for the longer words of a translation), B at 371 - classic/back -, the
    further keys at 196 and 40), each key's words "=" and the word in large_ui; Y in the wider place"""
    lines = []
    names = []
    if y:
        lines.append(f'\t<widget name="{prefix}/y_butn" left="40" top="414" width="20" height="25" bitmap="ui\\shell\\bitmaps\\y_butn"/>')
        extra = f' string_list="{ylist}" string_index="{yindex}"' if ylist else ''
        lines.append(text(f'{prefix}/y_label', 61, 414, 132, 24, 'large', BLUE, extra))
        names += ['y_butn', 'y_label']
    if x:
        left = 196 if y else 40
        lines.append(f'\t<widget name="{prefix}/x_butn" left="{left}" top="414" width="20" height="25" bitmap="ui\\shell\\bitmaps\\x_butn"/>')
        lines.append(text(f'{prefix}/x_label', left + 21, 414, 150 if y else 152, 24, 'large'))
        names += ['x_butn', 'x_label']
    if a:
        lines.append(f'\t<widget name="{prefix}/a_butn" left="481" top="414" width="21" height="26" bitmap="ui\\shell\\bitmaps\\a_butn"/>')
        lines.append(text(f'{prefix}/a_label', 502, 414, 134, 24, 'large'))
        names += ['a_butn', 'a_label']
    return lines, [f'{prefix}/{n}' for n in names]

def text_row(name, bitmap, extra, on):
    """a row whose words are its own text box in the small font (row_name: 13 in, ending 14 short of the
    tab's end, clear of the field the tab runs under)"""
    w(f'\t<widget name="{name}/row_name" type="text" left="13" top="5" width="199" height="18" font="ui\\small_ui" color="{BLUE}"{extra}/>')
    w(f'\t<widget name="{name}" width="226" height="{ROW_H}" bitmap="{bitmap}">')
    w(f'\t\t{on}')
    w(f'\t\t<child widget="{name}/row_name"/>')
    w('\t</widget>')

def rows(prefix, run, count, open_=None, cols=('row_name', 'row_tag')):
    """a list's rows: list_item_bkd (the first list_item_bkd_top, as the Xbox's lists), the name in the large font, the tag at the right"""
    for i in range(1, count + 1):
        handler = f'<on event="a start" run="{run}"' + (f' open="{open_}"' if open_ else '') + '/>'
        w(f'\t<widget name="{prefix}/row_{i}" width="226" height="{ROW_H}" bitmap="{ITEM_TOP if i == 1 else ITEM}">')
        w(f'\t\t{handler}')
        for c in cols:
            w(f'\t\t<child widget="{prefix}/{c}"/>')
        w('\t</widget>')

def row_columns(prefix):
    """a row's name and its tag at the right, both in ui.map's small font (the original menus' font for
    a dense list: one size for every row of the screen), the tag ending well short of the field"""
    w(f'\t<widget name="{prefix}/row_name" type="text" left="13" top="5" width="199" height="18" font="ui\\small_ui" color="{BLUE}"/>')
    w(f'\t<widget name="{prefix}/row_tag" type="text" left="92" top="5" width="120" height="18" align="right" font="ui\\small_ui" color="{BLUE}"/>')

w('<?xml version="1.0" encoding="UTF-8"?>')
w('''<!-- Menu style Halo CE: FRIENDS, ADD FRIEND, PARTY and INVITE FRIENDS in the look of the original
     menus (the Xbox's Multiplayer screens: the gradient, the heading at 35,11, the list of
     list_item_bkd rows at the left, the description's field at the right, the button key along the
     foot, ui.map's large_ui and small_ui). A classic/<name> screen stands for the Halo 3 set's
     h3/<name> while the Menu style is Halo CE (menu_tags.c, the classic set, as menus/online's do
     for Menu style Online): the same functions (menu_functions.c "port h3 friends ...", "port h3
     party ...", "port h3 invite ...") fill the same names, so only the look is ours (the screens are classic/, their parts ce/). Every picture
     and font is the player's ui.map's (tag paths); the headings are the port's own words drawn in
     the Multiplayer screens' look (tools/classic_menu_art.py, classic/<lang>/header_*.png). FRIENDS'
     ADD FRIEND row opens ADD FRIEND here (menu_functions.c open_classic_add), whose own row types
     the code. Written by tools/classic_friends_xml.py. -->''')
w('<menus>')
w('\t<strings name="classic/friends_text">')
for s in ('YOUR FRIEND CODE', '=OPTIONS', '=TYPE', '=SELECT'):
    w(f'\t\t<string text="{s}"/>')
w('\t</strings>')
w('\t<strings name="classic/back_text">')
w('\t\t<string text="BACK"/>')
w('\t\t<string text="=BACK"/>')
w('\t</strings>')
for name, png in (('friends', 'header_friends'), ('party', 'header_party'), ('add', 'header_add_friend'), ('invite', 'header_invite_friends')):
    w(f'\t<bitmap name="classic/header/{name}_art">')
    w(f'\t\t<frame png="classic/{{lang}}/{png}.png" width="512" height="64"/>')
    w('\t</bitmap>')
    w(f'\t<widget name="classic/header/{name}" left="35" top="11" width="512" height="64" bitmap="classic/header/{name}_art"/>')
w(text('classic/back/label', 392, 414, 86, 24, 'large', BLUE, ' string_list="classic/back_text" string_index="1"'))
w(f'\t<widget name="classic/back/b_butn" left="371" top="414" width="21" height="26" bitmap="ui\\shell\\bitmaps\\b_butn"/>')

# ---- FRIENDS
w('\n\t<!-- FRIENDS: the own rows (ADD FRIEND, APPEAR OFFLINE, FRIENDS CAN JOIN, PARTY, FRIEND CODE BACKUP),\n\t     then the requests, invites, friends, requests sent and the blocked; the chosen one\'s facts\n\t     in the field at the right with this Vita\'s friend code -->')
row_columns('classic/friends')
rows('classic/friends', 'port h3 friends act', 10)
w('\t<widget name="classic/friends/list" type="column_list" flags="pass_unhandled_to_focused_child up_down_tabs_items">')
w('\t\t<data input="port h3 friends"/>')
w('\t\t<on event="x" run="port h3 friends second"/>')
w('\t\t<on event="y" run="port h3 friends options ask" open="h3/friends/options"/>')
w('\t\t<on event="left_trigger" run="port h3 friends page up"/>')
w('\t\t<on event="right_trigger" run="port h3 friends page down"/>')
for i in range(10):
    w(f'\t\t<child widget="classic/friends/row_{i + 1}" x="{ROW_X}" y="{78 + ROW_H * i}"/>')
w('\t</widget>')
w(f'\t<widget name="classic/friends/field" left="{FIELD_X}" top="{FIELD_Y}" width="{FIELD_W}" height="{FIELD_H}" bitmap="{FIELD}"/>')
w(text('classic/friends/code_label', IN_X, 88, IN_W, 18, extra=' string_list="classic/friends_text" string_index="0"'))
w(text('classic/friends/code_value', IN_X, 104, IN_W, 26, 'large', WHITE))
w(text('classic/friends/friends_status', IN_X, 132, IN_W, 54))
w(text('classic/friends/detail_name', IN_X, 172, IN_W, 26, 'large', WHITE))
w(text('classic/friends/detail_state', IN_X, 198, IN_W, 36))
w(text('classic/friends/detail_help', IN_X, 238, IN_W, 80))
w(text('classic/friends/friends_message', IN_X, 326, IN_W, 66, color=YELLOW))
k, knames = keys('classic/friends', x=True, y=True, ylist='classic/friends_text', yindex=1)
for l in k:
    w(l)
w(f'\t<widget name="classic/friends/screen" flags="pass_unhandled_to_focused_child" bitmap="{GRADIENT}">')
w('\t\t<on event="created" run="port h3 friends init"/>')
w('\t\t<on event="b back" back="true"/>')
for c in ['classic/header/friends', 'classic/friends/field', 'classic/friends/code_label', 'classic/friends/code_value',
          'classic/friends/friends_status', 'classic/friends/list', 'classic/friends/detail_name', 'classic/friends/detail_state',
          'classic/friends/detail_help', 'classic/friends/friends_message'] + knames:
    w(f'\t\t<child widget="{c}"/>')
w('\t\t<child widget="classic/back/b_butn"/>')
w('\t\t<child widget="classic/back/label"/>')
w('\t</widget>')

# ---- options (both)
def options(prefix, run, count, header):
    """a chosen one's options as the Xbox's list screens: the screen's heading, the rows at the left
    (BACK first), the one they are about and what they do in the field at the right"""
    w(f'\t<widget name="{prefix}/options_field" left="{FIELD_X}" top="{FIELD_Y}" width="{FIELD_W}" height="{FIELD_H}" bitmap="{FIELD}"/>')
    w(text(f'{prefix}/options_title', IN_X, 88, IN_W, 26, 'large', WHITE))
    w(text(f'{prefix}/options_text', IN_X, 118, IN_W, 270))
    # (each option's words in its row_name, a long one's in its smaller twin: menu_functions.c's
    # text_show gives a row's words to its row_name)
    w(f'\t<widget name="{prefix}/option_name" type="text" left="13" top="5" width="199" height="18" font="ui\\small_ui" color="{BLUE}"/>')
    for i in range(1, count + 1):
        w(f'\t<widget name="{prefix}/option_{i}" width="226" height="{ROW_H}" bitmap="{ITEM}">')
        w(f'\t\t<on event="a" run="{run}" back="true"/>')
        w(f'\t\t<child widget="{prefix}/option_name"/>')
        w('\t</widget>')
    text_row(f'{prefix}/option_back', ITEM_TOP, ' string_list="classic/back_text" string_index="0"', '<on event="a" back="true"/>')
    w(f'\t<widget name="{prefix}/options_list" type="column_list" flags="pass_unhandled_to_focused_child up_down_tabs_items">')
    w(f'\t\t<data input="{run}s"/>')
    w(f'\t\t<child widget="{prefix}/option_back" y="0"/>')
    for i in range(1, count + 1):
        w(f'\t\t<child widget="{prefix}/option_{i}" y="{ROW_H * i}"/>')
    w('\t</widget>')
    w(f'\t<widget name="{prefix}/options_a_butn" left="481" top="414" width="21" height="26" bitmap="ui\\shell\\bitmaps\\a_butn"/>')
    w(text(f'{prefix}/options_select', 502, 414, 134, 24, 'large', BLUE, ' string_list="classic/friends_text" string_index="3"'))
    w(f'\t<widget name="{prefix}/options" flags="pass_unhandled_to_focused_child" bitmap="{GRADIENT}">')
    w('\t\t<on event="b back" back="true"/>')
    w(f'\t\t<child widget="{header}"/>')
    for c in ['options_field', 'options_title', 'options_text', 'options_a_butn', 'options_select']:
        w(f'\t\t<child widget="{prefix}/{c}"/>')
    w('\t\t<child widget="classic/back/b_butn"/>')
    w('\t\t<child widget="classic/back/label"/>')
    w(f'\t\t<child widget="{prefix}/options_list" x="{ROW_X}" y="78"/>')
    w('\t</widget>')


w('\n\t<!-- FRIENDS\' Triangle: the chosen one\'s options (and FRIEND CODE BACKUP\'s question), in the field -->')
options('classic/friends', 'port h3 friends option', 3, 'classic/header/friends')

# ---- ADD FRIEND
w('\n\t<!-- ADD FRIEND: this Vita\'s code to give, and the row that types a friend\'s (the FRIENDS list\'s\n\t     data: its first row is ADD FRIEND); the outcome under it -->')
row_columns('classic/add')
rows('classic/add', 'port h3 friends act', 1)
w('\t<widget name="classic/add/list" type="column_list" flags="pass_unhandled_to_focused_child up_down_tabs_items">')
w('\t\t<data input="port h3 friends"/>')
w('\t\t<child widget="classic/add/row_1" x="51" y="78"/>')
w('\t</widget>')
w(f'\t<widget name="classic/add/field" left="{FIELD_X}" top="{FIELD_Y}" width="{FIELD_W}" height="{FIELD_H}" bitmap="{FIELD}"/>')
w(text('classic/add/code_label', IN_X, 88, IN_W, 18, extra=' string_list="classic/friends_text" string_index="0"'))
w(text('classic/add/code_value', IN_X, 106, IN_W, 26, 'large', WHITE))
w(text('classic/add/friends_status', IN_X, 136, IN_W, 54))
w(text('classic/add/detail_help', IN_X, 180, IN_W, 100))
w(text('classic/add/friends_message', IN_X, 300, IN_W, 80, color=YELLOW))
k, knames = keys('classic/add')
for l in k:
    w(l)
w(f'\t<widget name="classic/friends/add" flags="pass_unhandled_to_focused_child" bitmap="{GRADIENT}">')
w('\t\t<on event="created" run="port h3 friends init"/>')
w('\t\t<on event="b back" back="true"/>')
for c in ['classic/header/add', 'classic/add/field', 'classic/add/code_label', 'classic/add/code_value', 'classic/add/friends_status',
          'classic/add/list', 'classic/add/detail_help', 'classic/add/friends_message'] + knames:
    w(f'\t\t<child widget="{c}"/>')
w('\t\t<child widget="classic/back/b_butn"/>')
w('\t\t<child widget="classic/back/label"/>')
w('\t</widget>')

# ---- PARTY
w('\n\t<!-- PARTY: the members (the leader marked), the party invites, LEAVE PARTY; the chosen one, the\n\t     outcome and the party\'s chat in the field -->')
row_columns('classic/party')
rows('classic/party', 'port h3 party act', 10, open_='h3/party/options')
w('\t<widget name="classic/party/list" type="column_list" flags="pass_unhandled_to_focused_child up_down_tabs_items">')
w('\t\t<data input="port h3 party"/>')
w('\t\t<on event="x" run="port h3 party second" open="h3/party/quick"/>')
w('\t\t<on event="y" run="port h3 party type"/>')
w('\t\t<on event="left_trigger" run="port h3 party page up"/>')
w('\t\t<on event="right_trigger" run="port h3 party page down"/>')
for i in range(10):
    w(f'\t\t<child widget="classic/party/row_{i + 1}" x="{ROW_X}" y="{78 + ROW_H * i}"/>')
w('\t</widget>')
w(f'\t<widget name="classic/party/field" left="{FIELD_X}" top="{FIELD_Y}" width="{FIELD_W}" height="{FIELD_H}" bitmap="{FIELD}"/>')
w(text('classic/party/party_status', IN_X, 88, IN_W, 54, 'large', WHITE))
w(text('classic/party/detail', IN_X, 128, IN_W, 52))
w(text('classic/party/party_message', IN_X, 182, IN_W, 34, color=YELLOW))
w('\t<strings name="classic/party_text">')
w('\t\t<string text="PARTY CHAT"/>')
w('\t\t<string text="QUICK CHAT"/>')
w('\t</strings>')
w(text('classic/party/chat_title', IN_X, 220, IN_W, 18, extra=' string_list="classic/party_text" string_index="0"'))
for i in range(5):
    w(text(f'classic/party/chat_{i + 1}', IN_X, 240 + 30 * i, IN_W, 30, color=WHITE))
w('\t<widget name="classic/party/chat">')
w('\t\t<data input="port h3 party chat"/>')
w('\t\t<child widget="classic/party/chat_title"/>')
for i in range(5):
    w(f'\t\t<child widget="classic/party/chat_{i + 1}"/>')
w('\t</widget>')
k, knames = keys('classic/party', x=True, y=True, ylist='classic/friends_text', yindex=2)
for l in k:
    w(l)
w(f'\t<widget name="classic/party/screen" flags="pass_unhandled_to_focused_child" bitmap="{GRADIENT}">')
w('\t\t<on event="created" run="port h3 party init"/>')
w('\t\t<on event="b back" back="true"/>')
for c in ['classic/header/party', 'classic/party/field', 'classic/party/party_status', 'classic/party/list', 'classic/party/detail',
          'classic/party/party_message', 'classic/party/chat'] + knames:
    w(f'\t\t<child widget="{c}"/>')
w('\t\t<child widget="classic/back/b_butn"/>')
w('\t\t<child widget="classic/back/label"/>')
w('\t</widget>')
w('\n\t<!-- a member\'s options (MUTE or UNMUTE, REMOVE FROM PARTY, LEAVE PARTY: those that apply) -->')
options('classic/party', 'port h3 party option', 2, 'classic/header/party')

# ---- QUICK CHAT
w('\n\t<!-- QUICK CHAT (Square): the presets, Cross sends one to the party -->')
for i in range(1, 9):
    text_row(f'classic/party/preset_{i}', ITEM, '', '<on event="a" run="port h3 party preset" back="true"/>')
text_row('classic/party/quick_back', ITEM_TOP, ' string_list="classic/back_text" string_index="0"', '<on event="a" back="true"/>')
w('\t<widget name="classic/party/quick_list" type="column_list" flags="pass_unhandled_to_focused_child up_down_tabs_items">')
w('\t\t<data input="port h3 party quick"/>')
w('\t\t<child widget="classic/party/quick_back" y="0"/>')
for i in range(1, 9):
    w(f'\t\t<child widget="classic/party/preset_{i}" y="{ROW_H * i}"/>')
w('\t</widget>')
w('\t<bitmap name="classic/header/quick_art">')
w('\t\t<frame png="classic/{lang}/header_quick_chat.png" width="512" height="64"/>')
w('\t</bitmap>')
w('\t<widget name="classic/party/quick_title" left="35" top="11" width="512" height="64" bitmap="classic/header/quick_art"/>')
w(f'\t<widget name="classic/party/quick_field" left="{FIELD_X}" top="{FIELD_Y}" width="{FIELD_W}" height="{FIELD_H}" bitmap="{FIELD}"/>')
w(f'\t<widget name="classic/party/quick_a_butn" left="481" top="414" width="21" height="26" bitmap="ui\\shell\\bitmaps\\a_butn"/>')
w(text('classic/party/quick_select', 502, 414, 134, 24, 'large', BLUE, ' string_list="classic/friends_text" string_index="3"'))
w(f'\t<widget name="classic/party/quick" flags="pass_unhandled_to_focused_child" bitmap="{GRADIENT}">')
w('\t\t<on event="b back" back="true"/>')
w('\t\t<child widget="classic/party/quick_title"/>')
w('\t\t<child widget="classic/party/quick_field"/>')
w('\t\t<child widget="classic/party/quick_a_butn"/>')
w('\t\t<child widget="classic/party/quick_select"/>')
w('\t\t<child widget="classic/back/b_butn"/>')
w('\t\t<child widget="classic/back/label"/>')
w(f'\t\t<child widget="classic/party/quick_list" x="{ROW_X}" y="78"/>')
w('\t</widget>')

# ---- INVITE FRIENDS
w('\n\t<!-- INVITE FRIENDS (LOBBY OPTIONS, the host online): the friends online, Cross invites the chosen one -->')
row_columns('classic/invite')
rows('classic/invite', 'port h3 invite act', 10)
w('\t<widget name="classic/invite/list" type="column_list" flags="pass_unhandled_to_focused_child up_down_tabs_items">')
w('\t\t<data input="port h3 invite"/>')
for i in range(10):
    w(f'\t\t<child widget="classic/invite/row_{i + 1}" x="{ROW_X}" y="{78 + ROW_H * i}"/>')
w('\t</widget>')
w(f'\t<widget name="classic/invite/field" left="{FIELD_X}" top="{FIELD_Y}" width="{FIELD_W}" height="{FIELD_H}" bitmap="{FIELD}"/>')
w(text('classic/invite/invite_status', IN_X, 88, IN_W, 54, 'large', WHITE))
w(text('classic/invite/invite_help', IN_X, 130, IN_W, 120))
w(text('classic/invite/invite_message', IN_X, 300, IN_W, 80, color=YELLOW))
k, knames = keys('classic/invite')
for l in k:
    w(l)
w(f'\t<widget name="classic/invite/screen" flags="pass_unhandled_to_focused_child" bitmap="{GRADIENT}">')
w('\t\t<on event="created" run="port h3 invite init"/>')
w('\t\t<on event="b back" back="true"/>')
for c in ['classic/header/invite', 'classic/invite/field', 'classic/invite/invite_status', 'classic/invite/invite_help',
          'classic/invite/list', 'classic/invite/invite_message'] + knames:
    w(f'\t\t<child widget="{c}"/>')
w('\t\t<child widget="classic/back/b_butn"/>')
w('\t\t<child widget="classic/back/label"/>')
w('\t</widget>')
w('</menus>')
import re
SCREENS = {'classic/friends/screen', 'classic/friends/options', 'classic/friends/add', 'classic/party/screen',
           'classic/party/options', 'classic/party/quick', 'classic/invite/screen'}
def rename(match):
    name = match.group(2)
    return match.group(1) + (name if name in SCREENS else 'ce/' + name[len('classic/'):]) + '"'
text_out = re.sub(r'((?:name|widget|bitmap|string_list)=")(classic/[^"]*)"', rename, '\n'.join(out) + '\n')
open(sys.argv[1], 'w').write(text_out)
