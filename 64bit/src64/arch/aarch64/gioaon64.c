/* BCM2712 always-on GPIO: M1's only observable debug channel. */
#include <arch/arch64.h>
#include <bootinfo64.h>
#include <stddef.h>
#include <stdint.h>

#define GIO_AON_BASE 0x107d517c00ULL
#define GIO_DATA_OFFSET 0x04
#define GIO_IODIR_OFFSET 0x08
#define ACT_LED_PIN 9
#define ACT_LED_MASK (1U << ACT_LED_PIN)

static uintptr_t gio_address64(uint32_t offset)
{
	uintptr_t pc;
	uintptr_t physical;

	__asm__ volatile ("adr %0, ." : "=r" (pc));
	physical = (uintptr_t) GIO_AON_BASE + offset;
	if (pc >= (uintptr_t) ARCH64_KERNEL_VA_BASE) {
		return arch64_phys_to_virt(physical);
	}
	return physical;
}

static inline void barrier64(void)
{
	__asm__ volatile ("dsb sy" ::: "memory");
}

static void act_led_set64(int on)
{
	volatile uint32_t *gio_data;
	uint32_t value;

	gio_data = (volatile uint32_t *) gio_address64(GIO_DATA_OFFSET);
	value = *gio_data;

	/* The Pi 5 ACT LED is active-low. */
	if (on != 0) {
		value &= ~ACT_LED_MASK;
	} else {
		value |= ACT_LED_MASK;
	}
	*gio_data = value;
	barrier64();
}

static void act_led_init64(void)
{
	volatile uint32_t *gio_iodir;
	uint32_t direction;

	gio_iodir = (volatile uint32_t *) gio_address64(GIO_IODIR_OFFSET);
	direction = *gio_iodir;

	/* brcmstb GIO uses 0 for output and 1 for input. */
	direction &= ~ACT_LED_MASK;
	*gio_iodir = direction;
	barrier64();
	act_led_set64(0);
}

static uint64_t counter64(void)
{
	uint64_t value;

	__asm__ volatile ("isb\n\tmrs %0, cntpct_el0" : "=r" (value));
	return value;
}

static void delay_ms64(uint32_t milliseconds)
{
	uint64_t frequency;
	uint64_t deadline;

	__asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (frequency));
	deadline = counter64() + (frequency / 1000) * milliseconds;
	while ((int64_t) (counter64() - deadline) < 0) {
		__asm__ volatile ("yield");
	}
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

void arch64_early_init(void)
{
	act_led_init64();
}

void arch64_panic_blink(int code)
{
	int i;

	act_led_init64();
	if (code < 1) {
		code = 1;
	}
	for (;;) {
		for (i = 0; i < code; i++) {
			act_led_set64(1);
			delay_ms64(150);
			act_led_set64(0);
			delay_ms64(150);
		}
		delay_ms64(1200);
	}
}

void aarch64_main(void)
{
	arch64_early_init();
	arch64_mmu_init();
	arch64_enter_high();
}

void aarch64_high_main(void)
{
	struct BOOTINFO64 bootinfo;
	int status;

	arch64_mmu_finish_high();
	arch64_cpu_init();
	status = arch64_fb_probe(&bootinfo);
	if (status != 0) {
		arch64_panic_blink(status == -2 ? 2 : 3);
	}
	status = arch64_mmu_self_test();
	if (status != 0) {
		arch64_panic_blink(5);
	}
	arch64_dbg_puts("Mowkow OS\n");
	arch64_dbg_puts("Raspberry Pi 5 / AArch64\n");
	arch64_dbg_puts("M2: MMU + mailbox framebuffer OK\n\n");
	arch64_dbg_puts("한글 화면 출력 성공\n");
	arch64_irqctl_init();
	arch64_scheduler_init();
	arch64_timer_init(NULL);
	arch64_dbg_puts("M3a: vectors + GIC + timer IRQ\n");
	arch64_dbg_puts("M3b: exception-frame scheduler\n");
	arch64_dbg_puts("M3c: shared mtask64 scheduler\n");
	arch64_dbg_puts("M3d: 4 KiB TTBR0 + TTBR1 paging\n");
	arch64_dbg_puts("M3e: TTBR1 high-half kernel + empty user TTBR0\n");
	arch64_dbg_puts("MTASK: ");
	arch64_irq_enable();

	/* Keep the M1 heartbeat as an independent liveness signal. */
	for (;;) {
		arch64_scheduler_main_beat();
		act_led_set64(1);
		delay_ms64(500);
		arch64_scheduler_main_beat();
		act_led_set64(0);
		delay_ms64(500);
	}
}
