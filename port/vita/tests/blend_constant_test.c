/*
BLEND_CONSTANT_TEST.C

A desktop test of the plans for blends GXM cannot take as they are
(port/vita/platform/blend_constant.c, included whole): every pair of
Direct3D factors with every operation, with and without the alpha written,
over random program outputs, targets and blend colours. The Xbox's blend
(the reference) is compared with the plan's passes as GXM makes them - the
fold's o * scale + offset saturated as the program returns it, GXM's own
factors, each pass's result stored in 8 bits. Every plan said to be exact
must match the reference to within the 8-bit rounding of its passes, and
only the invert screen flash's kind of blend (the source factor reading the
target, a destination constant) may be approximated. Then Halo's own
blends: the needler's max screen flash (the draw GXM refused to link), the
turret bolt's tint flash, the gauges and the HUD's meters.

Run port/vita/tests/run_blend_constant_test.sh.
*/

#include "../platform/blend_constant.c"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int failures, checks;

static void check(int condition, const char *what)
{
	checks++;
	if (!condition || getenv("BLEND_TEST_VERBOSE"))
		printf("%s %s\n", condition ? "PASS" : "FAIL", what);
	if (!condition)
		failures++;
}

static unsigned int noise_state = 12345;

static float random_unit(void)
{
	noise_state = noise_state * 1103515245u + 12345u;
	return (float)((noise_state >> 8) & 0xffff) / 65535.0f;
}

static float saturate(float x)
{
	return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
}

static float quantize(float x)
{
	return floorf(saturate(x) * 255.0f + 0.5f) / 255.0f;
}

/* a factor's value for a channel (0..3 = r, g, b, a) */
static float factor_value(unsigned long factor, int channel, const float o[4], const float d[4], const float k[4])
{
	switch (factor)
	{
	case _factor_zero: return 0.0f;
	case _factor_one: return 1.0f;
	case _factor_source_color: return o[channel];
	case _factor_inverse_source_color: return 1.0f - o[channel];
	case _factor_source_alpha: return o[3];
	case _factor_inverse_source_alpha: return 1.0f - o[3];
	case _factor_destination_alpha: return d[3];
	case _factor_inverse_destination_alpha: return 1.0f - d[3];
	case _factor_destination_color: return d[channel];
	case _factor_inverse_destination_color: return 1.0f - d[channel];
	case _factor_source_alpha_saturate:
		return channel == 3 ? 1.0f : (o[3] < 1.0f - d[3] ? o[3] : 1.0f - d[3]);
	case _factor_constant_color: return k[channel];
	case _factor_inverse_constant_color: return 1.0f - k[channel];
	case _factor_constant_alpha: return k[3];
	case _factor_inverse_constant_alpha: return 1.0f - k[3];
	}
	return -1000.0f;
}

enum
{
	_operation_subtract = 0x800a,
	_operation_reverse_subtract = 0x800b,
};

static float operate(unsigned long operation, float s, float d, float source_term, float destination_term)
{
	switch (operation)
	{
	case _operation_min: return s < d ? s : d;
	case _operation_max: return s > d ? s : d;
	case _operation_subtract: return source_term - destination_term;
	case _operation_reverse_subtract: return destination_term - source_term;
	default: return source_term + destination_term;
	}
}

/* one blend into the 8-bit target */
static void blend(unsigned long source, unsigned long destination, unsigned long operation, unsigned long color_write,
	const float o[4], float d[4], const float k[4])
{
	static const unsigned long channel_bits[4] = { 1UL << 16, 1UL << 8, 1UL, 1UL << 24 };
	float result[4];
	int channel;

	for (channel = 0; channel < 4; channel++)
	{
		float s_term = o[channel] * factor_value(source, channel, o, d, k);
		float d_term = d[channel] * factor_value(destination, channel, o, d, k);

		result[channel] = quantize(operate(operation, o[channel], d[channel], s_term, d_term));
	}
	for (channel = 0; channel < 4; channel++)
		if (color_write & channel_bits[channel])
			d[channel] = result[channel];
}

static const unsigned long factors[] = {
	_factor_zero, _factor_one, _factor_source_color, _factor_inverse_source_color, _factor_source_alpha,
	_factor_inverse_source_alpha, _factor_destination_alpha, _factor_inverse_destination_alpha,
	_factor_destination_color, _factor_inverse_destination_color, _factor_source_alpha_saturate,
	_factor_constant_color, _factor_inverse_constant_color, _factor_constant_alpha, _factor_inverse_constant_alpha,
};
static const unsigned long operations[] = {
	_operation_add, _operation_subtract, _operation_reverse_subtract, _operation_min, _operation_max,
};

