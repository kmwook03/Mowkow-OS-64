#include <mowkow/unix64.h>
	.text
	.global signal_restorer64
	.type signal_restorer64, @function
signal_restorer64:
#if defined(__x86_64__)
	mov $UNIX64_SYS_SIGNAL_RETURN, %rax
	int $0x80
	ud2
#elif defined(__aarch64__)
	mov x8, #UNIX64_SYS_SIGNAL_RETURN
	svc #0
	brk #0
#endif
	.size signal_restorer64, . - signal_restorer64
	.section .note.GNU-stack,"",@progbits
