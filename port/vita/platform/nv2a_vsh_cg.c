/*
NV2A_VSH_CG.C

NV2A vertex programs as Cg for the Vita's shader compiler: the translation of
port/linux/src/nv2a_vsh.c (which see for the instruction format), with the
Vita's conventions:
- inputs are parameters v<n>_in, bound by name (port/vita/host/vita_gxm.c);
  registers no stream feeds read their current value from the uniform buffer
  (vita_xgpu.h), and NORMPACKED3 inputs arrive as four raw bytes;
- the constants are read from the chunk buffers of vita_xgpu.h (BUFFER[0]
  and [2..6]), the other uniforms from BUFFER[1];
- every program writes every varying the pixel shaders read, which GXM
  requires to link them.
*/

#include "vita_xgpu.h"

#include <stdlib.h>

static unsigned long field(const DWORD *instruction, int word, int low_bit, int bit_count)
{
	return (instruction[word] >> low_bit) & ((1UL << bit_count) - 1);
}

enum
{
	_mac_nop, _mac_mov, _mac_mul, _mac_add, _mac_mad, _mac_dp3, _mac_dph, _mac_dp4,
	_mac_dst, _mac_min, _mac_max, _mac_slt, _mac_sge, _mac_arl,
};

enum
{
	_ilu_nop, _ilu_mov, _ilu_rcp, _ilu_rcc, _ilu_rsq, _ilu_exp, _ilu_log, _ilu_lit,
};

enum
{
	_mux_unknown, _mux_temporary, _mux_input, _mux_constant,
};

static const char *output_name(unsigned long address)
{
	switch (address)
	{
	case 0: return "oPos";
	case 3: return "oD0";
	case 4: return "oD1";
	case 5: return "oFog";
	case 6: return "oPts";
	case 7: return "oB0";
	case 8: return "oB1";
	case 9: return "oT0";
	case 10: return "oT1";
	case 11: return "oT2";
	case 12: return "oT3";
	default: return "oUnused";
	}
}

static void write_mask(unsigned long mask, char *out)
{
	int count = 0;

	if (mask & 8) out[count++] = 'x';
	if (mask & 4) out[count++] = 'y';
	if (mask & 2) out[count++] = 'z';
	if (mask & 1) out[count++] = 'w';
	out[count] = 0;
}

static void operand(struct xgpu_text *text, const DWORD *instruction, char which, int relative)
{
	static const char swizzle_names[] = "xyzw";
	unsigned long negate, swizzle[4], index, mux;

	switch (which)
	{
	case 'A':
		negate = field(instruction, 1, 8, 1);
		swizzle[0] = field(instruction, 1, 6, 2);
		swizzle[1] = field(instruction, 1, 4, 2);
		swizzle[2] = field(instruction, 1, 2, 2);
		swizzle[3] = field(instruction, 1, 0, 2);
		index = field(instruction, 2, 28, 4);
		mux = field(instruction, 2, 26, 2);
		break;
	case 'B':
		negate = field(instruction, 2, 25, 1);
		swizzle[0] = field(instruction, 2, 23, 2);
		swizzle[1] = field(instruction, 2, 21, 2);
		swizzle[2] = field(instruction, 2, 19, 2);
		swizzle[3] = field(instruction, 2, 17, 2);
		index = field(instruction, 2, 13, 4);
		mux = field(instruction, 2, 11, 2);
		break;
	default:
		negate = field(instruction, 2, 10, 1);
		swizzle[0] = field(instruction, 2, 8, 2);
		swizzle[1] = field(instruction, 2, 6, 2);
		swizzle[2] = field(instruction, 2, 4, 2);
		swizzle[3] = field(instruction, 2, 2, 2);
		index = (field(instruction, 2, 0, 2) << 2) | field(instruction, 3, 30, 2);
		mux = field(instruction, 3, 28, 2);
		break;
	}

	xgpu_text_append(text, "%s", negate ? "-" : "");
	switch (mux)
	{
	case _mux_temporary:
		if (index == 12)
			xgpu_text_append(text, "oPos");
		else
			xgpu_text_append(text, "r%lu", index);
		break;
	case _mux_input:
		xgpu_text_append(text, "v%lu", field(instruction, 1, 9, 4));
		break;
	case _mux_constant:
	{
		static const int first[VITA_VC_CHUNKS] = VITA_VC_FIRST;
		static const int end[VITA_VC_CHUNKS] = VITA_VC_END;
		static const char *const names[VITA_VC_CHUNKS] = { "cA", "cB", "cC1", "cC2", "cD", "cE" };
		unsigned long constant = field(instruction, 1, 13, 8);
		int chunk;

		for (chunk = 0; chunk < VITA_VC_CHUNKS; chunk++)
			if ((long)constant >= first[chunk] && (long)constant < end[chunk])
				break;
		if (chunk >= VITA_VC_CHUNKS)
			/* (beyond the 192 constants: nothing there on the NV2A either) */
			xgpu_text_append(text, "float4(0.0)");
		else if (relative)
			/* a read relative to a0 stays in its base's chunk: the node
			matrices (bases 60, 61, 62: chunk D) and a model's point lights
			(bases 17, 18, 19: chunk C1, the 11 registers the game writes
			for them) are all the programs index this way */
			xgpu_text_append(text, "%s[(int)clamp(a0 + %ld.0, 0.0, %d.0)]", names[chunk],
				(long)constant - first[chunk], end[chunk] - first[chunk] - 1);
		else
			xgpu_text_append(text, "%s[%ld]", names[chunk], (long)constant - first[chunk]);
		break;
	}
	default:
		xgpu_text_append(text, "float4(0.0)");
		break;
	}
	xgpu_text_append(text, ".%c%c%c%c",
		swizzle_names[swizzle[0]], swizzle_names[swizzle[1]], swizzle_names[swizzle[2]], swizzle_names[swizzle[3]]);
}

