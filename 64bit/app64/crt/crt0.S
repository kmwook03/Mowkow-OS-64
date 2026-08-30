	.section .text.start
	.global _start
_start:
	xor %rbp, %rbp
	call main
	mov %eax, %edi
	call exit
