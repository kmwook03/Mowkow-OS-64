/* Model configuration/MMIO registers and dropped writes around real PCIe/RP1 code. */
#define AARCH64_IO_HOST_TEST
#include "poll_test64.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src64/arch/aarch64/pcie64.c"
#include "../src64/arch/aarch64/rp164.c"

static uint32_t registers64[0x10000 / 4], config64[4096 / 4];
static unsigned int io64, config_reads64, peripheral_reads64;
static uint32_t drop64;
static uint32_t uart_values64[32];
static uint64_t clock64, frequency64 = 1000;
uint64_t a64_counter64(void) { return clock64++; }
uint64_t a64_frequency64(void) { return frequency64; }
void a64_relax64(void) { clock64 += 10; }

uint32_t a64_read32_64(uintptr_t address)
{
	uintptr_t offset = address - arch64_phys_to_virt(PCIE2_BASE);

	io64++;
	if (address >= arch64_phys_to_virt(PCIE_OUTBOUND_BASE64) &&
		address < arch64_phys_to_virt(PCIE_OUTBOUND_BASE64 + RP1_BAR_BYTES64)) {
		peripheral_reads64++;
		uintptr_t uart_offset = address - arch64_phys_to_virt(PCIE_OUTBOUND_BASE64 + RP1_UART0_BASE);

		if (uart_offset < sizeof(uart_values64)) { return uart_values64[uart_offset / 4]; }
		return address == arch64_phys_to_virt(PCIE_OUTBOUND_BASE64) ? RP1_C0_CHIP_ID : 0;
	}
	assert(offset < sizeof(registers64) && (offset & 3U) == 0);
	if (offset >= PCIE_EXT_CFG_DATA && offset < PCIE_EXT_CFG_DATA + 4096U) {
		assert(registers64[PCIE_EXT_CFG_INDEX / 4] == 0x100000U);
		config_reads64++;
		return config64[(offset - PCIE_EXT_CFG_DATA) / 4];
	}
	return registers64[offset / 4];
}
void a64_write32_64(uintptr_t address, uint32_t value)
{
	uintptr_t offset = address - arch64_phys_to_virt(PCIE2_BASE);

	io64++;
	uintptr_t uart_offset = address - arch64_phys_to_virt(PCIE_OUTBOUND_BASE64 + RP1_UART0_BASE);

	if (uart_offset < sizeof(uart_values64)) {
		uart_values64[uart_offset / 4] = value;
		return;
	}
	assert(offset < sizeof(registers64) && (offset & 3U) == 0);
	if (drop64 && offset == drop64) { drop64 = 0; return; }
	registers64[offset / 4] = value;
}
uint16_t a64_read16_64(uintptr_t address) { return (uint16_t) a64_read32_64(address); }
void a64_write16_64(uintptr_t address, uint16_t value)
{
	uintptr_t offset = address - arch64_phys_to_virt(PCIE2_BASE);

	assert(offset < sizeof(registers64) && (offset & 3U) == 0);
	a64_write32_64(address, (registers64[offset / 4] & 0xffff0000U) | value);
}
void a64_barrier64(void) { }

