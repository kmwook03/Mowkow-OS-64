#ifndef MOWKOW64_MPPORT_MATH_H
#define MOWKOW64_MPPORT_MATH_H

#define INFINITY (__builtin_inf())
#define NAN      (__builtin_nan(""))

/* x86_64 math.c and AArch64 math_aarch64.c supply the active core operations.
 * Instruction-backed wrappers avoid external calls back to the same symbol.
 * The additional declarations support disabled vendor features; they are not
 * a promise of a hosted libm implementation. Enabling those features must also
 * provide their implementations or the link will fail.
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
