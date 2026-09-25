/* EL1 exception setup and early fault reporting. */
#include <arch/arch64.h>
#include <arch/exception_frame64.h>
#include <interrupt64.h>
#include <syscall64.h>
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

uint64_t arch64_sync_dispatch(struct ARCH64_EXCEPTION_FRAME *frame,
	uint64_t esr)
{
	struct INTERRUPT_FRAME64 syscall_frame = {0};
	uint64_t exception_class;
	uint64_t action;

	exception_class = (esr >> 26) & 0x3fU;
	if (exception_class != 0x15U) {
		uint64_t far;

		__asm__ volatile ("mrs %0, far_el1" : "=r" (far));
		arch64_exception_handler(8, esr, frame->elr, far);
	}
	syscall_frame.rax = frame->x[8];
	syscall_frame.rdi = frame->x[0];
	syscall_frame.rsi = frame->x[1];
	syscall_frame.rdx = frame->x[2];
	syscall_frame.r10 = frame->x[3];
	syscall_frame.r8 = frame->x[4];
	syscall_frame.r9 = frame->x[5];
	action = syscall_handler64(&syscall_frame);
	frame->x[0] = syscall_frame.rax;
	return action;
}
