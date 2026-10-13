/* EL1 exception setup and early fault reporting. */
#include <arch/arch64.h>
#include <arch/exception_frame64.h>
#include <interrupt64.h>
#include <process64.h>
#include <syscall64.h>
#include <stdint.h>

#define ESR64_EC_SHIFT 26
#define ESR64_EC_MASK  0x3fU
#define ESR64_EC_SVC64 0x15U
#define SPSR64_MODE_MASK 0x0fU
#define SPSR64_MODE_EL0T 0x00U

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

static void print_register_name(unsigned int index)
{
	char name[4];
	int position = 0;

	name[position++] = 'X';
	if (index >= 10U) {
		name[position++] = (char) ('0' + index / 10U);
	}
	name[position++] = (char) ('0' + index % 10U);
	name[position] = '\0';
	arch64_dbg_puts(name);
}

static void print_exception_frame(const char *kind,
	const struct ARCH64_EXCEPTION_FRAME *frame, uint64_t esr, uint64_t far,
	uint64_t stack_pointer)
{
	unsigned int i;

	arch64_dbg_puts("\n");
	arch64_dbg_puts(kind);
	arch64_dbg_puts(" ESR=");
	print_hex64(esr);
	arch64_dbg_puts(" FAR=");
	print_hex64(far);
	arch64_dbg_puts("\nELR=");
	print_hex64(frame->elr);
	arch64_dbg_puts(" SPSR=");
	print_hex64(frame->spsr);
	arch64_dbg_puts(" SP=");
	print_hex64(stack_pointer);
	arch64_dbg_puts("\n");
	for (i = 0; i < 31U; i++) {
		print_register_name(i);
		arch64_dbg_puts("=");
		print_hex64(frame->x[i]);
		arch64_dbg_puts((i & 3U) == 3U || i == 30U ? "\n" : " ");
	}
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

void arch64_kernel_sync_handler(const struct ARCH64_EXCEPTION_FRAME *frame,
	uint64_t esr, uint64_t far)
{
	uint64_t stack_pointer;

	stack_pointer = (uintptr_t) frame + ARCH64_EXCEPTION_FRAME_SIZE;
	print_exception_frame("EL1 PANIC", frame, esr, far, stack_pointer);
	arch64_panic_blink(4);
}

uint64_t arch64_sync_dispatch(struct ARCH64_EXCEPTION_FRAME *frame,
	uint64_t esr)
{
	struct INTERRUPT_FRAME64 syscall_frame = {0};
	uint64_t exception_class;
	uint64_t action;
	uint64_t far;
	uint64_t stack_pointer;

	exception_class = (esr >> ESR64_EC_SHIFT) & ESR64_EC_MASK;
	if (exception_class != ESR64_EC_SVC64) {
		__asm__ volatile ("mrs %0, far_el1" : "=r" (far));
		if ((frame->spsr & SPSR64_MODE_MASK) != SPSR64_MODE_EL0T ||
				process64_current() == NULL) {
			arch64_kernel_sync_handler(frame, esr, far);
		}
		__asm__ volatile ("mrs %0, sp_el0" : "=r" (stack_pointer));
		print_exception_frame("EL0 FAULT", frame, esr, far,
			stack_pointer);
		process64_exit_current(PROCESS64_EXIT_FAULT_BASE +
			(int) exception_class);
		return 1;
	}
	if ((frame->spsr & SPSR64_MODE_MASK) != SPSR64_MODE_EL0T) {
		__asm__ volatile ("mrs %0, far_el1" : "=r" (far));
		arch64_kernel_sync_handler(frame, esr, far);
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
