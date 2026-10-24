#!/usr/bin/env python3
"""Menu style Online: writes port/vita/app0/menus/online/customize.xml (its rows are many and alike).

    python3 tools/online_menu_customize.py port/vita/app0/menus/online/customize.xml
"""
import sys
out = sys.argv[1]
LABELS = ["PLAYER NAME", "SERVICE TAG", "MODEL", "HELMET", "RIGHT SHOULDER", "LEFT SHOULDER", "CHEST",
          "FLAMING HELMET", "ARMOR PRIMARY", "ARMOR SECONDARY", "ARMOR VISOR", "ARMOR LIGHTS", "STICK LAYOUT",
          "BUTTON LAYOUT", "LOOK SENSITIVITY", "INVERT LOOK", "INVERT FLIGHT", "AUTO-CENTER", "VIBRATION"]
ROWS = ["op_name", "na_tag", "op_model", "na_helmet", "na_rshoulder", "na_lshoulder", "na_chest", "op_flame",
        "op_color", "na_secondary", "na_visor", "na_lights", "op_stick", "op_buttons", "op_look", "op_invert",
        "op_flight", "op_autocenter", "op_vibration"]
L = []
w = L.append
w('''<?xml version="1.0" encoding="UTF-8"?>
<!-- Menu style Online (a preview): CUSTOMIZATION's editor. It opens in place of the Halo 3 editor
(h3/custom/screen: menu_tags.c menu_tags_online) and edits the same profile with the same rows and
functions (menu_functions.c "port h3 custom ..."), laid out the online way: the header band with
CUSTOMIZATION and its tabs - ARMORY, COLORS, CONTROLS, L and R change them -, the tab's rows at the
left with the orange bar, their values beside them, the help under them, the profile's Spartan
large at the right, the buttons at the foot. ARMORY: PLAYER NAME, MODEL (the Spartan or a Custom
Edition player model), the tester's FLAMING HELMET, and the armor pieces Halo CE has no models
for greyed ("not yet"); COLORS: ARMOR PRIMARY (the game's eighteen colours, in a grid with the
chosen one framed and its hex), the other colours greyed; CONTROLS: the profile's controller
settings. The tabs' rows: menu_functions.c online_custom_update. Circle saves, as the Halo 3
editor's. The art is this folder's own (tools/online_menu_art.py) and the Halo 3 set's. -->
<menus>
	<bitmap name="online/header">
		<frame png="online/header.png" width="64" height="64"/>
	</bitmap>
	<bitmap name="online/tab">
		<frame png="online/tab_off.png" width="128" height="16"/>
		<frame png="online/tab_on.png" width="128" height="16"/>
	</bitmap>
	<bitmap name="online/row_dim">
		<frame png="online/item_off.png" width="128" height="16"/>
		<frame png="online/row_dim_on.png" width="128" height="16"/>
	</bitmap>
	<bitmap name="online/box">
		<frame png="online/box.png" width="64" height="16"/>
	</bitmap>
	<bitmap name="online/select_frame">
		<frame png="online/select_frame.png" width="32" height="16"/>
	</bitmap>''')
w('''	<bitmap name="online/colour_random">
		<frame png="online/colour_random.png" width="32" height="16"/>
	</bitmap>''')
for i in range(18):
    w('''	<bitmap name="online/colour_%02d">
		<frame png="online/colour_%02d.png" width="32" height="16"/>
	</bitmap>''' % (i, i))
w('''	<strings name="online/custom_labels">''')
for label in LABELS:
    w('		<string text="%s"/>' % label)
