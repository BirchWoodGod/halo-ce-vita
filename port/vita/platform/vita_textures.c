/*
VITA_TEXTURES.C

Xbox texture decoding (port/linux/src/xbox_textures.c's, unchanged) and the
Vita's texture cache: textures are decoded into the GPU's texture pool and
used through GXM control words (port/vita/host/vita_gxm.c). DXT textures
keep their blocks, reordered into GXM's twiddled layout; everything else is
decoded to 32-bit BGRA rows padded to 8 texels, as GXM lays out linear
textures, one Xbox mip level after another.

(The original's description follows.)
Xbox texture decoding and the OpenGL texture cache.

An Xbox texture is a Direct3D header - Common, Data (physical address),
Lock, Format and Size - over texels in guest memory. Power-of-two textures
are swizzled (Morton order, one level after another); textures with a Size
field are linear, with a pitch, and are addressed with texel coordinates.
DXT textures are stored as plain 4x4 blocks. Everything except DXT is
converted to 32-bit BGRA on upload.

A cached texture stays valid until any page it was read from is written;
memory_watch.c detects that by write-protecting the pages.
*/

#include "vita_xgpu.h"
#include "vita_gxm.h"
#include "port_config.h"
#include "../../linux/game/cache_file_formats.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83f1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83f2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83f3
#endif

/* ---------- formats */

enum texel_kind
{
	_texel_unknown,
	_texel_a8r8g8b8, _texel_x8r8g8b8, _texel_r5g6b5, _texel_a1r5g5b5, _texel_x1r5g5b5, _texel_a4r4g4b4,
	_texel_l8, _texel_al8, _texel_a8, _texel_a8l8, _texel_p8, _texel_g8b8, _texel_r8b8, _texel_r6g5b5,
	_texel_l16, _texel_v16u16, _texel_a8b8g8r8, _texel_b8g8r8a8, _texel_r8g8b8a8, _texel_r5g5b5a1,
	_texel_r4g4b4a4, _texel_yuy2, _texel_uyvy, _texel_d24s8, _texel_d16,
	_texel_dxt1, _texel_dxt3, _texel_dxt5,
};

struct format_information
{
	unsigned char kind;
	unsigned char bytes; /* per texel; per 4x4 block for DXT */
	unsigned char linear;
};

static struct format_information format_information(DWORD format)
{
	static const struct format_information table[0x42] =
	{
		[0x00] = { _texel_l8, 1, 0 },
		[0x01] = { _texel_al8, 1, 0 },
		[0x02] = { _texel_a1r5g5b5, 2, 0 },
		[0x03] = { _texel_x1r5g5b5, 2, 0 },
		[0x04] = { _texel_a4r4g4b4, 2, 0 },
		[0x05] = { _texel_r5g6b5, 2, 0 },
		[0x06] = { _texel_a8r8g8b8, 4, 0 },
		[0x07] = { _texel_x8r8g8b8, 4, 0 },
		[0x0b] = { _texel_p8, 1, 0 },
		[0x0c] = { _texel_dxt1, 8, 0 },
		[0x0e] = { _texel_dxt3, 16, 0 },
		[0x0f] = { _texel_dxt5, 16, 0 },
		[0x10] = { _texel_a1r5g5b5, 2, 1 },
		[0x11] = { _texel_r5g6b5, 2, 1 },
		[0x12] = { _texel_a8r8g8b8, 4, 1 },
		[0x13] = { _texel_l8, 1, 1 },
		[0x16] = { _texel_r8b8, 2, 1 },
		[0x17] = { _texel_g8b8, 2, 1 },
		[0x19] = { _texel_a8, 1, 0 },
		[0x1a] = { _texel_a8l8, 2, 0 },
		[0x1b] = { _texel_al8, 1, 1 },
		[0x1c] = { _texel_x1r5g5b5, 2, 1 },
		[0x1d] = { _texel_a4r4g4b4, 2, 1 },
		[0x1e] = { _texel_x8r8g8b8, 4, 1 },
		[0x1f] = { _texel_a8, 1, 1 },
		[0x20] = { _texel_a8l8, 2, 1 },
		[0x24] = { _texel_yuy2, 2, 1 },
		[0x25] = { _texel_uyvy, 2, 1 },
		[0x27] = { _texel_r6g5b5, 2, 0 },
		[0x28] = { _texel_g8b8, 2, 0 },
		[0x29] = { _texel_r8b8, 2, 0 },
		[0x2a] = { _texel_d24s8, 4, 0 },
		[0x2b] = { _texel_d24s8, 4, 0 },
		[0x2c] = { _texel_d16, 2, 0 },
		[0x2d] = { _texel_d16, 2, 0 },
		[0x2e] = { _texel_d24s8, 4, 1 },
		[0x2f] = { _texel_d24s8, 4, 1 },
		[0x30] = { _texel_d16, 2, 1 },
		[0x31] = { _texel_d16, 2, 1 },
		[0x32] = { _texel_l16, 2, 0 },
		[0x33] = { _texel_v16u16, 4, 0 },
		[0x35] = { _texel_l16, 2, 1 },
		[0x36] = { _texel_v16u16, 4, 1 },
		[0x37] = { _texel_r6g5b5, 2, 1 },
		[0x38] = { _texel_r5g5b5a1, 2, 0 },
		[0x39] = { _texel_r4g4b4a4, 2, 0 },
		[0x3a] = { _texel_a8b8g8r8, 4, 0 },
		[0x3b] = { _texel_b8g8r8a8, 4, 0 },
		[0x3c] = { _texel_r8g8b8a8, 4, 0 },
		[0x3d] = { _texel_r5g5b5a1, 2, 1 },
		[0x3e] = { _texel_r4g4b4a4, 2, 1 },
		[0x3f] = { _texel_a8b8g8r8, 4, 1 },
		[0x40] = { _texel_b8g8r8a8, 4, 1 },
		[0x41] = { _texel_r8g8b8a8, 4, 1 },
	};
	struct format_information unknown = { _texel_a8r8g8b8, 4, 0 };

	if (format < sizeof(table) / sizeof(table[0]) && table[format].kind != _texel_unknown)
		return table[format];
	return unknown;
}

static BOOL kind_compressed(unsigned char kind)
{
	return kind == _texel_dxt1 || kind == _texel_dxt3 || kind == _texel_dxt5;
}

/* ---------- geometry of a texture in memory */

static unsigned long floor_log2(unsigned long value)
{
	unsigned long result = 0;

	while (value > 1)
	{
		value >>= 1;
		result++;
	}
	return result;
}

static unsigned long level_dimension(unsigned long base, unsigned long level)
{
	unsigned long value = base >> level;

	return value ? value : 1;
}

void xgpu_texture_describe(DWORD format_word, DWORD size_word, struct xgpu_texture_description *description)
{
	struct format_information information;

	memset(description, 0, sizeof(*description));
	description->format = (format_word & D3DFORMAT_FORMAT_MASK) >> D3DFORMAT_FORMAT_SHIFT;
	information = format_information(description->format);
	description->cube_map = (format_word & D3DFORMAT_CUBEMAP) != 0;
	description->compressed = kind_compressed(information.kind);
	if (size_word)
	{
		description->width = (size_word & D3DSIZE_WIDTH_MASK) + 1;
		description->height = ((size_word & D3DSIZE_HEIGHT_MASK) >> D3DSIZE_HEIGHT_SHIFT) + 1;
		description->depth = 1;
		description->levels = 1;
		description->pitch = (((size_word & D3DSIZE_PITCH_MASK) >> D3DSIZE_PITCH_SHIFT) + 1) * D3DTEXTURE_PITCH_ALIGNMENT;
		description->linear = TRUE;
	}
	else
	{
		description->width = 1UL << ((format_word & D3DFORMAT_USIZE_MASK) >> D3DFORMAT_USIZE_SHIFT);
		description->height = 1UL << ((format_word & D3DFORMAT_VSIZE_MASK) >> D3DFORMAT_VSIZE_SHIFT);
		description->depth = 1UL << ((format_word & D3DFORMAT_PSIZE_MASK) >> D3DFORMAT_PSIZE_SHIFT);
		description->levels = (format_word & D3DFORMAT_MIPMAP_MASK) >> D3DFORMAT_MIPMAP_SHIFT;
		if (!description->levels)
			description->levels = 1;
		description->linear = information.linear;
		description->pitch = description->width * information.bytes;
	}
	if ((format_word & D3DFORMAT_DIMENSION_MASK) >> D3DFORMAT_DIMENSION_SHIFT != 3)
		description->depth = 1;
}

static unsigned long level_bytes(const struct xgpu_texture_description *description, unsigned long level)
{
	struct format_information information = format_information(description->format);
	unsigned long width = level_dimension(description->width, level);
	unsigned long height = level_dimension(description->height, level);
	unsigned long depth = level_dimension(description->depth, level);

	if (description->compressed)
		return ((width + 3) / 4) * ((height + 3) / 4) * information.bytes * depth;
	if (description->linear)
		return description->pitch * height;
	return width * height * depth * information.bytes;
}

unsigned long xgpu_texture_level_offset(const struct xgpu_texture_description *description, unsigned long level)
{
	unsigned long offset = 0;
	unsigned long index;

	for (index = 0; index < level && index < description->levels; index++)
		offset += level_bytes(description, index);
	return offset;
}

unsigned long xgpu_texture_face_size(const struct xgpu_texture_description *description)
{
	unsigned long size = xgpu_texture_level_offset(description, description->levels);

	if (description->cube_map)
		size = (size + D3DTEXTURE_CUBEFACE_ALIGNMENT - 1) & ~(unsigned long)(D3DTEXTURE_CUBEFACE_ALIGNMENT - 1);
	return size;
}

unsigned long xgpu_texture_level_pitch(const struct xgpu_texture_description *description, unsigned long level)
{
	struct format_information information = format_information(description->format);

	if (description->linear)
		return description->pitch;
	if (description->compressed)
		return ((level_dimension(description->width, level) + 3) / 4) * information.bytes;
	return level_dimension(description->width, level) * information.bytes;
}

/* ---------- swizzling */

struct swizzle_masks
{
	unsigned long x, y, z;
};

static struct swizzle_masks swizzle_masks(unsigned long width, unsigned long height, unsigned long depth)
{
	struct swizzle_masks masks = { 0, 0, 0 };
	unsigned long bit = 1, mask_bit = 1;
	BOOL done;

	/* bits of x, y and z alternate until each dimension runs out */
	do
	{
		done = TRUE;
		if (bit < width)
		{
			masks.x |= mask_bit;
			mask_bit <<= 1;
			done = FALSE;
		}
		if (bit < height)
		{
			masks.y |= mask_bit;
			mask_bit <<= 1;
			done = FALSE;
		}
		if (bit < depth)
		{
			masks.z |= mask_bit;
			mask_bit <<= 1;
			done = FALSE;
		}
		bit <<= 1;
	} while (!done);
	return masks;
}

static unsigned long spread(unsigned long mask, unsigned long value)
{
	unsigned long result = 0, bit = 1;

	while (value && bit)
	{
		if (mask & bit)
		{
			if (value & 1)
				result |= bit;
			value >>= 1;
		}
		bit <<= 1;
	}
	return result;
}

/* ---------- texel conversion */

static unsigned long expand5(unsigned long v) { return (v << 3) | (v >> 2); }
static unsigned long expand6(unsigned long v) { return (v << 2) | (v >> 4); }
static unsigned long expand4(unsigned long v) { return v * 0x11; }

static unsigned long argb(unsigned long a, unsigned long r, unsigned long g, unsigned long b)
{
	return (a << 24) | (r << 16) | (g << 8) | b;
}

static unsigned char clamp_byte(long value)
{
	return (unsigned char)(value < 0 ? 0 : value > 255 ? 255 : value);
}

static unsigned long yuv_to_argb(long y, long u, long v)
{
	long c = y - 16, d = u - 128, e = v - 128;

	return argb(255, clamp_byte((298 * c + 409 * e + 128) >> 8),
		clamp_byte((298 * c - 100 * d - 208 * e + 128) >> 8),
		clamp_byte((298 * c + 516 * d + 128) >> 8));
}