/* the plan's passes over a target, as GXM makes them */
static void run_plan(const struct blend_plan *plan, unsigned long color_write, const float o[4], float d[4])
{
	static const float no_constant[4] = { -1000.0f, -1000.0f, -1000.0f, -1000.0f };
	int index, channel;

	for (index = 0; index < plan->pass_count; index++)
	{
		const struct blend_pass *pass = &plan->passes[index];
		float output[4];

		for (channel = 0; channel < 4; channel++)
			output[channel] = pass->fold ? saturate(o[channel] * pass->scale[channel] + pass->offset[channel]) : o[channel];
		blend(pass->source, pass->destination, pass->operation, color_write, output, d, no_constant);
	}
}

static unsigned long random_color(int grey_color)
{
	unsigned long r = (unsigned long)(random_unit() * 255.0f + 0.5f);
	unsigned long g = grey_color ? r : (unsigned long)(random_unit() * 255.0f + 0.5f);
	unsigned long b = grey_color ? r : (unsigned long)(random_unit() * 255.0f + 0.5f);
	unsigned long a = (unsigned long)(random_unit() * 255.0f + 0.5f);

	return a << 24 | r << 16 | g << 8 | b;
}

static void color_to_floats(unsigned long color, float k[4])
{
	k[0] = (float)((color >> 16) & 0xff) / 255.0f;
	k[1] = (float)((color >> 8) & 0xff) / 255.0f;
	k[2] = (float)(color & 0xff) / 255.0f;
	k[3] = (float)((color >> 24) & 0xff) / 255.0f;
}

/* the largest difference of a plan from the reference over random values */
static float plan_error(unsigned long source, unsigned long destination, unsigned long operation, unsigned long color_write,
	unsigned long color, int samples)
{
	struct blend_plan plan;
	float k[4], worst = 0.0f;
	int sample, channel;

	if (!blend_constant_plan(source, destination, operation, color_write, color, &plan))
		return -1.0f;
	color_to_floats(color, k);
	for (sample = 0; sample < samples; sample++)
	{
		float o[4], reference[4], planned[4];

		for (channel = 0; channel < 4; channel++)
		{
			o[channel] = quantize(random_unit());
			reference[channel] = planned[channel] = quantize(random_unit());
		}
		blend(source, destination, operation, color_write, o, reference, k);
		run_plan(&plan, color_write, o, planned);
		for (channel = 0; channel < 4; channel++)
			if (fabsf(reference[channel] - planned[channel]) > worst)
				worst = fabsf(reference[channel] - planned[channel]);
	}
	return worst;
}

static void sweep(void)
{
	static const unsigned long writes[2] = { 0x00010101UL, 0x01010101UL };
	unsigned long s, d, op, w;
	int planned = 0, approximated = 0, bad_exact = 0, bad_approximation = 0, refused = 0, colors;
	char line[256];

	for (s = 0; s < sizeof(factors) / sizeof(factors[0]); s++)
		for (d = 0; d < sizeof(factors) / sizeof(factors[0]); d++)
			for (op = 0; op < sizeof(operations) / sizeof(operations[0]); op++)
				for (w = 0; w < 2; w++)
					for (colors = 0; colors < 24; colors++)
					{
						unsigned long color = random_color(colors & 1);
						struct blend_plan plan;
						int count = blend_constant_plan(factors[s], factors[d], operations[op], writes[w], color, &plan);
						int pass;

						if (!count)
						{
							if (blend_constant_factor(factors[s]) || blend_constant_factor(factors[d]) ||
								operations[op] == _operation_min || operations[op] == _operation_max)
							{
								if (!(factors[s] == _factor_one && factors[d] == _factor_one))
									refused++;
							}
							continue;
						}
						planned++;
						for (pass = 0; pass < plan.pass_count; pass++)
						{
							if (blend_constant_factor(plan.passes[pass].source) ||
								blend_constant_factor(plan.passes[pass].destination))
								refused++;
							if ((plan.passes[pass].operation == _operation_min || plan.passes[pass].operation == _operation_max) &&
								(plan.passes[pass].source != _factor_one || plan.passes[pass].destination != _factor_one))
								refused++;
						}
						if (plan.exact)
						{
							/* (two passes: two 8-bit roundings) */
							float error = plan_error(factors[s], factors[d], operations[op], writes[w], color, 200);

							if (error > (plan.pass_count == 2 ? 2.5f : 1.5f) / 255.0f)
							{
								bad_exact++;
								if (bad_exact <= 8)
								{
									snprintf(line, sizeof(line), "exact plan %lu/%lu op %lu write %08lx color %08lx (%s) off by %.1f/255",
										factors[s], factors[d], operations[op], writes[w], color, plan.how, error * 255.0f);
									check(0, line);
								}
							}
						}
						else
						{
							approximated++;
							if (!reads_destination(factors[s]) || !blend_constant_factor(factors[d]))
								bad_approximation++;
						}
					}
	snprintf(line, sizeof(line), "sweep: %d plans, every exact one within its rounding (%d off)", planned, bad_exact);
	check(planned > 0 && bad_exact == 0, line);
	snprintf(line, sizeof(line), "sweep: no constant factor nor min/max factor left for GXM (%d left)", refused);
	check(refused == 0, line);
	snprintf(line, sizeof(line), "sweep: %d approximated, all a target-reading source with a destination constant (%d not)",
		approximated, bad_approximation);
	check(bad_approximation == 0, line);
}

