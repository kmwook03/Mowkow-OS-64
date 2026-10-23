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
struct ARCH64_EXCEPTION_FRAME;
struct FIFO64;

void arch64_irq_disable(void);
void arch64_irq_enable(void);
void arch64_halt(void);
void arch64_halt_with_irq(void);

void arch64_early_init(void);
void aarch64_main(void);
void aarch64_high_main(void);
void arch64_cpu_init(void);
void arch64_exception_handler(uint64_t vector, uint64_t esr, uint64_t elr,
	uint64_t far);
void arch64_kernel_sync_handler(const struct ARCH64_EXCEPTION_FRAME *frame,
	uint64_t esr, uint64_t far);
uint64_t arch64_sync_dispatch(struct ARCH64_EXCEPTION_FRAME *frame,
	uint64_t esr);
void arch64_mmu_init(void);
void arch64_mmu_finish_high(void);
int arch64_mmu_self_test(void);
int arch64_user_map_range(uintptr_t base, size_t size, int executable);
void arch64_user_unmap_all(void);
void arch64_sync_user_code(uintptr_t physical, size_t size);
void arch64_irqctl_init(void);
uintptr_t arch64_irq_dispatch(uintptr_t frame);
void arch64_timer_init(struct FIFO64 *fifo);
void arch64_timer_set_debug_output(int enabled);
uint64_t arch64_timer_ticks(void);
uintptr_t arch64_timer_handle_irq(uintptr_t frame);
void arch64_input_init(struct FIFO64 *fifo);
int arch64_fb_probe(struct BOOTINFO64 *bootinfo);
void arch64_fb_set_hangul_font(const uint8_t *font);
int aarch64_mailbox_call(uint8_t channel, volatile uint32_t *message,
	size_t bytes);
int pcie64_probe_rp1(uint32_t *vendor_device, uint32_t *class_revision,
	uint32_t *bar0, uint32_t *bar1, uint32_t *command_status,
	uint32_t *root_bus_numbers, uint32_t *root_memory_base,
	uint32_t *root_memory_limit);
int pcie64_enable_rp1_dma(uint32_t state[4]);
int rp164_probe(uint32_t bar1, uint32_t root_memory_base,
	uint32_t command_status, uint32_t *chip_id, uint32_t *platform);
int rp164_probe_uart0(uint32_t bar1, uint32_t root_memory_base,
	uint32_t registers[5]);
int rp164_uart0_loopback(uint32_t bar1, uint32_t root_memory_base,
	uint32_t *echoed);
int rp164_bar1_base(uint32_t bar1, uint32_t root_memory_base,
	uintptr_t *virtual_base);
int arch64_scheduler_init(void);
uintptr_t arch64_scheduler_tick(uintptr_t frame);
void arch64_scheduler_main_beat(void);
int arch64_scheduler_healthy(void);
int arch64_fp_context_self_test(void);
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