static unsigned long convert_texel(unsigned char kind, const unsigned char *source, const D3DCOLOR *palette,
	unsigned long x, const unsigned char *row)
{
	unsigned long v16 = source[0] | ((unsigned long)source[1] << 8);
	unsigned long v32 = v16 | ((unsigned long)source[2] << 16) | ((unsigned long)source[3] << 24);

	switch (kind)
	{
	case _texel_a8r8g8b8: return v32;
	case _texel_x8r8g8b8: return v32 | 0xff000000UL;
	case _texel_r5g6b5: return argb(255, expand5(v16 >> 11), expand6((v16 >> 5) & 0x3f), expand5(v16 & 0x1f));
	case _texel_a1r5g5b5: return argb((v16 & 0x8000) ? 255 : 0, expand5((v16 >> 10) & 0x1f), expand5((v16 >> 5) & 0x1f), expand5(v16 & 0x1f));
	case _texel_x1r5g5b5: return argb(255, expand5((v16 >> 10) & 0x1f), expand5((v16 >> 5) & 0x1f), expand5(v16 & 0x1f));
	case _texel_a4r4g4b4: return argb(expand4(v16 >> 12), expand4((v16 >> 8) & 0xf), expand4((v16 >> 4) & 0xf), expand4(v16 & 0xf));
	case _texel_l8: return argb(255, source[0], source[0], source[0]);
	case _texel_al8: return argb(source[0], source[0], source[0], source[0]);
	case _texel_a8: return argb(source[0], 255, 255, 255);
	case _texel_a8l8: return argb(source[1], source[0], source[0], source[0]);
	case _texel_p8: return palette ? palette[source[0]] : argb(255, source[0], source[0], source[0]);
	/* V8U8 shares this format: U (the low byte) reads as red, V as green */
	case _texel_g8b8: return argb(255, source[0], source[1], 0);
	case _texel_r8b8: return argb(255, source[1], 0, source[0]);
	case _texel_r6g5b5: return argb(255, expand6(v16 >> 10), expand5((v16 >> 5) & 0x1f), expand5(v16 & 0x1f));
	case _texel_l16: return argb(255, source[1], source[1], source[1]);
	case _texel_v16u16: return argb(255, source[1], source[3], 0);
	case _texel_a8b8g8r8: return argb(source[3], source[0], source[1], source[2]);
	case _texel_b8g8r8a8: return argb(source[0], source[1], source[2], source[3]);
	case _texel_r8g8b8a8: return argb(source[0], source[3], source[2], source[1]);
	case _texel_r5g5b5a1: return argb((v16 & 1) ? 255 : 0, expand5(v16 >> 11), expand5((v16 >> 6) & 0x1f), expand5((v16 >> 1) & 0x1f));
	case _texel_r4g4b4a4: return argb(expand4(v16 & 0xf), expand4(v16 >> 12), expand4((v16 >> 8) & 0xf), expand4((v16 >> 4) & 0xf));
	case _texel_yuy2:
	{
		const unsigned char *pair = row + (x & ~1UL) * 2;

		return yuv_to_argb(pair[(x & 1) ? 2 : 0], pair[1], pair[3]);
	}
	case _texel_uyvy:
	{
		const unsigned char *pair = row + (x & ~1UL) * 2;

		return yuv_to_argb(pair[(x & 1) ? 3 : 1], pair[0], pair[2]);
	}
	case _texel_d24s8: return argb(255, source[3], source[3], source[3]);
	case _texel_d16: return argb(255, source[1], source[1], source[1]);
	default: return v32;
	}
}

/* one level (or 3D slice set) of an uncompressed texture into BGRA */
static void decode_level(const struct xgpu_texture_description *description, unsigned long level,
	const unsigned char *source, const D3DCOLOR *palette, unsigned long *destination)
{
	struct format_information information = format_information(description->format);
	unsigned long width = level_dimension(description->width, level);
	unsigned long height = level_dimension(description->height, level);
	unsigned long depth = level_dimension(description->depth, level);
	unsigned long x, y, z;

	static int fast_rows = -1;

	if (fast_rows < 0)
	{
		const char *setting = getenv("HALO_TEX_FAST_ROWS");
		fast_rows = !setting || atoi(setting) != 0;
	}
	if (fast_rows && description->linear && (information.kind == _texel_a8r8g8b8 || information.kind == _texel_x8r8g8b8))
	{
		/* (32-bit ARGB rows are GXM's byte order already: copied, the X
		formats with their alpha set - the movie's frame each frame) */
		unsigned long opaque = information.kind == _texel_x8r8g8b8 ? 0xff000000UL : 0;

		for (y = 0; y < height; y++)
		{
			const uint32_t *row = (const uint32_t *)(source + y * description->pitch);
			unsigned long *out = destination + y * width;

			if (!opaque)
				memcpy(out, row, width * 4);
			else
				for (x = 0; x < width; x++)
					out[x] = row[x] | opaque;
		}
		return;
	}
	if (description->linear)
	{
		for (y = 0; y < height; y++)
		{
			const unsigned char *row = source + y * description->pitch;

			for (x = 0; x < width; x++)
				destination[y * width + x] = convert_texel(information.kind, row + x * information.bytes, palette, x, row);
		}
		return;
	}
	{
		struct swizzle_masks masks = swizzle_masks(width, height, depth);
		unsigned long *x_offsets = malloc(width * sizeof(unsigned long));

		for (x = 0; x < width; x++)
			x_offsets[x] = spread(masks.x, x);
		for (z = 0; z < depth; z++)
		{
			unsigned long z_offset = spread(masks.z, z);

			for (y = 0; y < height; y++)
			{
				unsigned long y_offset = spread(masks.y, y) | z_offset;

				for (x = 0; x < width; x++)
				{
					const unsigned char *texel = source + (x_offsets[x] | y_offset) * information.bytes;

					destination[(z * height + y) * width + x] = convert_texel(information.kind, texel, palette, x, texel);
				}
			}
		}
		free(x_offsets);
	}
}

/* ---------- DXT decoding, for blocks GXM cannot take as they are */

static unsigned long color565(unsigned long value)
{
	return argb(255, expand5(value >> 11), expand6((value >> 5) & 0x3f), expand5(value & 0x1f));
}

static unsigned long mix(unsigned long a, unsigned long b, unsigned long weight_a, unsigned long weight_b,
	unsigned long divisor)
{
	unsigned long result = 0;
	int shift;

	for (shift = 0; shift < 24; shift += 8)
	{
		unsigned long channel = (((a >> shift) & 0xff) * weight_a + ((b >> shift) & 0xff) * weight_b) / divisor;

		result |= channel << shift;
	}
	return result | 0xff000000UL;
}

/* one 4x4 block's colors; dxt1 selects the punch-through alpha mode */
static void dxt_color_block(const unsigned char *block, BOOL dxt1, unsigned long colors[16])
{
	unsigned long c0 = block[0] | (block[1] << 8);
	unsigned long c1 = block[2] | (block[3] << 8);
	unsigned long palette[4];
	unsigned long bits = block[4] | (block[5] << 8) | ((unsigned long)block[6] << 16) | ((unsigned long)block[7] << 24);
	int index;

	palette[0] = color565(c0);
	palette[1] = color565(c1);
	if (c0 > c1 || !dxt1)
	{
		palette[2] = mix(palette[0], palette[1], 2, 1, 3);
		palette[3] = mix(palette[0], palette[1], 1, 2, 3);
	}
	else
	{
		palette[2] = mix(palette[0], palette[1], 1, 1, 2);
		palette[3] = 0;
	}
	for (index = 0; index < 16; index++)
		colors[index] = palette[(bits >> (index * 2)) & 3];
}

/* one 4x4 block's 16 texels as ARGB words, row by row */
static void dxt_block_texels(unsigned char kind, const unsigned char *block, unsigned long texels[16])
{
	unsigned long colors[16];
	unsigned long alpha[16];
	int index;

	if (kind == _texel_dxt1)
	{
		dxt_color_block(block, TRUE, colors);
		for (index = 0; index < 16; index++)
			alpha[index] = colors[index] >> 24;
	}
	else
	{
		dxt_color_block(block + 8, FALSE, colors);
		if (kind == _texel_dxt3)
		{
			for (index = 0; index < 16; index++)
				alpha[index] = expand4((block[index / 2] >> ((index & 1) * 4)) & 0xf);
		}
		else
		{
			unsigned long a0 = block[0], a1 = block[1], values[8];
			unsigned long long bits = 0;
			int bit;

			for (bit = 0; bit < 6; bit++)
				bits |= (unsigned long long)block[2 + bit] << (bit * 8);
			values[0] = a0;
			values[1] = a1;
			if (a0 > a1)
			{
				for (index = 2; index < 8; index++)
					values[index] = ((8 - index) * a0 + (index - 1) * a1) / 7;
			}
			else
			{
				for (index = 2; index < 6; index++)
					values[index] = ((6 - index) * a0 + (index - 1) * a1) / 5;
				values[6] = 0;
				values[7] = 255;
			}
			for (index = 0; index < 16; index++)
				alpha[index] = values[(bits >> (index * 3)) & 7];
		}
	}
	for (index = 0; index < 16; index++)
		texels[index] = (colors[index] & 0x00ffffffUL) | (alpha[index] << 24);
}

static void dxt_decode_level(unsigned char kind, const unsigned char *source, unsigned long width, unsigned long height,
	unsigned long depth, unsigned long *destination)
{
	unsigned long blocks_x = (width + 3) / 4, blocks_y = (height + 3) / 4;
	unsigned long block_bytes = kind == _texel_dxt1 ? 8 : 16;
	unsigned long z, bx, by, x, y;

	for (z = 0; z < depth; z++)
	{
		for (by = 0; by < blocks_y; by++)
		{
			for (bx = 0; bx < blocks_x; bx++)
			{
				const unsigned char *block = source + ((z * blocks_y + by) * blocks_x + bx) * block_bytes;
				unsigned long texels[16];

				dxt_block_texels(kind, block, texels);
				for (y = 0; y < 4; y++)
				{
					for (x = 0; x < 4; x++)
					{
						unsigned long px = bx * 4 + x, py = by * 4 + y;

						if (px < width && py < height)
							destination[(z * height + py) * width + px] = texels[y * 4 + x];
					}
				}
			}
		}
	}
}


/* ---------- GXM layouts */

/* the texel index i of GXM's twiddled layout: Y takes the even bits and X
the odd ones up to the shorter side, and the longer axis continues above
(as Xita found on hardware: the transpose of the NV2A's order) */
static unsigned long compact_bits(unsigned long v)
{
	v &= 0x55555555UL;
	v = (v | (v >> 1)) & 0x33333333UL;
	v = (v | (v >> 2)) & 0x0f0f0f0fUL;
	v = (v | (v >> 4)) & 0x00ff00ffUL;
	return (v | (v >> 8)) & 0x0000ffffUL;
}

static void gxm_twiddle_position(unsigned long width, unsigned long height, unsigned long index,
	unsigned long *x, unsigned long *y)
{
	unsigned long shorter = width < height ? width : height;
	unsigned long bits = floor_log2(shorter);
	unsigned long mask = shorter - 1;
	unsigned long upper = (index >> (bits * 2)) << bits;

	*x = compact_bits(index >> 1) & mask;
	*y = compact_bits(index) & mask;
	if (width >= height)
		*x |= upper;
	else
		*y |= upper;
}

/* one level of 32-bit texels, rows to GXM's twiddled order, written in
order; a 4x4 block at a time where both sides are 4 or more (the low four
bits of an index pick the texel in its block: y, x, y, x) */
static void twiddle_level(unsigned long *destination, const unsigned long *source, unsigned long width,
	unsigned long height)
{
	static const unsigned char block_x[16] = { 0, 0, 1, 1, 0, 0, 1, 1, 2, 2, 3, 3, 2, 2, 3, 3 };
	static const unsigned char block_y[16] = { 0, 1, 0, 1, 2, 3, 2, 3, 0, 1, 0, 1, 2, 3, 2, 3 };
	unsigned long count = width * height, index, x, y, k;

	if (width >= 4 && height >= 4)
	{
		for (index = 0; index < count; index += 16)
		{
			const unsigned long *block;

			gxm_twiddle_position(width, height, index, &x, &y);
			block = source + y * width + x;
			for (k = 0; k < 16; k++)
				destination[index + k] = block[block_y[k] * width + block_x[k]];
		}
		return;
	}
	for (index = 0; index < count; index++)
	{
		gxm_twiddle_position(width, height, index, &x, &y);
		destination[index] = source[y * width + x];
	}
}

/* one level of DXT blocks, NV2A row order to GXM twiddled order */
static void reorder_blocks(const unsigned char *source, unsigned char *destination, unsigned long width,
	unsigned long height, unsigned long block_bytes)
{
	unsigned long blocks_x = width / 4, blocks_y = height / 4, count = blocks_x * blocks_y, index;

	for (index = 0; index < count; index++)
	{
		unsigned long x, y;

		gxm_twiddle_position(blocks_x, blocks_y, index, &x, &y);
		memcpy(destination + index * block_bytes, source + (y * blocks_x + x) * block_bytes, block_bytes);
	}
}

static BOOL power_of_two(unsigned long value)
{
	return value && !(value & (value - 1));
}

#define LINEAR_ROW(texels) (((texels) + 7) & ~7UL)

/* ---------- cache */

struct texture_entry
{
	struct texture_entry *next;
	DWORD data, format_word, size_word;
	unsigned long palette_hash;
	struct vgxm_texture texture;
	BOOL valid;
	struct xgpu_texture_description description;
	unsigned long address, size;
	unsigned long generation;
	unsigned long last_used_frame;
	/* the pool generation the texels were decoded into */
	unsigned long pool_serial;
	/* the memory watch serial at the last page check: while it stands, no
	page has been written and the check can be skipped */
	unsigned long checked_serial;
	/* a texture outside the map's tag data (the game fills it at run time:
	the text glyph cache, the loading screen...): the Vita sees no writes
	by game code, so its texels are checksummed once a frame instead */
	int dynamic;
	unsigned long checksum, checksum_frame;
	/* the pool memory the texels were decoded into, reused when a dynamic
	texture changes (the movie's frame each frame would otherwise fill the
	pool in seconds) */
	void *memory;
	unsigned long memory_size;
	/* (the decoder, below) its decode in the background, while a lower
	level of it is drawn (texture holds that level meanwhile) */
	struct decode_job *job;
};

