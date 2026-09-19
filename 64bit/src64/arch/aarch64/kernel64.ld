ENTRY(_start)

PHDRS
{
	text PT_LOAD FLAGS(5);
	data PT_LOAD FLAGS(6);
}

SECTIONS
{
	. = 0x80000;
	__image_start = .;

	.text : ALIGN(16) {
		KEEP(*(.text.boot))
		*(.text)
		*(.text.*)
	} :text

	.vectors : ALIGN(2048) {
		KEEP(*(.vectors))
	} :text

	.rodata : ALIGN(16) {
		*(.rodata)
		*(.rodata.*)
	} :text

	.data : ALIGN(16) {
		*(.data)
		*(.data.*)
	} :data

	.bss (NOLOAD) : ALIGN(16) {
		__bss_start = .;
		*(COMMON)
		*(.bss)
		*(.bss.*)
		. = ALIGN(8);
		__bss_end = .;
	} :data

	.stack (NOLOAD) : ALIGN(16) {
		__stack_bottom = .;
		. += 64K;
		__stack_top = .;
	} :data

	__image_end = .;

	/DISCARD/ : {
		*(.comment)
		*(.note*)
		*(.eh_frame*)
	}
}

ASSERT(__image_end < 0x800000, "AArch64 image overlaps the early allocator area")
