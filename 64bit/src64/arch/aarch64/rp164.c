/*
 * rp164.c -- RP1 BAR1 peripheral-window probe
 *
 * BCM2712 PCIe2 maps 32-bit PCI memory addresses at CPU physical
 * 0x1f00000000. RP1 exposes its 0xc040000000 peripheral window through BAR1.
 * Convert the firmware-assigned BAR relative to the root bridge's PCI memory
 * base; that base is commonly 0x80000000 rather than zero.
 */
#include <arch/arch64.h>
#include <arch/aarch64_memory64.h>
#include <arch/aarch64_poll64.h>
#include "platform_io64.h"
#include <stdint.h>

#define PCI_COMMAND_MEMORY (1U << 1)

#define RP1_SYSINFO_CHIP_ID 0x00U
#define RP1_SYSINFO_PLATFORM 0x04U
#define RP1_C0_CHIP_ID 0x20001927U
#define RP1_UART0_BASE 0x30000U
#define UARTDR 0x00U
#define UARTECR 0x04U
#define UARTFR 0x18U
#define UARTIBRD 0x24U
#define UARTFBRD 0x28U
#define UARTLCR_H 0x2cU
#define UARTCR 0x30U
#define UARTICR 0x44U

#define UARTFR_RXFE (1U << 4)
#define UARTFR_TXFF (1U << 5)
#define UARTLCR_H_FEN (1U << 4)
#define UARTLCR_H_WLEN_8 (3U << 5)
#define UARTCR_UARTEN (1U << 0)
#define UARTCR_LBE (1U << 7)
#define UARTCR_TXE (1U << 8)
#define UARTCR_RXE (1U << 9)
#define UART_LOOPBACK_BYTE 0x4dU
#define UART_TIMEOUT_MS64 1000U

static int bar1_physical64(uint32_t bar1, uint32_t root_memory_base,
	uint64_t *physical)
{
	uintptr_t base;

	if (physical == NULL || pcie_rp1_range64(bar1, root_memory_base, 0,
			RP1_BAR_BYTES64, &base) != 0) { return -22; }
	*physical = base;
	return 0;
}

int rp164_bar1_base(uint32_t bar1, uint32_t root_memory_base,
	uintptr_t *virtual_base)
{
	uint64_t physical;

	if (virtual_base == NULL ||
			bar1_physical64(bar1, root_memory_base, &physical) != 0) {
		return -1;
	}
	*virtual_base = arch64_phys_to_virt((uintptr_t) physical);
	return 0;
}

int rp164_probe(uint32_t bar1, uint32_t root_memory_base,
	uint32_t command_status, uint32_t *chip_id, uint32_t *platform)
{
	volatile uint32_t *sysinfo;
	uint64_t physical;
	uint32_t chip;

	if ((command_status & PCI_COMMAND_MEMORY) == 0) {
		return -1;
	}
	if (bar1_physical64(bar1, root_memory_base, &physical) != 0) {
		return -2;
	}
	sysinfo = (volatile uint32_t *) arch64_phys_to_virt((uintptr_t) physical);
	chip = a64_read32_64((uintptr_t) sysinfo + RP1_SYSINFO_CHIP_ID);
	if (chip_id != NULL) {
		*chip_id = chip;
	}
	if (platform != NULL) {
		*platform = a64_read32_64((uintptr_t) sysinfo + RP1_SYSINFO_PLATFORM);
	}
	if (chip != RP1_C0_CHIP_ID) {
		return -3;
	}
	return 0;
}

int rp164_probe_uart0(uint32_t bar1, uint32_t root_memory_base,
	uint32_t registers[5])
{
	static const uint32_t offsets[5] = {
		UARTFR, UARTIBRD, UARTFBRD, UARTLCR_H, UARTCR
	};
	uintptr_t uart;
	uint64_t physical;
	unsigned int i;

	if (registers == NULL ||
			bar1_physical64(bar1, root_memory_base, &physical) != 0) {
		return -1;
	}
	uart = arch64_phys_to_virt((uintptr_t) (physical + RP1_UART0_BASE));
	for (i = 0; i < 5; i++) {
		registers[i] = a64_read32_64(uart + offsets[i]);
		if (registers[i] == 0xdeaddeadU || registers[i] == 0xffffffffU) {
			return -2;
		}
	}
	return 0;
}

