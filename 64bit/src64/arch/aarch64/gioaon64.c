/* BCM2712 always-on GPIO: M1's only observable debug channel. */
#include <arch/arch64.h>
#include <bootinfo64.h>
#include <stdint.h>

#define GIO_AON_BASE 0x107d517c00ULL
#define GIO_DATA_OFFSET 0x04
#define GIO_IODIR_OFFSET 0x08
#define ACT_LED_PIN 9
#define ACT_LED_MASK (1U << ACT_LED_PIN)

static volatile uint32_t *const gio_data =
	(volatile uint32_t *) (uintptr_t) (GIO_AON_BASE + GIO_DATA_OFFSET);
static volatile uint32_t *const gio_iodir =
	(volatile uint32_t *) (uintptr_t) (GIO_AON_BASE + GIO_IODIR_OFFSET);

static inline void barrier64(void)
{
	__asm__ volatile ("dsb sy" ::: "memory");
}

static void act_led_set64(int on)
{
	uint32_t value = *gio_data;

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
	uint32_t direction = *gio_iodir;

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
	struct BOOTINFO64 bootinfo;
	int status;

	arch64_early_init();
	arch64_mmu_init();
	status = arch64_fb_probe(&bootinfo);
	if (status != 0) {
		arch64_panic_blink(status == -2 ? 2 : 3);
	}
	arch64_dbg_puts("Mowkow OS\n");
	arch64_dbg_puts("Raspberry Pi 5 / AArch64\n");
	arch64_dbg_puts("M2: MMU + mailbox framebuffer OK\n\n");
	arch64_dbg_puts("한글 화면 출력 성공\n");

	/* Keep the M1 heartbeat as an independent liveness signal. */
	for (;;) {
		act_led_set64(1);
		delay_ms64(500);
		act_led_set64(0);
		delay_ms64(500);
	}
}
