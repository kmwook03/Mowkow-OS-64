#include <math.h>

#ifdef __aarch64__

#include <stdint.h>

/*
 * AArch64 has scalar square-root and rounding instructions, but no floating
 * remainder or transcendental instructions.  Keep the instruction-backed
 * operations here and implement fmod/pow without a hosted libm.  This file is
 * separate from math.c so the established x86_64 SSE2/x87 path is unchanged.
 */

union DOUBLE_BITS64 {
	double value;
	uint64_t bits;
};

#define DOUBLE_SIGN64 (1ULL << 63)
#define DOUBLE_FRAC64 ((1ULL << 52) - 1)
#define DOUBLE_EXP64  (0x7ffULL << 52)

static uint64_t double_bits64(double value)
{
	union DOUBLE_BITS64 bits;

	bits.value = value;
	return bits.bits;
}

static double bits_double64(uint64_t bits)
{
	union DOUBLE_BITS64 value;

	value.bits = bits;
	return value.value;
}

int isnan(double x)
{
	uint64_t bits = double_bits64(x) & ~DOUBLE_SIGN64;

	return bits > DOUBLE_EXP64;
}

int isinf(double x)
{
	return (double_bits64(x) & ~DOUBLE_SIGN64) == DOUBLE_EXP64;
}

int signbit(double x)
{
	return (int) (double_bits64(x) >> 63);
}

double copysign(double x, double y)
{
	return bits_double64((double_bits64(x) & ~DOUBLE_SIGN64) |
		(double_bits64(y) & DOUBLE_SIGN64));
}

double fabs(double x)
{
	return bits_double64(double_bits64(x) & ~DOUBLE_SIGN64);
}

double floor(double x)
{
	double result;

	__asm__ volatile ("frintm %d0, %d1" : "=w" (result) : "w" (x));
	return result;
}

double ceil(double x)
{
	double result;

	__asm__ volatile ("frintp %d0, %d1" : "=w" (result) : "w" (x));
	return result;
}

double trunc(double x)
{
	double result;

	__asm__ volatile ("frintz %d0, %d1" : "=w" (result) : "w" (x));
	return result;
}

double nearbyint(double x)
{
	double result;

	__asm__ volatile ("frinti %d0, %d1" : "=w" (result) : "w" (x));
	return result;
}

double sqrt(double x)
{
	double result;

	__asm__ volatile ("fsqrt %d0, %d1" : "=w" (result) : "w" (x));
	return result;
}

double nan(const char *tagp)
{
	(void) tagp;
	return bits_double64(0x7ff8000000000000ULL);
}

/* Shift/subtract remainder from musl's public-domain style algorithm. */
double fmod(double x, double y)
{
	uint64_t ux = double_bits64(x);
	uint64_t uy = double_bits64(y);
	uint64_t sx = ux & DOUBLE_SIGN64;
	uint64_t difference;
	int exponent_x = (int) ((ux >> 52) & 0x7ff);
	int exponent_y = (int) ((uy >> 52) & 0x7ff);

	ux &= ~DOUBLE_SIGN64;
	uy &= ~DOUBLE_SIGN64;
	if (uy == 0 || uy > DOUBLE_EXP64 || ux >= DOUBLE_EXP64) {
		return nan("");
	}
	if (ux <= uy) {
		return ux == uy ? copysign(0.0, x) : x;
	}
	if (exponent_x == 0) {
		for (difference = ux << 12; (difference >> 63) == 0;
			difference <<= 1) {
			exponent_x--;
		}
		ux <<= (unsigned int) (1 - exponent_x);
	} else {
		ux &= DOUBLE_FRAC64;
		ux |= 1ULL << 52;
	}
	if (exponent_y == 0) {
		for (difference = uy << 12; (difference >> 63) == 0;
			difference <<= 1) {
			exponent_y--;
		}
		uy <<= (unsigned int) (1 - exponent_y);
	} else {
		uy &= DOUBLE_FRAC64;
		uy |= 1ULL << 52;
	}
	for (; exponent_x > exponent_y; exponent_x--) {
		difference = ux - uy;
		if ((difference >> 63) == 0) {
			if (difference == 0) {
				return copysign(0.0, x);
			}
			ux = difference;
		}
		ux <<= 1;
	}
	difference = ux - uy;
	if ((difference >> 63) == 0) {
		if (difference == 0) {
			return copysign(0.0, x);
		}
		ux = difference;
	}
	while ((ux >> 52) == 0) {
		ux <<= 1;
		exponent_x--;
	}
	if (exponent_x > 0) {
		ux -= 1ULL << 52;
	} else {
		ux >>= (unsigned int) (1 - exponent_x);
		exponent_x = 0;
	}
	ux |= (uint64_t) exponent_x << 52;
	ux |= sx;
	return bits_double64(ux);
}

