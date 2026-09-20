/*
 * rp164.c -- RP1 BAR1 peripheral-window probe
 *
 * BCM2712 PCIe2 maps 32-bit PCI memory addresses at CPU physical
 * 0x1f00000000. RP1 exposes its 0xc040000000 peripheral window through BAR1.
 * Convert the firmware-assigned BAR relative to the root bridge's PCI memory
 * base; that base is commonly 0x80000000 rather than zero.
 */
#include <arch/arch64.h>
#include <stdint.h>

#define PCIE2_OUTBOUND_BASE 0x1f00000000ULL
#define PCI_COMMAND_MEMORY (1U << 1)
#define PCI_BAR_IO (1U << 0)
#define PCI_BAR_ADDRESS_MASK 0xfffffff0U

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
#define UART_POLL_LIMIT 100000U

static int bar1_physical64(uint32_t bar1, uint32_t root_memory_base,
	uint64_t *physical)
{
	uint32_t bar_address;

	if ((bar1 & PCI_BAR_IO) != 0) {
		return -1;
	}
	bar_address = bar1 & PCI_BAR_ADDRESS_MASK;
	if (bar_address < root_memory_base) {
		return -2;
	}
	*physical = PCIE2_OUTBOUND_BASE +
		(uint64_t) (bar_address - root_memory_base);
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
	chip = sysinfo[RP1_SYSINFO_CHIP_ID / sizeof(uint32_t)];
	if (chip_id != NULL) {
		*chip_id = chip;
	}
	if (platform != NULL) {
		*platform = sysinfo[RP1_SYSINFO_PLATFORM / sizeof(uint32_t)];
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
	volatile uint8_t *uart;
	uint64_t physical;
	unsigned int i;

	if (registers == NULL ||
			bar1_physical64(bar1, root_memory_base, &physical) != 0) {
		return -1;
	}
	uart = (volatile uint8_t *) arch64_phys_to_virt(
		(uintptr_t) (physical + RP1_UART0_BASE));
	for (i = 0; i < 5; i++) {
		registers[i] = *(volatile uint32_t *) (uart + offsets[i]);
		if (registers[i] == 0xdeaddeadU || registers[i] == 0xffffffffU) {
			return -2;
		}
	}
	return 0;
}

int rp164_uart0_loopback(uint32_t bar1, uint32_t root_memory_base,
	uint32_t *echoed)
{
	volatile uint8_t *uart;
	uint64_t physical;
	uint32_t old_ibrd;
	uint32_t old_fbrd;
	uint32_t old_lcrh;
	uint32_t old_cr;
	uint32_t received;
	unsigned int count;
	int status;

	if (echoed == NULL ||
			bar1_physical64(bar1, root_memory_base, &physical) != 0) {
		return -1;
	}
	uart = (volatile uint8_t *) arch64_phys_to_virt(
		(uintptr_t) (physical + RP1_UART0_BASE));
	old_ibrd = *(volatile uint32_t *) (uart + UARTIBRD);
	old_fbrd = *(volatile uint32_t *) (uart + UARTFBRD);
	old_lcrh = *(volatile uint32_t *) (uart + UARTLCR_H);
	old_cr = *(volatile uint32_t *) (uart + UARTCR);

	*(volatile uint32_t *) (uart + UARTCR) = 0;
	*(volatile uint32_t *) (uart + UARTECR) = 0;
	*(volatile uint32_t *) (uart + UARTICR) = 0x7ffU;
	/* RP1 clk_uart is 48 MHz: 26 + 3/64 gives 115200 baud. */
	*(volatile uint32_t *) (uart + UARTIBRD) = 26U;
	*(volatile uint32_t *) (uart + UARTFBRD) = 3U;
	*(volatile uint32_t *) (uart + UARTLCR_H) =
		UARTLCR_H_FEN | UARTLCR_H_WLEN_8;
	*(volatile uint32_t *) (uart + UARTCR) = UARTCR_UARTEN | UARTCR_LBE |
		UARTCR_TXE | UARTCR_RXE;
	__asm__ volatile ("dsb sy" ::: "memory");

	status = -2;
	for (count = 0; count < UART_POLL_LIMIT; count++) {
		if ((*(volatile uint32_t *) (uart + UARTFR) & UARTFR_TXFF) == 0) {
			status = 0;
			break;
		}
	}
	if (status == 0) {
		*(volatile uint32_t *) (uart + UARTDR) = UART_LOOPBACK_BYTE;
		__asm__ volatile ("dsb sy" ::: "memory");
		status = -3;
		for (count = 0; count < UART_POLL_LIMIT; count++) {
			if ((*(volatile uint32_t *) (uart + UARTFR) &
					UARTFR_RXFE) == 0) {
				status = 0;
				break;
			}
		}
	}
	if (status == 0) {
		received = *(volatile uint32_t *) (uart + UARTDR);
		*echoed = received;
		if ((received & 0xffU) != UART_LOOPBACK_BYTE ||
				(received & 0xf00U) != 0) {
			status = -4;
		}
	}

	/* Leave firmware state exactly as M5c observed it. */
	*(volatile uint32_t *) (uart + UARTCR) = 0;
	*(volatile uint32_t *) (uart + UARTIBRD) = old_ibrd;
	*(volatile uint32_t *) (uart + UARTFBRD) = old_fbrd;
	*(volatile uint32_t *) (uart + UARTLCR_H) = old_lcrh;
	*(volatile uint32_t *) (uart + UARTCR) = old_cr;
	__asm__ volatile ("dsb sy" ::: "memory");
	return status;
}
