#include <math.h>

#include <stdint.h>

/* The freestanding core needs real libm symbols, even for simple predicates.
 * Compiler builtins are used only where the x86 target provides the operation;
 * builtins that lower to an external libm call would recurse into this shim.
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

/* Builtin sqrt/fmod lowering depends on target and optimization flags. Explicit
 * SSE2/x87 instructions avoid a libm call back to the same wrapper at any -O.
 * fprem repeats while C2 marks a partial remainder.
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
		"fprem\n\t"            /* st0 = st0/st1의 부분 나머지 */
		"fnstsw %%ax\n\t"
		"testb $4, %%ah\n\t"   /* C2: 아직 덜 줄었다, 한 번 더 */
		"jnz 1b\n\t"
		"fstp %%st(1)\n\t"     /* y를 버린다, 나머지는 st0에 남는다 */
		"fstpl %0\n\t"
		: "=m" (result)
		: "m" (x), "m" (y)
		: "ax", "cc", "st", "st(1)"
	);
	return result;
}

/* The baseline x86 target does not assume SSE4.1 roundsd. Use x87 frndint
 * instead of a builtin that can call this same symbol; honor the FPU control
 * word's rounding mode (normally nearest, ties to even).
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
	v.bits = 0x7ff8000000000000ULL; /* quiet NaN, 페이로드는 쓰지 않는다 */
	return v.value;
}

/*
 * x^y = 2^(y*log2(x)). x87의 고전적인 fyl2x/f2xm1/fscale 차례로 계산한다.
 * 쓰기 전에 정수, 소수, 음수 지수를 두루 넣어 glibc의 pow()와 비트 단위로
 * 같은지 확인했다. x > 0에서만 맞고, 나머지 경우는 아래 C 코드에서 따로
 * 처리한다.
 */
static double pow_positive64(double x, double y)
{
	double result;

	__asm__ volatile (
		"fldl %1\n\t"                 /* st0 = y */
		"fldl %2\n\t"                 /* st0 = x, st1 = y */
		"fyl2x\n\t"                   /* st0 = y*log2(x) */
		"fld %%st(0)\n\t"             /* st0 = st1 = y*log2(x) */
		"frndint\n\t"                 /* st0 = round(y*log2(x)), st1 = y*log2(x) */
		"fxch %%st(1)\n\t"            /* st0 = y*log2(x), st1 = round(...) */
		"fsub %%st(1), %%st(0)\n\t"   /* st0 = 소수부, [-0.5, 0.5] */
		"f2xm1\n\t"                   /* st0 = 2^frac - 1 */
		"fld1\n\t"
		"faddp\n\t"                   /* st0 = 2^frac */
		"fscale\n\t"                  /* st0 = 2^frac * 2^round(...) */
		"fstp %%st(1)\n\t"            /* 남은 정수부를 버린다 */
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
		double r = pow_positive64(-x, y);

		if (iy != y) {
			/* 밑이 음수인데 지수가 정수가 아니면 실수 해가 없다. */
			return nan("");
		}
		return (((int64_t) iy) & 1) ? -r : r;
	}
	return pow_positive64(x, y);
}
