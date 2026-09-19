#ifndef MOWKOW64_ARCH_ARCH64_H
#define MOWKOW64_ARCH_ARCH64_H

#include <stddef.h>
#include <stdint.h>

#define ARCH64_KERNEL_VA_BASE 0xffffff8000000000ULL

static inline uintptr_t arch64_phys_to_virt(uintptr_t physical)
{
	return (uintptr_t) ARCH64_KERNEL_VA_BASE + physical;
}

static inline uintptr_t arch64_virt_to_phys(uintptr_t virtual_address)
{
	if (virtual_address >= (uintptr_t) ARCH64_KERNEL_VA_BASE) {
		return virtual_address - (uintptr_t) ARCH64_KERNEL_VA_BASE;
	}
	return virtual_address;
}

struct BOOTINFO64;
struct FIFO64;

void arch64_irq_disable(void);
void arch64_irq_enable(void);
void arch64_halt(void);
void arch64_halt_with_irq(void);

void arch64_early_init(void);
void arch64_cpu_init(void);
void arch64_mmu_init(void);
void arch64_mmu_finish_high(void);
int arch64_mmu_self_test(void);
void arch64_irqctl_init(void);
void arch64_timer_init(struct FIFO64 *fifo);
void arch64_input_init(struct FIFO64 *fifo);
int arch64_fb_probe(struct BOOTINFO64 *bootinfo);

void arch64_scheduler_init(void);
uintptr_t arch64_scheduler_tick(uintptr_t frame);
void arch64_scheduler_main_beat(void);
int arch64_scheduler_healthy(void);
uintptr_t arch64_task_frame_init(void (*entry)(void), uintptr_t stack_base,
	size_t stack_size);
uint64_t arch64_irq_save(void);
void arch64_irq_restore(uint64_t state);

int arch64_enter_user(uintptr_t entry, uintptr_t stack, uint64_t argc,
	uintptr_t argv, uintptr_t *saved_kernel_sp);
void arch64_leave_user(uintptr_t kernel_sp, int status);

void arch64_dbg_puts(const char *s);
void arch64_panic_blink(int code);
void arch64_enter_high(void) __attribute__((noreturn));

#endif