#define TEXTURE_BUCKET_COUNT 4096
#define MAXIMUM_PALETTE_VARIANTS 8

static struct texture_entry *texture_buckets[TEXTURE_BUCKET_COUNT];
static unsigned long texture_frame;
/* changes when the pool is emptied, which invalidates every entry */
static unsigned long pool_serial = 1;

static unsigned long bucket_index(DWORD data, DWORD format_word, DWORD size_word)
{
	return ((data >> 7) ^ (format_word * 2654435761UL) ^ size_word) % TEXTURE_BUCKET_COUNT;
}

static unsigned long palette_hash(const D3DCOLOR *palette)
{
	unsigned long hash = 2166136261UL, index;

	if (!palette)
		return 0;
	for (index = 0; index < 256; index++)
		hash = (hash ^ palette[index]) * 16777619UL;
	return hash ? hash : 1;
}

/* (texture_build's allocation: the entry's own memory again when it is
being rebuilt in place, and what was allocated, for the entry) */
static void *pool_reuse;
static unsigned long pool_reuse_size;
static void *pool_last;
static unsigned long pool_last_size;

/* (the texture report, halo_texture_stats_report) textures decoded and
their bytes in the pool, and the times the pool was emptied and a segment
of it recycled */
static unsigned long stats_builds, stats_build_bytes, stats_pool_resets, stats_pool_recycles;

/* (the pool's segments: vgxm_memory.h) a full pool frees one at a time,
this many at most for one texture before it is emptied */
#define POOL_RECYCLE_ATTEMPTS 15

unsigned long vita_texture_cache_forget(const void *base, unsigned long size);
void vita_texture_decodes_quiesce(void);
/* (counts the pool's recycles and resets: an allocation that saw one may
have had the memory allocated before it forgotten) */
static unsigned long pool_events;

/* HALO_TEX_RECYCLE=0: a full pool is emptied at once, as before the ring */
static int pool_recycling(void)
{
	static int enabled = -1;

	if (enabled < 0)
		enabled = !getenv("HALO_TEX_RECYCLE") || atoi(getenv("HALO_TEX_RECYCLE")) != 0;
	return enabled;
}

static void *pool_alloc(unsigned long size)
{
	void *memory;

	if (pool_reuse && size <= pool_reuse_size)
	{
		memory = pool_reuse;
		pool_reuse = NULL;
		pool_last = memory;
		pool_last_size = pool_reuse_size;
		return memory;
	}
	memory = vgxm_pool_alloc(size, 128);
	if (!memory)
	{
		/* (the background decodes stopped first: none may write into
		memory forgotten here) */
		vita_texture_decodes_quiesce();
		pool_events++;
	}
	if (!memory && pool_recycling())
	{
		/* full: the next segment's textures forgotten (decoded again when
		next used) and its memory decoded into, a segment at a time round
		the pool (vgxm_memory.h, the ring); a texture bigger than the
		segments, or a pool that will not take it so, empties it */
		unsigned long attempts;

		for (attempts = 0; !memory && attempts <= POOL_RECYCLE_ATTEMPTS; attempts++)
		{
			void *base;
			unsigned long bytes;

			if (!vgxm_pool_recycle(&base, &bytes))
				break;
			if (bytes)
				vita_texture_cache_forget(base, bytes);
			stats_pool_recycles++;
			memory = vgxm_pool_alloc(size, 128);
		}
	}
	if (!memory)
	{
		/* full: start again, and every texture is decoded again as it is used */
		platform_log("texture pool full (%lu KB): emptied", vgxm_pool_used() / 1024);
		vgxm_pool_reset();
		pool_serial++;
		stats_pool_resets++;
		memory = vgxm_pool_alloc(size, 128);
	}
	if (memory)
	{
		stats_builds++;
		stats_build_bytes += size;
	}
	pool_last = memory;
	pool_last_size = size;
	return memory;
}

/* HALO_SWIZZLED_TEXTURES=0: the power-of-two BGRA textures as linear
rows, as before (texture_build) */
static int swizzled_textures(void)
{
	static int enabled = -1;

	if (enabled < 0)
	{
		const char *setting = getenv("HALO_SWIZZLED_TEXTURES");

		enabled = !setting || atoi(setting) != 0;
	}
	return enabled;
}

static unsigned char custom_edition_texels_order(unsigned long address);
static void custom_edition_texels_reorder(unsigned long *texels, unsigned long count, unsigned char order);

/* A texture's decoding (texture_decode): what it reads, the memory it
decodes into and the GPU layout it leaves there. Decoded on the worker
(texture_build), the memory is taken from the pool as it goes; decoded in
the background (the decoder, below), the worker has taken it beforehand
(preallocated) and makes the control words afterwards (texture_initialize),
so nothing but the pool memory given to it is touched off the worker. */
struct texture_build
{
	const struct xgpu_texture_description *description;
	const unsigned char *base;
	const D3DCOLOR *palette;
	unsigned char channel_order;
	/* (HALO_TEXTURE_DUMP_DIR) the Xbox texture's words and texel bytes */
	DWORD format_word, size_word;
	unsigned long bytes;
	/* the memory to decode into, the worker's (0: from the pool) */
	void *preallocated;
	unsigned long preallocated_size;
	/* measuring: no decode, the size it would take in *measured */
	int measure;
	unsigned long measured;
	/* what was decoded */
	void *memory;
	unsigned long memory_size;
	unsigned long format, layout, width, height, levels;
};

static void *build_alloc(struct texture_build *build, unsigned long size)
{
	void *memory;

	if (build->measure)
	{
		build->measured = size;
		return NULL;
	}
	if (build->preallocated)
	{
		memory = size <= build->preallocated_size ? build->preallocated : NULL;
		build->memory = memory;
		build->memory_size = build->preallocated_size;
		return memory;
	}
	memory = pool_alloc(size);
	build->memory = memory;
	build->memory_size = memory ? pool_last_size : 0;
	return memory;
}

static BOOL build_layout(struct texture_build *build, void *memory, unsigned long format, unsigned long layout,
	unsigned long width, unsigned long height, unsigned long levels)
{
	build->memory = memory;
	build->format = format;
	build->layout = layout;
	build->width = width;
	build->height = height;
	build->levels = levels;
	return memory != NULL;
}

/* The texture's GXM control words over what was decoded.
HALO_TEXTURE_DUMP_DIR names an existing directory. Capture each source /
channel-order / GPU-layout combination once, without changing the upload.
The .source bytes are also what desktop GL passes to glCompressedTexImage
for DXT textures, split at the source mip offsets listed in the manifest. */
static int texture_initialize(struct vgxm_texture *texture, const struct texture_build *build)
{
	int result = vgxm_texture_initialize(texture, build->memory, build->format, build->layout, build->width,
		build->height, build->levels);
	const char *directory = getenv("HALO_TEXTURE_DUMP_DIR");
	const struct xgpu_texture_description *description = build->description;
	const unsigned char *source = build->base;
	unsigned long index, hash = 2166136261UL;
	unsigned long format = build->format, layout = build->layout, width = build->width, height = build->height;
	unsigned long levels = build->levels;
	unsigned char order;
	char stem[400], path[512];
	FILE *file;

	if (result || !directory || !*directory || !build->memory)
		return result;
	for (index = 0; index < build->bytes; index++)
		hash = (hash ^ source[index]) * 16777619UL;
	order = build->channel_order;
	snprintf(stem, sizeof(stem), "%s/%08lx-%08lx-%08lx-o%u-g%lu-%lu-%lux%lu-%lu",
		directory, hash, (unsigned long)build->format_word, (unsigned long)build->size_word,
		(unsigned)order, format, layout, width, height, levels);
	snprintf(path, sizeof(path), "%s.txt", stem);
	file = fopen(path, "r");
	if (file)
	{
		fclose(file);
		return result;
	}
	file = fopen(path, "w");
	if (!file)
		return result;
	fprintf(file, "source width=%lu height=%lu depth=%lu format=%lx levels=%lu linear=%d pitch=%lu cube=%d bytes=%lu order=%u\n",
		description->width, description->height, description->depth, (unsigned long)description->format,
		description->levels, description->linear, description->pitch, description->cube_map, build->bytes, (unsigned)order);
	for (index = 0; index < description->levels; index++)
		fprintf(file, "source_mip level=%lu offset=%lu bytes=%lu pitch=%lu\n", index,
			xgpu_texture_level_offset(description, index), level_bytes(description, index),
			xgpu_texture_level_pitch(description, index));
	fprintf(file, "source_face_stride=%lu\n", xgpu_texture_face_size(description));
	fprintf(file, "gpu width=%lu height=%lu format=%lu layout=%lu levels=%lu allocation_bytes=%lu\n",
		width, height, format, layout, levels, build->memory_size);
	fprintf(file, "control=%08lx %08lx %08lx %08lx\n",
		(unsigned long)texture->control[0], (unsigned long)texture->control[1],
		(unsigned long)texture->control[2], (unsigned long)texture->control[3]);
	/* Cube storage includes a full mip chain and aligned face padding;
	2D DXT storage is whole blocks in Y-first Morton order at each mip. */
	{
		unsigned long offset = 0, count = layout == _vgxm_texture_cube ? floor_log2(width) + 1 : levels;
		for (index = 0; index < count; index++)
		{
			unsigned long w = level_dimension(width, index), h = level_dimension(height, index);
			unsigned long pitch = format == _vgxm_texture_bgra8 ?
				(layout == _vgxm_texture_linear ? LINEAR_ROW(w) : w) * 4 :
				((w + 3) / 4) * (format == _vgxm_texture_dxt1 ? 8 : 16);
			unsigned long bytes = pitch * (format == _vgxm_texture_bgra8 ? h : (h + 3) / 4);
			fprintf(file, "gpu_mip level=%lu offset=%lu bytes=%lu row_bytes=%lu\n", index, offset, bytes, pitch);
			offset += bytes;
		}
		if (layout == _vgxm_texture_cube)
			fprintf(file, "gpu_face_stride=%lu\n", width >= 16 ? (offset + 2047) & ~2047UL : offset);
	}
	fclose(file);
	snprintf(path, sizeof(path), "%s.source", stem);
	if ((file = fopen(path, "wb")) != NULL)
	{
		fwrite(source, 1, build->bytes, file);
		fclose(file);
	}
	snprintf(path, sizeof(path), "%s.gpu", stem);
	if ((file = fopen(path, "wb")) != NULL)
	{
		fwrite(build->memory, 1, build->memory_size, file);
		fclose(file);
	}
	return result;
}

