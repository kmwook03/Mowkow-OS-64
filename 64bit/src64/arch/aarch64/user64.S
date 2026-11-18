#include <arch/exception_frame64.h>
#include "exception_macros64.inc"

	.text
	.align 4
	.global enter_user_mode64
	.type enter_user_mode64, %function
	.global arch64_leave_user_mode
	.type arch64_leave_user_mode, %function

/* Save the console continuation before moving SP_EL1 to the process stack.
 * x0=entry, x1=user_sp, x2=argc, x3=argv, x6=&saved_kernel_rsp.
 */
enter_user_mode64:
	msr	daifset, #2
	sub	sp, sp, #16
	mrs	x9, tpidr_el0
	mrs	x10, tpidrro_el0
	stp	x9, x10, [sp]
	SAVE_EXCEPTION_FRAME
	mov	x19, sp
	str	x19, [x6]
	bl	process64_entry_stack_top
	mov	sp, x0
	sub	sp, sp, #ARCH64_EXCEPTION_FRAME_SIZE
	mov	x9, sp
	mov	x10, #(ARCH64_EXCEPTION_FRAME_SIZE / 16)
1:
	stp	xzr, xzr, [x9], #16
	subs	x10, x10, #1
	b.ne	1b
	ldp	x9, x10, [x19, #16]
	stp	x9, x10, [sp]
	ldp	x9, x10, [x19]
	str	x9, [sp, #ARCH64_FRAME_ELR_OFFSET]
	str	x10, [sp, #ARCH64_FRAME_SP_EL0_OFFSET]
	msr	tpidr_el0, xzr
	msr	tpidrro_el0, xzr
	RESTORE_EXCEPTION_FRAME
	eret
	.size enter_user_mode64, . - enter_user_mode64

/* x0=saved console SP, x1=application exit status; return IRQ-off. */
arch64_leave_user_mode:
	msr	daifset, #2
	mov	sp, x0
	str	x1, [sp]
	ldr	x9, [sp, #ARCH64_EXCEPTION_FRAME_SIZE]
	ldr	x10, [sp, #(ARCH64_EXCEPTION_FRAME_SIZE + 8)]
	msr	tpidr_el0, x9
	msr	tpidrro_el0, x10
	RESTORE_EXCEPTION_FRAME
	add	sp, sp, #16
	ret
	.size arch64_leave_user_mode, . - arch64_leave_user_mode
	.section .note.GNU-stack, "", %progbits