static int integer_exponent64(double value, int *odd)
{
	double magnitude = fabs(value);

	if (!isinf(magnitude) && floor(magnitude) == magnitude) {
		if (magnitude >= 9007199254740992.0) {
			*odd = 0;
		} else {
			*odd = ((uint64_t) magnitude & 1U) != 0;
		}
		return 1;
	}
	return 0;
}

static double scale_pow2_64(double value, int exponent)
{
	uint64_t scale_bits;

	if (exponent > 1023) {
		if (exponent == 1024) {
			return scale_pow2_64(value * 2.0, 1023);
		}
		return bits_double64(DOUBLE_EXP64);
	}
	if (exponent < -1074) {
		if (exponent == -1075) {
			return scale_pow2_64(value * 0.5, -1074);
		}
		return 0.0;
	}
	if (exponent >= -1022) {
		scale_bits = (uint64_t) (exponent + 1023) << 52;
	} else {
		scale_bits = 1ULL << (unsigned int) (exponent + 1074);
	}
	return value * bits_double64(scale_bits);
}

static double log2_positive64(double x)
{
	uint64_t bits = double_bits64(x);
	int exponent = (int) ((bits >> 52) & 0x7ff);
	double mantissa;
	double z;
	double z_squared;
	double term;
	double sum;
	int divisor;

	if (exponent == 0) {
		x *= 18014398509481984.0; /* 2^54: normalize a subnormal. */
		bits = double_bits64(x);
		exponent = (int) ((bits >> 52) & 0x7ff) - 54;
	}
	mantissa = bits_double64((bits & DOUBLE_FRAC64) | (1023ULL << 52));
	exponent -= 1023;
	z = (mantissa - 1.0) / (mantissa + 1.0);
	z_squared = z * z;
	term = z;
	sum = term;
	for (divisor = 3; divisor <= 39; divisor += 2) {
		term *= z_squared;
		sum += term / (double) divisor;
	}
	return (double) exponent + (2.0 * sum) /
		0.693147180559945309417232121458176568;
}

static double exp2_portable64(double x)
{
	double nearest;
	double fraction;
	double term;
	double sum;
	int exponent;
	int i;

	if (x > 1024.0) {
		return bits_double64(DOUBLE_EXP64);
	}
	if (x < -1075.0) {
		return 0.0;
	}
	nearest = nearbyint(x);
	exponent = (int) nearest;
	fraction = (x - nearest) *
		0.693147180559945309417232121458176568;
	term = 1.0;
	sum = 1.0;
	for (i = 1; i <= 18; i++) {
		term *= fraction / (double) i;
		sum += term;
	}
	return scale_pow2_64(sum, exponent);
}

static double pow_positive64(double x, double y)
{
	return exp2_portable64(y * log2_positive64(x));
}

static double pow_integer64(double base, double exponent)
{
	uint64_t power = (uint64_t) fabs(exponent);
	double result = 1.0;

	while (power != 0) {
		if ((power & 1U) != 0) {
			result *= base;
		}
		power >>= 1;
		if (power != 0) {
			base *= base;
		}
	}
	return exponent < 0.0 ? 1.0 / result : result;
}

double pow(double x, double y)
{
	double magnitude;
	double result;
	int odd = 0;
	int integer_y;

	if (y == 0.0 || x == 1.0) {
		return 1.0;
	}
	if (isnan(x) || isnan(y)) {
		return nan("");
	}
	magnitude = fabs(x);
	if (isinf(y)) {
		if (magnitude == 1.0) {
			return 1.0;
		}
		if ((magnitude > 1.0) == (y > 0.0)) {
			return bits_double64(DOUBLE_EXP64);
		}
		return 0.0;
	}
	integer_y = integer_exponent64(y, &odd);
	if (x == 0.0) {
		result = y > 0.0 ? 0.0 : bits_double64(DOUBLE_EXP64);
		return signbit(x) && odd ? -result : result;
	}
	if (isinf(x)) {
		result = y > 0.0 ? bits_double64(DOUBLE_EXP64) : 0.0;
		return signbit(x) && odd ? -result : result;
	}
	if (x < 0.0 && !integer_y) {
		return nan("");
	}
	if (integer_y && fabs(y) <= 2048.0) {
		result = pow_integer64(magnitude, y);
	} else {
		result = pow_positive64(magnitude, y);
	}
	return x < 0.0 && odd ? -result : result;
}

#endif
