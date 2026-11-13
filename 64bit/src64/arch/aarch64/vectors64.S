#include <arch/exception_frame64.h>
#include "exception_macros64.inc"

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

	.macro VECTOR_KERNEL_SYNC
	b	arch64_kernel_sync_common
	.space	124
	.endm

arch64_vector_table:
	/* Current EL with SP0. */
	VECTOR_FATAL 0
	VECTOR_IRQ
	VECTOR_FATAL 2
	VECTOR_FATAL 3
	/* Current EL with SPx (the kernel runs here). */
	VECTOR_KERNEL_SYNC
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
	.extern arch64_kernel_sync_handler
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

arch64_kernel_sync_common:
	SAVE_EXCEPTION_FRAME
	mov	x0, sp
	mrs	x1, esr_el1
	mrs	x2, far_el1
	bl	arch64_kernel_sync_handler
1:
	wfe
	b	1b

arch64_sync_common:
	SAVE_EXCEPTION_FRAME
	mov	x0, sp
	mrs	x1, esr_el1
	/* Capture fault registers before another task can take an exception. */
	mrs	x2, far_el1
	msr	fpcr, xzr
	msr	fpsr, xzr
	msr	daifclr, #2
	bl	arch64_sync_dispatch
	msr	daifset, #2
	cbnz	x0, .Lsync_exit
	RESTORE_EXCEPTION_FRAME
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
	SAVE_EXCEPTION_FRAME
	mov	x0, sp
	msr	fpcr, xzr
	msr	fpsr, xzr
	bl	arch64_irq_dispatch
	mov	sp, x0
	RESTORE_EXCEPTION_FRAME
	eret

	.section .note.GNU-stack, "", %progbits
