ENTRY(_start)

SECTIONS
{
	. = 0x80000;
	__image_start = .;

	.text : ALIGN(16) {
		KEEP(*(.text.boot))
		*(.text)
		*(.text.*)
	}

	.rodata : ALIGN(16) {
		*(.rodata)
		*(.rodata.*)
	}

	.data : ALIGN(16) {
		*(.data)
		*(.data.*)
	}

	.bss (NOLOAD) : ALIGN(16) {
		__bss_start = .;
		*(COMMON)
		*(.bss)
		*(.bss.*)
		. = ALIGN(8);
		__bss_end = .;
	}

	.stack (NOLOAD) : ALIGN(16) {
		__stack_bottom = .;
		. += 64K;
		__stack_top = .;
	}

	__image_end = .;

	/DISCARD/ : {
		*(.comment)
		*(.note*)
		*(.eh_frame*)
	}
}

ASSERT(__image_end < 0x200000, "AArch64 M1 image overlaps the future kernel area")