w('''	</strings>
	<strings name="online/custom_text">
		<string text="CUSTOMIZATION"/>
		<string text="ARMORY"/>
		<string text="COLORS"/>
		<string text="CONTROLS"/>
		<string text="Close"/>
		<string text="Change"/>
		<string text="Tabs"/>
	</strings>

	<!-- the rows: a label, the value beside it (the focused row's arrows), as the Halo 3 editor's
	children named alike (value, arrow_left, arrow_right) -->
	<widget name="online/custom/value" type="text" left="150" top="0" width="140" height="18" align="center" font="h3/font_item" color="#FFAFC4DE" text_y="2" text_flags="no_focus_test"/>
	<widget name="online/custom/na/value" type="text" left="150" top="0" width="140" height="18" align="center" font="h3/font_small" color="#FF6A7C94" text_y="3" text_flags="no_focus_test"/>
	<widget name="online/custom/arrow_left" left="146" top="1" width="8" height="16" bitmap="h3/arrow_left"/>
	<widget name="online/custom/arrow_right" left="286" top="1" width="8" height="16" bitmap="h3/arrow_right"/>''')
for i, (row, label) in enumerate(zip(ROWS, LABELS)):
    dim = row.startswith('na_')
    colour = '#FF7E8EA4' if dim else '#FFF0F4F8'
    w('	<widget name="online/custom/%s_label" type="text" left="0" top="0" width="150" height="18" font="h3/font_item" color="%s" text_x="7" text_y="2" string_list="online/custom_labels" string_index="%d"/>' % (row, colour, i))
    if dim:
        w('	<widget name="online/custom/%s" width="300" height="18" flags="pass_unhandled_to_focused_child" bitmap="online/row_dim">' % row)
        w('		<child widget="online/custom/%s_label"/>' % row)
        w('		<child widget="online/custom/na/value"/>')
        w('	</widget>')
        continue
    w('	<widget name="online/custom/%s" width="300" height="18" flags="pass_unhandled_to_focused_child" bitmap="online/item">' % row)
    if row == 'op_name':
        w('		<on event="a start" run="port h3 custom next"/>')
    else:
        w('		<on event="a right stick_right" run="port h3 custom next"/>')
        w('		<on event="left stick_left" run="port h3 custom previous"/>')
    w('		<child widget="online/custom/%s_label"/>' % row)
    w('		<child widget="online/custom/value"/>')
    if row != 'op_name':
        w('		<child widget="online/custom/arrow_left"/>')
        w('		<child widget="online/custom/arrow_right"/>')
    w('	</widget>')
X = -70   # the list's left (in the wide screen's margin)
w('''	<widget name="online/custom/list" type="column_list" flags="pass_unhandled_to_focused_child up_down_tabs_items">
		<data input="port h3 custom"/>
		<data input="port online custom"/>
		<on event="left_trigger" run="port online custom tab previous"/>
		<on event="right_trigger" run="port online custom tab next"/>''')
for row in ROWS:
    w('		<child widget="online/custom/%s" x="0" y="0"/>' % row)
w('	</widget>')
w('''
	<!-- the header band (full width), CUSTOMIZATION and the tabs -->
	<widget name="online/custom/header" left="0" top="0" width="640" height="80" bitmap="online/header"/>
	<widget name="online/custom/title" type="text" left="%d" top="12" width="400" height="34" font="online/font_header" color="#FFF2F6FA" string_list="online/custom_text" string_index="0"/>''' % X)
for i in range(3):
    w('	<widget name="online/custom/tab_%d" left="%d" top="52" width="118" height="24" bitmap="online/tab"/>' % (i, X - 8 + i * 118))
    w('	<widget name="online/custom/tab_label_%d" type="text" left="%d" top="52" width="100" height="24" font="online/font_tab" color="#FFE8EEF6" text_y="4" string_list="online/custom_text" string_index="%d"/>' % (i, X + i * 118, i + 1))