static void halo_blend(const char *name, unsigned long source, unsigned long destination, unsigned long operation,
	unsigned long color_write, unsigned long color, int passes_wanted)
{
	struct blend_plan plan;
	int count = blend_constant_plan(source, destination, operation, color_write, color, &plan);
	float error = plan_error(source, destination, operation, color_write, color, 4000);
	char line[256];

	snprintf(line, sizeof(line), "%s: %lu/%lu op %lu color %08lx - %d pass(es), exact, off by %.1f/255 (%s)", name,
		source, destination, operation, color, count, error * 255.0f, plan.how ? plan.how : "as it is");
	check(count == passes_wanted && plan.exact && error <= (count == 2 ? 2.5f : 1.5f) / 255.0f, line);
}

int main(void)
{
	const unsigned long rgb = 0x00010101UL;

	sweep();
	/* the needler's (and the sentinel beam's) max flash: its damage
	effects' colour, alpha 1 and (0.97, 0.04, 0.80), at full intensity
	(rasterizer_xbox_screen_effect.c) */
	halo_blend("needler max screen flash", 775, 32770, _operation_max, rgb, 0xfff709cdUL, 1);
	halo_blend("min screen flash", 775, 32770, _operation_min, rgb, 0x80406080UL, 1);
	/* the turret bolt's tint flash: alpha 0 and (0.18, 1.0, 0.98) at
	intensity 0.6, the blend colour its inverse, (1 - colour) x 0.6 */
	halo_blend("turret bolt tint screen flash", 1, 32770, _operation_add, rgb, 0x007d0104UL, 2);
	/* the plasma gauges: tint (0.8, 1, 0.8), brightness 1 */
	halo_blend("plasma pistol gauge", 32771, 32769, _operation_add, rgb, 0xffccffccUL, 2);
	/* the Warthog's meters: tint white */
	halo_blend("warthog meter", 32771, 32769, _operation_add, rgb, 0xffffffffUL, 1);
	/* tint mode 2 (the flamethrower's) and the HUD's meters: grey, alpha the fade */
	halo_blend("hud meter", 32769, 770, _operation_add, rgb, 0x80e5e5e5UL, 1);
	halo_blend("hud meter, alpha written", 32769, 770, _operation_add, 0x01010101UL, 0x80e5e5e5UL, 2);
	/* the invert flash with a grey colour: exact in one pass */
	halo_blend("grey invert screen flash", 775, 32770, _operation_add, rgb, 0xff808080UL, 1);
	{
		struct blend_plan plan;

		check(blend_constant_plan(770, 771, _operation_add, rgb, 0x12345678UL, &plan) == 0,
			"an ordinary alpha blend is left as it is");
		check(blend_constant_plan(1, 1, _operation_max, rgb, 0, &plan) == 0,
			"component max (ONE/ONE MAX) is left as it is");
		check(blend_constant_plan(775, 32770, _operation_add, rgb, 0xff00ff80UL, &plan) == 1 && !plan.exact,
			"a coloured invert flash is approximated (and logged)");
	}
	printf("%d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
