#ifndef MOWKOW64_MPPORT_MATH_H
#define MOWKOW64_MPPORT_MATH_H

#define INFINITY (__builtin_inf())
#define NAN      (__builtin_nan(""))

/*
 * sqrt/fabs/floor/fmod/isnan/isinf/signbit/copysign/nearbyint/pow/nan:
 * 실제 몸체는 math.c에 있다(Stage 4). 대부분 __builtin_*을 거쳐 진짜
 * SSE2/x87 명령(sqrtsd, andpd, fprem, ...)으로 내려간다. pow와 nan만은
 * 대응하는 명령이 없어 직접 구현해야 한다. 그 둘의 __builtin_* 형태는
 * 같은 이름의 libm 함수 호출로 바뀌는데, 그러면 우리 자신을 다시 부르게
 * 된다.
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
