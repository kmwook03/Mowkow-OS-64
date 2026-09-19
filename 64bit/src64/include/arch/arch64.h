#ifndef MOWKOW64_ARCH_ARCH64_H
#define MOWKOW64_ARCH_ARCH64_H

#include <stdint.h>

struct BOOTINFO64;
struct FIFO64;

void arch64_irq_disable(void);
void arch64_irq_enable(void);
void arch64_halt(void);
void arch64_halt_with_irq(void);

void arch64_early_init(void);
void arch64_cpu_init(void);
void arch64_mmu_init(void);
void arch64_irqctl_init(void);
void arch64_timer_init(struct FIFO64 *fifo);
void arch64_input_init(struct FIFO64 *fifo);
int arch64_fb_probe(struct BOOTINFO64 *bootinfo);

int arch64_enter_user(uintptr_t entry, uintptr_t stack, uint64_t argc,
	uintptr_t argv, uintptr_t *saved_kernel_sp);
void arch64_leave_user(uintptr_t kernel_sp, int status);

void arch64_dbg_puts(const char *s);
void arch64_panic_blink(int code);

#endif
