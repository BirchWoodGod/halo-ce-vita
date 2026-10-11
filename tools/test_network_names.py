"""Exercise the imported name checks used by profile selection and host bans."""
import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class NameTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        folder = Path(cls.temp.name)
        source = (ROOT / 'source/game/players.c').read_text()
        body = source[source.index('char player_name_character_ascii('):source.index('/* ---------- private code */')]
        harness = folder / 'names.c'
        harness.write_text('#include <wchar.h>\ntypedef int boolean;\n#define TRUE 1\n#define FALSE 0\n#define NUMBEROF(a) (sizeof(a)/sizeof((a)[0]))\n' + body)
        lib = folder / 'names.so'
        subprocess.run(['cc', '-shared', '-fPIC', str(harness), '-o', str(lib)], check=True)
        cls.code = ctypes.CDLL(str(lib))
        cls.code.player_name_clean.argtypes = [ctypes.POINTER(ctypes.c_wchar), ctypes.c_long]
        cls.code.player_name_valid.argtypes = [ctypes.POINTER(ctypes.c_wchar), ctypes.c_long]
        cls.code.player_name_character_ascii.argtypes = [ctypes.c_wchar]
        cls.code.player_name_character_ascii.restype = ctypes.c_char

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_controls_and_invisible_names(self):
        name = ctypes.create_unicode_buffer('  A\u200b|\nlice\u00a0 ', 32)
        self.assertEqual(self.code.player_name_valid(name, 32), 0)
        self.assertEqual(self.code.player_name_clean(name, 32), 1)
        self.assertEqual(name.value, 'Alice')
        self.assertEqual(self.code.player_name_valid(name, 32), 1)
        name = ctypes.create_unicode_buffer('\u200b\u2800\u3164', 32)
        self.assertEqual(self.code.player_name_clean(name, 32), 0)
        self.assertEqual(name.value, '')

    def test_accented_names_remain_bannable(self):
        name = ctypes.create_unicode_buffer('René', 32)
        self.assertEqual(self.code.player_name_valid(name, 32), 1)
        self.assertEqual(self.code.player_name_character_ascii('é'), b'e')
        self.assertEqual(self.code.player_name_character_ascii('É'), b'E')
