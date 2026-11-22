/* Entry ABI: 16-byte aligned SP -> argc/argv/envp/auxv; no register argv.
 * __mlibc_entry belongs to the future Mowkow mlibc sysdeps, never Linux's.
 */
#include <mowkow/unix64.h>

.section .text.start,"ax"
.global _start
_start:
#ifdef __aarch64__
	mov x29, xzr
	mov x30, xzr
	mov x0, sp
	adrp x1, main
	add x1, x1, :lo12:main
	bl __mlibc_entry
	brk #0
#else
	xor %ebp, %ebp
	mov %rsp, %rdi
	lea main(%rip), %rsi
	call __mlibc_entry
	ud2
#endif

.section .note.mowkow,"a",%note
.balign 4
.long UNIX64_NOTE_NAMESZ
.long UNIX64_NOTE_DESCSZ
.long UNIX64_NOTE_TYPE
.asciz UNIX64_NOTE_NAME
.balign 4
.long UNIX64_ABI_VERSION
.section .note.GNU-stack,"",%progbits