static BOOL texture_decode(struct texture_build *build)
{
	const struct xgpu_texture_description *description = build->description;
	const unsigned char *base = build->base;
	const D3DCOLOR *palette = build->palette;
	struct format_information information = format_information(description->format);
	unsigned long width = description->width, height = description->height;
	unsigned long levels = description->levels, level;
	unsigned long *scratch;
	unsigned char *memory;
	unsigned long size;
	/* (a Custom Edition map's multipurpose maps and HUD meters: decoded and
	reordered, below) */
	unsigned char channel_order = build->channel_order;

	if (description->cube_map)
	{
		/* six square faces, each with every level the Xbox texture holds
		decoded and twiddled. The Vita reads a cube with mips (any mip count
		but "none") face by face, each face laid out with every level down
		to 1x1 and starting 2 KB-aligned once a face is 16x16 or more
		(Vita3K's renderer, texture/cache.cpp): the six faces packed at
		level 0 only were read at the wrong offsets - the reflections of the
		menu ship's hull and the Pelican's glass showed rainbow noise. The
		levels the Xbox texture lacks only fill the layout (each 2x2 average
		of the one above): the mip count stops at the Xbox's, so they are
		never sampled */
		unsigned long face_size = xgpu_texture_face_size(description), face;
		unsigned long chain = 0, face_stride = 0, level_count = 0, original_levels;

		static int cube_debug = -1;

		if (cube_debug < 0)
		{
			/* (debug) HALO_CUBE_DEBUG=1: one level; 2: every texel grey */
			const char *setting = getenv("HALO_CUBE_DEBUG");
			cube_debug = setting ? atoi(setting) : 0;
		}
		if (width != height || !power_of_two(width))
			return FALSE;
		for (level = 0; (width >> level) > 0; level++)
		{
			face_stride += (width >> level) * (width >> level) * 4;
			level_count++;
		}
		original_levels = levels < 1 ? 1 : levels > level_count ? level_count : levels;
		chain = face_stride;
		if (width >= 16)
			face_stride = (face_stride + 2047) & ~2047UL;
		memory = build_alloc(build, face_stride * 6);
		scratch = malloc(width * height * 4);
		if (!memory || !scratch)
		{
			free(scratch);
			return FALSE;
		}
		for (face = 0; face < 6; face++)
		{
			unsigned char *face_memory = memory + face * face_stride;
			unsigned long offset = 0, size_at = width;

			for (level = 0; level < level_count; level++, size_at /= 2)
			{
				unsigned long *destination = (unsigned long *)(face_memory + offset), index;

				if (level < original_levels)
				{
					const unsigned char *source = base + face * face_size + xgpu_texture_level_offset(description, level);

					if (description->compressed)
						dxt_decode_level(information.kind, source, size_at, size_at, 1, scratch);
					else
						decode_level(description, level, source, palette, scratch);
				}
				else
				{
					/* (layout filler: each 2x2 block of the level above,
					averaged per channel, in place) */
					unsigned long above = size_at * 2, x, y;

					for (y = 0; y < size_at; y++)
						for (x = 0; x < size_at; x++)
						{
							unsigned long a = scratch[(2 * y) * above + 2 * x], b = scratch[(2 * y) * above + 2 * x + 1];
							unsigned long c = scratch[(2 * y + 1) * above + 2 * x], d = scratch[(2 * y + 1) * above + 2 * x + 1];
							unsigned long result = 0, shift;

							for (shift = 0; shift < 32; shift += 8)
								result |= ((((a >> shift) & 0xff) + ((b >> shift) & 0xff) + ((c >> shift) & 0xff) +
									((d >> shift) & 0xff) + 2) / 4) << shift;
							scratch[y * size_at + x] = result;
						}
				}
				if (cube_debug == 2)
					for (index = 0; index < size_at * size_at; index++)
						scratch[index] = 0xff808080u;
				for (index = 0; index < size_at * size_at; index++)
				{
					unsigned long x, y;

					gxm_twiddle_position(size_at, size_at, index, &x, &y);
					destination[index] = scratch[y * size_at + x];
				}
				offset += size_at * size_at * 4;
			}
			if (face_stride > chain)
				memset(face_memory + chain, 0, face_stride - chain);
		}
		free(scratch);
		return build_layout(build, memory, _vgxm_texture_bgra8, _vgxm_texture_cube,
			width, height, cube_debug == 1 ? 1 : original_levels);
	}

	if (description->compressed && power_of_two(width) && power_of_two(height) && width >= 4 && height >= 4 &&
		channel_order == _custom_edition_channels_xbox)
	{
		unsigned long block_bytes = information.bytes;
		unsigned long format = information.kind == _texel_dxt1 ? _vgxm_texture_dxt1 :
			information.kind == _texel_dxt3 ? _vgxm_texture_dxt3 : _vgxm_texture_dxt5;

		/* the Xbox's mip chain down to 4x4 (no level smaller than a block):
		without it a distant surface samples the full-size level - shimmer
		(the menu ring's rainbow speckle) and GPU bandwidth. HALO_DXT_MIPS=0
		keeps level 0 only (Xita once saw a GPU fault it suspected chained
		BC levels of) */
		static int dxt_mips = -1;
		unsigned long chained = 1, offset = 0;

		if (dxt_mips < 0)
		{
			const char *setting = getenv("HALO_DXT_MIPS");
			dxt_mips = !setting || atoi(setting) != 0;
		}
		static int smallest = -1;

		if (smallest < 0)
		{
			/* (debug) HALO_DXT_MIN_SIZE=n: no chained level smaller than n */
			const char *setting = getenv("HALO_DXT_MIN_SIZE");
			smallest = setting && atoi(setting) >= 4 ? atoi(setting) : 4;
		}
		{
			/* (debug) HALO_DXT_NOCHAIN_KIND=1/3/5: that DXT kind unchained */
			static int unchained_kind = -1;

			if (unchained_kind < 0)
			{
				const char *setting = getenv("HALO_DXT_NOCHAIN_KIND");
				unchained_kind = setting ? atoi(setting) : 0;
			}
			if ((unchained_kind == 1 && information.kind == _texel_dxt1) || (unchained_kind == 3 && information.kind == _texel_dxt3) ||
				(unchained_kind == 5 && information.kind == _texel_dxt5))
				levels = 1;
		}
		if (dxt_mips)
			while (chained < levels && level_dimension(width, chained) >= (unsigned long)smallest &&
				level_dimension(height, chained) >= (unsigned long)smallest)
				chained++;
		{
			/* (debug) HALO_DXT_FIRST_LEVEL=k: the texture from the Xbox's
			level k down, one level (tells the source levels from the chain) */
			static int first_level = -1;

			if (first_level < 0)
			{
				const char *setting = getenv("HALO_DXT_FIRST_LEVEL");
				first_level = setting ? atoi(setting) : 0;
			}
			if (first_level > 0 && (unsigned long)first_level < chained)
			{
				unsigned long level_width = level_dimension(width, first_level), level_height = level_dimension(height, first_level);

				memory = build_alloc(build, (level_width / 4) * (level_height / 4) * block_bytes);
				if (!memory)
					return FALSE;
				reorder_blocks(base + xgpu_texture_level_offset(description, first_level), memory, level_width, level_height,
					block_bytes);
				return build_layout(build, memory, format, _vgxm_texture_swizzled, level_width,
					level_height, 1);
			}
		}
		size = 0;
		for (level = 0; level < chained; level++)
			size += (level_dimension(width, level) / 4) * (level_dimension(height, level) / 4) * block_bytes;
		memory = build_alloc(build, size);
		if (!memory)
			return FALSE;
		for (level = 0; level < chained; level++)
		{
			unsigned long level_width = level_dimension(width, level), level_height = level_dimension(height, level);

			reorder_blocks(base + xgpu_texture_level_offset(description, level), memory + offset, level_width, level_height,
				block_bytes);
			offset += (level_width / 4) * (level_height / 4) * block_bytes;
		}
		return build_layout(build, memory, format, _vgxm_texture_swizzled, width, height, chained);
	}

	if (description->depth > 1 && !description->compressed && !description->linear &&
		width * description->depth <= 4096)
	{
		/* A volume texture: GXM has none, and only its first slice used to
		be kept. The one the game samples everywhere is the 32x32x32
		distance attenuation of the dynamic lights on the environment (the
		flashlight, muzzle flashes, plasma, explosions): a ball of falloff
		whose first slice is its empty face, so no dynamic light ever lit a
		wall. Its level 0 slices now lie side by side in one 2D texture
		(slice z at u from z/depth to (z+1)/depth), which the fragment
		program reads as a volume, filtering between the two nearest slices
		(nv2a_psh_cg.c tex3D_slices). */
		unsigned long depth = description->depth, z, row;
		unsigned long atlas_width = width * depth;

		memory = build_alloc(build, LINEAR_ROW(atlas_width) * height * 4);
		scratch = malloc(width * height * depth * 4);
		if (!memory || !scratch)
		{
			free(scratch);
			return FALSE;
		}
		decode_level(description, 0, base, palette, scratch);
		for (z = 0; z < depth; z++)
			for (row = 0; row < height; row++)
				memcpy(memory + (row * LINEAR_ROW(atlas_width) + z * width) * 4, scratch + (z * height + row) * width,
					width * 4);
		free(scratch);
		return build_layout(build, memory, _vgxm_texture_bgra8, _vgxm_texture_linear,
			atlas_width, height, 1);
	}

	if (!description->linear && description->depth <= 1 && power_of_two(width) && power_of_two(height) &&
		swizzled_textures())
	{
		/* A power-of-two texture as GXM's twiddled (Morton order) BGRA,
		every Xbox level, each level's texels in the order cube faces and
		DXT blocks already have them (gxm_twiddle_position), the levels one
		after another. The same texels as the rows below, laid out so that
		a fetch's neighbours share the GPU's texture cache lines: linear
		rows put each 2x2 filter footprint on two lines, and these textures
		(lightmaps, bump and detail maps: 40% of the fetches in a d40
		frame) were the only ones the GPU read that way. */
		unsigned long *destination;

		size = 0;
		for (level = 0; level < levels; level++)
			size += level_dimension(width, level) * level_dimension(height, level) * 4;
		memory = build_alloc(build, size);
		if (!memory)
			return FALSE;
		scratch = malloc(width * height * 4);
		if (!scratch)
			return FALSE;
		destination = (unsigned long *)memory;
		for (level = 0; level < levels; level++)
		{
			unsigned long level_width = level_dimension(width, level);
			unsigned long level_height = level_dimension(height, level);
			const unsigned char *source = base + xgpu_texture_level_offset(description, level);
			unsigned long count = level_width * level_height;

			if (description->compressed)
				dxt_decode_level(information.kind, source, level_width, level_height, 1, scratch);
			else
				decode_level(description, level, source, palette, scratch);
			/* (a Custom Edition multipurpose map - always a power of two, and
			decoded here rather than kept compressed for this - with its
			channels where this build reads them, as the rows below and the
			OpenGL renderer have them: they were left in Halo PC's order) */
			if (channel_order != _custom_edition_channels_xbox)
				custom_edition_texels_reorder(scratch, count, channel_order);
			twiddle_level(destination, scratch, level_width, level_height);
			destination += count;
		}
		free(scratch);
		return build_layout(build, memory, _vgxm_texture_bgra8, _vgxm_texture_swizzled,
			width, height, levels);
	}

	/* everything else as BGRA rows, every Xbox level */
	if (description->linear || !power_of_two(width) || !power_of_two(height))
		levels = 1;
	size = 0;
	for (level = 0; level < levels; level++)
		size += LINEAR_ROW(level_dimension(width, level)) * level_dimension(height, level) * 4;
	memory = build_alloc(build, size);
	scratch = malloc(width * height * description->depth * 4);
	if (!memory || !scratch)
	{
		free(scratch);
		return FALSE;
	}
	{
		unsigned char *destination = memory;

		for (level = 0; level < levels; level++)
		{
			unsigned long level_width = level_dimension(width, level);
			unsigned long level_height = level_dimension(height, level);
			const unsigned char *source = base + xgpu_texture_level_offset(description, level);
			unsigned long row;

			if (description->compressed)
				dxt_decode_level(information.kind, source, level_width, level_height, 1, scratch);
			else
				decode_level(description, level, source, palette, scratch);
			if (channel_order != _custom_edition_channels_xbox)
				custom_edition_texels_reorder(scratch, level_width * level_height, channel_order);
			/* (a volume texture keeps its first slice: GXM has none) */
			for (row = 0; row < level_height; row++)
				memcpy(destination + row * LINEAR_ROW(level_width) * 4, scratch + row * level_width, level_width * 4);
			destination += LINEAR_ROW(level_width) * level_height * 4;
		}
	}
	free(scratch);
	{
		/* (debug) HALO_TEX_DUMP=<width>: the n-th upload of a linear texture
		that wide is written to ux0:data/haloce-vita/tex_dump.raw */
		static int dump_width = -2, dumps;

		if (dump_width == -2)
		{
			const char *setting = getenv("HALO_TEX_DUMP");
			dump_width = setting ? atoi(setting) : -1;
		}
		if (dump_width > 0 && (long)width == dump_width && ++dumps == 60)
		{
			FILE *file = fopen("ux0:data/haloce-vita/tex_dump.raw", "wb");

			if (file)
			{
				fwrite(memory, 1, LINEAR_ROW(width) * height * 4, file);
				fclose(file);
			}
			platform_log("texture dump: %lux%lu linear row %lu levels %lu format %lx", width, height,
				(unsigned long)LINEAR_ROW(width), levels, (unsigned long)description->format);
		}
	}
	return build_layout(build, memory, _vgxm_texture_bgra8, _vgxm_texture_linear,
		width, height, levels);
}

/* (the hitch log, d3d8_gxm.c) textures decoded since it last looked */
unsigned long long vita_host_time_us(void);

/* the tag cache (the loaded map's read-only data: what lies there changes
only by file reads, which the memory watch sees) */
void *physical_memory_get_tag_cache_base_address(void);
#define TAG_CACHE_BYTES 0x01600000UL

/* a fast checksum of a texture's bytes: every word up to 256 KB, a stride
of words beyond (large run-time textures are rare: the loading screen) */
static unsigned long texel_checksum(const unsigned char *data, unsigned long size)
{
	const unsigned long *words = (const unsigned long *)((unsigned long)data & ~3UL);
	unsigned long count = size / 4, step = count > 65536 ? count / 65536 : 1, index, sum = 2166136261UL;

	for (index = 0; index < count; index += step)
		sum = (sum ^ words[index]) * 16777619UL;
	return sum ^ size;
}
volatile unsigned long long vita_texture_build_us;
volatile unsigned long vita_texture_builds, vita_texture_build_bytes;

void vita_host_sleep_us(unsigned long microseconds);

