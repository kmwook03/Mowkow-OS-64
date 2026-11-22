ENTRY(_start)

PHDRS
{
	phdr PT_PHDR PHDRS;
	rodata PT_LOAD FILEHDR PHDRS FLAGS(4);
	note PT_NOTE FLAGS(4);
	text PT_LOAD FLAGS(5);
	data PT_LOAD FLAGS(6);
	tls PT_TLS FLAGS(4);
	stack PT_GNU_STACK FLAGS(6);
}

SECTIONS
{
	. = 0x400000 + SIZEOF_HEADERS;
	PROVIDE(__ehdr_start = 0x400000);
	.note.mowkow : ALIGN(4) { KEEP(*(.note.mowkow)) } :rodata :note
	.rodata : { *(.rodata .rodata.*) } :rodata
	.eh_frame : { KEEP(*(.eh_frame)) } :rodata
	.text : ALIGN(0x1000) { *(.text.start) *(.text .text.*) } :text
	.data : ALIGN(0x1000) { *(.data .data.*) *(.sdata .sdata.*) } :data
	.preinit_array : ALIGN(8) {
		PROVIDE_HIDDEN(__preinit_array_start = .);
		KEEP(*(.preinit_array))
		PROVIDE_HIDDEN(__preinit_array_end = .);
	} :data
	.init_array : ALIGN(8) {
		PROVIDE_HIDDEN(__init_array_start = .);
		KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*))) KEEP(*(.init_array))
		PROVIDE_HIDDEN(__init_array_end = .);
	} :data
	.fini_array : ALIGN(8) {
		PROVIDE_HIDDEN(__fini_array_start = .);
		KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*))) KEEP(*(.fini_array))
		PROVIDE_HIDDEN(__fini_array_end = .);
	} :data
	.tdata : ALIGN(16) { *(.tdata .tdata.*) } :data :tls
	.tbss : { *(.tbss .tbss.*) *(.tcommon) } :data :tls
	.bss : ALIGN(16) { *(COMMON) *(.bss .bss.*) *(.sbss .sbss.*) } :data
	PROVIDE(_end = .);
	/DISCARD/ : { *(.comment) *(.note.GNU-stack) }
	ASSERT(SIZEOF(.note.mowkow) == 24, "exactly one UNIX ABI note required")
	ASSERT(. <= 0x4000000, "UNIX image exceeds initial LOAD window")
	ASSERT((ADDR(.text) & 0xfff) == 0, "UNIX RX page alignment")
	ASSERT((ADDR(.data) & 0xfff) == 0, "UNIX RW page alignment")
}
