#!/bin/sh
# Builds and runs the desktop test of the desktop build's button icons
# (port/linux/src/button_glyphs.c: the key each of the game's buttons shows,
# display.button_icons and auto's device; button_icons_test.c), then checks
# that each key shown is one xinput_sdl.c's keyboard_gamepad really puts on
# that button (in the menus, no mouse button: the mouse is the pointer
# there). No SDK needed.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/button_icons_test.$$
. "$here/test_out.sh"
test_out_begin "$out"
mkdir -p "$out"
trap 'test_out_done $?' EXIT
cc=${CC:-gcc}
$cc -m32 -g -O1 -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE \
	"$here/button_icons_test.c" -o "$out/button_icons_test"
status=0
"$out/button_icons_test" || status=$?
python3 - "$root" <<'PY' || status=1
import re, sys
root = sys.argv[1]
source = open(root + '/port/linux/src/xinput_sdl.c').read()
body = source[source.index('static void keyboard_gamepad('):]
body = body[:body.index('\n}\n')]
buttons = ['A', 'B', 'X', 'Y', 'BLACK', 'WHITE', 'LEFT_TRIGGER', 'RIGHT_TRIGGER', 'DPAD_UP', 'DPAD_DOWN', 'DPAD_LEFT',
           'DPAD_RIGHT', 'START', 'BACK', 'LEFT_THUMB', 'RIGHT_THUMB']
bound = {name: set() for name in buttons}
for statement in body.split(';'):
    names = [name for name in re.findall(r'XINPUT_GAMEPAD_(\w+)', statement) if name in bound]
    for name in names:
        bound[name] |= set(re.findall(r'SDL_(?:SCANCODE|BUTTON)_\w+', statement))
keys = {'SPACE': 'SDL_SCANCODE_SPACE', 'ENTER': 'SDL_SCANCODE_RETURN', 'ESC': 'SDL_SCANCODE_ESCAPE',
        'TAB': 'SDL_SCANCODE_TAB', 'BACKSPACE': 'SDL_SCANCODE_BACKSPACE', 'DELETE': 'SDL_SCANCODE_DELETE',
        'CTRL': 'SDL_SCANCODE_LCTRL', 'SHIFT': 'SDL_SCANCODE_LSHIFT', 'F1': 'SDL_SCANCODE_F1',
        'UP': 'SDL_SCANCODE_UP', 'DOWN': 'SDL_SCANCODE_DOWN', 'LEFT': 'SDL_SCANCODE_LEFT', 'RIGHT': 'SDL_SCANCODE_RIGHT',
        'LMB': 'SDL_BUTTON_LEFT', 'RMB': 'SDL_BUTTON_RIGHT', 'MMB': 'SDL_BUTTON_MIDDLE', 'MOUSE4': 'SDL_BUTTON_X1'}
for letter in 'EFRQXGCZWASD':
    keys[letter] = 'SDL_SCANCODE_' + letter
glyphs = open(root + '/port/linux/src/button_glyphs.c').read()
failures = 0
for table, menus in (('play_keys', False), ('menu_keys', True)):
    text = glyphs[glyphs.index('static const signed char %s[18] = {' % table):]
    text = text[text.index('{') + 1:text.index('};')]
    entries = [entry.strip() for entry in text.split(',') if entry.strip()]
    if len(entries) != 18:
        print('FAIL %s: %d entries, not 18' % (table, len(entries)))
        failures += 1
        continue
    for button, entry in zip(buttons, entries):
        if entry == '-1':
            continue
        key = entry.replace('HALO_KEY_', '')
        token = keys.get(key)
        ok = token in bound[button] and not (menus and token.startswith('SDL_BUTTON_'))
        print('%s %s %s: %s is %s' % ('PASS' if ok else 'FAIL', 'menus' if menus else 'play', button, key,
                                      'bound to it' if ok else 'not bound to it in keyboard_gamepad'))
        failures += not ok
    # (the sticks: W A S D on the left one's lines)
    if entries[16] != 'HALO_KEY_WASD' or not all('SDL_SCANCODE_%s' % letter in body for letter in 'WASD'):
        print('FAIL %s: the left stick is not W A S D' % table)
        failures += 1
sys.exit(1 if failures else 0)
PY
exit $status
