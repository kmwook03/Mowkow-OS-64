#include <mowkow/syscall64.h>

int64_t unix64_syscall6(uint64_t number, uint64_t a0, uint64_t a1,
	uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
#ifdef __aarch64__
	register uint64_t x0 __asm__("x0") = a0;
	register uint64_t x1 __asm__("x1") = a1;
	register uint64_t x2 __asm__("x2") = a2;
	register uint64_t x3 __asm__("x3") = a3;
	register uint64_t x4 __asm__("x4") = a4;
	register uint64_t x5 __asm__("x5") = a5;
	register uint64_t x8 __asm__("x8") = number;

	__asm__ volatile ("svc #0" : "+r" (x0) :
		"r" (x1), "r" (x2), "r" (x3), "r" (x4), "r" (x5), "r" (x8) :
		"memory", "cc");
	return (int64_t) x0;
#else
	register uint64_t r10 __asm__("r10") = a3;
	register uint64_t r8 __asm__("r8") = a4;
	register uint64_t r9 __asm__("r9") = a5;
	int64_t result;

	__asm__ volatile ("int $0x80" : "=a" (result) :
		"a" (number), "D" (a0), "S" (a1), "d" (a2),
		"r" (r10), "r" (r8), "r" (r9) : "memory", "cc", "rcx", "r11");
	return result;
#endif
}
