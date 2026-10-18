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

# (the menus draw a picture 1:1 and crop it to its widget, or stretch it when
# the widget is larger: ui_widget.c draw_bitmap_in_rect; so each is at most its
# widget's size, in the 640x480 screen, and a power of two)
EXPECTED = {
    'column.png': (64, 64),
    'haze.png': (64, 64),
    'panel.png': (64, 64),
    'item_on.png': (128, 16),
    'item_off.png': (128, 16),
    'row_on.png': (128, 16),
    'strip.png': (64, 16),
    'rule.png': (128, 2),
    'nameplate.png': (256, 16),
    'glyph_cross.png': (16, 16),
    'glyph_circle.png': (16, 16),
    'glyph_triangle.png': (16, 16),
    'glyph_square.png': (16, 16),
    'glyph_start.png': (16, 16),
    'title_halo.png': (256, 64),
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
                r, g, b, a = image.getpixel((5, 8))
            for got, want in zip((r, g, b, a), (184, 116, 58, 255)):
                self.assertLessEqual(abs(got - want), 3)
            with Image.open(os.path.join(folder, 'panel.png')) as image:
                r, g, b, a = image.getpixel((32, 0))
            for got, want in zip((r, g, b, a), (27, 41, 70, 242)):
                self.assertLessEqual(abs(got - want), 3)
            with Image.open(os.path.join(folder, 'row_on.png')) as image:
                self.assertEqual(image.getpixel((127, 8))[3], 0)


if __name__ == '__main__':
    unittest.main()
