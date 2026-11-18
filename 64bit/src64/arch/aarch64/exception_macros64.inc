	.macro SAVE_EXCEPTION_FRAME
	sub	sp, sp, #ARCH64_EXCEPTION_FRAME_SIZE
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
	mrs	x16, elr_el1
	str	x16, [sp, #ARCH64_FRAME_ELR_OFFSET]
	mrs	x16, spsr_el1
	str	x16, [sp, #ARCH64_FRAME_SPSR_OFFSET]
	mrs	x16, sp_el0
	str	x16, [sp, #ARCH64_FRAME_SP_EL0_OFFSET]
	stp	q0, q1, [sp, #272]
	stp	q2, q3, [sp, #304]
	stp	q4, q5, [sp, #336]
	stp	q6, q7, [sp, #368]
	stp	q8, q9, [sp, #400]
	stp	q10, q11, [sp, #432]
	stp	q12, q13, [sp, #464]
	stp	q14, q15, [sp, #496]
	stp	q16, q17, [sp, #528]
	stp	q18, q19, [sp, #560]
	stp	q20, q21, [sp, #592]
	stp	q22, q23, [sp, #624]
	stp	q24, q25, [sp, #656]
	stp	q26, q27, [sp, #688]
	stp	q28, q29, [sp, #720]
	stp	q30, q31, [sp, #752]
	mrs	x16, fpcr
	str	x16, [sp, #ARCH64_FRAME_FPCR_OFFSET]
	mrs	x16, fpsr
	str	x16, [sp, #ARCH64_FRAME_FPSR_OFFSET]
	.endm

	.macro RESTORE_EXCEPTION_FRAME
	ldr	x16, [sp, #ARCH64_FRAME_SP_EL0_OFFSET]
	msr	sp_el0, x16
	ldr	x16, [sp, #ARCH64_FRAME_ELR_OFFSET]
	msr	elr_el1, x16
	ldr	x16, [sp, #ARCH64_FRAME_SPSR_OFFSET]
	msr	spsr_el1, x16
	ldr	x16, [sp, #ARCH64_FRAME_FPCR_OFFSET]
	msr	fpcr, x16
	ldr	x16, [sp, #ARCH64_FRAME_FPSR_OFFSET]
	msr	fpsr, x16
	ldp	q0, q1, [sp, #272]
	ldp	q2, q3, [sp, #304]
	ldp	q4, q5, [sp, #336]
	ldp	q6, q7, [sp, #368]
	ldp	q8, q9, [sp, #400]
	ldp	q10, q11, [sp, #432]
	ldp	q12, q13, [sp, #464]
	ldp	q14, q15, [sp, #496]
	ldp	q16, q17, [sp, #528]
	ldp	q18, q19, [sp, #560]
	ldp	q20, q21, [sp, #592]
	ldp	q22, q23, [sp, #624]
	ldp	q24, q25, [sp, #656]
	ldp	q26, q27, [sp, #688]
	ldp	q28, q29, [sp, #720]
	ldp	q30, q31, [sp, #752]
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
	add	sp, sp, #ARCH64_EXCEPTION_FRAME_SIZE
	.endm