static void setup64(void)
{
	memset(registers64, 0, sizeof(registers64));
	memset(config64, 0, sizeof(config64));
	registers64[PCIE_MISC_PCIE_STATUS / 4] = PCIE_STATUS_DL_ACTIVE | PCIE_STATUS_PHY_LINK_UP;
	registers64[0x18 / 4] = 0x55000000;
	registers64[0x20 / 4] = 0xbff08000; /* 80000000..bfffffff */
	registers64[1] = 0x43210001;
	registers64[PCIE_MEM_WIN0_LO / 4] = 0x1234;
	registers64[PCIE_MEM_WIN0_HI / 4] = 0x55;
	registers64[PCIE_MEM_WIN0_BASE_LIMIT / 4] = 0x87654321;
	registers64[PCIE_MEM_WIN0_BASE_HI / 4] = 0x87;
	registers64[PCIE_MEM_WIN0_LIMIT_HI / 4] = 0x76;
	config64[0] = RP1_VENDOR_DEVICE; config64[1] = 6;
	config64[4] = 0x80400000; config64[5] = 0x80000000;
	pcie_ready64 = 0; pcie2 = 0; drop64 = 0;
	io64 = config_reads64 = peripheral_reads64 = 0;
}
static int probe64(void)
{
	return pcie64_probe_rp1(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
}
static void bar_bounds64(void)
{
	uint64_t physical = 99, dma;

	assert(rp1_bar_range64(0x80000000, 0x80000000, 0xbfffffff, &physical) == 0);
	assert(physical == PCIE_OUTBOUND_BASE64);
	assert(rp1_bar_range64(0xffc00000, 0, UINT32_MAX, &physical) == 0);
	assert(physical == PCIE_OUTBOUND_BASE64 + 0xffc00000ULL);
	for (uint32_t flags = 1; flags < 16; flags++) {
		assert(rp1_bar_range64(0x80000000 | flags, 0x80000000, 0xbfffffff, &physical) == -22);
	}
	assert(rp1_bar_range64(0x80000000, 0x80000000, 0x801fffff, &physical) == -22);
	assert(rp1_bar_range64(0x80001000, 0x80000000, 0xbfffffff, &physical) == -22);
	assert(rp1_bar_range64(0x80000000, 0x90000000, 0xbfffffff, &physical) == -22);
	assert(rp1_bar_range64(0x80000000, 0x80000001, 0xbfffffff, &physical) == -22);
	assert(rp1_bar_range64(0x80000000, 0x80000000, 0xbffffffe, &physical) == -22);
	assert(rp1_bar_range64(0, 0, UINT32_MAX, &physical) == -22);
	assert(rp1_dma_range64(AARCH64_RAM_BYTES64 - 4096U, 4096, 4096, &dma) == 0);
	assert(dma == RP1_DMA_BASE64 + AARCH64_RAM_BYTES64 - 4096U);
	assert(rp1_dma_range64(AARCH64_RAM_BYTES64 - 4096U, 4097, 4096, &dma) == -22);
	assert(rp1_dma_range64(UINTPTR_MAX, SIZE_MAX, 1, &dma) == -22);
	assert(rp1_dma_range64(1, 1, 16, &dma) == -22);
	assert(rp1_dma_range64(0, 1, 3, &dma) == -22);
	assert(rp1_dma_range64(0, 0, 1, &dma) == -22);
}
static void uart_poll64(void)
{
	uint32_t echoed = 99;
	static const uint32_t saved_offsets[] = { UARTIBRD, UARTFBRD, UARTLCR_H, UARTCR };

	setup64(); assert(probe64() == 0);
	for (unsigned int i = 0; i < 4; i++) { uart_values64[saved_offsets[i] / 4] = 50U + i; }
	clock64 = UINT64_MAX - 500U;
	assert(rp164_uart0_loopback(0x80000000, 0x80000000, &echoed) == 0);
	for (unsigned int failure = 0; failure < 3; failure++) {
		uart_values64[UARTFR / 4] = failure == 1 ? UARTFR_RXFE : UARTFR_TXFF;
		if (failure == 2) { frequency64 = 0; }
		assert(rp164_uart0_loopback(0x80000000, 0x80000000, &echoed) ==
			(failure == 1 ? -3 : -2));
		assert(strcmp(poll_last64.device, "rp1-uart0") == 0 &&
			strcmp(poll_last64.stage, failure == 1 ? "loopback-rx" : "loopback-tx") == 0);
		assert(poll_last64.error == (failure == 2 ? -22 : -110));
		for (unsigned int i = 0; i < 4; i++) {
			assert(uart_values64[saved_offsets[i] / 4] == 50U + i);
		}
	}
	frequency64 = 1000;
}

int main(void)
{
	uint32_t state[4], chip, platform;
	uint32_t before[5];
	uintptr_t physical, virtual;
	unsigned int io;
	static const uint32_t outbound[5] = { PCIE_MEM_WIN0_LO, PCIE_MEM_WIN0_HI,
		PCIE_MEM_WIN0_BASE_LIMIT, PCIE_MEM_WIN0_BASE_HI, PCIE_MEM_WIN0_LIMIT_HI };
	static const uint32_t dma[5] = { PCIE_MISC_MISC_CTRL, PCIE_MISC_RC_BAR4_CONFIG_LO,
		PCIE_MISC_RC_BAR4_CONFIG_HI, PCIE_MISC_UBUS_BAR4_REMAP_LO, PCIE_MISC_UBUS_BAR4_REMAP_HI };

	uart_poll64();
	bar_bounds64(); setup64();
	assert(pcie64_enable_rp1_dma(state) < 0 && io64 == 0);
	assert(rp164_bar1_base(0x80000000, 0x80000000, &virtual) < 0 && io64 == 0);
	assert(probe64() == 0 && pcie_ready64);
	assert(pcie_rp1_range64(0x80000000, 0x80000000, RP1_BAR_BYTES64 - 1U, 1, &physical) == 0);
	assert(physical == PCIE_OUTBOUND_BASE64 + RP1_BAR_BYTES64 - 1U);
	assert(pcie_rp1_range64(0x80000000, 0x80000000, RP1_BAR_BYTES64 - 1U, 2, &physical) < 0);
	assert(pcie_rp1_range64(0x80400000, 0x80000000, 0, 4, &physical) < 0);
	assert(pcie_rp1_range64(0x80000000, 0x80000000, SIZE_MAX, 4, &physical) < 0);
	assert(rp164_probe(0x80000000, 0x80000000, 6, &chip, &platform) == 0);
	assert(chip == RP1_C0_CHIP_ID && peripheral_reads64 == 2);
	io = io64;
	assert(rp164_probe(0x80000004, 0x80000000, 6, &chip, &platform) < 0 && io64 == io);
	assert(config_read32(256, 0, 0, 0) == UINT32_MAX && io64 == io);
	assert(config_read32(1, 32, 0, 0) == UINT32_MAX && io64 == io);
	assert(config_read32(1, 0, 8, 0) == UINT32_MAX && io64 == io);
	assert(config_read32(1, 0, 0, 4096) == UINT32_MAX && io64 == io);
	assert(config_read32(1, 0, 0, 1) == UINT32_MAX && io64 == io);
	assert(pcie64_enable_rp1_dma(state) == 0);
	/* Inbound failures restore every register rather than leaving a partial window. */
	for (unsigned int i = 0; i < 5; i++) { registers64[dma[i] / 4] = before[i] = 0x100U + i; }
	drop64 = PCIE_MISC_RC_BAR4_CONFIG_HI;
	assert(pcie64_enable_rp1_dma(state) == -2);
	assert(strcmp(poll_last64.stage, "dma-readback") == 0 && poll_last64.value == 0 &&
		poll_last64.address == pcie2 + PCIE_MISC_UBUS_BAR4_REMAP_HI);
	for (unsigned int i = 0; i < 5; i++) { assert(registers64[dma[i] / 4] == before[i]); }
	setup64();
	for (unsigned int i = 0; i < 5; i++) { before[i] = registers64[outbound[i] / 4]; }
	drop64 = PCIE_MEM_WIN0_HI;
	assert(probe64() < 0 && !pcie_ready64);
	assert(strcmp(poll_last64.stage, "outbound-readback") == 0 && poll_last64.value == 0x55 &&
		poll_last64.address == pcie2 + PCIE_MEM_WIN0_HI);
	for (unsigned int i = 0; i < 5; i++) { assert(registers64[outbound[i] / 4] == before[i]); }
	assert(registers64[1] == 0x43210001 && registers64[0x18 / 4] == 0x55000000);
	for (unsigned int bad = 0; bad < 6; bad++) {
		setup64();
		if (bad == 0) { config64[5] |= 4; }
		if (bad == 1) { config64[5] = 0xbfe00000; }
		if (bad == 2) { registers64[0x20 / 4] |= 1; }
		if (bad == 3) { config64[4] |= 4; }
		if (bad == 4) { config64[1] = 0; }
		if (bad == 5) { config64[0] = UINT32_MAX; }
		assert(probe64() < 0 && !pcie_ready64 && peripheral_reads64 == 0);
		assert(registers64[PCIE_MEM_WIN0_LO / 4] == 0x1234);
		assert(registers64[0x18 / 4] == 0x55000000);
	}
	setup64(); registers64[PCIE_MISC_PCIE_STATUS / 4] = 0;
	assert(probe64() < 0 && config_reads64 == 0);
	puts("aarch64 PCIe/RP1: PASS");
	return 0;
}
