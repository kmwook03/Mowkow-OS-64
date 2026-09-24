/* AArch64 EL1 exception vectors. Each architectural slot is 128 bytes. */
	.section .vectors, "ax"
	.align 11
	.global arch64_vector_table
	.type arch64_vector_table, %object

	.macro VECTOR_FATAL code
	mov	x16, #\code
	b	arch64_fatal_common
	.space	120
	.endm

	.macro VECTOR_IRQ
	b	arch64_irq_common
	.space	124
	.endm

	.macro VECTOR_SYNC
	b	arch64_sync_common
	.space	124
	.endm

arch64_vector_table:
	/* Current EL with SP0. */
	VECTOR_FATAL 0
	VECTOR_IRQ
	VECTOR_FATAL 2
	VECTOR_FATAL 3
	/* Current EL with SPx (the kernel runs here). */
	VECTOR_FATAL 4
	VECTOR_IRQ
	VECTOR_FATAL 6
	VECTOR_FATAL 7
	/* Lower EL using AArch64. */
	VECTOR_SYNC
	VECTOR_IRQ
	VECTOR_FATAL 10
	VECTOR_FATAL 11
	/* Lower EL using AArch32. */
	VECTOR_FATAL 12
	VECTOR_IRQ
	VECTOR_FATAL 14
	VECTOR_FATAL 15

	.size arch64_vector_table, . - arch64_vector_table

	.text
	.align 4
	.extern arch64_exception_handler
	.extern arch64_irq_dispatch
	.extern arch64_sync_dispatch
	.extern process64_current_exit_rsp
	.extern process64_current_exit_status
	.extern arch64_leave_user_mode

arch64_fatal_common:
	mov	x0, x16
	mrs	x1, esr_el1
	mrs	x2, elr_el1
	mrs	x3, far_el1
	bl	arch64_exception_handler
1:
	wfe
	b	1b

arch64_sync_common:
	sub	sp, sp, #272
	stp	x0, x1, [sp, #0]
	stp	x2, x3, [sp, #16]
	stp	x4, x5, [sp, #32]
	stp	x6, x7, [sp, #48]
	stp	x8, x9, [sp, #64]
	stp	x10, x11, [sp, #80]
	stp	x12, x13, [sp, #96]
	stp	x14, x15, [sp, #112]
	stp	x16, x17, [sp, #128]
	stp	x18, x19, [sp, #144]
	stp	x20, x21, [sp, #160]
	stp	x22, x23, [sp, #176]
	stp	x24, x25, [sp, #192]
	stp	x26, x27, [sp, #208]
	stp	x28, x29, [sp, #224]
	str	x30, [sp, #240]
	mrs	x1, elr_el1
	str	x1, [sp, #248]
	mrs	x1, spsr_el1
	str	x1, [sp, #256]
	mov	x0, sp
	mrs	x1, esr_el1
	bl	arch64_sync_dispatch
	cbnz	x0, .Lsync_exit
	ldr	x16, [sp, #248]
	msr	elr_el1, x16
	ldr	x16, [sp, #256]
	msr	spsr_el1, x16
	ldp	x0, x1, [sp, #0]
	ldp	x2, x3, [sp, #16]
	ldp	x4, x5, [sp, #32]
	ldp	x6, x7, [sp, #48]
	ldp	x8, x9, [sp, #64]
	ldp	x10, x11, [sp, #80]
	ldp	x12, x13, [sp, #96]
	ldp	x14, x15, [sp, #112]
	ldp	x16, x17, [sp, #128]
	ldp	x18, x19, [sp, #144]
	ldp	x20, x21, [sp, #160]
	ldp	x22, x23, [sp, #176]
	ldp	x24, x25, [sp, #192]
	ldp	x26, x27, [sp, #208]
	ldp	x28, x29, [sp, #224]
	ldr	x30, [sp, #240]
	add	sp, sp, #272
	eret
.Lsync_exit:
	bl	process64_current_exit_rsp
	mov	x19, x0
	bl	process64_current_exit_status
	mov	x1, x0
	mov	x0, x19
	b	arch64_leave_user_mode

/* The frame layout is shared with sched64.c. ELR/SPSR make it possible for
   the timer to return through a different task's exception frame. */
arch64_irq_common:
	sub	sp, sp, #272
	stp	x0, x1, [sp, #0]
	stp	x2, x3, [sp, #16]
	stp	x4, x5, [sp, #32]
	stp	x6, x7, [sp, #48]
	stp	x8, x9, [sp, #64]
	stp	x10, x11, [sp, #80]
	stp	x12, x13, [sp, #96]
	stp	x14, x15, [sp, #112]
	stp	x16, x17, [sp, #128]
	stp	x18, x19, [sp, #144]
	stp	x20, x21, [sp, #160]
	stp	x22, x23, [sp, #176]
	stp	x24, x25, [sp, #192]
	stp	x26, x27, [sp, #208]
	stp	x28, x29, [sp, #224]
	str	x30, [sp, #240]
	mrs	x0, elr_el1
	str	x0, [sp, #248]
	mrs	x0, spsr_el1
	str	x0, [sp, #256]
	mov	x0, sp
	bl	arch64_irq_dispatch
	mov	sp, x0
	ldr	x16, [sp, #248]
	msr	elr_el1, x16
	ldr	x16, [sp, #256]
	msr	spsr_el1, x16
	ldp	x0, x1, [sp, #0]
	ldp	x2, x3, [sp, #16]
	ldp	x4, x5, [sp, #32]
	ldp	x6, x7, [sp, #48]
	ldp	x8, x9, [sp, #64]
	ldp	x10, x11, [sp, #80]
	ldp	x12, x13, [sp, #96]
	ldp	x14, x15, [sp, #112]
	ldp	x16, x17, [sp, #128]
	ldp	x18, x19, [sp, #144]
	ldp	x20, x21, [sp, #160]
	ldp	x22, x23, [sp, #176]
	ldp	x24, x25, [sp, #192]
	ldp	x26, x27, [sp, #208]
	ldp	x28, x29, [sp, #224]
	ldr	x30, [sp, #240]
	add	sp, sp, #272
	eret

	.section .note.GNU-stack, "", %progbits
