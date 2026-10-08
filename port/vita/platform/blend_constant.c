/*
BLEND_CONSTANT.C

Blends with the Xbox's constant factors, and MIN/MAX with factors, made of
GXM's factors (blend_constant.h).
*/

#include "blend_constant.h"

#include <string.h>

/* (Direct3D's values: OpenGL's) */
enum
{
	_factor_zero = 0,
	_factor_one = 1,
	_factor_source_color = 0x300,
	_factor_inverse_source_color = 0x301,
	_factor_source_alpha = 0x302,
	_factor_inverse_source_alpha = 0x303,
	_factor_destination_alpha = 0x304,
	_factor_inverse_destination_alpha = 0x305,
	_factor_destination_color = 0x306,
	_factor_inverse_destination_color = 0x307,
	_factor_source_alpha_saturate = 0x308,
	_factor_constant_color = 0x8001,
	_factor_inverse_constant_color = 0x8002,
	_factor_constant_alpha = 0x8003,
	_factor_inverse_constant_alpha = 0x8004,

	_operation_add = 0x8006,
	_operation_min = 0x8007,
	_operation_max = 0x8008,

	_write_alpha = 1UL << 24,
};

int blend_constant_factor(unsigned long factor)
{
	return factor >= _factor_constant_color && factor <= _factor_inverse_constant_alpha;
}

/* a constant factor's value per channel (r, g, b, a) */
static int constant_value(unsigned long factor, const float color[4], float value[4])
{
	int channel;

	for (channel = 0; channel < 4; channel++)
	{
		switch (factor)
		{
		case _factor_constant_color: value[channel] = color[channel]; break;
		case _factor_inverse_constant_color: value[channel] = 1.0f - color[channel]; break;
		case _factor_constant_alpha: value[channel] = color[3]; break;
		case _factor_inverse_constant_alpha: value[channel] = 1.0f - color[3]; break;
		default: return 0;
		}
	}
	return 1;
}

static int reads_destination(unsigned long factor)
{
	return factor == _factor_destination_alpha || factor == _factor_inverse_destination_alpha ||
		factor == _factor_destination_color || factor == _factor_inverse_destination_color ||
		factor == _factor_source_alpha_saturate;
}

static int reads_source_color(unsigned long factor)
{
	return factor == _factor_source_color || factor == _factor_inverse_source_color;
}

static int reads_source_alpha(unsigned long factor)
{
	return factor == _factor_source_alpha || factor == _factor_inverse_source_alpha ||
		factor == _factor_source_alpha_saturate;
}

static float absolute(float x)
{
	return x < 0.0f ? -x : x;
}

/* the three colour channels within a step of 8 bits of each other */
static int grey(const float value[4])
{
	return absolute(value[0] - value[1]) <= 0.5f / 255.0f && absolute(value[0] - value[2]) <= 0.5f / 255.0f;
}

static void pass_set(struct blend_pass *pass, unsigned long source, unsigned long destination, unsigned long operation)
{
	memset(pass, 0, sizeof(*pass));
	pass->source = source;
	pass->destination = destination;
	pass->operation = operation;
	pass->scale[0] = pass->scale[1] = pass->scale[2] = pass->scale[3] = 1.0f;
}

int blend_constant_plan(unsigned long source, unsigned long destination, unsigned long operation,
	unsigned long color_write, unsigned long color, struct blend_plan *plan)
{
	float constant[4], source_value[4], destination_value[4];
	int source_constant, destination_constant;
	int alpha_written = (color_write & _write_alpha) != 0;
	struct blend_pass *pass = &plan->passes[0];
	int channel;

	memset(plan, 0, sizeof(*plan));
	plan->exact = 1;
	if (operation == _operation_min || operation == _operation_max)
	{
		/* min(o, D), max(o, D): the factors are not used */
		if (source == _factor_one && destination == _factor_one)
			return 0;
		pass_set(pass, _factor_one, _factor_one, operation);
		plan->how = "min/max, the factors unused: ONE/ONE";
		return plan->pass_count = 1;
	}
	constant[0] = (float)((color >> 16) & 0xff) / 255.0f;
	constant[1] = (float)((color >> 8) & 0xff) / 255.0f;
	constant[2] = (float)(color & 0xff) / 255.0f;
	constant[3] = (float)((color >> 24) & 0xff) / 255.0f;
	source_constant = constant_value(source, constant, source_value);
	destination_constant = constant_value(destination, constant, destination_value);
	if (!source_constant && !destination_constant)
		return 0;

	if (!destination_constant)
	{
		/* o * K + D * Fd: K folded into o, when Fd does not read o's colour,
		nor its alpha where the alpha is written and K's alpha is not 1 */
		if (!reads_source_color(destination) &&
			!(reads_source_alpha(destination) && alpha_written && source_value[3] != 1.0f))
		{
			pass_set(pass, _factor_one, destination, operation);
			pass->fold = 1;
			memcpy(pass->scale, source_value, sizeof(pass->scale));
			/* (the alpha the destination factor reads stays o's) */
			if (reads_source_alpha(destination))
				pass->scale[3] = 1.0f;
			plan->how = "source constant folded into the program";
			return plan->pass_count = 1;
		}
	}
	else
	{
		unsigned long single_source = source_constant ? _factor_one : source;

		/* D * k, k one value for the three channels: k in the program's
		alpha (unwritten) and the destination factor SRCALPHA, when the
		source term does not read o's alpha */
		if (grey(destination_value) && !alpha_written && !reads_source_alpha(single_source))
		{
			pass_set(pass, single_source, _factor_source_alpha, operation);
			pass->fold = 1;
			if (source_constant)
				memcpy(pass->scale, source_value, sizeof(pass->scale));
			pass->scale[3] = 0.0f;
			pass->offset[3] = destination_value[0];
			plan->how = "grey destination constant in the program's alpha";
			return plan->pass_count = 1;
		}
	}

	if (!reads_destination(source))
	{
		/* two passes: the target times the destination factor, then the
		source term with ONE */
		pass_set(&plan->passes[0], _factor_zero, destination, _operation_add);
		if (destination_constant)
		{
			plan->passes[0].destination = _factor_source_color;
			plan->passes[0].fold = 1;
			for (channel = 0; channel < 4; channel++)
			{
				plan->passes[0].scale[channel] = 0.0f;
				plan->passes[0].offset[channel] = destination_value[channel];
			}
		}
		pass_set(&plan->passes[1], source_constant ? _factor_one : source, _factor_one, operation);
		if (source_constant)
		{
			plan->passes[1].fold = 1;
			memcpy(plan->passes[1].scale, source_value, sizeof(plan->passes[1].scale));
		}
		plan->how = "two passes: the target times the destination factor, then the source term";
		return plan->pass_count = 2;
	}

	/* (here the source factor reads the target, so it is not a constant,
	and the destination factor is: a colour, or the alpha is written) the
	invert screen flash (INVDESTCOLOR/INVCONSTANTCOLOR, in none of Halo's
	maps) with a flash colour that is not grey: not expressible in GXM's
	factors - the destination constant's mean, in the program's alpha */
	plan->exact = 0;
	pass_set(pass, source, _factor_source_alpha, operation);
	pass->fold = 1;
	pass->scale[3] = 0.0f;
	pass->offset[3] = (destination_value[0] + destination_value[1] + destination_value[2]) / 3.0f;
	plan->how = "approximated: the destination constant's mean in the program's alpha";
	return plan->pass_count = 1;
}
