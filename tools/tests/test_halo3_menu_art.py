"""The Halo 3 menus' art (tools/halo3_menu_art.py): the files, their sizes
and format, the same bytes each run, and the colours the design pins
(docs/superpowers/specs/2026-10-18-halo3-menus-design.md).

    python3 -m unittest tools/tests/test_halo3_menu_art.py -v
"""
import os
import subprocess
import sys
import tempfile
import unittest

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
GENERATOR = os.path.join(HERE, '..', 'halo3_menu_art.py')

EXPECTED = {
    'column.png': (256, 256),
    'panel.png': (256, 256),
    'item_on.png': (512, 32),
    'row_on.png': (512, 32),
    'strip.png': (512, 32),
    'rule.png': (512, 2),
    'nameplate.png': (512, 48),
    'glyphs.png': (320, 64),
    'title_halo.png': (512, 128),
}


def generate(folder):
    subprocess.run([sys.executable, GENERATOR, folder], check=True)


class ArtTest(unittest.TestCase):
    def test_files_sizes_format(self):
        with tempfile.TemporaryDirectory() as folder:
            generate(folder)
            self.assertEqual(sorted(f for f in os.listdir(folder) if f.endswith('.png')), sorted(EXPECTED))
            for name, size in EXPECTED.items():
                path = os.path.join(folder, name)
                with Image.open(path) as image:
                    self.assertEqual(image.size, size, name)
                    self.assertEqual(image.mode, 'RGBA', name)
                    self.assertFalse(image.info.get('interlace'), name)
                self.assertLess(os.path.getsize(path), 512 * 1024, name)

    def test_same_bytes_each_run(self):
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            generate(first)
            generate(second)
            for name in EXPECTED:
                with open(os.path.join(first, name), 'rb') as a, open(os.path.join(second, name), 'rb') as b:
                    self.assertEqual(a.read(), b.read(), name)

    def test_pinned_colours(self):
        with tempfile.TemporaryDirectory() as folder:
            generate(folder)
            with Image.open(os.path.join(folder, 'item_on.png')) as image:
                r, g, b, a = image.getpixel((10, 16))
            for got, want in zip((r, g, b, a), (184, 116, 58, 255)):
                self.assertLessEqual(abs(got - want), 3)
            with Image.open(os.path.join(folder, 'panel.png')) as image:
                r, g, b, a = image.getpixel((128, 0))
            for got, want in zip((r, g, b, a), (27, 41, 70, 242)):
                self.assertLessEqual(abs(got - want), 3)
            with Image.open(os.path.join(folder, 'row_on.png')) as image:
                self.assertEqual(image.getpixel((511, 16))[3], 0)


if __name__ == '__main__':
    unittest.main()
