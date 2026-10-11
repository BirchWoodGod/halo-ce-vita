"""Tests for the game's own zlib (source/memory/zlib, 1.1.3 with 1.1.4's
fix), which still inflates other people's data: map sharing's downloads
(port/linux/game/map_share_protocol.c) and the menus' pictures
(menu_tags.c).

The library is built with AddressSanitizer where the compiler has it, and a
driver inflates dynamic-Huffman streams made here while its allocator fails
each allocation in turn: zlib 1.1.3 freed a dynamic block's code lengths
when its trees failed to build (or the allocation after them failed) and
then again as the stream ended (CVE-2002-0059), which a peer's crafted
stream, or any stream on a Vita short of memory, reached. Each failure must
end in an error and a clean end; with none, the data must come back whole.
"""
from pathlib import Path
import random
import shutil
import subprocess
import sys
import zlib

import pytest

ROOT = Path(__file__).resolve().parents[1]
ZLIB = ROOT / "source/memory/zlib"
ZLIB_SOURCES = ["adler32", "infblock", "infcodes", "inffast", "inflate", "inftrees", "infutil", "zutil"]

DRIVER = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zlib.h"

static int calls, fail_at;

static voidpf allocate(voidpf opaque, uInt items, uInt size)
{
	(void)opaque;
	return ++calls == fail_at ? NULL : calloc(items, size);
}

static void release(voidpf opaque, voidpf address)
{
	(void)opaque;
	free(address);
}

/* driver STREAM EXPECTED: inflates STREAM with each allocation failing in
turn, then with none, which must give EXPECTED */
int main(int argc, char **argv)
{
	static unsigned char in[1 << 20], out[1 << 20], expected[1 << 20];
	FILE *file;
	size_t in_size, expected_size;

	if (argc != 3 || !(file = fopen(argv[1], "rb")))
		return 2;
	in_size = fread(in, 1, sizeof(in), file);
	fclose(file);
	if (!(file = fopen(argv[2], "rb")))
		return 2;
	expected_size = fread(expected, 1, sizeof(expected), file);
	fclose(file);
	for (fail_at = 1; fail_at <= 64; fail_at++)
	{
		z_stream stream;
		int result;

		memset(&stream, 0, sizeof(stream));
		calls = 0;
		stream.zalloc = allocate;
		stream.zfree = release;
		if (inflateInit(&stream) != Z_OK)
			continue;
		stream.next_in = in;
		stream.avail_in = (uInt)in_size;
		stream.next_out = out;
		stream.avail_out = sizeof(out);
		result = inflate(&stream, Z_FINISH);
		inflateEnd(&stream);
		if (calls < fail_at)
		{
			/* (nothing failed: the data whole) */
			return result == Z_STREAM_END && stream.total_out == expected_size &&
				!memcmp(out, expected, expected_size) ? 0 : 3;
		}
		if (result == Z_STREAM_END)
			return 4;
	}
	return 5;
}
"""


def find_clang():
    for candidate in (shutil.which("clang"), r"C:\Program Files\LLVM\bin\clang.exe"):
        if candidate and Path(candidate).is_file():
            return candidate
    return None


@pytest.fixture(scope="session")
def driver(tmp_path_factory):
    clang = find_clang()
    if clang is None:
        pytest.skip("clang is needed to build the driver")
    folder = tmp_path_factory.mktemp("game-zlib")
    # (the game's zlib takes debug_malloc from cseries.h: the C library's here)
    (folder / "cseries.h").write_text("#include <stdlib.h>\n#define debug_malloc(size, clear, file, line) calloc(1, (size))\n"
                                      "#define debug_free(pointer, file, line) free(pointer)\n")
    (folder / "driver.c").write_text(DRIVER)
    output = folder / ("driver.exe" if sys.platform == "win32" else "driver")
    errors = []
    for extra in (["-fsanitize=address", "-fno-omit-frame-pointer"], []):
        command = [clang, "-O1", "-g", "-w", *extra, f"-I{folder}", f"-I{ZLIB}", str(folder / "driver.c"),
                   *(str(ZLIB / f"{source}.c") for source in ZLIB_SOURCES), "-o", str(output)]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode == 0:
            return output
        errors.append(result.stdout + result.stderr)
    pytest.fail("could not build the driver:\n" + "\n".join(errors))


@pytest.mark.parametrize("seed", range(4))
def test_every_failed_allocation_ends_cleanly(driver, tmp_path, seed):
    generator = random.Random(seed)
    alphabet = bytes(range(32, 127)) if seed % 2 else b"abcdefgh 0123"
    data = bytes(generator.choice(alphabet) for _ in range(4000 + seed * 3000))
    stream = tmp_path / "stream.z"
    expected = tmp_path / "expected.bin"
    stream.write_bytes(zlib.compress(data, 9))
    expected.write_bytes(data)
    result = subprocess.run([str(driver), str(stream), str(expected)], capture_output=True, text=True,
                            env={"ASAN_OPTIONS": "detect_leaks=1:abort_on_error=0"})
    assert result.returncode == 0, (result.returncode, result.stderr[-4000:])
