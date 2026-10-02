"""Exercise production message formatting and modal input with a host shim."""
import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class MessageTests(unittest.TestCase):
    def test_wrap_and_modal_dismissal(self):
        source = (ROOT / 'port/vita/host/vita_settings.c').read_text()
        body = source[source.index('static pthread_mutex_t message_lock'):]
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            harness = folder / 'message.c'
            harness.write_text('''
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#define VITA_BUTTON_SELECT 1
#define VITA_BUTTON_START 2
#define VITA_BUTTON_CIRCLE 4
#define VITA_BUTTON_CROSS 8
#define VITA_BUTTON_UP 16
#define VITA_BUTTON_DOWN 32
#define VITA_BUTTON_LEFT 64
#define VITA_BUTTON_RIGHT 128
#define SETTING_COUNT 1
struct vita_host_pad { unsigned long buttons; };
static int panel_open, selected;
static unsigned long previous_buttons;
static unsigned long long both_since, last_move;
static char displayed[2048];
static unsigned long long now_us(void) { return 1000000; }
static void vgxm_menu_set(const char *text, int row) {
    snprintf(displayed, sizeof(displayed), "%s", text ? text : "");
}
static void show(void) {}
static void change(int step) {}
static void close_panel(void) { panel_open = 0; vgxm_menu_set(NULL, 0); }
''' + body + '''
const char *test_displayed(void) { return displayed; }
int test_input(unsigned long buttons) {
    struct vita_host_pad pad = {buttons}; return vita_settings_input(&pad);
}
''')
            lib = folder / 'message.so'
            subprocess.run(['cc', '-shared', '-fPIC', '-pthread', str(harness), '-o', str(lib)], check=True)
            code = ctypes.CDLL(str(lib))
            code.vita_settings_message.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
            code.test_displayed.restype = ctypes.c_char_p
            text = b'You are on version 5. The host is on version 9. Please install the updated build.'
            code.vita_settings_message(b'Join rejected', text)
            # Opening click must not dismiss the new error or reach the game.
            self.assertEqual(code.test_input(8), 1)
            displayed = code.test_displayed().decode()
            self.assertIn('Join rejected', displayed)
            self.assertTrue(all(len(line) <= 46 for line in displayed.splitlines()))
            self.assertIn(' '.join(text.decode().split()), ' '.join(displayed.split()))
            self.assertEqual(code.test_input(8), 1)  # held Cross
            self.assertEqual(code.test_input(0), 1)
            self.assertEqual(code.test_input(4), 1)  # new Circle dismisses
            self.assertEqual(code.test_displayed(), b'')
            self.assertEqual(code.test_input(0), 0)
