/* EL1 exception setup and early fault reporting. */
#include <arch/arch64.h>
#include <stdint.h>

extern char arch64_vector_table[];

static void print_hex64(uint64_t value)
{
	static const char digits[] = "0123456789abcdef";
	char text[19];
	int shift;
	int position;

	text[0] = '0';
	text[1] = 'x';
	position = 2;
	for (shift = 60; shift >= 0; shift -= 4) {
		text[position++] = digits[(value >> shift) & 0x0f];
	}
	text[position] = '\0';
	arch64_dbg_puts(text);
}

void arch64_cpu_init(void)
{
	__asm__ volatile ("msr vbar_el1, %0\n\tisb" ::
		"r" ((uintptr_t) arch64_vector_table) : "memory");
}

void arch64_exception_handler(uint64_t vector, uint64_t esr, uint64_t elr,
	uint64_t far)
{
	arch64_dbg_puts("\nM3 EXCEPTION vector=");
	print_hex64(vector);
	arch64_dbg_puts(" ESR=");
	print_hex64(esr);
	arch64_dbg_puts("\nELR=");
	print_hex64(elr);
	arch64_dbg_puts(" FAR=");
	print_hex64(far);
	arch64_dbg_puts("\n");
	arch64_panic_blink(4);
}
