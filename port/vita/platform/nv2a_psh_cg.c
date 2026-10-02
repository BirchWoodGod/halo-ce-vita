/*
NV2A_PSH_CG.C

Xbox pixel shaders as Cg for the Vita's shader compiler: the translation of
port/linux/src/nv2a_psh.c, with the uniforms read from BUFFER[0] (vita_xgpu.h)
and the varyings the Vita's vertex programs write (nv2a_vsh_cg.c).

(The original's description follows.)
Translation of Xbox pixel shaders - the NV2A texture shader stages and
register combiners, as held in the pixel shader render states - into GLSL.

A pixel shader runs in three parts:
- texture stages 0-3 (PSTextureModes) fetch t0-t3, some of them using the
  result of an earlier stage (dot product and bump environment modes);
- up to eight general combiner stages (PSRGBInputs/Outputs and
  PSAlphaInputs/Outputs) that each compute A*B, C*D and their sum or mux
  on the registers and write them back;
- the final combiner (PSFinalCombinerInputsABCD/EFG):
  rgb = A*B + (1-A)*C + D, alpha = G.
Register values are clamped to [-1, 1] between stages, as on the hardware.
*/

#include "vita_xgpu.h"
#include "port_config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* HALO_FRAG_PRECISION=float puts the colour arithmetic back in fp32. The
default is half: the register combiners it emulates are 9-bit fixed point,
so fp16 is nearer the Xbox than fp32 is, and the SGX runs it at twice the
rate (the full-precision combiners were most of the GPU's frame). Texture
coordinates and dot products that feed fetches stay float. */
static const char *precision_type(const char *half_type, const char *float_type)
{
	static int use_float = -1;

	if (use_float < 0)
	{
		const char *setting = getenv("HALO_FRAG_PRECISION");
		use_float = setting && strcmp(setting, "float") == 0;
	}
	return use_float ? float_type : half_type;
}
#define H1 precision_type("half", "float")
#define H3 precision_type("half3", "float3")
#define H4 precision_type("half4", "float4")


enum
{
	_register_zero = 0,
	_register_c0 = 1,
	_register_c1 = 2,
	_register_fog = 3,
	_register_v0 = 4,
	_register_v1 = 5,
	_register_t0 = 8,
	_register_t1 = 9,
	_register_t2 = 10,
	_register_t3 = 11,
	_register_r0 = 12,
	_register_r1 = 13,
	_register_v1r0_sum = 14,
	_register_ef_product = 15,
};

enum
{
	_mode_none = 0x00,
	_mode_project2d = 0x01,
	_mode_project3d = 0x02,
	_mode_cubemap = 0x03,
	_mode_passthru = 0x04,
	_mode_clipplane = 0x05,
	_mode_bumpenvmap = 0x06,
	_mode_bumpenvmap_luminance = 0x07,
	_mode_brdf = 0x08,
	_mode_dot_st = 0x09,
	_mode_dot_zw = 0x0a,
	_mode_dot_reflect_diffuse = 0x0b,
	_mode_dot_reflect_specular = 0x0c,
	_mode_dot_str_3d = 0x0d,
	_mode_dot_str_cube = 0x0e,
	_mode_dependent_ar = 0x0f,
	_mode_dependent_gb = 0x10,
	_mode_dot_product = 0x11,
	_mode_dot_reflect_specular_constant = 0x12,
};

/* ---------- combiner inputs */

/* a fragment uniform register by name: fuA[] (BUFFER[0]: the combiner
constants c0, c1 and the final combiner's, which change per material and
part) or fuB[] (BUFFER[1]: fog, the alpha reference, bump and texture
scale, which change per view) - two buffers, so a part's constants do not
re-copy the rest (vita_xgpu.h) */
static const char *fu(int index)
{
	static char buffer[8][24];
	static int next = 0;
	char *result = buffer[next++ & 7];

	if (index < VITA_FU_A_COUNT)
		snprintf(result, sizeof(buffer[0]), "fuA[%d]", index);
	else
		snprintf(result, sizeof(buffer[0]), "fuB[%d]", index - VITA_FU_A_COUNT);
	return result;
}

static const char *register_expression(unsigned long reg, int stage, BOOL unique_c0, BOOL unique_c1)
{
	static char buffer[4][32];
	static int next = 0;
	char *result = buffer[next++ & 3];

	switch (reg)
	{
	case _register_c0:
		if (stage < 0)
			return fu(VITA_FU_PS_FINAL_C0);
		return fu(VITA_FU_PS_C0 + (unique_c0 ? stage : 0));
	case _register_c1:
		if (stage < 0)
			return fu(VITA_FU_PS_FINAL_C1);
		return fu(VITA_FU_PS_C1 + (unique_c1 ? stage : 0));
	case _register_fog: return "fog";
	case _register_v0: return "v0";
	case _register_v1: return "v1";
	case _register_t0: return "t0";
	case _register_t1: return "t1";
	case _register_t2: return "t2";
	case _register_t3: return "t3";
	case _register_r0: return "r0";
	case _register_r1: return "r1";
	case _register_v1r0_sum: return stage < 0 ? "v1r0_sum" : "half4(0.0h)";
	case _register_ef_product: return stage < 0 ? "ef_product" : "half4(0.0h)";
	default: return "half4(0.0h)";
	}
}

/* one combiner input byte as a float3 (rgb) or float (alpha) expression */
static void combiner_input(struct xgpu_text *text, unsigned long input, BOOL alpha_portion, int stage,
	BOOL unique_c0, BOOL unique_c1)
{
	unsigned long reg = input & 0x0f;
	BOOL alpha_channel = (input & 0x10) != 0;
	unsigned long mapping = input & 0xe0;
	const char *source = register_expression(reg, stage, unique_c0, unique_c1);
	char value[64];

	if (alpha_portion)
		snprintf(value, sizeof(value), "%s.%s", source, alpha_channel ? "a" : "b");
	else if (alpha_channel)
		snprintf(value, sizeof(value), "(float3)(%s.a)", source);
	else
		snprintf(value, sizeof(value), "%s.rgb", source);

	switch (mapping)
	{
	case 0x00: xgpu_text_append(text, "max(%s, 0.0h)", value); break;
	case 0x20: xgpu_text_append(text, "(1.0h - clamp(%s, 0.0h, 1.0h))", value); break;
	case 0x40: xgpu_text_append(text, "(2.0h * max(%s, 0.0h) - 1.0h)", value); break;
	case 0x60: xgpu_text_append(text, "(1.0h - 2.0h * max(%s, 0.0h))", value); break;
	case 0x80: xgpu_text_append(text, "(max(%s, 0.0h) - 0.5h)", value); break;
	case 0xa0: xgpu_text_append(text, "(0.5h - max(%s, 0.0h))", value); break;
	case 0xc0: xgpu_text_append(text, "(%s)", value); break;
	default: xgpu_text_append(text, "(-%s)", value); break;
	}
}

/* final combiner inputs only have the unsigned identity and invert mappings */
static void final_input(struct xgpu_text *text, unsigned long input, BOOL alpha_portion)
{
	unsigned long reg = input & 0x0f;
	BOOL alpha_channel = (input & 0x10) != 0;
	const char *source = register_expression(reg, -1, FALSE, FALSE);
	char value[64];

	if (alpha_portion)
		snprintf(value, sizeof(value), "%s.%s", source, alpha_channel ? "a" : "b");
	else if (alpha_channel)
		snprintf(value, sizeof(value), "(float3)(%s.a)", source);
	else
		snprintf(value, sizeof(value), "%s.rgb", source);
	if (input & 0x20)
		xgpu_text_append(text, "(1.0h - clamp(%s, 0.0h, 1.0h))", value);
	else
		xgpu_text_append(text, "clamp(%s, 0.0h, 1.0h)", value);
}

static const char *destination_name(unsigned long reg)
{
	switch (reg)
	{
	case _register_v0: return "v0";
	case _register_v1: return "v1";
	case _register_t0: return "t0";
	case _register_t1: return "t1";
	case _register_t2: return "t2";
	case _register_t3: return "t3";
	case _register_r0: return "r0";
	case _register_r1: return "r1";
	default: return NULL;
	}
}

static const char *output_mapping(unsigned long flags)
{
	switch (flags & 0x38)
	{
	case 0x08: return "(%s - 0.5h)";
	case 0x10: return "(%s * 2.0h)";
	case 0x18: return "((%s - 0.5h) * 2.0h)";
	case 0x20: return "(%s * 4.0h)";
	case 0x30: return "(%s * 0.5h)";
	default: return "(%s)";
	}
}

/* ---------- one general combiner stage */

static void combiner_stage(struct xgpu_text *text, const DWORD *state, int stage)
{
	DWORD combiner_count = state[D3DRS_PSCOMBINERCOUNT];
	BOOL unique_c0 = (combiner_count & 0x1000) != 0;
	BOOL unique_c1 = (combiner_count & 0x10000) != 0;
	BOOL mux_msb = (combiner_count & 0x100) != 0;
	int portion;

	xgpu_text_append(text, "\t/* combiner stage %d */\n\t{\n", stage);
	for (portion = 0; portion < 2; portion++)
	{
		BOOL alpha = portion == 1;
		DWORD inputs = alpha ? state[D3DRS_PSALPHAINPUTS0 + stage] : state[D3DRS_PSRGBINPUTS0 + stage];
		DWORD outputs = alpha ? state[D3DRS_PSALPHAOUTPUTS0 + stage] : state[D3DRS_PSRGBOUTPUTS0 + stage];
		unsigned long flags = outputs >> 12;
		const char *type = alpha ? H1 : H3;
		const char *prefix = alpha ? "a" : "c";
		const char *mapping = output_mapping(flags);
		char mapped[64];

		xgpu_text_append(text, "\t\t%s %sA = ", type, prefix);
		combiner_input(text, (inputs >> 24) & 0xff, alpha, stage, unique_c0, unique_c1);
		xgpu_text_append(text, ";\n\t\t%s %sB = ", type, prefix);
		combiner_input(text, (inputs >> 16) & 0xff, alpha, stage, unique_c0, unique_c1);
		xgpu_text_append(text, ";\n\t\t%s %sC = ", type, prefix);
		combiner_input(text, (inputs >> 8) & 0xff, alpha, stage, unique_c0, unique_c1);
		xgpu_text_append(text, ";\n\t\t%s %sD = ", type, prefix);
		combiner_input(text, inputs & 0xff, alpha, stage, unique_c0, unique_c1);
		xgpu_text_append(text, ";\n");

		if (!alpha && (flags & 0x02))
			xgpu_text_append(text, "\t\t%s cAB = (%s)(dot(cA, cB));\n", H3, H3);
		else
			xgpu_text_append(text, "\t\t%s %sAB = %sA * %sB;\n", type, prefix, prefix, prefix);
		if (!alpha && (flags & 0x01))
			xgpu_text_append(text, "\t\t%s cCD = (%s)(dot(cC, cD));\n", H3, H3);
		else
			xgpu_text_append(text, "\t\t%s %sCD = %sC * %sD;\n", type, prefix, prefix, prefix);
		if (flags & 0x04)
		{
			if (mux_msb)
				xgpu_text_append(text, "\t\t%s %sSUM = r0.a >= 0.5h ? %sCD : %sAB;\n", type, prefix, prefix, prefix);
			else
				xgpu_text_append(text, "\t\t%s %sSUM = fmod(floor(r0.a * 255.0 + 0.5), 2.0) != 0.0 ? %sCD : %sAB;\n",
					type, prefix, prefix, prefix);
		}
		else
		{
			xgpu_text_append(text, "\t\t%s %sSUM = %sAB + %sCD;\n", type, prefix, prefix, prefix);
		}
		snprintf(mapped, sizeof(mapped), mapping, "%sAB");
		xgpu_text_append(text, "\t\t%sAB = clamp(", prefix);
		xgpu_text_append(text, mapped, prefix);
		xgpu_text_append(text, ", -1.0h, 1.0h);\n");
		snprintf(mapped, sizeof(mapped), mapping, "%sCD");
		xgpu_text_append(text, "\t\t%sCD = clamp(", prefix);
		xgpu_text_append(text, mapped, prefix);
		xgpu_text_append(text, ", -1.0h, 1.0h);\n");
		snprintf(mapped, sizeof(mapped), mapping, "%sSUM");
		xgpu_text_append(text, "\t\t%sSUM = clamp(", prefix);
		xgpu_text_append(text, mapped, prefix);
		xgpu_text_append(text, ", -1.0h, 1.0h);\n");
	}

	/* write back only after both portions have read their inputs */
	for (portion = 0; portion < 2; portion++)
	{
		BOOL alpha = portion == 1;
		DWORD outputs = alpha ? state[D3DRS_PSALPHAOUTPUTS0 + stage] : state[D3DRS_PSRGBOUTPUTS0 + stage];
		unsigned long flags = outputs >> 12;
		const char *prefix = alpha ? "a" : "c";
		const char *component = alpha ? "a" : "rgb";
		const char *ab = destination_name((outputs >> 4) & 0x0f);
		const char *cd = destination_name(outputs & 0x0f);
		const char *sum = destination_name((outputs >> 8) & 0x0f);

		if (ab)
		{
			xgpu_text_append(text, "\t\t%s.%s = %sAB;\n", ab, component, prefix);
			if (!alpha && (flags & 0x80))
				xgpu_text_append(text, "\t\t%s.a = cAB.b;\n", ab);
		}
		if (cd)
		{
			xgpu_text_append(text, "\t\t%s.%s = %sCD;\n", cd, component, prefix);
			if (!alpha && (flags & 0x40))
				xgpu_text_append(text, "\t\t%s.a = cCD.b;\n", cd);
		}
		if (sum)
			xgpu_text_append(text, "\t\t%s.%s = %sSUM;\n", sum, component, prefix);
	}
	xgpu_text_append(text, "\t}\n");
}

/* ---------- texture stages */

static const char *sampler_declaration(unsigned char type)
{
	switch (type)
	{
	/* GXM has no volume textures: a 3D sampler reads its first slice */
	case _xgpu_sampler_3d: return "sampler2D";
	case _xgpu_sampler_cube: return "samplerCUBE";
	default: return "sampler2D";
	}
}

static unsigned long stage_mode(const struct nv2a_pixel_shader_key *key, int stage)
{
	return (key->texture_modes >> (5 * stage)) & 0x1f;
}

static int stage_input(const DWORD *state, int stage)
{
	switch (stage)
	{
	case 2: return (state[D3DRS_PSINPUTTEXTURE] >> 16) & 1;
	case 3: return (state[D3DRS_PSINPUTTEXTURE] >> 20) & 3;
	default: return 0;
	}
}

/* the input texel of a dot product stage, mapped per PSDotMapping */
static void dot_input(struct xgpu_text *text, const DWORD *state, int stage)
{
	unsigned long mapping = (state[D3DRS_PSDOTMAPPING] >> ((stage - 1) * 4)) & 7;
	int input = stage_input(state, stage);

	switch (mapping)
	{
	case 0: xgpu_text_append(text, "t%d.rgb", input); break;
	case 1: xgpu_text_append(text, "((t%d.rgb * 255.0 - 128.0) / 127.0)", input); break;
	case 3: xgpu_text_append(text, "signed_bytes(t%d.rgb)", input); break;
	default: xgpu_text_append(text, "(t%d.rgb * 2.0 - 1.0)", input); break;
	}
}

/* the texture's LOD bias is part of its GXM sampler state (d3d8_gxm.c);
a stage addressed with D3DTADDRESS_BORDER returns its border colour where
the (normalised) coordinate leaves the texture (d3d8_gxm.c key_border) */
static void sample(struct xgpu_text *text, const struct nv2a_pixel_shader_key *key, int stage, const char *coordinates)
{
	int border = (key->border_mask >> stage) & 1;
	char inside[200];

	inside[0] = 0;
	switch (key->sampler_type[stage])
	{
	case _xgpu_sampler_cube:
		border = 0;
		xgpu_text_append(text, "texCUBE(tex%d, (%s).xyz)", stage, coordinates);
		break;
	default:
		/* (mode 3 of HALO_SIMPLE_FRAG_MODE: the varying itself as the
		coordinate, so the fetch does not depend on fragment arithmetic:
		the SGX prefetches such reads) */
		if ((key->pad == 3 || (key->raw_coordinates & (1U << stage))) && strncmp(coordinates, "float4(xT", 9) == 0)
		{
			snprintf(inside, sizeof(inside), "xT%d.xy", stage);
			if (border)
				xgpu_text_append(text, "border2(");
			xgpu_text_append(text, "tex2D(tex%d, xT%d.xy)", stage, stage);
		}
		else if ((key->projective_coordinates & (1U << stage)) && strncmp(coordinates, "float4(xT", 9) == 0)
		{
			snprintf(inside, sizeof(inside), "xT%d.xy / xT%d.w", stage, stage);
			if (border)
				xgpu_text_append(text, "border2(");
			xgpu_text_append(text, "tex2Dproj(tex%d, float3(xT%d.x, xT%d.y, xT%d.w))", stage, stage, stage, stage);
		}
		else
		{
			snprintf(inside, sizeof(inside), "(%s).xy * %s.xy", coordinates, fu(VITA_FU_TEXTURE_SCALE + stage));
			if (border)
				xgpu_text_append(text, "border2(");
			xgpu_text_append(text, "tex2D(tex%d, (%s).xy * %s.xy)", stage, coordinates, fu(VITA_FU_TEXTURE_SCALE + stage));
		}
		break;
	}
	if (border)
	{
		DWORD color = key->border_color[stage];

		xgpu_text_append(text, ", %s, float4(%.6f, %.6f, %.6f, %.6f))", inside,
			((color >> 16) & 0xff) / 255.0, ((color >> 8) & 0xff) / 255.0, (color & 0xff) / 255.0, ((color >> 24) & 0xff) / 255.0);
	}
}

static void texture_stage(struct xgpu_text *text, const struct nv2a_pixel_shader_key *key, int stage)
{
	const DWORD *state = key->combiner_state;
	unsigned long mode = stage_mode(key, stage);
	char coordinates[96];

	xgpu_text_append(text, "\t/* texture stage %d, mode %lu */\n", stage, mode);
	if (key->sampler_type[stage] == _xgpu_sampler_none &&
		mode != _mode_passthru && mode != _mode_clipplane && mode != _mode_dot_product && mode != _mode_dot_zw)
	{
		mode = _mode_none;
	}
	switch (mode)
	{
	case _mode_project2d:
	case _mode_project3d:
		snprintf(coordinates, sizeof(coordinates), "float4(xT%d.xyz / (xT%d.w != 0.0 ? xT%d.w : 1.0), 1.0)", stage, stage, stage);
		xgpu_text_append(text, "\tt%d = ", stage);
		sample(text, key, stage, coordinates);
		xgpu_text_append(text, ";\n");
		break;
	case _mode_cubemap:
		snprintf(coordinates, sizeof(coordinates), "xT%d", stage);
		xgpu_text_append(text, "\tt%d = ", stage);
		sample(text, key, stage, coordinates);
		xgpu_text_append(text, ";\n");
		break;
	case _mode_passthru:
		xgpu_text_append(text, "\tt%d = clamp(xT%d, 0.0, 1.0);\n", stage, stage);
		break;
	case _mode_clipplane:
	{
		unsigned long compare = (state[D3DRS_PSCOMPAREMODE] >> (4 * stage)) & 0xf;
		static const char components[] = "xyzw";
		int component;

		for (component = 0; component < 4; component++)
		{
			xgpu_text_append(text, "\tif (xT%d.%c %s 0.0) discard;\n", stage, components[component],
				(compare & (1 << component)) ? ">=" : "<");
		}
		xgpu_text_append(text, "\tt%d = float4(0.0);\n", stage);
		break;
	}
	case _mode_bumpenvmap:
	case _mode_bumpenvmap_luminance:
	{
		int input = stage - 1;

		xgpu_text_append(text, "\t{\n\t\tfloat2 d = signed_bytes(t%d.rgb).rg;\n", input);
		xgpu_text_append(text, "\t\tfloat4 m = %s;\n", fu(VITA_FU_BUMP_MATRIX + stage));
		xgpu_text_append(text, "\t\tfloat2 coordinates = xT%d.xy + float2(m.x * d.x + m.z * d.y, m.y * d.x + m.w * d.y);\n", stage);
		xgpu_text_append(text, "\t\tt%d = ", stage);
		sample(text, key, stage, "float4(coordinates, 0.0, 1.0)");
		xgpu_text_append(text, ";\n");
		if (mode == _mode_bumpenvmap_luminance)
		{
			xgpu_text_append(text, "\t\tt%d.rgb *= clamp(%s.x * t%d.b + %s.y, 0.0, 1.0);\n",
				stage, fu(VITA_FU_BUMP_LUMINANCE + stage), input, fu(VITA_FU_BUMP_LUMINANCE + stage));
		}
		xgpu_text_append(text, "\t}\n");
		break;
	}
	case _mode_dot_product:
		xgpu_text_append(text, "\tdot%d = dot(xT%d.xyz, ", stage, stage);
		dot_input(text, state, stage);
		xgpu_text_append(text, ");\n\tt%d = float4(0.0);\n", stage);
		break;
	case _mode_dot_st:
		xgpu_text_append(text, "\tdot%d = dot(xT%d.xyz, ", stage, stage);
		dot_input(text, state, stage);
		xgpu_text_append(text, ");\n\tt%d = ", stage);
		snprintf(coordinates, sizeof(coordinates), "float4(dot%d, dot%d, 0.0, 1.0)", stage - 1, stage);
		sample(text, key, stage, coordinates);
		xgpu_text_append(text, ";\n");
		break;
	case _mode_dot_zw:
		xgpu_text_append(text, "\tdot%d = dot(xT%d.xyz, ", stage, stage);
		dot_input(text, state, stage);
		xgpu_text_append(text, ");\n\tt%d = float4(0.0);\n", stage);
		break;
	case _mode_dot_reflect_diffuse:
		/* the normal takes its third component from stage 3's dot product */
		xgpu_text_append(text, "\tdot%d = dot(xT%d.xyz, ", stage, stage);
		dot_input(text, state, stage);
		xgpu_text_append(text, ");\n\tdot3 = dot(xT3.xyz, ");
		dot_input(text, state, 3);
		xgpu_text_append(text, ");\n\tt%d = ", stage);
		sample(text, key, stage, "float4(dot1, dot2, dot3, 1.0)");
		xgpu_text_append(text, ";\n");
		break;
	case _mode_dot_reflect_specular:
	case _mode_dot_reflect_specular_constant:
		xgpu_text_append(text, "\tdot%d = dot(xT%d.xyz, ", stage, stage);
		dot_input(text, state, stage);
		xgpu_text_append(text, ");\n\t{\n\t\tfloat3 n = float3(dot1, dot2, dot3);\n");
		if (mode == _mode_dot_reflect_specular)
			xgpu_text_append(text, "\t\tfloat3 e = float3(xT1.w, xT2.w, xT3.w);\n");
		else
			xgpu_text_append(text, "\t\tfloat3 e = %s.xyz;\n", fu(0));
		xgpu_text_append(text, "\t\tfloat3 r = 2.0 * n * dot(n, e) / max(dot(n, n), 1.0e-20) - e;\n\t\tt%d = ", stage);
		sample(text, key, stage, "float4(r, 1.0)");
		xgpu_text_append(text, ";\n\t}\n");
		break;
	case _mode_dot_str_3d:
	case _mode_dot_str_cube:
		xgpu_text_append(text, "\tdot%d = dot(xT%d.xyz, ", stage, stage);
		dot_input(text, state, stage);
		xgpu_text_append(text, ");\n\tt%d = ", stage);
		sample(text, key, stage, "float4(dot1, dot2, dot3, 1.0)");
		xgpu_text_append(text, ";\n");
		break;
	case _mode_dependent_ar:
		snprintf(coordinates, sizeof(coordinates), "float4(t%d.a, t%d.r, 0.0, 1.0)", stage_input(state, stage), stage_input(state, stage));
		xgpu_text_append(text, "\tt%d = ", stage);
		sample(text, key, stage, coordinates);
		xgpu_text_append(text, ";\n");
		break;
	case _mode_dependent_gb:
		snprintf(coordinates, sizeof(coordinates), "float4(t%d.g, t%d.b, 0.0, 1.0)", stage_input(state, stage), stage_input(state, stage));
		xgpu_text_append(text, "\tt%d = ", stage);
		sample(text, key, stage, coordinates);
		xgpu_text_append(text, ";\n");
		break;
	default:
		xgpu_text_append(text, "\tt%d = float4(0.0);\n", stage);
		break;
	}

	/* channels the application marked signed (D3DTSS_COLORSIGN) */
	if (key->color_sign[stage] && mode != _mode_none)
	{
		static const char channels[] = "argb";
		int bit;

		for (bit = 0; bit < 4; bit++)
		{
			if (key->color_sign[stage] & (1 << bit))
				xgpu_text_append(text, "\tt%d.%c = signed_byte(t%d.%c);\n", stage, channels[bit], stage, channels[bit]);
		}
	}
	if (key->alpha_kill[stage] && mode != _mode_none)
		xgpu_text_append(text, "\tif (t%d.a == 0.0) discard;\n", stage);
}

/* ---------- the whole shader */

static const char *comparison_operator(unsigned long function)
{
	switch (function)
	{
	case D3DCMP_NEVER: return NULL;
	case D3DCMP_LESS: return "<";
	case D3DCMP_EQUAL: return "==";
	case D3DCMP_LESSEQUAL: return "<=";
	case D3DCMP_GREATER: return ">";
	case D3DCMP_NOTEQUAL: return "!=";
	case D3DCMP_GREATEREQUAL: return ">=";
	default: return "";
	}
}

char *nv2a_pixel_shader_to_cg(const struct nv2a_pixel_shader_key *key)
{
	const DWORD *state = key->combiner_state;
	struct xgpu_text text = { 0 };
	unsigned long combiner_count = state[D3DRS_PSCOMBINERCOUNT] & 0xff;
	DWORD final_abcd = state[D3DRS_PSFINALCOMBINERINPUTSABCD];
	DWORD final_efg = state[D3DRS_PSFINALCOMBINERINPUTSEFG];
	int stage;

	if (combiner_count > 8)
		combiner_count = 8;

	xgpu_text_append(&text,
		"float signed_byte(float x)\n"
		"{\n"
		"	float b = floor(x * 255.0 + 0.5);\n"
		"	return (b >= 128.0 ? b - 256.0 : b) / 127.0;\n"
		"}\n"
		"float3 signed_bytes(float3 x)\n"
		"{\n"
		"	return float3(signed_byte(x.r), signed_byte(x.g), signed_byte(x.b));\n"
		"}\n");
	if (key->border_mask)
	{
		xgpu_text_append(&text,
			"float4 border2(float4 t, float2 c, float4 b)\n"
			"{\n"
			"	return (c.x < 0.0 || c.x > 1.0 || c.y < 0.0 || c.y > 1.0) ? b : t;\n"
			"}\n"
			"float4 border3(float4 t, float3 c, float4 b)\n"
			"{\n"
			"	return (c.x < 0.0 || c.x > 1.0 || c.y < 0.0 || c.y > 1.0 || c.z < 0.0 || c.z > 1.0) ? b : t;\n"
			"}\n");
	}
	xgpu_text_append(&text,
		"float4 main(\n"
		"\tfloat4 xD0 : COLOR0,\n"
		"\tfloat4 xD1 : COLOR1,\n"
		"\tfloat4 xT0 : TEXCOORD0,\n"
		"\tfloat4 xT1 : TEXCOORD1,\n"
		"\tfloat4 xT2 : TEXCOORD2,\n"
		"\tfloat4 xT3 : TEXCOORD3,\n"
		"\tfloat xFog : FOG,\n");
	for (stage = 0; stage < 4; stage++)
	{
		if (key->sampler_type[stage] != _xgpu_sampler_none)
			xgpu_text_append(&text, "\tuniform %s tex%d,\n", sampler_declaration(key->sampler_type[stage]), stage);
	}
	xgpu_text_append(&text,
		"\tuniform float4 fuA[%d] : BUFFER[0],\n"
		"\tuniform float4 fuB[%d] : BUFFER[1]) : COLOR\n"
		"{\n"
		"\t%s v0 = xD0;\n"
		"\t%s v1 = xD1;\n"
		"\t%s t0 = %s(0.0), t1 = %s(0.0), t2 = %s(0.0), t3 = %s(0.0);\n"
		"\tfloat dot0 = 0.0, dot1 = 0.0, dot2 = 0.0, dot3 = 0.0;\n", VITA_FU_A_COUNT, VITA_FU_COUNT - VITA_FU_A_COUNT, H4, H4, H4, H4, H4, H4, H4);
	/* (HALO_SIMPLE_FRAG_BLENDS, d3d8_gxm.c: a constant in place of the
	program, to measure the program's share of a pass's GPU time) */
	if (key->pad == 1)
	{
		xgpu_text_append(&text, "\treturn float4(0.5, 0.5, 0.5, 1.0);\n}\n");
		return text.buffer;
	}
	/* (mode 2: the texture stages as they are, the combiners replaced by a
	product, to split the fetch cost from the combiner arithmetic) */
	if (key->pad == 2)
	{
		for (stage = 0; stage < 4; stage++)
			texture_stage(&text, key, stage);
		xgpu_text_append(&text, "\treturn saturate(t0 * t1 + t2 * 0.5 + t3 * 0.25);\n}\n");
		return text.buffer;
	}

	for (stage = 0; stage < 4; stage++)
		texture_stage(&text, key, stage);

	if (key->fog_enable)
	{
		switch (key->fog_table_mode)
		{
		case D3DFOG_EXP:
			xgpu_text_append(&text, "\tfloat fog_factor = exp(-%s.z * xFog);\n", fu(VITA_FU_FOG_PARAMETERS));
			break;
		case D3DFOG_EXP2:
			xgpu_text_append(&text, "\tfloat fog_factor = exp(-(%s.z * xFog) * (%s.z * xFog));\n",
				fu(VITA_FU_FOG_PARAMETERS), fu(VITA_FU_FOG_PARAMETERS));
			break;
		case D3DFOG_LINEAR:
			xgpu_text_append(&text, "\tfloat fog_factor = (%s.y - xFog) / max(%s.y - %s.x, 1.0e-6);\n",
				fu(VITA_FU_FOG_PARAMETERS), fu(VITA_FU_FOG_PARAMETERS), fu(VITA_FU_FOG_PARAMETERS));
			break;
		default:
			xgpu_text_append(&text, "\tfloat fog_factor = xFog;\n");
			break;
		}
	}
	else
	{
		xgpu_text_append(&text, "\tfloat fog_factor = 1.0;\n");
	}
	xgpu_text_append(&text,
		"\t%s fog = %s(%s.rgb, saturate(fog_factor));\n"
		"\t%s r0 = %s(0.0, 0.0, 0.0, t0.a);\n"
		"\t%s r1 = %s(0.0);\n", H4, H4, fu(VITA_FU_FOG_COLOR), H4, H4, H4, H4);

	for (stage = 0; stage < (int)combiner_count; stage++)
		combiner_stage(&text, state, stage);

	if (final_abcd == 0 && final_efg == 0)
	{
		xgpu_text_append(&text, "\t%s result = r0;\n", H4);
	}
	else
	{
		unsigned long settings = final_efg & 0xff;

		xgpu_text_append(&text, "\t%s ef_product = %s(", H4, H4);
		final_input(&text, (final_efg >> 24) & 0xff, FALSE);
		xgpu_text_append(&text, " * ");
		final_input(&text, (final_efg >> 16) & 0xff, FALSE);
		xgpu_text_append(&text, ", 0.0h);\n");
		xgpu_text_append(&text, "\t%s v1r0_sum = %s(%s + %s, 0.0h);\n", H4, H4,
			(settings & 0x40) ? "(1.0h - saturate(v1.rgb))" : "saturate(v1.rgb)",
			(settings & 0x20) ? "(1.0h - saturate(r0.rgb))" : "saturate(r0.rgb)");
		if (settings & 0x80)
			xgpu_text_append(&text, "\tv1r0_sum = saturate(v1r0_sum);\n");
		xgpu_text_append(&text, "\t%s fA = ", H3);
		final_input(&text, (final_abcd >> 24) & 0xff, FALSE);
		xgpu_text_append(&text, ";\n\t%s fB = ", H3);
		final_input(&text, (final_abcd >> 16) & 0xff, FALSE);
		xgpu_text_append(&text, ";\n\t%s fC = ", H3);
		final_input(&text, (final_abcd >> 8) & 0xff, FALSE);
		xgpu_text_append(&text, ";\n\t%s fD = ", H3);
		final_input(&text, final_abcd & 0xff, FALSE);
		xgpu_text_append(&text, ";\n\t%s fG = ", H1);
		final_input(&text, (final_efg >> 8) & 0xff, TRUE);
		xgpu_text_append(&text, ";\n\t%s result = %s(fA * fB + (1.0h - fA) * fC + fD, fG);\n", H4, H4);
	}

	if (key->alpha_test_function)
	{
		const char *comparison = comparison_operator(key->alpha_test_function);

		if (!comparison)
			xgpu_text_append(&text, "\tdiscard;\n");
		else if (*comparison)
			xgpu_text_append(&text, "\tif (!(floor(saturate((float)result.a) * 255.0 + 0.5) %s %s.x)) discard;\n",
				comparison, fu(VITA_FU_MISCELLANEOUS));
	}
	if (*config_string("debug.gpu_debug_expression"))
		xgpu_text_append(&text, "\tresult = float4((float3)(%s), 1.0);\n", config_string("debug.gpu_debug_expression"));
	if (config_boolean("debug.gpu_debug_texture0"))
		xgpu_text_append(&text, "\tresult = float4(t0.rgb, 1.0);\n");
	if (config_boolean("debug.gpu_debug_flat"))
		xgpu_text_append(&text, "\tresult = xD0.a > 0.0 ? float4(xD0.rgb, 1.0) : float4(1.0, 0.0, 1.0, 1.0);\n");
	xgpu_text_append(&text, "\treturn saturate(result);\n}\n");
	return text.buffer;
}
