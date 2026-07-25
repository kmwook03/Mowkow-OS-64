#ifndef MOWKOW64_MPPORT_MATH_H
#define MOWKOW64_MPPORT_MATH_H

#define INFINITY (__builtin_inf())
#define NAN      (__builtin_nan(""))

/*
 * sqrt/fabs/floor/fmod/isnan/isinf/signbit/copysign/nearbyint/pow/nan:
 * real bodies in math.c (Stage 4) -- each compiles to a genuine SSE2/x87
 * instruction (sqrtsd, andpd, fprem, ...) via __builtin_*, except pow/nan
 * which have no hardware equivalent and need real implementations (their
 * __builtin_* forms degrade to calling a libm function of the same name,
 * i.e. calling straight back into us).
 *
 * Everything else here (sin/cos/tan/atan/atan2/log/log10/exp/trunc/
 * frexp/ldexp/modf/ceil): still declarations-only dead code, gated behind
 * MICROPY_PY_MATH which stays off (Stage 1.3/1.4's math.h note still
 * applies to these).
 */
double sqrt(double x);
double pow(double x, double y);
double nan(const char *tagp);
double nearbyint(double x);
double fabs(double x);
double floor(double x);
double ceil(double x);
double sin(double x);
double cos(double x);
double tan(double x);
double atan(double x);
double atan2(double y, double x);
double log(double x);
double log10(double x);
double exp(double x);
double fmod(double x, double y);
double trunc(double x);
double copysign(double x, double y);
double frexp(double x, int *exp);
double ldexp(double x, int exp);
double modf(double x, double *iptr);
int isnan(double x);
int isinf(double x);
int signbit(double x);

#endif
