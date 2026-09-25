	.text
	.align 4
	.global enter_user_mode64
	.type enter_user_mode64, %function
	.global arch64_leave_user_mode
	.type arch64_leave_user_mode, %function

/* x0=entry, x1=user_sp, x2=argc, x3=argv, x6=&saved_kernel_rsp. */
enter_user_mode64:
	sub	sp, sp, #96
	stp	x19, x20, [sp, #0]
	stp	x21, x22, [sp, #16]
	stp	x23, x24, [sp, #32]
	stp	x25, x26, [sp, #48]
	stp	x27, x28, [sp, #64]
	stp	x29, x30, [sp, #80]
	mov	x7, sp
	str	x7, [x6]
	msr	sp_el0, x1
	msr	elr_el1, x0
	msr	spsr_el1, xzr
	mov	x0, x2
	mov	x1, x3
	eret
	.size enter_user_mode64, . - enter_user_mode64

/* x0=saved kernel SP, x1=application exit status. */
arch64_leave_user_mode:
	mov	sp, x0
	mov	x0, x1
	ldp	x19, x20, [sp, #0]
	ldp	x21, x22, [sp, #16]
	ldp	x23, x24, [sp, #32]
	ldp	x25, x26, [sp, #48]
	ldp	x27, x28, [sp, #64]
	ldp	x29, x30, [sp, #80]
	add	sp, sp, #96
	ret

	.size arch64_leave_user_mode, . - arch64_leave_user_mode
	.section .note.GNU-stack, "", %progbits
