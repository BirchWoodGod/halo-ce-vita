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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

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
				for (y = 0; y < 4; y++)
				{
					for (x = 0; x < 4; x++)
					{
						unsigned long px = bx * 4 + x, py = by * 4 + y;

						if (px < width && py < height)
						{
							destination[(z * height + py) * width + px] =
								(colors[y * 4 + x] & 0x00ffffffUL) | (alpha[y * 4 + x] << 24);
						}
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
		/* full: start again, and every texture is decoded again as it is used */
		platform_log("texture pool full (%lu KB): emptied", vgxm_pool_used() / 1024);
		vgxm_pool_reset();
		pool_serial++;
		memory = vgxm_pool_alloc(size, 128);
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

/* decodes the texture at base into the pool; FALSE if it cannot be */
static BOOL texture_build(struct texture_entry *entry, const unsigned char *base, const D3DCOLOR *palette)
{
	const struct xgpu_texture_description *description = &entry->description;
	struct format_information information = format_information(description->format);
	unsigned long width = description->width, height = description->height;
	unsigned long levels = description->levels, level;
	unsigned long *scratch;
	unsigned char *memory;
	unsigned long size;

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
		memory = pool_alloc(face_stride * 6);
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
		return vgxm_texture_initialize(&entry->texture, memory, _vgxm_texture_bgra8, _vgxm_texture_cube,
			width, height, cube_debug == 1 ? 1 : original_levels) == 0;
	}

	if (description->compressed && power_of_two(width) && power_of_two(height) && width >= 4 && height >= 4)
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

				memory = pool_alloc((level_width / 4) * (level_height / 4) * block_bytes);
				if (!memory)
					return FALSE;
				reorder_blocks(base + xgpu_texture_level_offset(description, first_level), memory, level_width, level_height,
					block_bytes);
				return vgxm_texture_initialize(&entry->texture, memory, format, _vgxm_texture_swizzled, level_width,
					level_height, 1) == 0;
			}
		}
		size = 0;
		for (level = 0; level < chained; level++)
			size += (level_dimension(width, level) / 4) * (level_dimension(height, level) / 4) * block_bytes;
		memory = pool_alloc(size);
		if (!memory)
			return FALSE;
		for (level = 0; level < chained; level++)
		{
			unsigned long level_width = level_dimension(width, level), level_height = level_dimension(height, level);

			reorder_blocks(base + xgpu_texture_level_offset(description, level), memory + offset, level_width, level_height,
				block_bytes);
			offset += (level_width / 4) * (level_height / 4) * block_bytes;
		}
		return vgxm_texture_initialize(&entry->texture, memory, format, _vgxm_texture_swizzled, width, height, chained) == 0;
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

		memory = pool_alloc(LINEAR_ROW(atlas_width) * height * 4);
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
		return vgxm_texture_initialize(&entry->texture, memory, _vgxm_texture_bgra8, _vgxm_texture_linear,
			atlas_width, height, 1) == 0;
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
		memory = pool_alloc(size);
		scratch = malloc(width * height * 4);
		if (!memory || !scratch)
		{
			free(scratch);
			return FALSE;
		}
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
			twiddle_level(destination, scratch, level_width, level_height);
			destination += count;
		}
		free(scratch);
		return vgxm_texture_initialize(&entry->texture, memory, _vgxm_texture_bgra8, _vgxm_texture_swizzled,
			width, height, levels) == 0;
	}

	/* everything else as BGRA rows, every Xbox level */
	if (description->linear || !power_of_two(width) || !power_of_two(height))
		levels = 1;
	size = 0;
	for (level = 0; level < levels; level++)
		size += LINEAR_ROW(level_dimension(width, level)) * level_dimension(height, level) * 4;
	memory = pool_alloc(size);
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
	return vgxm_texture_initialize(&entry->texture, memory, _vgxm_texture_bgra8, _vgxm_texture_linear,
		width, height, levels) == 0;
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
	if (entry->valid && entry->generation && entry->pool_serial == pool_serial && entry->checked_serial == memory_watch_serial())
	{
		entry->last_used_frame = texture_frame;
		*description = entry->description;
		return &entry->texture;
	}
	entry->checked_serial = memory_watch_serial();
	generation = memory_watch_generation(entry->address, entry->size);
	if (!entry->generation || generation > entry->generation || entry->pool_serial != pool_serial)
	{
		entry->generation = generation ? generation : 1;
		entry->valid = FALSE;
		if (platform_is_contiguous((void *)entry->address) &&
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
				vita_texture_build_us += vita_host_time_us() - before;
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
	return entry->valid ? &entry->texture : NULL;
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
	texture_frame++;
}
