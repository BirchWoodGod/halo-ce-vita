/*
BLEND_CONSTANT.H

Blends GXM cannot take as they are (port/vita/platform/blend_constant.c):
the Xbox's constant blend factors (D3DBLEND_CONSTANTCOLOR,
INVCONSTANTCOLOR, CONSTANTALPHA, INVCONSTANTALPHA, of D3DRS_BLENDCOLOR) -
SceGxmBlendInfo has no constant factor - and D3DBLENDOP_MIN/MAX with
factors other than ONE (the factors are not used by MIN and MAX, on the
Xbox as in OpenGL, but GXM refuses to link such a fragment program:
SCE_GXM_ERROR_INVALID_VALUE, and the draw was lost).

Halo's draws with a constant factor: the screen flashes of damage effects
(rasterizer_xbox_screen_effect.c: max and min INVDESTCOLOR/INVCONSTANTCOLOR
with MAX/MIN - the needler's and the sentinel beam's; invert
INVDESTCOLOR/INVCONSTANTCOLOR with ADD; tint ONE/INVCONSTANTCOLOR - the
Covenant turret's bolt), the transparent meter shaders
(rasterizer_xbox_transparent_geometry.c: CONSTANTALPHA/CONSTANTCOLOR - the
plasma pistol's and rifle's gauges, the Warthog's meters - or, tint mode 2,
CONSTANTCOLOR/SRCALPHA) and the HUD's meters (rasterizer_xbox_dynavobgeom.c:
CONSTANTCOLOR/SRCALPHA).

A plan says how such a draw is made with GXM's factors. The program's
output o (after its saturate) can be replaced by o * scale + offset (the
fold: a variant of the program, nv2a_psh_cg.c, with the two rows in its
uniforms), which takes a constant factor on the source side, or a constant
destination factor into the output's alpha (one value for the three
channels) or colour. A destination factor that takes the output's colour
while the source term needs it too is made in two passes when the source
term does not read the target: the first multiplies the target by the
destination factor (ZERO and SRCCOLOR, the program's output the factor),
the second adds (or subtracts) the source term with a destination factor
of ONE. Pure arithmetic, no platform calls
(port/vita/tests/blend_constant_test.c).
*/

#ifndef __HALO_BLEND_CONSTANT_H
#define __HALO_BLEND_CONSTANT_H

struct blend_pass
{
	/* Direct3D factors and operation, none of them constant */
	unsigned long source, destination, operation;
	/* the program's output becomes o * scale + offset */
	int fold;
	float scale[4], offset[4];
};

struct blend_plan
{
	/* 1 or 2; with 2 the first multiplies the target (it must not write
	depth or stencil, nor count for a visibility test) */
	int pass_count;
	/* 0: the result is an approximation (logged) */
	int exact;
	struct blend_pass passes[2];
	/* what was done, for the log */
	const char *how;
};

/* the plan for a blended draw: 0 when GXM takes the blend as it is, else
the number of passes. color_write: D3DCOLORWRITEENABLE_* bits; color: the
D3DCOLOR of D3DRS_BLENDCOLOR. */
int blend_constant_plan(unsigned long source, unsigned long destination, unsigned long operation,
	unsigned long color_write, unsigned long color, struct blend_plan *plan);

/* nonzero for a Direct3D blend factor GXM has no equal of */
int blend_constant_factor(unsigned long factor);

#endif