w('''	<widget name="online/custom/profile_name" type="text" left="%d" top="57" width="190" height="16" align="right" font="h3/font_small" color="#FF9FB6CC"/>''' % (X + 360))
w('''
	<!-- under the rows: a rule, the colour's name, the row's help, the editor's word (a name
	refused, a model that cannot be built) -->
	<widget name="online/custom/rule" left="%d" top="268" width="300" height="2" bitmap="online/rule"/>
	<widget name="online/custom/colour_name" type="text" left="%d" top="274" width="290" height="20" font="h3/font_item" color="#FFF0F4F8" text_x="7"/>
	<widget name="online/custom/custom_help" type="text" left="%d" top="296" width="300" height="60" font="h3/font_small" color="#FFC4D2E2" text_x="7" string_list="h3/custom_help"/>
	<widget name="online/custom/na_help" type="text" left="%d" top="296" width="300" height="60" font="h3/font_small" color="#FFC4D2E2" text_x="7"/>
	<widget name="online/custom/status" type="text" left="%d" top="362" width="300" height="34" font="h3/font_small" color="#FFF0B070" text_x="7"/>
	<widget name="online/custom/preview_name" type="text" left="460" top="453" width="270" height="16" align="right" font="h3/font_small" color="#FF9FB6CC"/>''' % (X, X, X, X, X))
GX, GY = 248, 112
w('''
	<!-- COLORS: the hex of the chosen colour in its box, then the grid (RANDOM, then the game's
	eighteen in its order), the chosen one framed (online_custom_update moves the frame) -->
	<widget name="online/custom/hex_box" left="%d" top="92" width="100" height="16" bitmap="online/box"/>
	<widget name="online/custom/hex" type="text" left="%d" top="92" width="100" height="16" align="center" font="h3/font_small" color="#FFF0F4F8" text_y="1"/>''' % (GX, GX))
