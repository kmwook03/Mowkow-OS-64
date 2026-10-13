ENTRY(_start)

SECTIONS
{
	. = 1M;
	__kernel_start = .;

	.text : ALIGN(4K) {
		*(.text)
		*(.text.*)
	}

	.rodata : ALIGN(4K) {
		*(.rodata)
		*(.rodata.*)
	}

	.data : ALIGN(4K) {
		*(.data)
		*(.data.*)
	}
	__kernel_file_end = .;

	.bss : ALIGN(4K) {
		_bss_start = .;
		*(COMMON)
		*(.bss)
		*(.bss.*)
		_bss_end = .;
	}
	__kernel_end = .;
}

ASSERT(__kernel_start == 0x100000,
	"x86_64 kernel must load at 1 MiB")
ASSERT(__kernel_file_end - __kernel_start <= KERNEL_MAX_BYTES,
	"x86_64 kernel exceeds the loader sector budget")
ASSERT(_bss_start >= __kernel_file_end && _bss_end >= _bss_start,
	"x86_64 kernel section order is invalid")
ASSERT(__kernel_end < 0x100000000,
	"x86_64 kernel lies outside the early identity map")
