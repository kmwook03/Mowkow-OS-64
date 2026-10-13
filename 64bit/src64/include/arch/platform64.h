#ifndef MOWKOW64_ARCH_PLATFORM64_H
#define MOWKOW64_ARCH_PLATFORM64_H

#include <stddef.h>
#include <stdint.h>

struct CONTEXT64;
struct TASK64;

/*
 * Common code must not depend on DAIF, RFLAGS, WFI, or a platform's saved
 * task-frame layout.  The returned IRQ state is opaque and must only be
 * passed back to platform_irq_restore64().
 */
uint64_t platform_irq_save64(void);
void platform_irq_restore64(uint64_t state);
void platform_irq_disable64(void);
void platform_irq_enable64(void);
void platform_halt64(void);
void platform_halt_with_irq64(void);
void platform_wait_for_interrupt64(void);

/* Non-sleeping display visibility hook; bytes must not wrap the address.
 * AArch64 cleans enclosing D-cache lines to PoC; x86 VBE needs no cache clean.
 */
void platform_framebuffer_flush64(uintptr_t address, size_t bytes);
/* Complete a batch of row cleans with one architecture visibility barrier. */
void platform_framebuffer_sync64(void);

int platform_task_context_init64(struct CONTEXT64 *context,
	void (*entry)(void), uintptr_t stack_base, size_t stack_size);
int platform_task_sleep_current64(struct TASK64 *task);
void platform_task_switch64(struct CONTEXT64 *old_context,
	struct CONTEXT64 *new_context);

#endif