/* (harness) HALO_TEX_DECODE_KBPMS=<KB per ms>: a decode that took less
than its texels' size at this rate waits out the rest - the Vita decodes
about 15-30 KB of texels a millisecond (halo.log's hitch lines), the PC a
few hundred - so the gxm-null harness's frames wait for decoding as the
Vita's do */
static void decode_rate_emulate(unsigned long long started_us, unsigned long bytes)
{
	static long rate = -1;
	unsigned long long wanted, elapsed;

	if (rate < 0)
		rate = getenv("HALO_TEX_DECODE_KBPMS") ? atol(getenv("HALO_TEX_DECODE_KBPMS")) : 0;
	if (rate <= 0)
		return;
	wanted = (unsigned long long)bytes * 1000ull / 1024ull / (unsigned long long)rate;
	elapsed = vita_host_time_us() - started_us;
	if (elapsed < wanted)
		vita_host_sleep_us((unsigned long)(wanted - elapsed));
}

/* the Xbox texture's decoding on the worker, into the pool, and its
control words */
static BOOL texture_build(struct texture_entry *entry, const unsigned char *base, const D3DCOLOR *palette)
{
	struct texture_build build;

	memset(&build, 0, sizeof(build));
	build.description = &entry->description;
	build.base = base;
	build.palette = palette;
	build.channel_order = custom_edition_texels_order(entry->address);
	build.format_word = entry->format_word;
	build.size_word = entry->size_word;
	build.bytes = entry->size;
	return texture_decode(&build) && texture_initialize(&entry->texture, &build) == 0;
}

/* ---------- decoding in the background

A texture first drawn used to be decoded on the worker in the middle of
its frame, and the game waited for the worker at the next present: 17
textures (3.5 MB) took 228 ms on the hardware, 74 (5.2 MB) 320 ms - the
hitches of a new room, a turn or a checkpoint. A texture of a map's bitmaps
(the texture cache's texels or the tag data's, power-of-two 2D, larger than
STAND_IN_SIZE) is now decoded by a thread of its own, at a low priority
(the fourth core with Fourth core helpers at All async), into pool memory
the worker took for it; until it is done the texture is drawn from a lower
level of its own mip chain - at most STAND_IN_SIZE texels a side, decoded
on the worker at once (a few KB) - or, without such a level, from its
texels sampled down to that size: the right colours, blurred, for a few
frames. The worker swaps the decoded texture in at its next use or the
frame's end, once and for good (a texture whose texels changed meanwhile
is decoded again). Only the worker allocates, makes control words and
touches the entries; the decoder writes only the memory it was given, and
the pool's recycling and moves stop it first (vita_texture_decodes_quiesce).
A decode the thread has not started after HALO_TEX_ASYNC_DEADLINE_MS (1500)
is done by the worker, so no texture stays a stand-in when the cores are
busy. HALO_TEX_ASYNC=0: every texture decoded on the worker, as before.
(Nothing like this upstream: OpenCE's desktop renderer uploads at once;
its "Visibility tests read on the CPU no longer stall for the GPU" keeps the
last result until the new one is ready, the same idea for queries.) */
#define STAND_IN_SIZE 32
#define DECODE_JOBS 96

enum
{
	_decode_free,
	_decode_queued,
	_decode_running,
	_decode_done,
};

struct decode_job
{
	volatile int state;
	unsigned long sequence;
	/* the entry it is for; NULL once given up (the worker's) */
	struct texture_entry *entry;
	struct xgpu_texture_description description;
	struct texture_build build;
	unsigned long generation, pool_serial;
	int ok;
	unsigned long long queued_us, decode_us;
};

static struct decode_job decode_jobs[DECODE_JOBS];
static unsigned long decode_sequence;
/* 1 running, -1 could not be started */
static int decoder_state;

/* (halo.log's texture streaming line, the worker's counts) */
static unsigned long stream_background, stream_background_bytes, stream_stand_ins, stream_stand_in_draws, stream_stale,
	stream_taken_back, stream_no_job, stream_quiesce_waits;
static unsigned long long stream_decode_us, stream_wait_us, stream_wait_longest_us;
/* why a texture was decoded on the worker: the streaming off (a map's or a
revert's first 2 s), STAND_IN_SIZE or smaller, not a power-of-two 2D
texture (cube maps, volumes, linear, palettized), not the map's bitmaps */
static unsigned long stream_worker_reason[4];
/* the worker's decoding this frame (texture_build, stand-ins, decodes taken
back), and the frames it held up: over 33 ms (a frame missed at 30 fps)
and over 100 ms (a hitch), the longest; with the decodes on the worker */
static unsigned long long frame_decode_us, frame_decode_longest_us;
static unsigned long frame_decode_over33, frame_decode_over100, stream_worker_builds, stream_worker_bytes;
/* (the frames of a map's or a revert's first 2 s, the streaming off: the
picture waited for, as the game did) */
static unsigned long frame_decode_loading_frames;
static unsigned long long frame_decode_loading_us;

/* (the hitch log, d3d8_gxm.c) decodes swapped in and stand-ins made since it
last looked */
volatile unsigned long vita_texture_background_builds, vita_texture_background_bytes, vita_texture_stand_ins;

int vita_host_thread_start_priority(const char *name, void (*function)(void *), void *argument, int core,
	int priority);
#ifdef HALO_VITA
int vita_host_fourth_core_join(const char *role, int level);
#endif
void *physical_memory_get_texture_cache_base_address(void);

extern unsigned long halo_map_generation;
int halo_repeatable_run(void);

/* whether the texture streaming is on now (HALO_TEXTURE_STREAMING, the
Vita's default, off on the PC: rasterizer_xbox.c), as it decides: not in a
map's first 2 s (its first picture whole, behind the loading screen; a
revert's too) nor in a repeatable run */
static int texture_streaming_live(void)
{
	static int enabled = -1;
	static unsigned long generation;
	static unsigned long long generation_started_us;
	unsigned long long now;

	if (enabled < 0)
	{
		const char *streaming = getenv("HALO_TEXTURE_STREAMING");

#ifdef HALO_VITA
		enabled = streaming ? atoi(streaming) != 0 : 1;
#else
		enabled = streaming && atoi(streaming) != 0;
#endif
	}
	if (!enabled || halo_repeatable_run())
		return 0;
	now = vita_host_time_us();
	if (generation != halo_map_generation)
	{
		generation = halo_map_generation;
		generation_started_us = now;
	}
	return now - generation_started_us > 2000000ull;
}

/* the textures decoded in the background while the streaming is on;
HALO_TEX_ASYNC=0: on the worker, as before */
static int background_decoding(void)
{
	static int enabled = -1;

	if (enabled < 0)
		enabled = !getenv("HALO_TEX_ASYNC") || atoi(getenv("HALO_TEX_ASYNC")) != 0;
	return enabled && texture_streaming_live();
}

static unsigned long long background_deadline_us(void)
{
	static long milliseconds = -1;

	if (milliseconds < 0)
		milliseconds = getenv("HALO_TEX_ASYNC_DEADLINE_MS") ? atol(getenv("HALO_TEX_ASYNC_DEADLINE_MS")) : 1500;
	return (unsigned long long)milliseconds * 1000ull;
}

static void decode_job_run(struct decode_job *job)
{
	unsigned long long before = vita_host_time_us();

	job->build.description = &job->description;
	job->ok = texture_decode(&job->build);
	decode_rate_emulate(before, job->build.bytes);
	job->decode_us = vita_host_time_us() - before;
}

static void decoder_thread(void *unused)
{
	(void)unused;
#ifdef HALO_VITA
	/* (Fourth core helpers, All async: the decodes on the fourth core) */
	vita_host_fourth_core_join("texture decoder", 2);
#endif
	for (;;)
	{
		struct decode_job *job = NULL;
		int index, expected = _decode_queued;

		/* the oldest queued first */
		for (index = 0; index < DECODE_JOBS; index++)
			if (__atomic_load_n(&decode_jobs[index].state, __ATOMIC_ACQUIRE) == _decode_queued &&
				(!job || (long)(decode_jobs[index].sequence - job->sequence) < 0))
			{
				job = &decode_jobs[index];
			}
		if (!job)
		{
			vita_host_sleep_us(2000);
			continue;
		}
		if (!__atomic_compare_exchange_n(&job->state, &expected, _decode_running, 0, __ATOMIC_ACQ_REL,
			__ATOMIC_ACQUIRE))
		{
			continue;
		}
		decode_job_run(job);
		__atomic_store_n(&job->state, _decode_done, __ATOMIC_RELEASE);
	}
}

static int decoder_start(void)
{
	if (!decoder_state)
	{
		/* (below the game's threads, which take its core whenever they
		have work: it decodes in their idle time) */
		int result = vita_host_thread_start_priority("texture decoder", decoder_thread, NULL, -1, 180);

		decoder_state = result == 0 ? 1 : -1;
		platform_log("texture streaming: %s", decoder_state > 0 ?
			"textures decoded in the background, drawn from a lower level until they are in" :
			"the decoder thread could not be started: textures decoded on the worker");
	}
	return decoder_state > 0;
}

/* whether the texture is decoded in the background: one of the map's
bitmaps (texels that change only when the texture cache reads others
there), a power-of-two 2D texture with a lower level worth drawing first */
static BOOL decode_in_background(const struct texture_entry *entry)
{
	const struct xgpu_texture_description *description = &entry->description;
	unsigned long texture_cache = (unsigned long)physical_memory_get_texture_cache_base_address();
	unsigned long tag_cache = (unsigned long)physical_memory_get_tag_cache_base_address();
	static int first_level = -1;

	if (first_level < 0)
		first_level = getenv("HALO_DXT_FIRST_LEVEL") ? atoi(getenv("HALO_DXT_FIRST_LEVEL")) : 0;
	if (!background_decoding() || entry->dynamic || first_level > 0 || !swizzled_textures())
	{
		stream_worker_reason[0]++;
		return FALSE;
	}
	if (description->width <= STAND_IN_SIZE && description->height <= STAND_IN_SIZE)
	{
		stream_worker_reason[1]++;
		return FALSE;
	}
	if (description->cube_map || description->depth > 1 || description->linear || description->format == 0x0b ||
		!power_of_two(description->width) || !power_of_two(description->height))
	{
		stream_worker_reason[2]++;
		return FALSE;
	}
	if (!((texture_cache && entry->address >= texture_cache && entry->address + entry->size <= texture_cache + 0x1600000UL) ||
		(tag_cache && entry->address >= tag_cache && entry->address + entry->size <= tag_cache + TAG_CACHE_BYTES)))
	{
		stream_worker_reason[3]++;
		return FALSE;
	}
	return decoder_start();
}

/* no lower level small enough: the stand-in is the lowest level there is,
sampled at the centre of each block of it, drop levels down from the top */
static BOOL stand_in_sample(struct texture_build *build, const struct texture_entry *entry, unsigned long drop)
{
	const struct xgpu_texture_description *description = &entry->description;
	struct format_information information = format_information(description->format);
	unsigned long level = description->levels ? description->levels - 1 : 0, shift = drop - level;
	unsigned long width = level_dimension(description->width, drop), height = level_dimension(description->height, drop);
	unsigned long source_width = level_dimension(description->width, level);
	unsigned long source_height = level_dimension(description->height, level);
	const unsigned char *source = build->base + xgpu_texture_level_offset(description, level);
	struct swizzle_masks masks = swizzle_masks(source_width, source_height, 1);
	unsigned long *scratch, x, y;
	void *memory;

	memory = build_alloc(build, width * height * 4);
	if (!memory)
		return FALSE;
	scratch = malloc(width * height * 4);
	if (!scratch)
		return FALSE;
	for (y = 0; y < height; y++)
		for (x = 0; x < width; x++)
		{
			unsigned long sx = (x << shift) + ((1UL << shift) >> 1), sy = (y << shift) + ((1UL << shift) >> 1);
			unsigned long value;

			if (sx >= source_width)
				sx = source_width - 1;
			if (sy >= source_height)
				sy = source_height - 1;
			if (description->compressed)
			{
				unsigned long texels[16];

				dxt_block_texels(information.kind, source + ((sy / 4) * ((source_width + 3) / 4) + sx / 4) *
					information.bytes, texels);
				value = texels[(sy & 3) * 4 + (sx & 3)];
			}
			else
			{
				const unsigned char *texel = source + (spread(masks.x, sx) | spread(masks.y, sy)) * information.bytes;

				value = convert_texel(information.kind, texel, NULL, sx, texel);
			}
			scratch[y * width + x] = value;
		}
	if (build->channel_order != _custom_edition_channels_xbox)
		custom_edition_texels_reorder(scratch, width * height, build->channel_order);
	twiddle_level((unsigned long *)memory, scratch, width, height);
	free(scratch);
	return build_layout(build, memory, _vgxm_texture_bgra8, _vgxm_texture_swizzled, width, height, 1);
}

