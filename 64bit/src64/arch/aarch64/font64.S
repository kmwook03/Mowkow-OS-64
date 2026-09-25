	.section .rodata, "a"
	.align 4
	.global hankaku64
hankaku64:
	.incbin "common/font/hankaku.bin"

	.align 4
	.global hangul_font64
hangul_font64:
	.incbin "common/font/H04.FNT"

	.section .note.GNU-stack, "", %progbits
