#include <arch/platform64.h>
#include <arch/x86_boot64.h>
#include <pci64.h>
#include <timer64.h>

#define PIIX4_PM64 0x71138086U
#define ICH9_LPC64 0x29188086U
#define PMBASE64 0x40
#define PM1_CNT64 4U
#define SLEEP_ENABLE64 (1U << 13)

#ifdef POWER64_HOST_TEST
void power64_test_out16(uint16_t port, uint16_t value);
uint16_t power64_test_in16(uint16_t port);
#endif

static void power_out16_64(uint16_t port, uint16_t value)
{
#ifdef POWER64_HOST_TEST
	power64_test_out16(port, value);
#else
	__asm__ volatile ("outw %0, %1" :: "a" (value), "Nd" (port) : "memory");
#endif
}

static uint16_t power_in16_64(uint16_t port)
{
#ifdef POWER64_HOST_TEST
	return power64_test_in16(port);
#else
	uint16_t value;

	__asm__ volatile ("inw %1, %0" : "=a" (value) : "Nd" (port) : "memory");
	return value;
#endif
}

int platform_poweroff64(void)
{
	uint32_t id;
	uint32_t base;
	uint64_t deadline;

	/* Supported QEMU pc/q35 chipsets encode S5 as SLP_TYP=0. Read firmware's
	 * enabled PMBASE instead of guessing an I/O port. Other hardware needs
	 * full ACPI table/AML support and is deliberately left to the safe halt.
	 */
	for (uint32_t bdf = 0; bdf < 256; bdf++) {
		id = pci64_read32(bdf, 0);
		if (id == PIIX4_PM64) {
			if ((pci64_read32(bdf, 0x80) & 1U) == 0) { continue; }
			base = pci64_read32(bdf, PMBASE64) & 0xffc0U;
		} else if (id == ICH9_LPC64) {
			if ((pci64_read32(bdf, 0x44) & 0x80U) == 0) { continue; }
			base = pci64_read32(bdf, PMBASE64) & 0xff80U;
		} else {
			continue;
		}
		if (base == 0) { continue; }
		power_out16_64((uint16_t) (base + PM1_CNT64), SLEEP_ENABLE64);
		deadline = poll_deadline64(1000);
		for (uint32_t polls = 0; polls < 10000000U; polls++) {
			if (poll_deadline_expired64(deadline)) { break; }
		}
		x86_boot_log64("acpi: poweroff timeout PM1_CNT port=");
		x86_boot_log_uint64(base + PM1_CNT64);
		x86_boot_log64(" value=");
		x86_boot_log_uint64(power_in16_64((uint16_t) (base + PM1_CNT64)));
		x86_boot_log64("\n");
		return -110;
	}
	return -95;
}
