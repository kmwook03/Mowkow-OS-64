bits 64

section .rodata
global hankaku64
hankaku64:
	incbin "src/graphics/font/hankaku.bin"

section .note.GNU-stack noalloc noexec nowrite progbits
