#include <arch/platform64.h>
#include <asmfunc64.h>
#include <memory64.h>
#include <process64.h>
#include <dsctbl64.h>
#include <kstring64.h>
#include <mtask64.h>
#include <stdint.h>
#include <arch/user_context64.h>
#include <interrupt64.h>
#include <user_access64.h>

void platform_user_capture64(struct USER_CONTEXT64 *context,
	const void *frame, const void *fp, int zero_result)
{
	uint32_t low, high;

	memset(context, 0, sizeof(*context));
	struct INTERRUPT_FRAME64 saved = *(const struct INTERRUPT_FRAME64 *) frame;

	if (zero_result) { saved.rax = 0; }
	memcpy(context->frame, &saved, sizeof(saved));
	memcpy(context->fp, fp, sizeof(context->fp));
	__asm__ volatile ("rdmsr" : "=a" (low), "=d" (high) : "c" (0xc0000100U));
	context->tls = (uintptr_t) low | (uintptr_t) high << 32;
}

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

extern void task_start64(void);

int platform_task_context_init64(struct CONTEXT64 *context,
	void (*entry)(void), uintptr_t stack_base, size_t stack_size)
{
	uint64_t *stack_pointer;

	if (context == NULL || entry == NULL || stack_base == 0 ||
			stack_size < 1024 || (stack_base & 0x0fU) != 0 ||
			(stack_size & 0x0fU) != 0 ||
			stack_size > UINTPTR_MAX - stack_base) {
		return -1;
	}
	stack_pointer = (uint64_t *) align_down64(stack_base + stack_size, 16);
	*--stack_pointer = 0;
	*--stack_pointer = (uintptr_t) task_start64;
	*--stack_pointer = 0; /* rbx */
	*--stack_pointer = 0; /* rbp */
	*--stack_pointer = (uintptr_t) entry; /* r12 */
	*--stack_pointer = 0; /* r13 */
	*--stack_pointer = 0; /* r14 */
	*--stack_pointer = 0; /* r15 */
	uintptr_t registers = (uintptr_t) stack_pointer;
	uintptr_t fp = (registers - 520) & ~(uintptr_t) 15;

	memset((void *) fp, 0, 520);
	*(uint16_t *) fp = 0x037f;
	*(uint32_t *) (fp + 24) = 0x1f80;
	*(uintptr_t *) (fp + 512) = registers;
	context->stack_pointer = fp;
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
	uint32_t low, high;

	__asm__ volatile ("rdmsr" : "=a" (low), "=d" (high) : "c" (0xc0000100U));
	old_context->thread_pointer = (uintptr_t) low | (uintptr_t) high << 32;
	process64_switch_task64(task_now64());
	platform_user_tls64(new_context->thread_pointer);
	context_switch64(old_context, new_context);
}

void platform_user_tls64(uintptr_t pointer)
{
	__asm__ volatile ("wrmsr" :: "c" (0xc0000100U), "a" ((uint32_t) pointer),
		"d" ((uint32_t) (pointer >> 32)) : "memory");
}

void platform_process_stack64(uintptr_t top)
{
	set_tss_stack64(top);
}

void platform_framebuffer_flush64(uintptr_t address, size_t bytes)
{
	(void) address;
	(void) bytes;
}

void platform_framebuffer_sync64(void)
{
}

void platform_user_restore_tls64(const struct USER_CONTEXT64 *context)
{
	uint64_t irq = platform_irq_save64();

	platform_user_tls64(context->tls);
	task_now64()->context.thread_pointer = context->tls;
	platform_irq_restore64(irq);
}