static const char shader_prologue[] =
	/* D3DVSDT_NORMPACKED3: 11:11:10 signed, from the attribute's four raw
	bytes (as Xita binds it) */
	"float4 unpack_normpacked3(float4 b)\n"
	"{\n"
	"	float x = b.x + fmod(b.y, 8.0) * 256.0;\n"
	"	float y = floor(b.y / 8.0) + fmod(b.z, 64.0) * 32.0;\n"
	"	float z = floor(b.z / 64.0) + b.w * 4.0;\n"
	"	x = x >= 1024.0 ? x - 2048.0 : x;\n"
	"	y = y >= 1024.0 ? y - 2048.0 : y;\n"
	"	z = z >= 512.0 ? z - 1024.0 : z;\n"
	"	return float4(x / 1023.0, y / 1023.0, z / 511.0, 1.0);\n"
	"}\n"
	"float4 nv2a_rcc(float x)\n"
	"{\n"
	"	float r = 1.0 / x;\n"
	"	r = r > 0.0 ? clamp(r, 5.42101e-20, 1.884467e+19) : clamp(r, -1.884467e+19, -5.42101e-20);\n"
	"	return float4(r);\n"
	"}\n"
	"float4 nv2a_exp(float x)\n"
	"{\n"
	"	return float4(exp2(floor(x)), frac(x), exp2(x), 1.0);\n"
	"}\n"
	"float4 nv2a_log(float x)\n"
	"{\n"
	"	x = abs(x);\n"
	"	float e = floor(log2(x));\n"
	"	return x == 0.0 ? float4(-1.0e30, 1.0, -1.0e30, 1.0) : float4(e, x / exp2(e), log2(x), 1.0);\n"
	"}\n"
	"float4 nv2a_lit(float4 s)\n"
	"{\n"
	"	float specular = s.x > 0.0 ? pow(max(s.y, 0.0), clamp(s.w, -127.9961, 127.9961)) : 0.0;\n"
	"	return float4(1.0, max(s.x, 0.0), specular, 1.0);\n"
	"}\n";

/* the texture coordinate outputs (oT0..3) whose w the program writes; the
others keep the default w of 1 */
unsigned long nv2a_vertex_shader_texcoord_w_mask(const DWORD *instructions, unsigned long instruction_count)
{
	unsigned long index, mask = 0;

	for (index = 0; index < instruction_count; index++)
	{
		const DWORD *instruction = instructions + index * 4;
		unsigned long output_mask = field(instruction, 3, 12, 4);
		unsigned long output_is_register = field(instruction, 3, 11, 1);
		unsigned long output_address = field(instruction, 3, 3, 8);

		/* (the mask's low bit is w, as write_mask lays it out) */
		if (output_mask && output_is_register && output_address >= 9 && output_address <= 12 && (output_mask & 1))
			mask |= 1UL << (output_address - 9);
		if (field(instruction, 3, 0, 1))
			break;
	}
	return mask;
}