/* the entry's stand-in, decoded on the worker into the pool: its own level
no larger than STAND_IN_SIZE a side and those below it */
static BOOL stand_in_build(struct texture_entry *entry, unsigned char channel_order)
{
	const struct xgpu_texture_description *description = &entry->description;
	struct xgpu_texture_description lower = *description;
	struct texture_build build;
	unsigned long drop = 0;
	BOOL ok;

	while (level_dimension(description->width, drop) > STAND_IN_SIZE ||
		level_dimension(description->height, drop) > STAND_IN_SIZE)
	{
		drop++;
	}
	memset(&build, 0, sizeof(build));
	build.channel_order = channel_order;
	build.format_word = entry->format_word;
	build.size_word = entry->size_word;
	build.base = (const unsigned char *)entry->address;
	if (drop < description->levels)
	{
		lower.width = level_dimension(description->width, drop);
		lower.height = level_dimension(description->height, drop);
		lower.levels = description->levels - drop;
		lower.pitch = lower.width * format_information(description->format).bytes;
		build.description = &lower;
		build.base += xgpu_texture_level_offset(description, drop);
		build.bytes = xgpu_texture_level_offset(&lower, lower.levels);
		ok = texture_decode(&build);
	}
	else
	{
		build.description = description;
		ok = stand_in_sample(&build, entry, drop);
	}
	if (!ok || texture_initialize(&entry->texture, &build) != 0)
		return FALSE;
	entry->memory = build.memory;
	entry->memory_size = build.memory_size;
	return TRUE;
}

/* the job's texture in place of the entry's stand-in (the worker), or the
entry decoded again at its next use if its texels changed meanwhile; the
job freed */
static void decode_job_finish(struct decode_job *job)
{
	struct texture_entry *entry = job->entry;

	if (entry && entry->job == job)
	{
		entry->job = NULL;
		if (job->ok && job->pool_serial == pool_serial &&
			memory_watch_generation(entry->address, entry->size) <= job->generation &&
			texture_initialize(&entry->texture, &job->build) == 0)
		{
			unsigned long long waited = vita_host_time_us() - job->queued_us;

			entry->memory = job->build.memory;
			entry->memory_size = job->build.memory_size;
			stream_background++;
			stream_background_bytes += entry->size;
			stream_decode_us += job->decode_us;
			stream_wait_us += waited;
			if (waited > stream_wait_longest_us)
				stream_wait_longest_us = waited;
			vita_texture_background_builds++;
			vita_texture_background_bytes += entry->size;
		}
		else
		{
			/* (decoded again, from its stand-in, at its next use) */
			stream_stale++;
			entry->generation = 0;
		}
	}
	job->entry = NULL;
	__atomic_store_n(&job->state, _decode_free, __ATOMIC_RELEASE);
}

