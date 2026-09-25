/* Timer-preemption test for the complete AArch64 FP/SIMD task context. */
	.text
	.align 4
	.extern fp_context_a_beats
	.extern fp_context_b_beats
	.extern fp_context_errors

	.macro FP_CONTEXT_TASK name, sentinel, fpcr_value, fpsr_value, counter
	.global \name
	.type \name, %function
\name:
.Linit\@:
	movi	v0.16b, #\sentinel
	movi	v31.16b, #\sentinel
	fmov	d1, xzr
	mov	x19, xzr
	mov	x9, #\fpcr_value
	msr	fpcr, x9
	mov	x9, #\fpsr_value
	msr	fpsr, x9
.Lloop\@:
	/* Check both ends of the Q-register file after every preemption. */
	movi	v2.16b, #\sentinel
	cmeq	v2.16b, v0.16b, v2.16b
	uminv	b2, v2.16b
	umov	w10, v2.b[0]
	cmp	w10, #0xff
	b.ne	.Lfail\@
	movi	v2.16b, #\sentinel
	cmeq	v2.16b, v31.16b, v2.16b
	uminv	b2, v2.16b
	umov	w10, v2.b[0]
	cmp	w10, #0xff
	b.ne	.Lfail\@

	mrs	x10, fpcr
	mov	x11, #\fpcr_value
	cmp	x10, x11
	b.ne	.Lfail\@
	mrs	x10, fpsr
	mov	x11, #\fpsr_value
	cmp	x10, x11
	b.ne	.Lfail\@

	/* d1 must retain an exact double accumulator across task switches. */
	fmov	d2, #1.0
	fadd	d1, d1, d2
	add	x19, x19, #1
	fcvtzu	x10, d1
	cmp	x10, x19
	b.ne	.Lfail\@

	adrp	x10, \counter
	add	x10, x10, :lo12:\counter
	ldr	x11, [x10]
	add	x11, x11, #1
	str	x11, [x10]
	yield
	b	.Lloop\@

.Lfail\@:
	adrp	x10, fp_context_errors
	add	x10, x10, :lo12:fp_context_errors
	ldr	x11, [x10]
	add	x11, x11, #1
	str	x11, [x10]
	b	.Linit\@
	.size \name, . - \name
	.endm

	FP_CONTEXT_TASK fp_context_task_a64, 0x11, 0x000000, 0x01, fp_context_a_beats
	FP_CONTEXT_TASK fp_context_task_b64, 0x22, 0x400000, 0x02, fp_context_b_beats

	.section .note.GNU-stack, "", %progbits
