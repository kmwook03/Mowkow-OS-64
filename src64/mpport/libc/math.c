#include <math.h>

#include <stdint.h>

/*
 * Every function here is called (from objfloat.c, formatfloat.c,
 * modbuiltins.c, ...) as a plain out-of-line call -- with -ffreestanding,
 * GCC does NOT implicitly treat a bare `isnan(x)` call in another
 * translation unit as equivalent to __builtin_isnan, so a real exported
 * symbol is required regardless of how simple the operation is. Confirmed
 * by the link failing on all of these once MICROPY_PY_BUILTINS_FLOAT was
 * turned on (Stage 4).
 *
 * Within a function that defines the identically-named symbol, though,
 * __builtin_X reliably compiles to a real inline instruction (sqrtsd,
 * andpd, fprem, ...) for all of these except pow/nan, which have no
 * hardware equivalent and would otherwise degrade to calling a libm
 * function of the same name -- i.e. calling straight back into us
 * (confirmed: compiled to an infinite self-jump). Those two get real
 * implementations instead of a __builtin_ wrapper.
 */

int isnan(double x)
{
	return __builtin_isnan(x);
}

int isinf(double x)
{
	return __builtin_isinf(x);
}

int signbit(double x)
{
	return __builtin_signbit(x);
}

double copysign(double x, double y)
{
	return __builtin_copysign(x, y);
}

double floor(double x)
{
	return __builtin_floor(x);
}

double fabs(double x)
{
	return __builtin_fabs(x);
}

/*
 * __builtin_sqrt/__builtin_fmod only lower to real instructions (sqrtsd,
 * fprem) at higher optimization levels -- our real build flags (X64_CFLAGS)
 * carry no -O, i.e. -O0, where GCC emits a call to an external "sqrt"/
 * "fmod" instead, straight back into us. Same self-reference class of bug
 * as pow/nan/nearbyint, just level-dependent rather than always-broken --
 * caught by disassembling the actual build64/upy object, not a -O2 test
 * build. Direct inline asm sidesteps the optimization-level dependency
 * entirely. Both verified bit-exact against glibc across several cases
 * (including negative/fractional fmod operands) before use here.
 */
double sqrt(double x)
{
	double result;

	__asm__ volatile ("sqrtsd %1, %0" : "=x" (result) : "x" (x));
	return result;
}

double fmod(double x, double y)
{
	double result;

	__asm__ volatile (
		"fldl %2\n\t"          /* st0 = y */
		"fldl %1\n\t"          /* st0 = x, st1 = y */
		"1:\n\t"
		"fprem\n\t"            /* st0 = partial remainder of st0/st1 */
		"fnstsw %%ax\n\t"
		"testb $4, %%ah\n\t"   /* C2: reduction incomplete, needs another pass */
		"jnz 1b\n\t"
		"fstp %%st(1)\n\t"     /* pop y, remainder stays in st0 */
		"fstpl %0\n\t"
		: "=m" (result)
		: "m" (x), "m" (y)
		: "ax", "cc", "st", "st(1)"
	);
	return result;
}

/*
 * __builtin_nearbyint compiles to a call to an external "nearbyint" (no
 * SSE4.1 roundsd assumed on this baseline target) -- same self-reference
 * problem as pow/nan, caught the same way: a real crash under `round()`
 * in a live QEMU test (infinite #UD-or-similar fault loop, our exception
 * handler stuck re-printing the same faulting rip). frndint is x87's
 * direct "round per FPU control word" instruction
 * instruction (default: round-to-nearest-even) -- verified bit-exact
 * against glibc's nearbyint() across half-to-even, negative, and zero
 * cases before use here.
 */
double nearbyint(double x)
{
	double result;

	__asm__ volatile (
		"fldl %1\n\t"
		"frndint\n\t"
		"fstpl %0\n\t"
		: "=m" (result)
		: "m" (x)
		: "st"
	);
	return result;
}

double nan(const char *tagp)
{
	union {
		uint64_t bits;
		double value;
	} v;

	(void) tagp;
	v.bits = 0x7ff8000000000000ULL; /* quiet NaN, payload unused */
	return v.value;
}

/*
 * x^y = 2^(y*log2(x)), computed via the classic x87 fyl2x/f2xm1/fscale
 * sequence -- verified against glibc's pow() for a spread of integer,
 * fractional, and negative exponents (all bit-exact) before use here.
 * Only valid for x > 0; special cases handled in C below.
 */
static double pow_positive(double x, double y)
{
	double result;

	__asm__ volatile (
		"fldl %1\n\t"                 /* st0 = y */
		"fldl %2\n\t"                 /* st0 = x, st1 = y */
		"fyl2x\n\t"                   /* st0 = y*log2(x) */
		"fld %%st(0)\n\t"             /* st0 = st1 = y*log2(x) */
		"frndint\n\t"                 /* st0 = round(y*log2(x)), st1 = y*log2(x) */
		"fxch %%st(1)\n\t"            /* st0 = y*log2(x), st1 = round(...) */
		"fsub %%st(1), %%st(0)\n\t"   /* st0 = fractional part, in [-0.5, 0.5] */
		"f2xm1\n\t"                   /* st0 = 2^frac - 1 */
		"fld1\n\t"
		"faddp\n\t"                   /* st0 = 2^frac */
		"fscale\n\t"                  /* st0 = 2^frac * 2^round(...) */
		"fstp %%st(1)\n\t"            /* drop the leftover integer part */
		"fstpl %0\n\t"
		: "=m" (result)
		: "m" (y), "m" (x)
		: "st", "st(1)", "st(2)"
	);
	return result;
}

double pow(double x, double y)
{
	if (y == 0.0) {
		return 1.0;
	}
	if (isnan(x) || isnan(y)) {
		return nan("");
	}
	if (x == 0.0) {
		return (y > 0.0) ? 0.0 : (1.0 / 0.0);
	}
	if (x < 0.0) {
		double iy = floor(y);
		double r = pow_positive(-x, y);

		if (iy != y) {
			/* Negative base with a non-integer exponent has no
			 * real result. */
			return nan("");
		}
		return (((int64_t) iy) & 1) ? -r : r;
	}
	return pow_positive(x, y);
}