/* the entry's job given up: freed now if the decoder has not taken it,
else when it is done */
static void decode_job_cancel(struct texture_entry *entry)
{
	struct decode_job *job = entry->job;
	int expected = _decode_queued;

	entry->job = NULL;
	if (!job)
		return;
	job->entry = NULL;
	__atomic_compare_exchange_n(&job->state, &expected, _decode_free, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

/* (the worker, with each use of a texture whose decode is in the
background) the decode swapped in if done; one the thread has not started
by the deadline done here */
static void decode_job_poll(struct texture_entry *entry)
{
	struct decode_job *job = entry->job;
	int state = __atomic_load_n(&job->state, __ATOMIC_ACQUIRE), expected = _decode_queued;

	if (state == _decode_queued && vita_host_time_us() - job->queued_us > background_deadline_us() &&
		__atomic_compare_exchange_n(&job->state, &expected, _decode_running, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
	{
		unsigned long long before = vita_host_time_us();

		decode_job_run(job);
		vita_texture_build_us += vita_host_time_us() - before;
		frame_decode_us += vita_host_time_us() - before;
		stream_taken_back++;
		state = _decode_done;
		__atomic_store_n(&job->state, _decode_done, __ATOMIC_RELEASE);
	}
	if (state == _decode_done)
		decode_job_finish(job);
}

/* (the worker, at each frame's end) every decode done swapped in */
static void decode_jobs_sweep(void)
{
	int index;

	for (index = 0; index < DECODE_JOBS; index++)
		if (__atomic_load_n(&decode_jobs[index].state, __ATOMIC_ACQUIRE) == _decode_done)
			decode_job_finish(&decode_jobs[index]);
}

/* (the worker, or the game's thread while the worker is idle) before the
pool forgets or moves memory: the queued decodes given up (their textures
decoded again at their next use), the one decoding waited for, and those
done swapped in, so that no decode writes into memory given to another */
void vita_texture_decodes_quiesce(void)
{
	int index, waited = 0;

	if (decoder_state <= 0)
		return;
	for (index = 0; index < DECODE_JOBS; index++)
	{
		struct decode_job *job = &decode_jobs[index];
		int expected = _decode_queued;

		if (__atomic_compare_exchange_n(&job->state, &expected, _decode_free, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) &&
			job->entry)
		{
			job->entry->job = NULL;
			job->entry->generation = 0;
			job->entry = NULL;
		}
	}
	for (index = 0; index < DECODE_JOBS; index++)
		while (__atomic_load_n(&decode_jobs[index].state, __ATOMIC_ACQUIRE) == _decode_running)
		{
			waited = 1;
			vita_host_sleep_us(100);
		}
	stream_quiesce_waits += waited;
	decode_jobs_sweep();
}

/* (the worker) the entry's texture decoded in the background, its stand-in
drawn meanwhile: FALSE if it cannot be (no job free, no pool memory), to be
decoded on the worker */
static BOOL decode_submit(struct texture_entry *entry)
{
	struct decode_job *job = NULL;
	struct texture_build measure;
	unsigned char channel_order = custom_edition_texels_order(entry->address);
	unsigned long events;
	void *memory;
	int index;

	for (index = 0; index < DECODE_JOBS && !job; index++)
		if (__atomic_load_n(&decode_jobs[index].state, __ATOMIC_ACQUIRE) == _decode_free)
			job = &decode_jobs[index];
	if (!job)
	{
		stream_no_job++;
		return FALSE;
	}
	memset(&measure, 0, sizeof(measure));
	measure.description = &entry->description;
	measure.base = (const unsigned char *)entry->address;
	measure.channel_order = channel_order;
	measure.measure = 1;
	texture_decode(&measure);
	if (!measure.measured)
		return FALSE;
	pool_reuse = NULL;
	if (!stand_in_build(entry, channel_order))
		return FALSE;
	/* (the stand-in's memory is the entry's; the pool recycled while the
	decode's was taken may have forgotten it: decoded here, then) */
	events = pool_events;
	memory = pool_alloc(measure.measured);
	if (!memory || events != pool_events)
		return FALSE;
	memset(job, 0, sizeof(*job));
	job->entry = entry;
	job->sequence = ++decode_sequence;
	job->description = entry->description;
	job->build.base = (const unsigned char *)entry->address;
	job->build.channel_order = channel_order;
	job->build.format_word = entry->format_word;
	job->build.size_word = entry->size_word;
	job->build.bytes = entry->size;
	job->build.preallocated = memory;
	job->build.preallocated_size = pool_last_size;
	job->generation = entry->generation;
	job->pool_serial = pool_serial;
	job->queued_us = vita_host_time_us();
	entry->job = job;
	entry->pool_serial = pool_serial;
	stream_stand_ins++;
	vita_texture_stand_ins++;
	__atomic_store_n(&job->state, _decode_queued, __ATOMIC_RELEASE);
	return TRUE;
}

const struct vgxm_texture *vita_texture_get(const DWORD *resource, const D3DCOLOR *palette,
	struct xgpu_texture_description *description)
{
	DWORD data = resource[1], format_word = resource[3], size_word = resource[4];
	struct texture_entry **bucket = &texture_buckets[bucket_index(data, format_word, size_word)];
	struct texture_entry *entry, *oldest_variant = NULL;
	unsigned long generation, variant_count = 0;
	BOOL palettized = ((format_word & D3DFORMAT_FORMAT_MASK) >> D3DFORMAT_FORMAT_SHIFT) == 0x0b;
	unsigned long hash = palettized ? palette_hash(palette) : 0;

	for (entry = *bucket; entry; entry = entry->next)
	{
		if (entry->data == data && entry->format_word == format_word && entry->size_word == size_word)
		{
			if (entry->palette_hash == hash)
				break;
			variant_count++;
			if (!oldest_variant || entry->last_used_frame < oldest_variant->last_used_frame)
				oldest_variant = entry;
		}
	}
	if (!entry && variant_count >= MAXIMUM_PALETTE_VARIANTS)
	{
		entry = oldest_variant;
		entry->palette_hash = hash;
		entry->generation = 0;
	}
	if (!entry)
	{
		entry = calloc(1, sizeof(*entry));
		entry->data = data;
		entry->format_word = format_word;
		entry->size_word = size_word;
		entry->palette_hash = hash;
		xgpu_texture_describe(format_word, size_word, &entry->description);
		entry->address = (unsigned long)PLATFORM_PHYSICAL_TO_VIRTUAL(data);
		entry->size = xgpu_texture_face_size(&entry->description) * (entry->description.cube_map ? 6 : 1);
		entry->dynamic = -1;
		entry->next = *bucket;
		*bucket = entry;
	}
	if (entry->dynamic < 0 || (entry->valid && entry->dynamic && entry->checksum_frame != texture_frame))
	{
		/* (see dynamic in the entry: changed texels rebuild the texture) */
		unsigned long sum;

		if (entry->dynamic < 0)
		{
			unsigned long base = (unsigned long)physical_memory_get_tag_cache_base_address();

			/* (HALO_TEX_CHECKSUM=1: textures outside the tag data are
			checksummed every frame, as before the locks were tracked -
			10% of the Pi's CPU) */
			static int checksum_on = -1;

			if (checksum_on < 0)
				checksum_on = getenv("HALO_TEX_CHECKSUM") && atoi(getenv("HALO_TEX_CHECKSUM"));
			entry->dynamic = checksum_on && !(base && entry->address >= base && entry->address + entry->size <= base + TAG_CACHE_BYTES);
		}
		if (entry->dynamic)
		{
			sum = texel_checksum((const unsigned char *)entry->address, entry->size);
			if (entry->valid && sum != entry->checksum)
				entry->generation = 0;
			entry->checksum = sum;
			entry->checksum_frame = texture_frame;
		}
	}
	/* (its decode in the background: swapped in once done) */
	if (entry->job)
		decode_job_poll(entry);
	if (entry->valid && entry->generation && entry->pool_serial == pool_serial && entry->checked_serial == memory_watch_serial())
	{
		entry->last_used_frame = texture_frame;
		*description = entry->description;
		if (entry->job)
			stream_stand_in_draws++;
		return &entry->texture;
	}
	entry->checked_serial = memory_watch_serial();
	generation = memory_watch_generation(entry->address, entry->size);
	if (!entry->generation || generation > entry->generation || entry->pool_serial != pool_serial)
	{
		entry->generation = generation ? generation : 1;
		entry->valid = FALSE;
		/* (a decode in the background of what was there before: given up) */
		if (entry->job)
			decode_job_cancel(entry);
		if (platform_is_contiguous((void *)entry->address) &&
			platform_is_contiguous((void *)(entry->address + entry->size - 1)) && decode_in_background(entry))
		{
			unsigned long long before = vita_host_time_us();

			/* (its stand-in now, the texture in the background) */
			entry->valid = decode_submit(entry);
			vita_texture_build_us += vita_host_time_us() - before;
			frame_decode_us += vita_host_time_us() - before;
		}
		if (!entry->valid && platform_is_contiguous((void *)entry->address) &&
			platform_is_contiguous((void *)(entry->address + entry->size - 1)))
		{
			{
				unsigned long long before = vita_host_time_us();

				/* (a dynamic texture rebuilt in the same pool generation
				decodes into its own memory again) */
				static int reuse_on = -1;

				if (reuse_on < 0)
					reuse_on = !getenv("HALO_TEX_REUSE") || atoi(getenv("HALO_TEX_REUSE")) != 0;
				pool_reuse = reuse_on && entry->memory && entry->pool_serial == pool_serial ? entry->memory : NULL;
				pool_reuse_size = pool_reuse ? entry->memory_size : 0;
				pool_last = NULL;
				{
					int halo_trace_active(void);

					if (halo_trace_active())
						platform_log("trace: texture %08lx %lux%lu format %02lx levels %lu", (unsigned long)data,
							entry->description.width, entry->description.height, (unsigned long)entry->description.format,
							entry->description.levels);
				}
				entry->valid = texture_build(entry, (const unsigned char *)entry->address, palette);
				pool_reuse = NULL;
				entry->memory = pool_last;
				entry->memory_size = pool_last ? pool_last_size : 0;
				decode_rate_emulate(before, entry->size);
				vita_texture_build_us += vita_host_time_us() - before;
				frame_decode_us += vita_host_time_us() - before;
				stream_worker_builds++;
				stream_worker_bytes += entry->size;
				vita_texture_builds++;
				vita_texture_build_bytes += entry->size;
			}
			entry->pool_serial = pool_serial;
			if (!entry->valid)
			{
				static unsigned long reported;

				if (reported++ < 32)
					platform_log("texture %08lx format %02lx %lux%lu (cube %d) not handled", (unsigned long)data,
						(unsigned long)entry->description.format, entry->description.width,
						entry->description.height, entry->description.cube_map);
			}
		}
	}
	entry->last_used_frame = texture_frame;
	*description = entry->description;
	if (entry->job)
		stream_stand_in_draws++;
	return entry->valid ? &entry->texture : NULL;
}

/* HALO_FRAME_TIMING: with each frame report (frame_timing.c), the game's
texture cache (the bitmaps read from the map files into it, the blocks it
evicted) and this cache (the decodes into the pool, the pool emptied), as
totals since the start: a texture the game's cache evicts and reads again
is decoded again here, into new pool memory */
void halo_texture_stats_report(void)
{
	extern volatile unsigned long halo_texture_cache_loads, halo_texture_cache_load_bytes, halo_texture_cache_evictions;

	platform_log("texture-stats: game cache %lu reads (%lu KB), %lu evicted | pool %lu decodes (%lu KB), at %lu KB, "
		"%lu segments recycled, emptied %lu times", halo_texture_cache_loads, halo_texture_cache_load_bytes / 1024,
		halo_texture_cache_evictions, stats_builds, stats_build_bytes / 1024, (unsigned long)(vgxm_pool_used() / 1024),
		stats_pool_recycles, stats_pool_resets);
	platform_log("texture decoding on the worker: %lu textures (%lu KB); in play, frames it held up over 33 ms %lu, over "
		"100 ms %lu, longest %.1f ms; %.1f ms in %lu frames of a map's or a revert's first 2 s; on the worker for: the "
		"streaming off %lu, small %lu, not power-of-two 2D %lu, not the map's bitmaps %lu", stream_worker_builds,
		stream_worker_bytes / 1024, frame_decode_over33, frame_decode_over100, frame_decode_longest_us / 1000.0,
		frame_decode_loading_us / 1000.0, frame_decode_loading_frames, stream_worker_reason[0], stream_worker_reason[1],
		stream_worker_reason[2], stream_worker_reason[3]);
	if (decoder_state > 0)
		platform_log("texture streaming: %lu decoded in the background (%lu KB, %.1f ms on the decoder), drawn from a lower "
			"level %lu times meanwhile (%lu stand-ins, %lu KB of the pool each at most), in after %.1f ms on average, "
			"%.1f ms at most; %lu decoded again (changed meanwhile), %lu taken back by the worker after %lu ms, "
			"%lu on the worker for want of a job, %lu pool stops waited for a decode",
			stream_background, stream_background_bytes / 1024, stream_decode_us / 1000.0, stream_stand_in_draws,
			stream_stand_ins, (unsigned long)(STAND_IN_SIZE * STAND_IN_SIZE * 4 * 4 / 3 / 1024),
			stream_background ? stream_wait_us / 1000.0 / stream_background : 0.0, stream_wait_longest_us / 1000.0,
			stream_stale, stream_taken_back, (unsigned long)(background_deadline_us() / 1000), stream_no_job,
			stream_quiesce_waits);
}

/* textures the game locked (to write their texels: the text glyph cache, the
movie frame, rebuilt bitmaps): marked written at the lock and again at the
frame's end, after the writes (the game thread; the worker may build a
texture between the two) */
#define MAXIMUM_LOCKED 64
static struct
{
	unsigned long address, size;
} locked[MAXIMUM_LOCKED];
static unsigned long locked_count;

static void locked_mark(unsigned long address, unsigned long size)
{
	memory_watch_prepare_write((void *)address, size);
}

void vita_texture_locked(const DWORD *resource)
{
	struct xgpu_texture_description description;
	unsigned long address, size, index;

	if (!resource || !resource[1])
		return;
	xgpu_texture_describe(resource[3], resource[4], &description);
	address = (unsigned long)PLATFORM_PHYSICAL_TO_VIRTUAL(resource[1]);
	size = xgpu_texture_face_size(&description) * (description.cube_map ? 6 : 1);
	locked_mark(address, size);
	for (index = 0; index < locked_count; index++)
		if (locked[index].address == address)
			return;
	if (locked_count < MAXIMUM_LOCKED)
	{
		locked[locked_count].address = address;
		locked[locked_count].size = size;
		locked_count++;
	}
}

void vita_texture_locks_flush(void)
{
	unsigned long index;

	for (index = 0; index < locked_count; index++)
		locked_mark(locked[index].address, locked[index].size);
	locked_count = 0;
}

void vita_texture_cache_begin_frame(void)
{
	/* (debug, Vita3K) HALO_TEX_REBUILD_AT=n: at the n-th frame every
	texture is decoded again, into new memory. Vita3K's Vulkan renderer
	draws the textures a map first uses in the first few frames after the
	program starts with other textures' texels (colour noise on the models
	and the base - any map loaded straight from init.txt: a Custom Edition
	map, or an Xbox map whose cache partition copy exists already), though
	the GPU is given the same texels and words as when the map is loaded
	later; the same bytes at new addresses draw right. The OpenGL renderer
	and the hardware read the pool as it is. Costs a second copy of the
	pool's textures. */
	static long rebuild_at = -2;

	/* (the decodes done in the background swapped in, drawn or not) */
	decode_jobs_sweep();
	if (texture_streaming_live() || !frame_decode_us)
	{
		if (frame_decode_us > frame_decode_longest_us)
			frame_decode_longest_us = frame_decode_us;
		frame_decode_over33 += frame_decode_us > 33000ull;
		frame_decode_over100 += frame_decode_us > 100000ull;
	}
	else
	{
		frame_decode_loading_frames++;
		frame_decode_loading_us += frame_decode_us;
	}
	frame_decode_us = 0;
	texture_frame++;
	if (rebuild_at == -2)
		rebuild_at = getenv("HALO_TEX_REBUILD_AT") ? atol(getenv("HALO_TEX_REBUILD_AT")) : -1;
	if (rebuild_at > 0 && texture_frame == (unsigned long)rebuild_at)
	{
		platform_log("textures: all decoded again at frame %lu (HALO_TEX_REBUILD_AT)", texture_frame);
		pool_serial++;
	}
}

unsigned long vita_texture_cache_forget(const void *base, unsigned long size)
{
	unsigned long bucket, count = 0;
	struct texture_entry *entry;

	if (!size)
		return 0;
	pool_events++;
	for (bucket = 0; bucket < TEXTURE_BUCKET_COUNT; bucket++)
		for (entry = texture_buckets[bucket]; entry; entry = entry->next)
			if (entry->memory && (const unsigned char *)entry->memory >= (const unsigned char *)base &&
				(const unsigned char *)entry->memory < (const unsigned char *)base + size)
			{
				/* (another pool generation's: decoded again, never into
				this memory - pool_reuse) */
				entry->pool_serial = 0;
				entry->memory = NULL;
				entry->memory_size = 0;
				count++;
			}
	return count;
}

/* ---------- Custom Edition channel orders

A Halo Custom Edition map keeps a model shader's multipurpose masks and a
HUD meter's channels where Halo PC reads them; the map loading says which
texels hold which order as they arrive (port/linux/game/
custom_edition_bitmaps.c). The OpenGL renderer samples such textures with a
texture swizzle (port/linux/src/xbox_textures.c); here they are decoded to
BGRA - DXT ones too, rather than kept compressed - with their channels moved
to where this build reads them (texture_build). Addresses stay listed until
other texels arrive there or the map goes. The loading (the tick or render
thread) and the worker's decoding share the list under a lock. */

/* for each order, the channel (red, green, blue, alpha) of the texels each
channel is taken from (as xbox_textures.c) */
static const unsigned char custom_edition_channel_sources[NUMBER_OF_CUSTOM_EDITION_CHANNEL_ORDERS][4] =
{
	{ 0, 1, 2, 3 },
	/* specular, self-illumination, color change and the auxiliary mask */
	{ 2, 1, 3, 0 },
	/* the fill order in color, the shape in alpha */
	{ 3, 3, 3, 0 },
};

#define MAXIMUM_CUSTOM_EDITION_TEXELS 512

static struct
{
	unsigned long address;
	unsigned char channel_order;
} custom_edition_texels[MAXIMUM_CUSTOM_EDITION_TEXELS];
static unsigned long custom_edition_texel_count;
static volatile int custom_edition_texels_lock;

static void custom_edition_texels_take(void)
{
	while (__atomic_load_n(&custom_edition_texels_lock, __ATOMIC_RELAXED) ||
		__atomic_exchange_n(&custom_edition_texels_lock, 1, __ATOMIC_ACQUIRE))
		;
}

static void custom_edition_texels_give(void)
{
	__atomic_store_n(&custom_edition_texels_lock, 0, __ATOMIC_RELEASE);
}

static unsigned char custom_edition_texels_order(unsigned long address)
{
	unsigned char order = _custom_edition_channels_xbox;
	unsigned long index;

	if (!custom_edition_texel_count)
		return order;
	custom_edition_texels_take();
	for (index = 0; index < custom_edition_texel_count; index++)
	{
		if (custom_edition_texels[index].address == address)
		{
			order = custom_edition_texels[index].channel_order;
			break;
		}
	}
	custom_edition_texels_give();
	return order;
}

/* the texels' channels (BGRA words, ARGB: alpha in the top byte) moved to
where this build reads them */
static void custom_edition_texels_reorder(unsigned long *texels, unsigned long count, unsigned char order)
{
	const unsigned char *sources = custom_edition_channel_sources[order];
	/* the byte shift of red, green, blue and alpha in an ARGB word */
	static const unsigned char shifts[4] = { 16, 8, 0, 24 };
	unsigned long index;

	for (index = 0; index < count; index++)
	{
		unsigned long texel = texels[index], reordered = 0, channel;

		for (channel = 0; channel < 4; channel++)
			reordered |= ((texel >> shifts[sources[channel]]) & 0xFF) << shifts[channel];
		texels[index] = reordered;
	}
}

void halo_custom_edition_texels_channels(const void *texels, unsigned char channel_order)
{
	unsigned long address = (unsigned long)texels;
	unsigned long index;

	/* (debug) HALO_CE_CHANNELS=0: every texture keeps its channels - a
	Custom Edition map built from Xbox tags (Invader's test maps) has its
	multipurpose maps in the Xbox's order already */
	static int channels_on = -1;

	if (channels_on < 0)
		channels_on = !getenv("HALO_CE_CHANNELS") || atoi(getenv("HALO_CE_CHANNELS")) != 0;
	if (channel_order >= NUMBER_OF_CUSTOM_EDITION_CHANNEL_ORDERS || !channels_on)
		channel_order = _custom_edition_channels_xbox;
	custom_edition_texels_take();
	for (index = 0; index < custom_edition_texel_count && custom_edition_texels[index].address != address; index++)
	{
	}
	if (index < custom_edition_texel_count)
	{
		if (channel_order == _custom_edition_channels_xbox)
			custom_edition_texels[index] = custom_edition_texels[--custom_edition_texel_count];
		else
			custom_edition_texels[index].channel_order = channel_order;
	}
	else if (channel_order != _custom_edition_channels_xbox)
	{
		if (custom_edition_texel_count < MAXIMUM_CUSTOM_EDITION_TEXELS)
		{
			custom_edition_texels[custom_edition_texel_count].address = address;
			custom_edition_texels[custom_edition_texel_count].channel_order = channel_order;
			custom_edition_texel_count++;
		}
		else
		{
			static int warned;

			if (!warned++)
				platform_log("custom edition: more than %d reordered textures; the rest keep Halo PC's channel order",
					MAXIMUM_CUSTOM_EDITION_TEXELS);
		}
	}
	custom_edition_texels_give();
}

void halo_custom_edition_texels_forget(void)
{
	custom_edition_texels_take();
	custom_edition_texel_count = 0;
	custom_edition_texels_give();
}

/* ---------- sprite texel bounds */

/* (port) Where a sprite's texels can colour anything: for a rectangle of a
texture (a sprite of a bitmap group's sheet, in UV), the region outside
which every sample the GPU can take reads texels that are zero in the
channels a blend mode needs (alpha for an alpha blend, colour for add and
the like), at every level of the Xbox mip chain (the GPU's levels are those
or fewer), with the bilinear filter's reach (a texel of level L touches the
samples within 1.5 of its texels around it) and the wrap of the rectangle's
edges (a superset of the clamp). Returned in level-0 texel units: the
rectangle and its diagonals (x + y, x - y), so a quad cut to that octagon
draws the same image (render_sprite.c). The zero tests read the Xbox texels
as the GPU gets them (convert_texel for the uncompressed formats; DXT
blocks conservatively: a value counts as zero only if every decoder gives
zero). Computed once per sprite and texture contents (the memory watch's
generation of the texels, as the texture cache), cached. */

enum
{
	_sprite_texels_alpha_bit = 1,
	_sprite_texels_color_bit = 2,
};

/* the channels that may be non-zero at texel (x, y) of a level, or -1 when
the format is not read here */
static int sprite_texel_channels(const struct xgpu_texture_description *description, unsigned char kind,
	const unsigned char *level_base, unsigned long level_width, unsigned long level_height,
	const struct swizzle_masks *masks, unsigned long x, unsigned long y)
{
	struct format_information information = format_information(description->format);

	if (kind == _texel_dxt1 || kind == _texel_dxt3 || kind == _texel_dxt5)
	{
		unsigned long blocks_x = (level_width + 3) / 4;
		const unsigned char *block = level_base + ((y / 4) * blocks_x + x / 4) * information.bytes;
		unsigned long index = (y & 3) * 4 + (x & 3);
		const unsigned char *color_block = kind == _texel_dxt1 ? block : block + 8;
		unsigned long c0 = color_block[0] | (color_block[1] << 8), c1 = color_block[2] | (color_block[3] << 8);
		unsigned long bits = color_block[4] | (color_block[5] << 8) | ((unsigned long)color_block[6] << 16) |
			((unsigned long)color_block[7] << 24);
		unsigned long selector = (bits >> (index * 2)) & 3;
		int channels = 0, color_zero, alpha_zero;

		(void)level_height;
		/* colour: an endpoint that is black, a mix of two black endpoints,
		or DXT1's transparent black (index 3 when c0 <= c1) */
		if (selector == 0)
			color_zero = c0 == 0;
		else if (selector == 1)
			color_zero = c1 == 0;
		else if (selector == 3 && kind == _texel_dxt1 && c0 <= c1)
			color_zero = 1;
		else
			color_zero = c0 == 0 && c1 == 0;
		if (kind == _texel_dxt1)
			alpha_zero = selector == 3 && c0 <= c1;
		else if (kind == _texel_dxt3)
			alpha_zero = ((block[index / 2] >> ((index & 1) * 4)) & 0xf) == 0;
		else
		{
			unsigned long a0 = block[0], a1 = block[1], code;
			unsigned long long alpha_bits = 0;
			int bit;

			for (bit = 0; bit < 6; bit++)
				alpha_bits |= (unsigned long long)block[2 + bit] << (bit * 8);
			code = (unsigned long)(alpha_bits >> (index * 3)) & 7;
			if (code == 0)
				alpha_zero = a0 == 0;
			else if (code == 1)
				alpha_zero = a1 == 0;
			else if (a0 <= a1 && code == 6)
				alpha_zero = 1;
			else if (a0 <= a1 && code == 7)
				alpha_zero = 0;
			else
				alpha_zero = a0 == 0 && a1 == 0;
		}
		if (!alpha_zero)
			channels |= _sprite_texels_alpha_bit;
		if (!color_zero)
			channels |= _sprite_texels_color_bit;
		return channels;
	}
	if (kind == _texel_p8 || kind == _texel_yuy2 || kind == _texel_uyvy || kind == _texel_unknown)
		return -1;
	{
		const unsigned char *texel = description->linear
			? level_base + y * description->pitch + x * information.bytes
			: level_base + (spread(masks->x, x) | spread(masks->y, y)) * information.bytes;
		unsigned long value = convert_texel(kind, texel, NULL, x, texel);

		return ((value >> 24) ? _sprite_texels_alpha_bit : 0) | ((value & 0x00ffffffUL) ? _sprite_texels_color_bit : 0);
	}
}

struct sprite_texels_entry
{
	DWORD data, format_word, size_word;
	uint32_t bounds_bits[4];
	unsigned long generation, checked_serial, last_used;
	int valid;
	/* per channel set (alpha; colour; either): empty, rectangle, diagonals */
	/* per channel set (alpha; colour; either): x0 y0 x1 y1 sum0 sum1
	difference0 difference1 in eighths of a texel, outward; empty sets'
	bits in empty */
	short packed[3][8];
	unsigned char empty;
};

struct vita_sprite_texels { int empty; float x0, y0, x1, y1, sum0, sum1, difference0, difference1; };

#define SPRITE_TEXELS_ENTRIES 1024
#define SPRITE_TEXELS_WAYS 4
static struct sprite_texels_entry sprite_texels_entries[SPRITE_TEXELS_ENTRIES];
static unsigned long sprite_texels_clock, sprite_texels_frame, sprite_texels_frame_count;
volatile unsigned long vita_sprite_texels_computed, vita_sprite_texels_texels;

/* every set of channels of one sprite's rectangle at once: alpha, colour,
either */
static int sprite_texels_compute(const unsigned char *base, const struct xgpu_texture_description *description,
	const float bounds[4], struct vita_sprite_texels result[3])
{
	unsigned char kind = format_information(description->format).kind;
	float u0 = bounds[0] < bounds[2] ? bounds[0] : bounds[2], u1 = bounds[0] < bounds[2] ? bounds[2] : bounds[0];
	float v0 = bounds[1] < bounds[3] ? bounds[1] : bounds[3], v1 = bounds[1] < bounds[3] ? bounds[3] : bounds[1];
	float width = (float)description->width, height = (float)description->height;
	unsigned long levels = description->linear ? 1 : description->levels, level;
	int set;

	if (description->cube_map || description->depth > 1 || !(u0 >= -4.0f && u1 <= 4.0f && v0 >= -4.0f && v1 <= 4.0f))
		return 0;
	for (set = 0; set < 3; set++)
	{
		result[set].empty = 1;
		result[set].x0 = result[set].y0 = result[set].sum0 = result[set].difference0 = 1.0e30f;
		result[set].x1 = result[set].y1 = result[set].sum1 = result[set].difference1 = -1.0e30f;
	}
	for (level = 0; level < levels; level++)
	{
		unsigned long level_width = level_dimension(description->width, level);
		unsigned long level_height = level_dimension(description->height, level);
		const unsigned char *level_base = base + xgpu_texture_level_offset(description, level);
		struct swizzle_masks masks = swizzle_masks(level_width, level_height, 1);
		/* level-0 texels per texel of this level */
		float scale_x = width / (float)level_width, scale_y = height / (float)level_height;
		long first_x = (long)floorf(u0 * level_width - 0.5f), last_x = (long)floorf(u1 * level_width - 0.5f) + 1;
		long first_y = (long)floorf(v0 * level_height - 0.5f), last_y = (long)floorf(v1 * level_height - 0.5f) + 1;
		long i, j;

		if ((last_x - first_x + 1) * (last_y - first_y + 1) > 1024L * 1024L)
			return 0;
		for (j = first_y; j <= last_y; j++)
		{
			unsigned long y = (unsigned long)(((j % (long)level_height) + (long)level_height) % (long)level_height);

			for (i = first_x; i <= last_x; i++)
			{
				unsigned long x = (unsigned long)(((i % (long)level_width) + (long)level_width) % (long)level_width);
				int channels = sprite_texel_channels(description, kind, level_base, level_width, level_height, &masks, x, y);
				/* the samples this texel reaches, in level-0 texels */
				float rx0 = (i - 0.5f) * scale_x, rx1 = (i + 1.5f) * scale_x;
				float ry0 = (j - 0.5f) * scale_y, ry1 = (j + 1.5f) * scale_y;

				if (channels < 0)
					return 0;
				vita_sprite_texels_texels++;
				for (set = 0; set < 3; set++)
				{
					int mask = set == 0 ? _sprite_texels_alpha_bit : set == 1 ? _sprite_texels_color_bit :
						_sprite_texels_alpha_bit | _sprite_texels_color_bit;
					struct vita_sprite_texels *r = &result[set];

					if (!(channels & mask))
						continue;
					r->empty = 0;
					if (rx0 < r->x0) r->x0 = rx0;
					if (rx1 > r->x1) r->x1 = rx1;
					if (ry0 < r->y0) r->y0 = ry0;
					if (ry1 > r->y1) r->y1 = ry1;
					if (rx0 + ry0 < r->sum0) r->sum0 = rx0 + ry0;
					if (rx1 + ry1 > r->sum1) r->sum1 = rx1 + ry1;
					if (rx0 - ry1 < r->difference0) r->difference0 = rx0 - ry1;
					if (rx1 - ry0 > r->difference1) r->difference1 = rx1 - ry0;
				}
			}
		}
	}
	for (set = 0; set < 3; set++)
	{
		/* a margin of a sixteenth of a texel against the GPU's coordinate
		precision */
		struct vita_sprite_texels *r = &result[set];

		r->x0 -= 0.0625f; r->y0 -= 0.0625f; r->sum0 -= 0.125f; r->difference0 -= 0.125f;
		r->x1 += 0.0625f; r->y1 += 0.0625f; r->sum1 += 0.125f; r->difference1 += 0.125f;
	}
	vita_sprite_texels_computed++;
	return 1;
}

/* resource: the bitmap's Direct3D texture header; bounds: the sprite's UV
rectangle (x0, y0, x1, y1); channels: 1 alpha, 2 colour, 3 either. 0 when
unknown (draw the whole quad); otherwise *texels and the texture's level-0
size */
int vita_sprite_texel_bounds(const void *resource_pointer, const float bounds[4], int channels,
	struct vita_sprite_texels *texels, float *width, float *height)
{
	const DWORD *resource = resource_pointer;
	DWORD data, format_word, size_word;
	struct sprite_texels_entry *entry;
	uint32_t bits[4];
	unsigned long hash, address, size;
	struct xgpu_texture_description description;

	if (!resource || channels < 1 || channels > 3)
		return 0;
	data = resource[1];
	format_word = resource[3];
	size_word = resource[4];
	memcpy(bits, bounds, sizeof(bits));
	{
		/* FNV-1a over the key's words, murmur3's finaliser */
		uint32_t words[7], h = 2166136261u;
		int index;

		words[0] = data; words[1] = format_word; words[2] = size_word;
		memcpy(words + 3, bits, sizeof(bits));
		for (index = 0; index < 7; index++)
			h = (h ^ words[index]) * 16777619u;
		h ^= h >> 16; h *= 0x85ebca6bu; h ^= h >> 13; h *= 0xc2b2ae35u; h ^= h >> 16;
		hash = h;
	}
	xgpu_texture_describe(format_word, size_word, &description);
	address = (unsigned long)PLATFORM_PHYSICAL_TO_VIRTUAL(data);
	size = xgpu_texture_face_size(&description);
	{
		/* four ways a set; a miss takes the way used longest ago */
		struct sprite_texels_entry *set = &sprite_texels_entries[(hash % (SPRITE_TEXELS_ENTRIES / SPRITE_TEXELS_WAYS)) *
			SPRITE_TEXELS_WAYS];
		int way;

		entry = NULL;
		for (way = 0; way < SPRITE_TEXELS_WAYS; way++)
		{
			struct sprite_texels_entry *candidate = &set[way];

			if (candidate->valid && candidate->data == data && candidate->format_word == format_word &&
				candidate->size_word == size_word && !memcmp(candidate->bounds_bits, bits, sizeof(bits)))
			{
				entry = candidate;
				break;
			}
		}
		if (!entry)
		{
			entry = &set[0];
			for (way = 1; way < SPRITE_TEXELS_WAYS; way++)
				if (set[way].last_used < entry->last_used)
					entry = &set[way];
			entry->valid = 0;
		}
		entry->last_used = ++sprite_texels_clock;
	}
	if (!(entry->valid &&
		(entry->checked_serial == memory_watch_serial() || entry->generation == memory_watch_generation(address, size))))
	{
		entry->valid = 0;
		if (!platform_is_contiguous((void *)address) || !platform_is_contiguous((void *)(address + size - 1)) ||
			custom_edition_texels_order(address) != _custom_edition_channels_xbox)
			return 0;
		entry->data = data;
		entry->format_word = format_word;
		entry->size_word = size_word;
		memcpy(entry->bounds_bits, bits, sizeof(bits));
		entry->generation = memory_watch_generation(address, size);
		{
			struct vita_sprite_texels result[3];
			int set, value;

			/* (at most 16 sprites' bounds a frame: an explosion's first
			frame draws the rest whole) */
			if (sprite_texels_frame != texture_frame)
			{
				sprite_texels_frame = texture_frame;
				sprite_texels_frame_count = 0;
			}
			if (++sprite_texels_frame_count > 16)
				return 0;
			if (!sprite_texels_compute((const unsigned char *)address, &description, bounds, result))
				return 0;
			entry->empty = 0;
			for (set = 0; set < 3; set++)
			{
				const float *values = &result[set].x0;

				if (result[set].empty)
					entry->empty |= 1 << set;
				for (value = 0; value < 8; value++)
				{
					/* minima rounded down, maxima (x1 y1 sum1
					difference1) up; within +-4095 texels */
					int maximum = value == 2 || value == 3 || value == 5 || value == 7;
					float eighths = values[value] * 8.0f;

					eighths = maximum ? ceilf(eighths) : floorf(eighths);
					if (result[set].empty)
						eighths = 0.0f;
					if (eighths < -32767.0f || eighths > 32767.0f)
						return 0;
					entry->packed[set][value] = (short)eighths;
				}
			}
		}
		entry->valid = 1;
	}
	entry->checked_serial = memory_watch_serial();
	{
		int value;

		texels->empty = (entry->empty >> (channels - 1)) & 1;
		for (value = 0; value < 8; value++)
			(&texels->x0)[value] = entry->packed[channels - 1][value] / 8.0f;
	}
	*width = (float)description.width;
	*height = (float)description.height;
	return 1;
}
