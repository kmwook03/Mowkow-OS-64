#ifndef MOWKOW64_UNIX_SYSCALL64_H
#define MOWKOW64_UNIX_SYSCALL64_H

#include <mowkow/unix64.h>

#ifdef __cplusplus
extern "C" {
#endif
/* Raw signed result, no libc errno access. int 0x80 (not the x86 SYSCALL
 * instruction) and svc #0 use the shared register ABI. Callers treat rcx/r11
 * and condition codes as clobbered; no legacy CRT symbols are linked here.
 */
int64_t unix64_syscall6(uint64_t number, uint64_t a0, uint64_t a1,
	uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);
#ifdef __cplusplus
}
#endif

#endif
