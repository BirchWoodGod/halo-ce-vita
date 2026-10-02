/* (port, not musl's) halo_sincos: halo_sin(x) and halo_cos(x) at once,
with one argument reduction - sin.c's and cos.c's branches, each kept as
it is (their own small-argument thresholds), so both results are bit for
bit those of the two calls (checked for every float). The render takes
the sine and the cosine of the same angle for every sprite and every
animated texture. */

#include "libm.h"

void halo_sincos(double x, double *sin_result, double *cos_result)
{
	double y[2];
	uint32_t ix;
	unsigned n;

	GET_HIGH_WORD(ix, x);
	ix &= 0x7fffffff;

	/* |x| ~< pi/4 */
	if (ix <= 0x3fe921fb) {
		if (ix < 0x3e500000) {  /* |x| < 2**-26 */
			FORCE_EVAL(ix < 0x00100000 ? x/0x1p120f : x+0x1p120f);
			*sin_result = x;
		} else
			*sin_result = __sin(x, 0.0, 0);
		if (ix < 0x3e46a09e) {  /* |x| < 2**-27 * sqrt(2) */
			FORCE_EVAL(x + 0x1p120f);
			*cos_result = 1.0;
		} else
			*cos_result = __cos(x, 0);
		return;
	}

	/* sin and cos of Inf or NaN are NaN */
	if (ix >= 0x7ff00000) {
		*sin_result = *cos_result = x - x;
		return;
	}

	/* argument reduction */
	n = __rem_pio2(x, y);
	switch (n&3) {
	case 0:
		*sin_result = __sin(y[0], y[1], 1);
		*cos_result = __cos(y[0], y[1]);
		break;
	case 1:
		*sin_result = __cos(y[0], y[1]);
		*cos_result = -__sin(y[0], y[1], 1);
		break;
	case 2:
		*sin_result = -__sin(y[0], y[1], 1);
		*cos_result = -__cos(y[0], y[1]);
		break;
	default:
		*sin_result = -__cos(y[0], y[1]);
		*cos_result = __sin(y[0], y[1], 1);
		break;
	}
}