static int uart_wait64(uintptr_t uart, uint32_t mask, const char *stage)
{
	struct POLL_DEADLINE64 deadline;
	uint64_t frequency = a64_frequency64();
	uint32_t value;

	if (poll_deadline_init64(&deadline, a64_counter64(), frequency,
			UART_TIMEOUT_MS64, 1000U) != 0) {
		aarch64_poll_fail64("rp1-uart0", stage, "CNTFRQ_EL0", 0, frequency, -22);
		return -22;
	}
	for (;;) {
		value = a64_read32_64(uart + UARTFR);
		if ((value & mask) == 0) { return 0; }
		if (poll_deadline_expired64(&deadline, a64_counter64())) {
			aarch64_poll_fail64("rp1-uart0", stage, "UARTFR", uart + UARTFR, value, -110);
			return -110;
		}
		a64_relax64();
	}
}

int rp164_uart0_loopback(uint32_t bar1, uint32_t root_memory_base,
	uint32_t *echoed)
{
	uintptr_t uart;
	uint64_t physical;
	uint32_t old_ibrd;
	uint32_t old_fbrd;
	uint32_t old_lcrh;
	uint32_t old_cr;
	uint32_t received;
	int status;

	if (echoed == NULL ||
			bar1_physical64(bar1, root_memory_base, &physical) != 0) {
		return -1;
	}
	uart = arch64_phys_to_virt((uintptr_t) (physical + RP1_UART0_BASE));
	old_ibrd = a64_read32_64(uart + UARTIBRD);
	old_fbrd = a64_read32_64(uart + UARTFBRD);
	old_lcrh = a64_read32_64(uart + UARTLCR_H);
	old_cr = a64_read32_64(uart + UARTCR);

	a64_write32_64(uart + UARTCR, 0);
	a64_write32_64(uart + UARTECR, 0);
	a64_write32_64(uart + UARTICR, 0x7ffU);
	/* RP1 clk_uart is 48 MHz: 26 + 3/64 gives 115200 baud. */
	a64_write32_64(uart + UARTIBRD, 26U);
	a64_write32_64(uart + UARTFBRD, 3U);
	a64_write32_64(uart + UARTLCR_H, UARTLCR_H_FEN | UARTLCR_H_WLEN_8);
	a64_write32_64(uart + UARTCR, UARTCR_UARTEN | UARTCR_LBE |
		UARTCR_TXE | UARTCR_RXE);
	a64_barrier64();

	status = uart_wait64(uart, UARTFR_TXFF, "loopback-tx") == 0 ? 0 : -2;
	if (status == 0) {
		a64_write32_64(uart + UARTDR, UART_LOOPBACK_BYTE);
		a64_barrier64();
		status = uart_wait64(uart, UARTFR_RXFE, "loopback-rx") == 0 ? 0 : -3;
	}
	if (status == 0) {
		received = a64_read32_64(uart + UARTDR);
		*echoed = received;
		if ((received & 0xffU) != UART_LOOPBACK_BYTE ||
				(received & 0xf00U) != 0) {
			status = -4;
			aarch64_poll_fail64("rp1-uart0", "loopback-echo", "UARTDR",
				uart + UARTDR, received, -5);
		}
	}

	/* Leave firmware state exactly as M5c observed it. */
	a64_write32_64(uart + UARTCR, 0);
	a64_write32_64(uart + UARTIBRD, old_ibrd);
	a64_write32_64(uart + UARTFBRD, old_fbrd);
	a64_write32_64(uart + UARTLCR_H, old_lcrh);
	a64_write32_64(uart + UARTCR, old_cr);
	a64_barrier64();
	return status;
}
