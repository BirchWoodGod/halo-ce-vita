"""Run the production invite-file consumer against a simulated Vita filesystem."""

import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class InviteFileTests(unittest.TestCase):
    def test_vita_root_and_single_consumption(self):
        source = (ROOT / "port/linux/src/p2p.c").read_text()
        start = source.index("static void poll_invite_file(void)")
        end = source.index("\n}\n#endif", start) + 2
        consumer = source[start:end]
        with tempfile.TemporaryDirectory() as name:
            folder = Path(name)
            harness = folder / "invite.c"
            harness.write_text(r'''
#include <stdio.h>
#include <string.h>
#define HALO_VITA 1
static char root[512], received[256];
static int count;
static int elapsed(unsigned long t, int ms) { return 1; }
static unsigned long p2p_now(void) { return 1; }
static void p2p_invite_received(const char *text) {
    snprintf(received, sizeof(received), "%s", text); count++;
}
static void map_path(const char *path, char *out) {
    const char *prefix = "ux0:data/haloce-vita/";
    if (strncmp(path, prefix, strlen(prefix)) == 0)
        snprintf(out, 1024, "%s/%s", root, path + strlen(prefix));
    else
        snprintf(out, 1024, "%s", path);
}
static int mapped_rename(const char *from, const char *to) {
    char a[1024], b[1024]; map_path(from, a); map_path(to, b);
    return rename(a, b);
}
static FILE *mapped_fopen(const char *path, const char *mode) {
    char p[1024]; map_path(path, p); return fopen(p, mode);
}
static int mapped_remove(const char *path) {
    char p[1024]; map_path(path, p); return remove(p);
}
#define rename mapped_rename
#define fopen mapped_fopen
#define remove mapped_remove
''' + consumer + r'''
void test_poll(const char *directory) {
    snprintf(root, sizeof(root), "%s", directory); poll_invite_file();
}
const char *test_received(void) { return received; }
int test_count(void) { return count; }
''')
            library = folder / "invite.so"
            subprocess.run(["cc", "-shared", "-fPIC", str(harness),
                            "-o", str(library)], check=True)
            test = ctypes.CDLL(str(library))
            test.test_poll.argtypes = [ctypes.c_char_p]
            test.test_received.restype = ctypes.c_char_p
            test.test_count.restype = ctypes.c_int
            path = str(folder).encode()
            # An unfinished upload is left alone until renamed into place.
            invite = "halo://join/" + "ab" * 32 + "\n"
            pending = folder / "join_link.tmp"
            pending.write_text(invite)
            test.test_poll(path)
            self.assertEqual(test.test_count(), 0)
            pending.rename(folder / "join_link.txt")
            test.test_poll(path)
            self.assertEqual(test.test_received(), invite.encode())
            self.assertEqual(test.test_count(), 1)
            self.assertFalse((folder / "join_link.txt").exists())
            self.assertFalse((folder / "join_link.taken").exists())
            test.test_poll(path)
            self.assertEqual(test.test_count(), 1)


if __name__ == "__main__":
    unittest.main()
