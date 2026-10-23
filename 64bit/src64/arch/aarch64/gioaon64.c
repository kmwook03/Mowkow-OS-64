/* BCM2712 always-on GPIO and the active-low Pi 5 ACT LED. */
#include <arch/arch64.h>
#include <arch/aarch64_boot64.h>
#include <arch/poll_deadline64.h>
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

void aarch64_act_led_set64(int on)
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
	aarch64_act_led_set64(0);
}

static void delay_ms64(uint32_t milliseconds)
{
	struct POLL_DEADLINE64 deadline;

	/* Fatal blinking cannot recover a broken counter or depend on logging. */
	if (poll_deadline_init64(&deadline, aarch64_boot_counter64(),
			aarch64_boot_frequency64(), milliseconds, 1000U) != 0) { return; }
	while (!poll_deadline_expired64(&deadline, aarch64_boot_counter64())) {
		__asm__ volatile ("yield");
	}
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
			aarch64_act_led_set64(1);
			delay_ms64(150);
			aarch64_act_led_set64(0);
			delay_ms64(150);
		}
		delay_ms64(1200);
	}
}
