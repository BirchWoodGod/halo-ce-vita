"""Ed25519 (RFC 8032, section 6's reference code, in plain Python) for the
Vita updater's release signing (tools/release/sign_update.py,
make_update_key.py, verify_update.py).

Only the owner's machine runs this, once per release: slow and simple on
purpose, no third-party package to install or trust. The game checks the
signatures with Monocypher's crypto_ed25519_check
(port/vita/host/vita_update_verify.c); port/vita/tests/run_update_install_test.sh
checks the two agree, and self_test() checks RFC 8032's test vectors.

The code is RFC 8032's (Simplified BSD License, IETF Trust and the authors:
S. Josefsson, I. Liusvaara), lightly arranged.
"""

import hashlib


def _sha512(data):
    return hashlib.sha512(data).digest()


_P = 2 ** 255 - 19
_Q = 2 ** 252 + 27742317777372353535851937790883648493


def _inverse(x):
    return pow(x, _P - 2, _P)


_D = -121665 * _inverse(121666) % _P
_SQRT_M1 = pow(2, (_P - 1) // 4, _P)


def _sha512_mod_q(data):
    return int.from_bytes(_sha512(data), "little") % _Q


def _add(a, b):
    first = (a[1] - a[0]) * (b[1] - b[0]) % _P
    second = (a[1] + a[0]) * (b[1] + b[0]) % _P
    c = 2 * a[3] * b[3] * _D % _P
    d = 2 * a[2] * b[2] % _P
    e, f, g, h = second - first, d - c, d + c, second + first
    return (e * f, g * h, f * g, e * h)


def _multiply(scalar, point):
    result = (0, 1, 1, 0)
    while scalar > 0:
        if scalar & 1:
            result = _add(result, point)
        point = _add(point, point)
        scalar >>= 1
    return result


def _equal(a, b):
    return (a[0] * b[2] - b[0] * a[2]) % _P == 0 and (a[1] * b[2] - b[1] * a[2]) % _P == 0


def _recover_x(y, sign):
    if y >= _P:
        return None
    x2 = (y * y - 1) * _inverse(_D * y * y + 1)
    if x2 == 0:
        return None if sign else 0
    x = pow(x2, (_P + 3) // 8, _P)
    if (x * x - x2) % _P != 0:
        x = x * _SQRT_M1 % _P
    if (x * x - x2) % _P != 0:
        return None
    if (x & 1) != sign:
        x = _P - x
    return x


_G_Y = 4 * _inverse(5) % _P
_G_X = _recover_x(_G_Y, 0)
_G = (_G_X, _G_Y, 1, _G_X * _G_Y % _P)


def _compress(point):
    z = _inverse(point[2])
    x = point[0] * z % _P
    y = point[1] * z % _P
    return int.to_bytes(y | ((x & 1) << 255), 32, "little")


def _decompress(data):
    if len(data) != 32:
        return None
    y = int.from_bytes(data, "little")
    sign = y >> 255
    y &= (1 << 255) - 1
    x = _recover_x(y, sign)
    if x is None:
        return None
    return (x, y, 1, x * y % _P)


def _expand(seed):
    if len(seed) != 32:
        raise ValueError("an Ed25519 private key (seed) is 32 bytes")
    h = _sha512(seed)
    a = int.from_bytes(h[:32], "little")
    a &= (1 << 254) - 8
    a |= 1 << 254
    return a, h[32:]


def public_key(seed):
    """the 32-byte public key of a 32-byte seed"""
    a, _ = _expand(seed)
    return _compress(_multiply(a, _G))


def sign(seed, message):
    """the 64-byte signature of message (bytes) by seed"""
    a, prefix = _expand(seed)
    public = _compress(_multiply(a, _G))
    r = _sha512_mod_q(prefix + message)
    point_r = _compress(_multiply(r, _G))
    h = _sha512_mod_q(point_r + public + message)
    s = (r + h * a) % _Q
    return point_r + int.to_bytes(s, 32, "little")


def verify(public, message, signature):
    """whether signature is public's of message"""
    if len(public) != 32 or len(signature) != 64:
        return False
    point_a = _decompress(public)
    if not point_a:
        return False
    point_r = _decompress(signature[:32])
    if not point_r:
        return False
    s = int.from_bytes(signature[32:], "little")
    if s >= _Q:
        return False
    h = _sha512_mod_q(signature[:32] + public + message)
    return _equal(_multiply(s, _G), _add(point_r, _multiply(h, point_a)))


# RFC 8032 7.1: TEST 1, TEST 2 and TEST 3
_VECTORS = [
    ("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
     "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
     "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"),
    ("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
     "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
     "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"),
    ("c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
     "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "af82",
     "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a"),
]


def self_test():
    """RFC 8032's vectors: raises if this code is wrong"""
    for seed, public, message, signature in _VECTORS:
        seed, public = bytes.fromhex(seed), bytes.fromhex(public)
        message, signature = bytes.fromhex(message), bytes.fromhex(signature)
        if public_key(seed) != public or sign(seed, message) != signature or not verify(public, message, signature):
            raise RuntimeError("Ed25519 self-test failed: do not sign with this machine's Python")
        if verify(public, message + b"x", signature):
            raise RuntimeError("Ed25519 self-test failed (a changed message verified)")
