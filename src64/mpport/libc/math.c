#include <math.h>

#include <stdint.h>

/*
 * 여기 있는 함수들은 코어(objfloat.c, formatfloat.c, modbuiltins.c 등)에서
 * 평범한 함수 호출로 불린다. -ffreestanding에서는 다른 번역 단위의
 * `isnan(x)` 호출을 GCC가 __builtin_isnan으로 알아서 바꿔 주지 않으므로,
 * 아무리 간단한 연산이라도 진짜 심볼이 있어야 한다.
 * MICROPY_PY_BUILTINS_FLOAT을 켜자 이 이름들이 전부 링크 오류로 나온 것으로
 * 확인했다(Stage 4).
 *
 * 반대로 같은 이름의 함수 안에서 부르는 __builtin_X는 진짜 명령(sqrtsd,
 * andpd, fprem, ...)으로 잘 내려간다. 다만 pow와 nan은 대응하는 명령이 없어
 * 같은 이름의 libm 함수 호출로 바뀌는데, 그러면 자기 자신을 다시 부르게
 * 된다(무한히 자기에게 뛰는 코드로 컴파일되는 것을 확인했다). 그 둘은
 * __builtin_ 감싸개 대신 직접 구현한다.
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
 * __builtin_sqrt/__builtin_fmod는 최적화 수준이 높을 때만 진짜 명령
 * (sqrtsd, fprem)으로 내려간다. 우리 빌드 옵션(X64_CFLAGS)에는 -O가 없어
 * 사실상 -O0이고, 그러면 GCC가 외부 "sqrt"/"fmod" 호출을 내보내 결국
 * 자기 자신을 다시 부른다. pow/nan/nearbyint와 같은 종류의 자기 참조
 * 문제인데, 늘 깨지는 것이 아니라 최적화 수준에 따라 달라진다. -O2 시험
 * 빌드가 아니라 실제 build64/upy 오브젝트를 역어셈블해서 잡았다. 인라인
 * 어셈블리로 직접 쓰면 최적화 수준과 무관해진다. 두 함수 모두 쓰기 전에
 * glibc와 비트 단위로 같은지 여러 경우로 확인했다(음수와 소수 fmod 인수
 * 포함).
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

/*
 * __builtin_nearbyint는 외부 "nearbyint" 호출로 컴파일된다(이 기본 대상
 * 에서는 SSE4.1 roundsd를 가정하지 않는다). pow/nan과 같은 자기 참조
 * 문제이고 잡은 방식도 같다. QEMU에서 round()를 부르자 실제로 죽었다
 * (같은 rip를 계속 다시 찍는 예외 반복). frndint는 FPU 제어 워드에 따라
 * 반올림하는 x87 명령이다(기본값은 짝수로 반올림). 쓰기 전에 glibc의
 * nearbyint()와 비트 단위로 같은지 확인했다(0.5 경계, 음수, 0 포함).
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
		double r = pow_positive(-x, y);

		if (iy != y) {
			/* 밑이 음수인데 지수가 정수가 아니면 실수 해가 없다. */
			return nan("");
		}
		return (((int64_t) iy) & 1) ? -r : r;
	}
	return pow_positive(x, y);
}
