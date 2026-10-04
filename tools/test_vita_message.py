"""Exercise production message formatting and modal input with a host shim.

The settings panel (port/vita/host/vita_settings.c) is now built whole by
port/vita/tests/vita_settings_test.c, which checks the message overlay with
the Multiplayer page: this runs it and requires its message checks."""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


class MessageTests(unittest.TestCase):
    def test_wrap_and_modal_dismissal(self):
        result = subprocess.run(['sh', str(ROOT / 'port/vita/tests/run_vita_settings_test.sh')],
                                capture_output=True, text=True)
        lines = [line for line in result.stdout.splitlines() if line.startswith(('PASS message', 'FAIL message'))]
        self.assertEqual(len(lines), 5, result.stdout + result.stderr)
        self.assertTrue(all(line.startswith('PASS') for line in lines), result.stdout)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
