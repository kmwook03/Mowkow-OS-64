	.section .text.start
	.global _start
	.type _start, %function
_start:
	mov	x29, xzr
	mov	x30, xzr
	bl	main
	bl	exit
1:
	wfe
	b	1b
	.size _start, . - _start
	.section .note.GNU-stack, "", %progbits