tiles = ['online/colour_random'] + ['online/colour_%02d' % i for i in range(18)]
for i, bitmap in enumerate(tiles):
    w('	<widget name="online/custom/cell_%d" left="%d" top="%d" width="32" height="16" bitmap="%s"/>' % (i, GX + (i % 3) * 34, GY + (i // 3) * 18, bitmap))
w('	<widget name="online/custom/grid_select" left="%d" top="%d" width="32" height="16" bitmap="online/select_frame"/>' % (GX, GY))
w('	<widget name="online/custom/grid">')
w('		<child widget="online/custom/hex_box"/>')
w('		<child widget="online/custom/hex"/>')
for i in range(len(tiles)):
    w('		<child widget="online/custom/cell_%d"/>' % i)
w('		<child widget="online/custom/grid_select"/>')
w('	</widget>')
w('''
	<!-- the foot: Circle closes (and saves), Cross changes, L and R the tabs -->
	<widget name="online/custom/foot" left="0" top="446" width="640" height="34" bitmap="online/footer"/>
	<widget name="online/custom/b_butn" left="%d" top="452" width="16" height="16" bitmap="h3/glyph_circle"/>
	<widget name="online/custom/close" type="text" left="%d" top="451" width="60" height="18" font="h3/font_item" color="#FFF0F4F8" text_y="1" string_list="online/custom_text" string_index="4"/>
	<widget name="online/custom/a_butn" left="%d" top="452" width="16" height="16" bitmap="h3/glyph_cross"/>
	<widget name="online/custom/change" type="text" left="%d" top="451" width="60" height="18" font="h3/font_item" color="#FFF0F4F8" text_y="1" string_list="online/custom_text" string_index="5"/>
	<widget name="online/custom/l_butn" left="%d" top="452" width="16" height="16" bitmap="h3/glyph_l"/>
	<widget name="online/custom/r_butn" left="%d" top="452" width="16" height="16" bitmap="h3/glyph_r"/>
	<widget name="online/custom/tabs" type="text" left="%d" top="451" width="60" height="18" font="h3/font_item" color="#FFF0F4F8" text_y="1" string_list="online/custom_text" string_index="6"/>''' % (X, X + 20, X + 84, X + 104, X + 172, X + 190, X + 210))
w('''
	<!-- the profiles first (the Halo 3 SELECT PROFILE's widgets, profiles.xml), under the header
	band: CUSTOMIZATION, the line saying to choose one -->
	<widget name="online/custprof/title" type="text" left="%d" top="12" width="400" height="34" font="online/font_header" color="#FFF2F6FA" string_list="online/custom_text" string_index="0"/>
	<widget name="online/custprof/subtitle" type="text" left="%d" top="56" width="500" height="16" font="h3/font_small" color="#FFC4D2E2" string_list="h3/profile_text" string_index="3"/>
	<widget name="online/custprof/panel" left="40" top="86" width="560" height="330" bitmap="online/panel"/>
	<widget name="online/custprof/screen" flags="pass_unhandled_to_focused_child" scene="multiplayer -40 -1.8 15.5 25 0">
		<on event="created" run="port h3 profiles customize"/>
		<on event="created" run="port h3 scene"/>
		<on event="deleted" run="port h3 profiles close"/>
		<on event="b back" back="true"/>
		<on event="x" run="port h3 profile delete ask" open="h3/profdel/screen"/>
		<on event="y" run="port h3 profile create"/>
		<child widget="online/main/haze"/>
		<child widget="online/custom/header"/>
		<child widget="online/custprof/panel"/>
		<child widget="online/custprof/title"/>
		<child widget="online/custprof/subtitle"/>
		<child widget="h3/profiles/info_swatch"/>
		<child widget="h3/profiles/info_name"/>
		<child widget="h3/profiles/info_level_label"/>
		<child widget="h3/profiles/info_level"/>
		<child widget="h3/profiles/info_skill_label"/>
		<child widget="h3/profiles/info_skill"/>
		<child widget="h3/profiles/info_controls_label"/>
		<child widget="h3/profiles/info_controls"/>
		<child widget="h3/profiles/info_text"/>
		<child widget="h3/profiles/status"/>
		<child widget="online/custom/foot"/>
		<child widget="h3/pick/a_butn" x="%d" y="31"/>
		<child widget="h3/pick/select" x="%d" y="31"/>
		<child widget="h3/pick/b_butn" x="%d" y="31"/>
		<child widget="h3/pick/back" x="%d" y="31"/>
		<child widget="h3/profiles/x_butn" x="%d" y="31"/>
		<child widget="h3/profiles/delete" x="%d" y="31"/>
		<child widget="h3/profiles/y_butn" x="%d" y="31"/>
		<child widget="h3/profiles/new" x="%d" y="31"/>
		<child widget="h3/profiles/list"/>
	</widget>''' % ((X, X) + (X - 52,) * 8))
w('''
	<!-- (scene: the Halo 3 editor's camera; the Spartan placed large at the right,
	menu_functions.c ONLINE_CUSTOM_SPARTAN_PLACEMENT) -->
	<widget name="online/custom/screen" flags="pass_unhandled_to_focused_child" scene="multiplayer -40 -1.8 15.5 25 0">
		<on event="created" run="port h3 scene"/>
		<on event="b back" run="port h3 custom save" back="true"/>
		<on event="deleted" run="port h3 custom close"/>
		<child widget="online/main/haze"/>
		<child widget="online/custom/header"/>
		<child widget="online/custom/title"/>''')
for i in range(3):
    w('		<child widget="online/custom/tab_%d"/>' % i)
    w('		<child widget="online/custom/tab_label_%d"/>' % i)
w('''		<child widget="online/custom/profile_name"/>
		<child widget="online/custom/rule"/>
		<child widget="online/custom/colour_name"/>
		<child widget="online/custom/custom_help"/>
		<child widget="online/custom/na_help"/>
		<child widget="online/custom/status"/>
		<child widget="online/custom/preview_name"/>
		<child widget="online/custom/grid"/>
		<child widget="online/custom/foot"/>
		<child widget="online/custom/b_butn"/>
		<child widget="online/custom/close"/>
		<child widget="online/custom/a_butn"/>
		<child widget="online/custom/change"/>
		<child widget="online/custom/l_butn"/>
		<child widget="online/custom/r_butn"/>
		<child widget="online/custom/tabs"/>
		<child widget="online/custom/list" x="%d" y="92"/>
	</widget>
</menus>''' % X)
open(out, 'w').write('\n'.join(L) + '\n')