/* the input registers (v0..v15) the program reads: an instruction names one
input register, read by whichever of its operands take the input mux */
unsigned long nv2a_vertex_shader_input_mask(const DWORD *instructions, unsigned long instruction_count)
{
	unsigned long index, mask = 0;

	for (index = 0; index < instruction_count; index++)
	{
		const DWORD *instruction = instructions + index * 4;

		if (field(instruction, 2, 26, 2) == _mux_input || field(instruction, 2, 11, 2) == _mux_input ||
			field(instruction, 3, 28, 2) == _mux_input)
			mask |= 1UL << field(instruction, 1, 9, 4);
		if (field(instruction, 3, 0, 1))
			break;
	}
	return mask;
}

void nv2a_vertex_shader_constant_usage(const DWORD *instructions, unsigned long instruction_count,
	struct nv2a_vertex_constant_usage *usage)
{
	static const int first[VITA_VC_CHUNKS] = VITA_VC_FIRST;
	static const int end[VITA_VC_CHUNKS] = VITA_VC_END;
	unsigned long index, low = XGPU_VERTEX_CONSTANT_COUNT, high = 0;

	usage->chunk_mask = 0;
	usage->d_absolute_end = 0;
	usage->relative = 0;
	usage->relative_lowest = XGPU_VERTEX_CONSTANT_COUNT;
	for (index = 0; index < instruction_count; index++)
	{
		const DWORD *instruction = instructions + index * 4;
		unsigned long constant = field(instruction, 1, 13, 8);
		BOOL reads = field(instruction, 2, 26, 2) == _mux_constant || field(instruction, 2, 11, 2) == _mux_constant ||
			field(instruction, 3, 28, 2) == _mux_constant;

		if (reads && constant < XGPU_VERTEX_CONSTANT_COUNT)
		{
			int chunk;

			for (chunk = 0; chunk < VITA_VC_CHUNKS; chunk++)
				if ((long)constant >= first[chunk] && (long)constant < end[chunk])
					break;
			usage->chunk_mask |= 1UL << chunk;
			if (field(instruction, 3, 1, 1))
			{
				/* a relative read reaches any register of its base's chunk
				(operand); in D, any node matrix */
				if (chunk == VITA_VC_D)
					usage->relative = 1;
				if (constant < usage->relative_lowest)
					usage->relative_lowest = constant;
			}
			else
			{
				if (constant < low)
					low = constant;
				if (constant > high)
					high = constant;
				if (chunk == VITA_VC_D && constant + 1 - VITA_VC_D_FIRST > usage->d_absolute_end)
					usage->d_absolute_end = constant + 1 - VITA_VC_D_FIRST;
			}
		}
		if (field(instruction, 3, 0, 1))
			break;
	}
	if (low > high)
		low = high = 0;
	usage->lowest = low;
	usage->highest = high;
}

