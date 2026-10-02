"""Known-vector checks for the shared internet protocol's cryptography.

Compile the production crypto unit with an empty platform header: it uses
no XDK declarations. Random-nonce entry points abort in this harness;
the tests supply the standard vectors' nonces explicitly.
"""

import ctypes
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class CryptoTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        directory = Path(cls.directory.name)
        for name in ("p2p_crypto.c", "p2p_internal.h", "p2p.h", "posix.h"):
            shutil.copyfile(ROOT / "port/linux/src" / name, directory / name)
        (directory / "platform.h").write_text("")
        (directory / "random.c").write_text(
            '#include "posix.h"\n#include <stdlib.h>\n'
            'void posix_random_bytes(void *p, posix_ulong n) '
            '{ (void)p; (void)n; abort(); }\n'
        )
        library = directory / "crypto.so"
        subprocess.run(
            ["cc", "-shared", "-fPIC", "-O2", str(directory / "p2p_crypto.c"),
             str(directory / "random.c"), "-o", str(library)], check=True,
        )
        cls.crypto = ctypes.CDLL(str(library))
        pointer = ctypes.c_void_p
        integer = ctypes.c_int
        cls.crypto.p2p_sha256.argtypes = [pointer, integer, pointer]
        cls.crypto.p2p_sha256.restype = None
        cls.crypto.p2p_x25519.argtypes = [pointer, pointer, pointer]
        cls.crypto.p2p_x25519.restype = None
        cls.crypto.p2p_aead_seal.argtypes = [pointer, pointer, pointer, integer,
                                           pointer, integer, pointer]
        cls.crypto.p2p_aead_open.argtypes = cls.crypto.p2p_aead_seal.argtypes
        cls.crypto.p2p_aead_seal.restype = integer
        cls.crypto.p2p_aead_open.restype = integer

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def test_sha256(self):
        digest = ctypes.create_string_buffer(32)
        self.crypto.p2p_sha256(b"abc", 3, digest)
        self.assertEqual(digest.raw.hex(),
                         "ba7816bf8f01cfea414140de5dae2223"
                         "b00361a396177a9cb410ff61f20015ad")

    def test_rfc7748_x25519(self):
        scalar = bytes.fromhex("77076d0a7318a57d3c16c17251b26645"
                               "df4c2f87ebc0992ab177fba51db92c2a")
        public = ctypes.create_string_buffer(32)
        self.crypto.p2p_x25519(public, scalar, None)
        self.assertEqual(public.raw.hex(),
                         "8520f0098930a754748b7ddcb43ef75a"
                         "0dbf3a0d26381af4eba4a98eaa9b4e6a")
        bob = bytes.fromhex("de9edb7d7b7dc1b4d35b61c2ece43537"
                            "3f8343c85b78674dadfc7e146f882b4f")
        self.crypto.p2p_x25519(public, scalar, bob)
        self.assertEqual(public.raw.hex(),
                         "4a5d9d5ba4ce2de1728e3bf480350f25"
                         "e07e21c947d19e3376f09b3c1e161742")

    def test_rfc8439_aead_and_tampering(self):
        key = bytes(range(0x80, 0xa0))
        nonce = bytes.fromhex("070000004041424344454647")
        aad = bytes.fromhex("50515253c0c1c2c3c4c5c6c7")
        plain = (b"Ladies and Gentlemen of the class of '99: If I could offer "
                 b"you only one tip for the future, sunscreen would be it.")
        expected = bytes.fromhex(
            "d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d6"
            "3dbea45e8ca9671282fafb69da92728b1a71de0a9e060b2905d6a5b67ecd3b36"
            "92ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc"
            "3ff4def08e4b7a9de576d26586cec64b6116"
            "1ae10b594f09e26a7e902ecbd0600691"
        )
        sealed = ctypes.create_string_buffer(len(expected))
        size = self.crypto.p2p_aead_seal(key, nonce, aad, len(aad), plain,
                                        len(plain), sealed)
        self.assertEqual(size, len(expected))
        self.assertEqual(sealed.raw, expected)
        opened = ctypes.create_string_buffer(len(plain))
        self.assertEqual(self.crypto.p2p_aead_open(
            key, nonce, aad, len(aad), expected, len(expected), opened), len(plain))
        self.assertEqual(opened.raw, plain)
        for index in (0, len(expected) - 1):
            altered = bytearray(expected)
            altered[index] ^= 1
            self.assertEqual(self.crypto.p2p_aead_open(
                key, nonce, aad, len(aad), bytes(altered), len(altered), opened), -1)
        altered_header = bytes([aad[0] ^ 1]) + aad[1:]
        self.assertEqual(self.crypto.p2p_aead_open(
            key, nonce, altered_header, len(aad), expected, len(expected), opened), -1)


if __name__ == "__main__":
    unittest.main()
