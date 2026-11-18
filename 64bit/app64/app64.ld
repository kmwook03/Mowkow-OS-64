ENTRY(_start)

PHDRS
{
	text PT_LOAD FLAGS(5);
	rodata PT_LOAD FLAGS(4);
	data PT_LOAD FLAGS(6);
}

SECTIONS
{
	. = 0x400000;

	.text : ALIGN(0x1000) {
		*(.text.start)
		*(.text .text.*)
	} :text

	.rodata : ALIGN(0x1000) {
		*(.rodata .rodata.*)
		*(.eh_frame .eh_frame.*)
	} :rodata

	.data : ALIGN(0x1000) {
		*(.data .data.*)
		*(.sdata .sdata.*)
	} :data

	.bss : ALIGN(0x1000) {
		*(COMMON)
		*(.bss .bss.*)
		*(.sbss .sbss.*)
	} :data

	ASSERT(ADDR(.text) == 0x400000, "app image start")
	ASSERT(. <= 0x800000, "app exceeds four MiB image window")
	ASSERT((ADDR(.rodata) & 0xfff) == 0, "app read-only page alignment")
	ASSERT((ADDR(.data) & 0xfff) == 0, "app writable page alignment")
}
