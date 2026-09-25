/*
 * syscall.c -- 앱에서 쓰는 시스템 콜 감싸개
 *
 * 번호를 rax에, 인수를 rdi/rsi/rdx/r10/r8/r9에 넣고 int 0x80을 부른다.
 * 커널과 맞춰야 하는 규약은 src64/include/syscall64.h에 있다.
 */
#include <mowos.h>
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

long write(int fd, const void *buf, size_t len)
{
	return syscall3(SYS_WRITE, fd, (long) buf, (long) len);
}

long read(int fd, void *buf, size_t len)
{
	return syscall3(SYS_READ, fd, (long) buf, (long) len);
}

int open(const char *path, int flags)
{
	return (int) syscall2(SYS_OPEN, (long) path, flags);
}

int close(int fd)
{
	return (int) syscall1(SYS_CLOSE, fd);
}

long seek(int fd, long offset, int whence)
{
	return syscall3(SYS_SEEK, fd, offset, whence);
}

void exit(int status)
{
	syscall1(SYS_EXIT, status);
	for (;;) {
	}
}

void *alloc(size_t size)
{
	return (void *) syscall1(SYS_ALLOC, (long) size);
}

void free_alloc(void *ptr, size_t size)
{
	syscall2(SYS_FREE, (long) ptr, (long) size);
}

unsigned long ticks(void)
{
	return (unsigned long) syscall0(SYS_TICKS);
}

void tty_raw(int on)
{
	syscall2(SYS_TTY, TTY_MODE, on);
}

unsigned long tty_readkey(void)
{
	return (unsigned long) syscall1(SYS_TTY, TTY_READKEY);
}

unsigned long tty_size(void)
{
	return (unsigned long) syscall1(SYS_TTY, TTY_SIZE);
}

void tty_move(int row, int col)
{
	syscall3(SYS_TTY, TTY_MOVE, row, col);
}

void tty_clear(int row, int col, int rows, int cols)
{
	syscall5(SYS_TTY, TTY_CLEAR, row, col, rows, cols);
}

void tty_attr(int fg, int bg)
{
	syscall3(SYS_TTY, TTY_ATTR, fg, bg);
}

void tty_flush(void)
{
	syscall1(SYS_TTY, TTY_FLUSH);
}

int puts(const char *s)
{
	size_t len;

	len = strlen(s);
	write(1, s, len);
	write(1, "\n", 1);
	return (int) len + 1;
}
