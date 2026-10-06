#include <arch/platform64.h>
#include <asmfunc64.h>
#include <memory64.h>
#include <mtask64.h>
#include <stdint.h>

uint64_t platform_irq_save64(void)
{
	uint64_t state;

	state = io_load_rflags();
	io_cli();
	return state;
}

void platform_irq_restore64(uint64_t state)
{
	io_store_rflags(state);
}

void platform_irq_disable64(void)
{
	io_cli();
}

void platform_irq_enable64(void)
{
	io_sti();
}

void platform_halt64(void)
{
	io_hlt();
}

void platform_halt_with_irq64(void)
{
	io_stihlt();
}

void platform_wait_for_interrupt64(void)
{
	io_hlt();
}

int platform_task_context_init64(struct CONTEXT64 *context,
	void (*entry)(void), uintptr_t stack_base, size_t stack_size)
{
	uint64_t *stack_pointer;

	if (context == NULL || entry == NULL || stack_base == 0 ||
			stack_size < 128 || (stack_base & 0x0fU) != 0 ||
			(stack_size & 0x0fU) != 0 ||
			stack_size > UINTPTR_MAX - stack_base) {
		return -1;
	}
	stack_pointer = (uint64_t *) align_down64(stack_base + stack_size, 16);
	*--stack_pointer = 0;
	*--stack_pointer = (uint64_t) entry;
	*--stack_pointer = 0;
	*--stack_pointer = 0;
	*--stack_pointer = 0;
	*--stack_pointer = 0;
	*--stack_pointer = 0;
	*--stack_pointer = 0;
	context->stack_pointer = (uintptr_t) stack_pointer;
	return 0;
}

int platform_task_sleep_current64(struct TASK64 *task)
{
	(void) task;
	return 0;
}

void platform_task_switch64(struct CONTEXT64 *old_context,
	struct CONTEXT64 *new_context)
{
	context_switch64(old_context, new_context);
}
