#include <mowkow/unix64.h>
.section .text.start,"ax"
.global _start
_start:
#ifdef __aarch64__
#ifndef EXEC_LAUNCHER64
	mov x0, sp
#endif
	bl exec_probe_entry64
	brk #0
#else
#ifndef EXEC_LAUNCHER64
	mov %rsp, %rdi
#endif
	call exec_probe_entry64
	ud2
#endif
#ifndef EXEC_LAUNCHER64
.section .note.mowkow,"a",%note
.balign 4
.long 7, 4, 1
.asciz "MOWKOW"
.balign 4
.long 1
#endif
.section .note.GNU-stack,"",%progbits
