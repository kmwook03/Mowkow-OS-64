#ifndef MOWKOW64_MPPORT_MATH_H
#define MOWKOW64_MPPORT_MATH_H

/*
 * Declarations only. All call sites are behind MICROPY_PY_BUILTINS_FLOAT /
 * MICROPY_PY_MATH / MICROPY_PY_CMATH, which default off at rom level
 * MINIMUM (Stage 1.3) -- dead code in this config. Real implementations
 * land in Stage 4 alongside enabling float support.
 */
double sqrt(double x);
double pow(double x, double y);
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
