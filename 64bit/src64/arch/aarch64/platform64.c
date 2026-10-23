#include <arch/arch64.h>
#include <arch/aarch64_boot64.h>
#include <arch/aarch64_cache64.h>
#include <arch/platform64.h>
#include <mtask64.h>

uint64_t platform_irq_save64(void)
{
	return arch64_irq_save();
}

void platform_irq_restore64(uint64_t state)
{
	arch64_irq_restore(state);
}

void platform_irq_disable64(void)
{
	arch64_irq_disable();
}

void platform_irq_enable64(void)
{
	arch64_irq_enable();
}

void platform_halt64(void)
{
	arch64_halt();
}

void platform_halt_with_irq64(void)
{
	arch64_halt_with_irq();
}

void platform_wait_for_interrupt64(void)
{
	arch64_halt_with_irq();
}

int platform_task_context_init64(struct CONTEXT64 *context,
	void (*entry)(void), uintptr_t stack_base, size_t stack_size)
{
	if (context == NULL) {
		return -1;
	}
	context->stack_pointer = arch64_task_frame_init(entry, stack_base,
		stack_size);
	return context->stack_pointer != 0 ? 0 : -1;
}

int platform_task_sleep_current64(struct TASK64 *task)
{
	if (task == NULL) {
		return 0;
	}
	while (task->flags == TASK64_FLAGS_SLEEP_PENDING) {
		platform_halt_with_irq64();
	}
	return 1;
}

void platform_task_switch64(struct CONTEXT64 *old_context,
	struct CONTEXT64 *new_context)
{
	/* AArch64 switches frames while returning from the timer exception. */
	(void) old_context;
	(void) new_context;
}

void platform_framebuffer_flush64(uintptr_t address, size_t bytes)
{
	(void) a64_cache_walk64(address, bytes, A64_CACHE_CLEAN64);
}

void platform_framebuffer_sync64(void)
{
	__asm__ volatile ("dsb sy" ::: "memory");
}

void arch64_irq_disable(void)
{
	__asm__ volatile ("msr daifset, #2" ::: "memory");
}

void arch64_irq_enable(void)
{
	__asm__ volatile ("msr daifclr, #2" ::: "memory");
}

uint64_t arch64_irq_save(void)
{
	uint64_t state;

	__asm__ volatile ("mrs %0, daif\n\tmsr daifset, #2" : "=r" (state) :: "memory");
	return state;
}

void arch64_irq_restore(uint64_t state)
{
	__asm__ volatile ("msr daif, %0" :: "r" (state) : "memory");
}

void arch64_halt(void)
{
	__asm__ volatile ("wfi");
}

void arch64_halt_with_irq(void)
{
	__asm__ volatile ("msr daifclr, #2\n\twfi" ::: "memory");
}

uint64_t aarch64_boot_counter64(void)
{
	uint64_t value;

	__asm__ volatile ("isb\n\tmrs %0, cntpct_el0" : "=r" (value));
	return value;
}

uint64_t aarch64_boot_frequency64(void)
{
	uint64_t frequency;

	__asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (frequency));
	return frequency;
}

void aarch64_boot_relax64(void)
{
	__asm__ volatile ("yield");
}
