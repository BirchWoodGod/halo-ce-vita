# SPDX-License-Identifier: GPL-3.0-only
"""
Synthetic stand-ins for the player's files, built here so the tests need no
game data: a small XDVDFS (Xbox disc) image, an LZX compressor (the
decoder's other half: verbatim, aligned and uncompressed blocks, run codes
in the length tables, repeated offsets, E8 translation), Microsoft cabinets
(stored, MSZIP, LZX), a Windows program with a cabinet among its resources,
fake resource maps, and a VitaShell-like FTP server (pyftpdlib: VitaShell's
LIST format, no NLST, no SIZE, a dropped connection on demand).
"""

import os
import struct
import threading
import zlib

SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"


# ---------------------------------------------------------------------------
# XDVDFS


def make_xiso(path, tree, base=0):
    """Writes an Xbox disc image at `path`: `tree` maps names to bytes (a
    file) or to a dict (a folder). `base` is where the game partition
    starts (0 for an XISO). Returns {"/".join(path): (sector, size)}."""
    chunks = {}
    layout = {}
    next_sector = [33]

    def allocate(size):
        sector = next_sector[0]
        next_sector[0] += max(1, (size + SECTOR - 1) // SECTOR)
        return sector

    def build(folder, prefix):
        if not folder:
            return 0, 0
        names = sorted(folder, key=str.lower)
        info = {}
        for name in names:
            value = folder[name]
            if isinstance(value, dict):
                sector, size = build(value, prefix + (name,))
                info[name] = (sector, size, 0x10)
            else:
                sector = allocate(len(value))
                chunks[sector] = value
                info[name] = (sector, len(value), 0x80)
            layout["/".join(prefix + (name,))] = info[name][:2]
        order = []

        def place(low, high):
            if low >= high:
                return None
            middle = (low + high) // 2
            index = len(order)
            order.append([names[middle], None, None])
            order[index][1] = place(low, middle)
            order[index][2] = place(middle + 1, high)
            return index

        place(0, len(names))
        offsets = []
        offset = 0
        for name, _, _ in order:
            offsets.append(offset)
            offset += (14 + len(name.encode("latin-1")) + 3) // 4 * 4
        table = bytearray()
        for name, left, right in order:
            sector, size, attributes = info[name]
            raw = name.encode("latin-1")
            entry = struct.pack("<HHIIBB", offsets[left] // 4 if left is not None else 0,
                                offsets[right] // 4 if right is not None else 0, sector, size, attributes,
                                len(raw)) + raw
            entry += b"\xff" * ((-len(entry)) % 4)
            table += entry
        table += b"\xff" * ((-len(table)) % SECTOR)
        sector = allocate(len(table))
        chunks[sector] = bytes(table)
        return sector, len(table)

    root_sector, root_size = build(tree, ())
    volume = bytearray(SECTOR)
    volume[0:20] = MAGIC
    struct.pack_into("<II", volume, 20, root_sector, root_size)
    volume[0x7EC:0x800] = MAGIC
    with open(path, "wb") as file:
        file.seek(base + 32 * SECTOR)
        file.write(volume)
        for sector, data in chunks.items():
            file.seek(base + sector * SECTOR)
            file.write(data)
        file.truncate(base + next_sector[0] * SECTOR)
    return layout


def fake_resource_map(kind, size=4096, seed=1):
    """A resource map header (type, names at 16, index at 16, 0 items) and
    compressible filler."""
    body = bytearray()
    value = seed
    while len(body) < size - 16:
        value = (value * 1103515245 + 12345) & 0x7FFFFFFF
        body += bytes([65 + (value >> 16) % 6]) * (1 + (value >> 8) % 9)
    return struct.pack("<IIIi", kind, 16, 16, 0) + bytes(body[:size - 16])


def xbox_tree(extra_maps=None, bink=True):
    maps = {name: (b"XMAP" + name.encode() * 50) for name in (
        "ui.map", "a10.map", "a30.map", "a50.map", "b30.map", "b40.map", "c10.map", "c20.map", "c40.map",
        "d20.map", "d40.map", "bloodgulch.map")}
    maps.update(extra_maps or {})
    tree = {"default.xbe": b"XBEH" + b"\0" * 300, "maps": maps}
    if bink:
        tree["bink"] = {"intro.bik": b"BIKi" + b"\1" * 999, "credits.bik": b"BIKi" + b"\2" * 500}
    return tree


# ---------------------------------------------------------------------------
# LZX, the compressing side


_EXTRA = []
_BASE = []
step = 0
for _index in range(0, 52, 2):
    _EXTRA.extend((step, step))
    if _index and step < 17:
        step += 1
_position = 0
for _index in range(52):
    _BASE.append(_position)
    _position += 1 << _EXTRA[_index]
SLOTS = {15: 30, 16: 32, 17: 34, 18: 36, 19: 38, 20: 42, 21: 50}


class BitWriter:
    def __init__(self):
        self.out = bytearray()
        self.value = 0
        self.count = 0

    def bits(self, value, count):
        assert 0 <= value < (1 << count) or count == 0
        self.value = (self.value << count) | value
        self.count += count
        while self.count >= 16:
            self.count -= 16
            self.out += struct.pack("<H", (self.value >> self.count) & 0xFFFF)
        self.value &= (1 << self.count) - 1

    def align16(self):
        if self.count:
            self.bits(0, 16 - self.count)

    def raw(self, data):
        assert self.count == 0
        self.out += data


def canonical(lengths):
    codes = {}
    code = 0
    for length in range(1, 17):
        for symbol, symbol_length in enumerate(lengths):
            if symbol_length == length:
                codes[symbol] = (code, length)
                code += 1
        code <<= 1
    return codes


def write_lengths(writer, previous, new, first, last):
    pretree_lengths = [5] * 20
    codes = canonical(pretree_lengths)
    for length in pretree_lengths:
        writer.bits(length, 4)

    def emit(symbol):
        writer.bits(*codes[symbol])

    index = first
    while index < last:
        if new[index] == 0:
            run = 0
            while index + run < last and new[index + run] == 0:
                run += 1
            if run >= 20:
                run = min(run, 51)
                emit(18)
                writer.bits(run - 20, 5)
                index += run
                continue
            if run >= 4:
                run = min(run, 19)
                emit(17)
                writer.bits(run - 4, 4)
                index += run
                continue
        run = 1
        while index + run < last and run < 5 and new[index + run] == new[index]:
            run += 1
        if run >= 4:
            emit(19)
            writer.bits(run - 4, 1)
            emit((previous[index] - new[index]) % 17)
            index += run
            continue
        emit((previous[index] - new[index]) % 17)
        index += 1
    previous[first:last] = new[first:last]


def main_lengths(window_bits):
    count = 256 + SLOTS[window_bits] * 8
    lengths = [8] * 128 + [9] * 127 + [16]
    matches = count - 256
    lengths += [11] * (matches // 2) + [15] * (matches - matches // 2)
    # unused match symbols (slot 5, slots 20-25): zero runs in the length
    # tables (pretree codes 17 and 18); the compressor avoids them
    for symbol in list(range(256 + 5 * 8, 256 + 6 * 8)) + list(range(256 + 20 * 8, 256 + 26 * 8)):
        lengths[symbol] = 0
    return lengths


# the length tree: short and long (> 12 bit) codes, the last 49 unused
LENGTH_LENGTHS = [8] * 100 + [14] * 100 + [0] * 49


def e8_encode(frame, position, file_size):
    """The compressor's half of the E8 translation (relative call targets to
    absolute), the inverse of the decoder's."""
    frame = bytearray(frame)
    end = len(frame) - 10
    index = 0
    while index < end:
        if frame[index] != 0xE8:
            index += 1
            continue
        current = position + index
        relative = struct.unpack_from("<i", frame, index + 1)[0]
        if -current <= relative < file_size - current:
            absolute = relative + current
        elif file_size - current <= relative < file_size:
            absolute = relative - file_size
        else:
            absolute = relative
        struct.pack_into("<I", frame, index + 1, absolute & 0xFFFFFFFF)
        index += 5
    return bytes(frame)


def lzx_compress(data, window_bits=16, blocks=None, e8_size=0):
    """`data` compressed as cabinet LZX; returns the compressed bytes of each
    32 KB frame. `blocks` lists (type, length): 1 verbatim, 2 aligned, 3
    uncompressed; the default alternates them. Matches never cross a block
    or frame boundary."""
    window = 1 << window_bits
    if e8_size:
        data = b"".join(e8_encode(data[start:start + 32768], start, e8_size)
                        for start in range(0, len(data), 32768))
    if blocks is None:
        blocks = []
        position = 0
        kinds = [1, 2, 3, 1, 2]
        sizes = [20000, 41001, 3333, 50000, 30000]
        turn = 0
        while position < len(data):
            length = min(sizes[turn % len(sizes)], len(data) - position)
            blocks.append((kinds[turn % len(kinds)], length))
            position += length
            turn += 1
    assert sum(length for _, length in blocks) == len(data)
    writer = BitWriter()
    frames = []
    frame_marks = []
    writer.bits(1 if e8_size else 0, 1)
    if e8_size:
        writer.bits(e8_size >> 16, 16)
        writer.bits(e8_size & 0xFFFF, 16)
    previous_main = [0] * (256 + SLOTS[window_bits] * 8)
    previous_length = [0] * 249
    main = main_lengths(window_bits)
    length_lengths = LENGTH_LENGTHS
    main_codes = canonical(main)
    length_codes = canonical(length_lengths)
    aligned_codes = canonical([3] * 8)
    R = [1, 1, 1]
    position = 0
    max_offset = _BASE[SLOTS[window_bits] - 1] + (1 << _EXTRA[SLOTS[window_bits] - 1]) - 3
    max_offset = min(max_offset, window - 3)
    table = {}
    byte_mode = False

    def frame_end_check():
        if position % 32768 == 0 and position < len(data):
            if not byte_mode:
                writer.align16()
            frame_marks.append(len(writer.out))

    for kind, length in blocks:
        block_end = position + length
        writer.bits(kind, 3)
        writer.bits(length >> 8, 16)
        writer.bits(length & 0xFF, 8)
        if kind == 3:
            if writer.count:
                writer.align16()
            else:
                writer.bits(0, 16)
            writer.raw(struct.pack("<III", *R))
            byte_mode = True
            while position < block_end:
                run = min(block_end - position, 32768 - position % 32768)
                writer.raw(data[position:position + run])
                for at in range(position, position + run - 2):
                    table[data[at:at + 3]] = at
                position += run
                frame_end_check()
            if length & 1:
                writer.raw(b"\0")
            byte_mode = False
            continue
        if kind == 2:
            for _ in range(8):
                writer.bits(3, 3)
        write_lengths(writer, previous_main, main, 0, 256)
        write_lengths(writer, previous_main, main, 256, len(main))
        write_lengths(writer, previous_length, length_lengths, 0, 249)
        while position < block_end:
            limit = min(block_end, (position // 32768 + 1) * 32768)
            best_length = 0
            best_offset = 0
            for candidate in (R[0], R[1], R[2], position - table.get(data[position:position + 3], -10 ** 9)):
                if not 1 <= candidate <= min(max_offset, position):
                    continue
                match = 0
                while (position + match < limit and match < 2 + 7 + 199 and
                       data[position + match] == data[position + match - candidate]):
                    match += 1
                if match > best_length:
                    best_length, best_offset = match, candidate
            if best_length >= 3 and best_offset not in R[:3]:
                formatted = best_offset + 2
                slot = max(index for index in range(SLOTS[window_bits]) if _BASE[index] <= formatted)
                if main[256 + (slot << 3 | min(best_length - 2, 7))] == 0:
                    best_length = 0
            if best_length >= 3:
                if best_offset == R[0]:
                    slot = 0
                elif best_offset == R[1]:
                    slot = 1
                    R[0], R[1] = R[1], R[0]
                elif best_offset == R[2]:
                    slot = 2
                    R[0], R[2] = R[2], R[0]
                else:
                    formatted = best_offset + 2
                    slot = max(index for index in range(SLOTS[window_bits]) if _BASE[index] <= formatted)
                    R[2], R[1], R[0] = R[1], R[0], best_offset
                header = min(best_length - 2, 7)
                writer.bits(*main_codes[256 + (slot << 3 | header)])
                if header == 7:
                    writer.bits(*length_codes[best_length - 9])
                if slot >= 4:
                    extra = _EXTRA[slot]
                    rest = best_offset + 2 - _BASE[slot]
                    if kind == 2 and extra >= 3:
                        writer.bits(rest >> 3, extra - 3)
                        writer.bits(*aligned_codes[rest & 7])
                    else:
                        writer.bits(rest, extra)
                for at in range(position, position + best_length):
                    table[data[at:at + 3]] = at
                position += best_length
            else:
                writer.bits(*main_codes[data[position]])
                table[data[position:position + 3]] = position
                position += 1
            frame_end_check()
    writer.align16()
    marks = [0] + frame_marks + [len(writer.out)]
    for start, end in zip(marks, marks[1:]):
        frames.append(bytes(writer.out[start:end]))
    return frames


# ---------------------------------------------------------------------------
# cabinets and the installer around one


def reference_checksum(data, seed):
    checksum = seed
    whole = len(data) // 4 * 4
    for index in range(0, whole, 4):
        checksum ^= struct.unpack_from("<I", data, index)[0]
    tail = 0
    for byte in data[whole:]:
        tail = (tail << 8) | byte
    return checksum ^ tail


def make_cab(folders, window_bits=21):
    """folders: [(kind, [(name, data), ...])], kind "stored", "mszip" or
    "lzx". Returns the cabinet's bytes."""
    folder_blocks = []
    file_entries = []
    for index, (kind, files) in enumerate(folders):
        joined = b"".join(data for _, data in files)
        offset = 0
        for name, data in files:
            file_entries.append((name, len(data), offset, index))
            offset += len(data)
        blocks = []
        if kind == "stored":
            blocks = [(joined[start:start + 32768], len(joined[start:start + 32768]))
                      for start in range(0, len(joined), 32768)]
            compression = 0
        elif kind == "mszip":
            history = b""
            for start in range(0, len(joined), 32768):
                piece = joined[start:start + 32768]
                compressor = zlib.compressobj(9, zlib.DEFLATED, -15, zdict=history) if history else \
                    zlib.compressobj(9, zlib.DEFLATED, -15)
                blocks.append((b"CK" + compressor.compress(piece) + compressor.flush(), len(piece)))
                history = (history + piece)[-32768:]
            compression = 1
        else:
            frames = lzx_compress(joined, window_bits)
            sizes = [min(32768, len(joined) - start) for start in range(0, len(joined), 32768)]
            assert len(frames) == len(sizes)
            blocks = list(zip(frames, sizes))
            compression = 3 | (window_bits << 8)
        folder_blocks.append((compression, blocks))
    header_size = 36 + 8 * len(folders)
    files_bytes = b""
    for name, size, offset, folder in file_entries:
        files_bytes += struct.pack("<IIHHHH", size, offset, folder, 0, 0, 0x20) + name.encode("latin-1") + b"\0"
    data_start = header_size + len(files_bytes)
    folder_table = b""
    data = b""
    for compression, blocks in folder_blocks:
        folder_table += struct.pack("<IHH", data_start + len(data), len(blocks), compression)
        for payload, size in blocks:
            sizes = struct.pack("<HH", len(payload), size)
            data += struct.pack("<I", reference_checksum(sizes, reference_checksum(payload, 0))) + sizes + payload
    total = data_start + len(data)
    header = b"MSCF" + struct.pack("<IIIIIBBHHHHH", 0, total, 0, header_size, 0, 3, 1, len(folders),
                                   len(file_entries), 0, 1234, 0)
    return header + folder_table + files_bytes + data


def make_pe(resources):
    """A minimal 32-bit Windows program whose .rsrc holds `resources`:
    [(type name, item name, data)], each in its own type/name/language
    branch (as halocesetup's .rsrc/CABFILE/CAB1.CAB)."""
    section_rva = 0x1000
    raw_offset = 0x400
    # the resource tree: root -> types -> names -> one language each
    tree = bytearray()

    def directory(named, numbered):
        return struct.pack("<IIHHHH", 0, 0, 4, 0, named, numbered)

    count = len(resources)
    root_size = 16 + 8 * count
    type_size = 16 + 8
    name_size = 16 + 8
    tables_size = root_size + count * (type_size + name_size)
    leaves_offset = tables_size
    strings_offset = leaves_offset + 16 * count
    strings = bytearray()
    string_offsets = []
    for type_name, item_name, _ in resources:
        pair = []
        for text in (type_name, item_name):
            pair.append(strings_offset + len(strings))
            encoded = text.encode("utf-16-le")
            strings += struct.pack("<H", len(text)) + encoded
            strings += b"\0" * ((-len(strings)) % 4)
        string_offsets.append(pair)
    data_offset = strings_offset + len(strings)
    data_offset += (-data_offset) % 16
    tree += directory(count, 0)
    for index in range(count):
        type_directory = root_size + index * (type_size + name_size)
        tree += struct.pack("<II", 0x80000000 | string_offsets[index][0], 0x80000000 | type_directory)
    blobs = bytearray()
    for index, (type_name, item_name, blob) in enumerate(resources):
        type_directory = root_size + index * (type_size + name_size)
        name_directory = type_directory + type_size
        tree += directory(1, 0) + struct.pack("<II", 0x80000000 | string_offsets[index][1],
                                               0x80000000 | name_directory)
        tree += directory(0, 1) + struct.pack("<II", 1033, leaves_offset + 16 * index)
    for index, (_, _, blob) in enumerate(resources):
        tree += struct.pack("<IIII", section_rva + data_offset + len(blobs), len(blob), 0, 0)
        blobs += blob + b"\0" * ((-len(blob)) % 16)
    tree += strings
    tree += b"\0" * (data_offset - len(tree))
    section = bytes(tree) + bytes(blobs)
    raw_size = len(section) + (-len(section)) % 512
    optional = bytearray(224)
    struct.pack_into("<H", optional, 0, 0x10B)
    struct.pack_into("<I", optional, 92, 16)
    struct.pack_into("<II", optional, 96 + 2 * 8, section_rva, len(tree))
    coff = struct.pack("<HHIIIHH", 0x14C, 1, 0, 0, 0, 224, 0x0102)
    section_header = b".rsrc\0\0\0" + struct.pack("<IIIIIIHHI", len(section), section_rva, raw_size, raw_offset,
                                                  0, 0, 0, 0, 0x40000040)
    dos = bytearray(64)
    dos[0:2] = b"MZ"
    struct.pack_into("<I", dos, 0x3C, 64)
    image = bytes(dos) + b"PE\0\0" + coff + bytes(optional) + section_header
    image += b"\0" * (raw_offset - len(image))
    image += section + b"\0" * (raw_size - len(section))
    return image


def fake_installer(extra_files=()):
    """A fake Halo CE installer: a program whose .rsrc/CABFILE/CAB1.CAB
    cabinet holds maps\\bitmaps.map, sounds.map (LZX) and loc.map (MSZIP),
    with stored and other files around them."""
    bitmaps = fake_resource_map(1, 150000, seed=3)
    sounds = fake_resource_map(2, 70000, seed=5)
    loc = fake_resource_map(3, 5000, seed=7)
    cabinet = make_cab([
        ("stored", [("redist\\readme.txt", b"not a map\n" * 100)] + list(extra_files)),
        ("lzx", [("maps\\beavercreek.map", fake_resource_map(0, 40000, seed=9)),
                 ("maps\\bitmaps.map", bitmaps), ("maps\\sounds.map", sounds)]),
        ("mszip", [("maps\\loc.map", loc), ("maps\\ui.map", b"ui" * 3000)]),
    ])
    program = make_pe([("ICONS", "MAIN", b"\0" * 300), ("CABFILE", "CAB1.CAB", cabinet)])
    return program, {"bitmaps.map": bitmaps, "sounds.map": sounds, "loc.map": loc}


# ---------------------------------------------------------------------------
# a VitaShell-like FTP server


class FakeVita:
    """pyftpdlib on 127.0.0.1 (a free high port) answering like VitaShell:
    its LIST lines, no NLST, no SIZE, paths /ux0:/data/... (the colon kept
    out of the folder on disk). `drop` makes the control connection drop
    once a file of that name has received that many bytes."""

    def __init__(self, root):
        from pyftpdlib.authorizers import DummyAuthorizer
        from pyftpdlib.filesystems import AbstractedFS
        from pyftpdlib.handlers import DTPHandler, FTPHandler
        from pyftpdlib.servers import FTPServer

        self.root = root
        os.makedirs(os.path.join(root, "ux0_", "data"), exist_ok=True)
        self.drop = {}
        self.double_226 = False
        self.dropped = []
        self.commands = []
        vita = self

        class VitaFS(AbstractedFS):
            def ftp2fs(self, ftppath):
                return super().ftp2fs(ftppath.replace("ux0:", "ux0_"))

            def fs2ftp(self, fspath):
                return super().fs2ftp(fspath).replace("ux0_", "ux0:")

            def format_list(self, basedir, listing, ignore_err=True):
                for name in listing:
                    status = self.lstat(os.path.join(basedir, name))
                    folder = os.path.isdir(os.path.join(basedir, name))
                    shown = name.replace("ux0_", "ux0:")
                    line = "%c%s 1 vita vita %d Oct %2d %02d:%02d %s\r\n" % (
                        "d" if folder else "-", "rwxr-xr-x" if folder else "rw-r--r--",
                        0 if folder else status.st_size, 8, 0, 31, shown)
                    yield line.encode("utf-8")

        class VitaDTP(DTPHandler):
            def handle_read(self):
                super().handle_read()
                name = os.path.basename(getattr(self.file_obj, "name", "") or "")
                limit = vita.drop.get(name)
                if limit is not None and self.tot_bytes_received >= limit:
                    del vita.drop[name]
                    vita.dropped.append(name)
                    self.cmd_channel.close()

            handle_read_event = handle_read

        class VitaHandler(FTPHandler):
            abstracted_fs = VitaFS
            dtp_handler = VitaDTP

            def pre_process_command(self, line, cmd, arg):
                vita.commands.append(cmd)
                return super().pre_process_command(line, cmd, arg)

            def respond(self, resp, logfun=None):
                if logfun is None:
                    super().respond(resp)
                else:
                    super().respond(resp, logfun)
                # (a quirk on demand: every transfer's 226 sent twice)
                if vita.double_226 and resp.startswith("226"):
                    super().respond("226 Transfer complete (again).")

            def ftp_NLST(self, path):
                self.respond("502 NLST not implemented.")

            def ftp_SIZE(self, path):
                self.respond("502 SIZE not implemented.")

        authorizer = DummyAuthorizer()
        authorizer.add_user("vita", "vita", root, perm="elradfmwMT")
        VitaHandler.authorizer = authorizer
        VitaHandler.banner = "VitaShell FTP"
        self.server = FTPServer(("127.0.0.1", 0), VitaHandler)
        self.port = self.server.address[1]
        self.thread = threading.Thread(target=self.server.serve_forever, kwargs={"timeout": 0.05},
                                       daemon=True)

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *exception):
        self.server.close_all()
        self.thread.join(5)

    def path(self, *parts):
        return os.path.join(self.root, "ux0_", *parts)
