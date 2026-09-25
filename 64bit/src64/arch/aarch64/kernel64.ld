ENTRY(_start)

KERNEL_PHYS_BASE = 0x80000;
KERNEL_VA_BASE = 0xffffff8000000000;

PHDRS
{
	text PT_LOAD FLAGS(5);
	data PT_LOAD FLAGS(6);
}

SECTIONS
{
	/*
	 * The firmware loads the flat image at KERNEL_PHYS_BASE, but the kernel
	 * executes through TTBR1 after the short bootstrap.  Giving sections their
	 * final high-half VMA makes pointers emitted by static initializers usable
	 * there.  Their LMA remains physical so objcopy still produces the exact
	 * firmware image.  PC-relative bootstrap references continue to resolve to
	 * the corresponding physical address while executing before the MMU jump.
	 */
	. = KERNEL_VA_BASE + KERNEL_PHYS_BASE;
	__image_start = .;

	.text ALIGN(16) : AT(ADDR(.text) - KERNEL_VA_BASE) {
		KEEP(*(.text.boot))
		*(.text)
		*(.text.*)
	} :text

	.vectors ALIGN(2048) : AT(ADDR(.vectors) - KERNEL_VA_BASE) {
		KEEP(*(.vectors))
	} :text

	.rodata ALIGN(16) : AT(ADDR(.rodata) - KERNEL_VA_BASE) {
		*(.rodata)
		*(.rodata.*)
	} :text

	.data ALIGN(16) : AT(ADDR(.data) - KERNEL_VA_BASE) {
		*(.data)
		*(.data.*)
	} :data

	.bss ALIGN(16) (NOLOAD) : AT(ADDR(.bss) - KERNEL_VA_BASE) {
		__bss_start = .;
		*(COMMON)
		*(.bss)
		*(.bss.*)
		. = ALIGN(8);
		__bss_end = .;
	} :data

	.stack ALIGN(16) (NOLOAD) : AT(ADDR(.stack) - KERNEL_VA_BASE) {
		__stack_bottom = .;
		stack_bottom = .;
		. += 64K;
		__stack_top = .;
	} :data

	__image_end = .;
	__image_phys_end = __image_end - KERNEL_VA_BASE;

	/DISCARD/ : {
		*(.comment)
		*(.note*)
		*(.eh_frame*)
	}
}

ASSERT(__image_phys_end < 0x800000,
	"AArch64 image overlaps the early allocator area")
