/* Native syscall gates; register conventions match include/syscall64.h. */
#include <syscall.h>

#ifdef __aarch64__
long syscall0(long nr)
{
	register long x0 __asm__ ("x0") = 0;
	register long x8 __asm__ ("x8") = nr;

	__asm__ volatile ("svc #0" : "+r" (x0) : "r" (x8) : "memory");
	return x0;
}

long syscall1(long nr, long a0)
{
	register long x0 __asm__ ("x0") = a0;
	register long x8 __asm__ ("x8") = nr;

	__asm__ volatile ("svc #0" : "+r" (x0) : "r" (x8) : "memory");
	return x0;
}

long syscall2(long nr, long a0, long a1)
{
	register long x0 __asm__ ("x0") = a0;
	register long x1 __asm__ ("x1") = a1;
	register long x8 __asm__ ("x8") = nr;

	__asm__ volatile ("svc #0" : "+r" (x0) : "r" (x1), "r" (x8) : "memory");
	return x0;
}

long syscall3(long nr, long a0, long a1, long a2)
{
	register long x0 __asm__ ("x0") = a0;
	register long x1 __asm__ ("x1") = a1;
	register long x2 __asm__ ("x2") = a2;
	register long x8 __asm__ ("x8") = nr;

	__asm__ volatile ("svc #0" : "+r" (x0) :
		"r" (x1), "r" (x2), "r" (x8) : "memory");
	return x0;
}

long syscall4(long nr, long a0, long a1, long a2, long a3)
{
	register long x0 __asm__ ("x0") = a0;
	register long x1 __asm__ ("x1") = a1;
	register long x2 __asm__ ("x2") = a2;
	register long x3 __asm__ ("x3") = a3;
	register long x8 __asm__ ("x8") = nr;

	__asm__ volatile ("svc #0" : "+r" (x0) :
		"r" (x1), "r" (x2), "r" (x3), "r" (x8) : "memory");
	return x0;
}

long syscall5(long nr, long a0, long a1, long a2, long a3, long a4)
{
	register long x0 __asm__ ("x0") = a0;
	register long x1 __asm__ ("x1") = a1;
	register long x2 __asm__ ("x2") = a2;
	register long x3 __asm__ ("x3") = a3;
	register long x4 __asm__ ("x4") = a4;
	register long x8 __asm__ ("x8") = nr;

	__asm__ volatile ("svc #0" : "+r" (x0) :
		"r" (x1), "r" (x2), "r" (x3), "r" (x4), "r" (x8) : "memory");
	return x0;
}

long syscall6(long nr, long a0, long a1, long a2, long a3, long a4, long a5)
{
	register long x0 __asm__ ("x0") = a0;
	register long x1 __asm__ ("x1") = a1;
	register long x2 __asm__ ("x2") = a2;
	register long x3 __asm__ ("x3") = a3;
	register long x4 __asm__ ("x4") = a4;
	register long x5 __asm__ ("x5") = a5;
	register long x8 __asm__ ("x8") = nr;

	__asm__ volatile ("svc #0" : "+r" (x0) :
		"r" (x1), "r" (x2), "r" (x3), "r" (x4), "r" (x5), "r" (x8) :
		"memory");
	return x0;
}
#else
long syscall0(long nr)
{
	long ret;

	__asm__ volatile ("int $0x80" : "=a" (ret) : "a" (nr) : "memory");
	return ret;
}

long syscall1(long nr, long a0)
{
	long ret;

	__asm__ volatile ("int $0x80" : "=a" (ret) : "a" (nr), "D" (a0) : "memory");
	return ret;
}

long syscall2(long nr, long a0, long a1)
{
	long ret;

	__asm__ volatile ("int $0x80" : "=a" (ret) : "a" (nr), "D" (a0), "S" (a1) : "memory");
	return ret;
}

long syscall3(long nr, long a0, long a1, long a2)
{
	long ret;

	__asm__ volatile ("int $0x80" : "=a" (ret) :
		"a" (nr), "D" (a0), "S" (a1), "d" (a2) : "memory");
	return ret;
}

long syscall4(long nr, long a0, long a1, long a2, long a3)
{
	long ret;
	register long r10 __asm__ ("r10") = a3;

	__asm__ volatile ("int $0x80" : "=a" (ret) :
		"a" (nr), "D" (a0), "S" (a1), "d" (a2), "r" (r10) : "memory");
	return ret;
}

long syscall5(long nr, long a0, long a1, long a2, long a3, long a4)
{
	long ret;
	register long r10 __asm__ ("r10") = a3;
	register long r8 __asm__ ("r8") = a4;

	__asm__ volatile ("int $0x80" : "=a" (ret) :
		"a" (nr), "D" (a0), "S" (a1), "d" (a2), "r" (r10), "r" (r8) : "memory");
	return ret;
}

long syscall6(long nr, long a0, long a1, long a2, long a3, long a4, long a5)
{
	long ret;
	register long r10 __asm__ ("r10") = a3;
	register long r8 __asm__ ("r8") = a4;
	register long r9 __asm__ ("r9") = a5;

	__asm__ volatile ("int $0x80" : "=a" (ret) :
		"a" (nr), "D" (a0), "S" (a1), "d" (a2), "r" (r10), "r" (r8), "r" (r9) :
		"memory");
	return ret;
}
#endif