char *nv2a_vertex_shader_to_cg(const DWORD *instructions, unsigned long instruction_count,
	unsigned long provided_mask, unsigned long packed_mask, unsigned long color_mask)
{
	struct xgpu_text text = { 0 };
	unsigned long index;

	xgpu_text_append(&text, "%s", shader_prologue);
	xgpu_text_append(&text, "void main(\n");
	for (index = 0; index < XGPU_VERTEX_ATTRIBUTE_COUNT; index++)
	{
		if (provided_mask & (1UL << index))
			xgpu_text_append(&text, "\tfloat4 v%lu_in,\n", index);
	}
	xgpu_text_append(&text,
		"\tuniform float4 cA[12] : BUFFER[0],\n"
		"\tuniform float4 vm[%d] : BUFFER[1],\n"
		"\tuniform float4 cB[5] : BUFFER[2],\n"
		"\tuniform float4 cC1[11] : BUFFER[3],\n"
		"\tuniform float4 cC2[32] : BUFFER[4],\n"
		"\tuniform float4 cD[%d] : BUFFER[5],\n"
		"\tuniform float4 cE[8] : BUFFER[6],\n"
		"\tout float4 out_position : POSITION,\n"
		"\tout float4 out_d0 : COLOR0,\n"
		"\tout float4 out_d1 : COLOR1,\n"
		"\tout float4 out_t0 : TEXCOORD0,\n"
		"\tout float4 out_t1 : TEXCOORD1,\n"
		"\tout float4 out_t2 : TEXCOORD2,\n"
		"\tout float4 out_t3 : TEXCOORD3,\n"
		"\tout float out_fog : FOG)\n"
		"{\n", VITA_VM_COUNT, VITA_VC_D_COUNT);
	for (index = 0; index < XGPU_VERTEX_ATTRIBUTE_COUNT; index++)
	{
		if (!(provided_mask & (1UL << index)))
			xgpu_text_append(&text, "\tfloat4 v%lu = vm[%d];\n", index, VITA_VM_ATTRIBUTES + (int)index);
		else if (packed_mask & (1UL << index))
			xgpu_text_append(&text, "\tfloat4 v%lu = unpack_normpacked3(v%lu_in);\n", index, index);
		else if (color_mask & (1UL << index))
			xgpu_text_append(&text, "\tfloat4 v%lu = v%lu_in.zyxw;\n", index, index);
		else
			xgpu_text_append(&text, "\tfloat4 v%lu = v%lu_in;\n", index, index);
	}
	xgpu_text_append(&text,
		"\tfloat4 r0 = float4(0.0), r1 = float4(0.0), r2 = float4(0.0), r3 = float4(0.0);\n"
		"\tfloat4 r4 = float4(0.0), r5 = float4(0.0), r6 = float4(0.0), r7 = float4(0.0);\n"
		"\tfloat4 r8 = float4(0.0), r9 = float4(0.0), r10 = float4(0.0), r11 = float4(0.0);\n"
		"\tfloat4 oPos = float4(0.0, 0.0, 0.0, 1.0);\n"
		"\tfloat4 oD0 = float4(0.0, 0.0, 0.0, 1.0), oD1 = float4(0.0, 0.0, 0.0, 1.0);\n"
		"\tfloat4 oB0 = float4(0.0, 0.0, 0.0, 1.0), oB1 = float4(0.0, 0.0, 0.0, 1.0);\n"
		"\tfloat4 oT0 = float4(0.0, 0.0, 0.0, 1.0), oT1 = float4(0.0, 0.0, 0.0, 1.0);\n"
		"\tfloat4 oT2 = float4(0.0, 0.0, 0.0, 1.0), oT3 = float4(0.0, 0.0, 0.0, 1.0);\n"
		"\tfloat4 oFog = float4(1.0), oPts = float4(vm[%d].x), oUnused = float4(0.0);\n"
		"\tfloat a0 = 0.0;\n"
		"\tfloat4 A, B, C, mac = float4(0.0), ilu = float4(0.0);\n", VITA_VM_MISCELLANEOUS);

	for (index = 0; index < instruction_count; index++)
	{
		const DWORD *instruction = instructions + index * 4;
		unsigned long mac = field(instruction, 1, 21, 4);
		unsigned long ilu = field(instruction, 1, 25, 3);
		unsigned long mac_mask = field(instruction, 3, 24, 4);
		unsigned long temporary = field(instruction, 3, 20, 4);
		unsigned long ilu_mask = field(instruction, 3, 16, 4);
		unsigned long output_mask = field(instruction, 3, 12, 4);
		unsigned long output_is_register = field(instruction, 3, 11, 1);
		unsigned long output_address = field(instruction, 3, 3, 8);
		unsigned long output_from_ilu = field(instruction, 3, 2, 1);
		int relative = (int)field(instruction, 3, 1, 1);
		char mask[5];

		xgpu_text_append(&text, "\t/* %lu */\n", index);
		xgpu_text_append(&text, "\tA = "); operand(&text, instruction, 'A', relative); xgpu_text_append(&text, ";\n");
		xgpu_text_append(&text, "\tB = "); operand(&text, instruction, 'B', relative); xgpu_text_append(&text, ";\n");
		xgpu_text_append(&text, "\tC = "); operand(&text, instruction, 'C', relative); xgpu_text_append(&text, ";\n");

		switch (mac)
		{
		case _mac_nop: break;
		case _mac_mov: xgpu_text_append(&text, "\tmac = A;\n"); break;
		case _mac_mul: xgpu_text_append(&text, "\tmac = A * B;\n"); break;
		case _mac_add: xgpu_text_append(&text, "\tmac = A + C;\n"); break;
		case _mac_mad: xgpu_text_append(&text, "\tmac = A * B + C;\n"); break;
		case _mac_dp3: xgpu_text_append(&text, "\tmac = float4(dot(A.xyz, B.xyz));\n"); break;
		case _mac_dph: xgpu_text_append(&text, "\tmac = float4(dot(A.xyz, B.xyz) + B.w);\n"); break;
		case _mac_dp4: xgpu_text_append(&text, "\tmac = float4(dot(A, B));\n"); break;
		case _mac_dst: xgpu_text_append(&text, "\tmac = float4(1.0, A.y * B.y, A.z, B.w);\n"); break;
		case _mac_min: xgpu_text_append(&text, "\tmac = min(A, B);\n"); break;
		case _mac_max: xgpu_text_append(&text, "\tmac = max(A, B);\n"); break;
		case _mac_slt: xgpu_text_append(&text, "\tmac = (float4)(A < B);\n"); break;
		case _mac_sge: xgpu_text_append(&text, "\tmac = (float4)(A >= B);\n"); break;
		case _mac_arl: xgpu_text_append(&text, "\tmac = A;\n"); break;
		default: xgpu_text_append(&text, "\tmac = float4(0.0);\n"); break;
		}
		switch (ilu)
		{
		case _ilu_nop: break;
		case _ilu_mov: xgpu_text_append(&text, "\tilu = C;\n"); break;
		case _ilu_rcp: xgpu_text_append(&text, "\tilu = float4(1.0 / C.x);\n"); break;
		case _ilu_rcc: xgpu_text_append(&text, "\tilu = nv2a_rcc(C.x);\n"); break;
		case _ilu_rsq: xgpu_text_append(&text, "\tilu = float4(rsqrt(abs(C.x)));\n"); break;
		case _ilu_exp: xgpu_text_append(&text, "\tilu = nv2a_exp(C.x);\n"); break;
		case _ilu_log: xgpu_text_append(&text, "\tilu = nv2a_log(C.x);\n"); break;
		case _ilu_lit: xgpu_text_append(&text, "\tilu = nv2a_lit(C);\n"); break;
		default: xgpu_text_append(&text, "\tilu = float4(0.0);\n"); break;
		}

		if (mac == _mac_arl)
		{
			xgpu_text_append(&text, "\ta0 = floor(mac.x + 0.001);\n");
		}
		else if (mac != _mac_nop && mac_mask)
		{
			write_mask(mac_mask, mask);
			if (temporary == 12)
				xgpu_text_append(&text, "\toPos.%s = mac.%s;\n", mask, mask);
			else
				xgpu_text_append(&text, "\tr%lu.%s = mac.%s;\n", temporary, mask, mask);
		}
		if (ilu != _ilu_nop && ilu_mask)
		{
			unsigned long ilu_temporary = mac != _mac_nop ? 1 : temporary;

			write_mask(ilu_mask, mask);
			if (ilu_temporary == 12)
				xgpu_text_append(&text, "\toPos.%s = ilu.%s;\n", mask, mask);
			else
				xgpu_text_append(&text, "\tr%lu.%s = ilu.%s;\n", ilu_temporary, mask, mask);
		}
		if (output_mask && (output_from_ilu ? ilu : mac) != 0)
		{
			write_mask(output_mask, mask);
			if (output_is_register)
				xgpu_text_append(&text, "\t%s.%s = %s.%s;\n", output_name(output_address), mask,
					output_from_ilu ? "ilu" : "mac", mask);
		}
		if (field(instruction, 3, 0, 1))
			break;
	}

	/* undo the screen-space conversion the program ends with (c[-38] and
	c[-37]); the GXM viewport is set to the same transform (d3d8_gxm.c),
	with Direct3D's integer pixel centres offset by half a pixel */
	xgpu_text_append(&text,
		"\tfloat3 scale = float3(vm[%d].x != 0.0 ? vm[%d].x : 1.0,\n"
		"\t\tvm[%d].y != 0.0 ? vm[%d].y : 1.0,\n"
		"\t\tvm[%d].z != 0.0 ? vm[%d].z : 1.0);\n"
		"\tfloat3 ndc = (float3(oPos.xy + float2(0.5 + vm[%d].y, 0.5), oPos.z) - vm[%d].xyz) / scale;\n"
		"\tout_position = float4(ndc * oPos.w, oPos.w);\n"
		"\tout_d0 = saturate(oD0);\n"
		"\tout_d1 = saturate(oD1);\n"
		"\tout_t0 = oT0;\n"
		"\tout_t1 = oT1;\n"
		"\tout_t2 = oT2;\n"
		"\tout_t3 = oT3;\n"
		"\tout_fog = oFog.x;\n"
		"}\n",
		VITA_VM_VIEWPORT_SCALE, VITA_VM_VIEWPORT_SCALE, VITA_VM_VIEWPORT_SCALE, VITA_VM_VIEWPORT_SCALE,
		VITA_VM_VIEWPORT_SCALE, VITA_VM_VIEWPORT_SCALE, VITA_VM_MISCELLANEOUS, VITA_VM_VIEWPORT_OFFSET);
	return text.buffer;
}
