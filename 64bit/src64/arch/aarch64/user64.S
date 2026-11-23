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

	.global platform_user_resume64
	.type platform_user_resume64, %function
platform_user_resume64:
	msr daifset, #2
	sub sp, sp, #16
	mrs x9, tpidr_el0
	mrs x10, tpidrro_el0
	stp x9, x10, [sp]
	SAVE_EXCEPTION_FRAME
	mov x19, sp
	str x19, [x1]
	bl process64_entry_stack_top
	mov sp, x0
	sub sp, sp, #ARCH64_EXCEPTION_FRAME_SIZE
	ldr x9, [x19]
	ldr x10, [x9, #1320]
	msr tpidrro_el0, x10
	add x9, x9, #512
	mov x10, sp
	mov x11, #(ARCH64_EXCEPTION_FRAME_SIZE / 16)
1:
	ldp x12, x13, [x9], #16
	stp x12, x13, [x10], #16
	subs x11, x11, #1
	b.ne 1b
	mov x0, sp
	mov x1, xzr
	bl process64_native_return64
	cbnz x0, 2f
	RESTORE_EXCEPTION_FRAME
	eret
2:
	bl process64_current_exit_rsp
	mov x19, x0
	bl process64_current_exit_status
	mov x1, x0
	mov x0, x19
	b arch64_leave_user_mode
	.size platform_user_resume64, . - platform_user_resume64

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
