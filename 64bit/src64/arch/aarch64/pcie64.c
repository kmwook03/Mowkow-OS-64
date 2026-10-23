/* Firmware-preserving BCM2712 PCIe/RP1 probe and checked address windows. */
#include <arch/aarch64_memory64.h>
#include <arch/aarch64_poll64.h>
#include "platform_io64.h"

#define PCIE2_BASE 0x1000120000ULL
#define PCIE2_OUTBOUND_BASE PCIE_OUTBOUND_BASE64

#define PCIE_MEM_WIN0_LO 0x400c
#define PCIE_MEM_WIN0_HI 0x4010
#define PCIE_MISC_MISC_CTRL 0x4008
#define PCIE_MISC_MISC_CTRL_SCB_ACCESS_EN (1U << 12)
#define PCIE_MISC_PCIE_STATUS 0x4068
#define PCIE_STATUS_DL_ACTIVE (1U << 5)
#define PCIE_STATUS_PHY_LINK_UP (1U << 4)
#define PCIE_MEM_WIN0_BASE_LIMIT 0x4070
#define PCIE_MEM_WIN0_BASE_HI 0x4080
#define PCIE_MEM_WIN0_LIMIT_HI 0x4084
#define PCIE_MISC_RC_BAR4_CONFIG_LO 0x40d4
#define PCIE_MISC_RC_BAR4_CONFIG_HI 0x40d8
#define PCIE_MISC_UBUS_BAR4_REMAP_LO 0x410c
#define PCIE_MISC_UBUS_BAR4_REMAP_HI 0x4110
#define PCIE_MISC_UBUS_BAR_REMAP_ENABLE (1U << 0)
#define PCIE_RC_BAR_SIZE_64GB 0x15U
#define RP1_SYSTEM_RAM_PCI_HI 0x10U
#define PCIE_EXT_CFG_DATA 0x8000
#define PCIE_EXT_CFG_INDEX 0x9000

#define PCI_COMMAND_MEMORY (1U << 1)
#define PCI_COMMAND_MASTER (1U << 2)

#define RP1_BUS 1U
#define RP1_DEVICE 0U
#define RP1_FUNCTION 0U
#define RP1_VENDOR_DEVICE 0x00011de4U

static uintptr_t pcie2;
static int pcie_ready64;
/* Boot-only setup precedes all RP1 users; IRQ paths never reconfigure windows. */
static uint32_t validated_bar64, validated_base64, validated_limit64;

static uint32_t read_pcie64(uint32_t offset) { return a64_read32_64(pcie2 + offset); }
static void write_pcie64(uint32_t offset, uint32_t value)
{
	a64_write32_64(pcie2 + offset, value);
}

static uint32_t config_read32(uint32_t bus, uint32_t device,
	uint32_t function, uint32_t offset)
{
	if (pcie2 == 0 || bus > 255U || device > 31U || function > 7U ||
		offset > 4092U || (offset & 3U) != 0) { return UINT32_MAX; }
	write_pcie64(PCIE_EXT_CFG_INDEX, (bus << 20) | (device << 15) | (function << 12));
	a64_barrier64();
	return read_pcie64(PCIE_EXT_CFG_DATA + offset);
}

static int configure_outbound_window64(uint32_t pci_base, uint32_t pci_limit)
{
	static const uint32_t offsets[5] = { PCIE_MEM_WIN0_LO, PCIE_MEM_WIN0_HI,
		PCIE_MEM_WIN0_BASE_LIMIT, PCIE_MEM_WIN0_BASE_HI, PCIE_MEM_WIN0_LIMIT_HI };
	uint32_t before[5], values[5];
	uint16_t command;
	uint64_t first = PCIE2_OUTBOUND_BASE >> 20;
	uint64_t last;
	int failed = 0;

	if (pcie2 == 0 || pci_limit < pci_base || (pci_base & 0xfffffU) != 0 ||
		(pci_limit & 0xfffffU) != 0xfffffU) { return -22; }
	last = first + ((uint64_t) pci_limit - pci_base) / 0x100000ULL;
	if (last >= (1ULL << 20)) { return -22; }
	for (size_t i = 0; i < 5; i++) { before[i] = read_pcie64(offsets[i]); }
	command = a64_read16_64(pcie2 + 4U);
	values[0] = pci_base; values[1] = 0;
	values[2] = (before[2] & ~(0xfff00000U | 0x0000fff0U)) |
		(((uint32_t) first & 0xfffU) << 4) | (((uint32_t) last & 0xfffU) << 20);
	values[3] = (before[3] & ~255U) | ((uint32_t) (first >> 12) & 255U);
	values[4] = (before[4] & ~255U) | ((uint32_t) (last >> 12) & 255U);
	for (size_t i = 0; i < 5; i++) { write_pcie64(offsets[i], values[i]); }
	a64_write16_64(pcie2 + 4U, command | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
	a64_barrier64();
	for (size_t i = 0; i < 5; i++) {
		uint32_t sampled = read_pcie64(offsets[i]);

		if (sampled != values[i]) {
			failed = 1;
			aarch64_poll_fail64("pcie2", "outbound-readback", "MEM_WIN0", pcie2 + offsets[i],
				sampled, -5);
		}
	}
	uint16_t sampled_command = a64_read16_64(pcie2 + 4U);

	if ((sampled_command & (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)) !=
			(PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)) {
		failed = 1;
		aarch64_poll_fail64("pcie2", "outbound-readback", "PCI_COMMAND", pcie2 + 4U,
			sampled_command, -5);
	}
	if (!failed) { return 0; }
	a64_write16_64(pcie2 + 4U, command);
	for (size_t i = 0; i < 5; i++) { write_pcie64(offsets[i], before[i]); }
	a64_barrier64();
	return -5;
}

int pcie_rp1_range64(uint32_t bar, uint32_t root_base, size_t offset,
	size_t bytes, uintptr_t *physical)
{
	uint64_t base;

	if (!pcie_ready64 || physical == NULL || bar != validated_bar64 ||
		root_base != validated_base64 || offset >= RP1_BAR_BYTES64 || bytes == 0 ||
		bytes > RP1_BAR_BYTES64 - offset ||
		rp1_bar_range64(bar, root_base, validated_limit64, &base) != 0) { return -22; }
	*physical = (uintptr_t) base + offset;
	return 0;
}

int pcie64_enable_rp1_dma(uint32_t state[4])
{
	static const uint32_t offsets[5] = { PCIE_MISC_MISC_CTRL,
		PCIE_MISC_RC_BAR4_CONFIG_LO, PCIE_MISC_RC_BAR4_CONFIG_HI,
		PCIE_MISC_UBUS_BAR4_REMAP_LO, PCIE_MISC_UBUS_BAR4_REMAP_HI };
	uint32_t before[5];
	uint32_t command;

	if (!pcie_ready64 || state == NULL ||
		(read_pcie64(PCIE_MISC_PCIE_STATUS) & (PCIE_STATUS_DL_ACTIVE | PCIE_STATUS_PHY_LINK_UP)) !=
		(PCIE_STATUS_DL_ACTIVE | PCIE_STATUS_PHY_LINK_UP)) { return -1; }
	command = config_read32(RP1_BUS, RP1_DEVICE, RP1_FUNCTION, 4U);
	if ((command & (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)) !=
		(PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)) { return -1; }
	/* BAR4 maps PCI 0x10_00000000..0x1f_ffffffff onto system RAM at zero.
	 * DMA producers additionally bound buffers to our first-GiB RAM mapping.
	 */
	for (size_t i = 0; i < 5; i++) { before[i] = read_pcie64(offsets[i]); }
	write_pcie64(offsets[0], before[0] | PCIE_MISC_MISC_CTRL_SCB_ACCESS_EN);
	write_pcie64(offsets[1], PCIE_RC_BAR_SIZE_64GB);
	write_pcie64(offsets[2], RP1_SYSTEM_RAM_PCI_HI);
	write_pcie64(offsets[4], 0);
	write_pcie64(offsets[3], PCIE_MISC_UBUS_BAR_REMAP_ENABLE);
	a64_barrier64();
	state[0] = read_pcie64(offsets[1]); state[1] = read_pcie64(offsets[2]);
	state[2] = read_pcie64(offsets[3]); state[3] = read_pcie64(offsets[0]);
	if ((state[0] & 0x1fU) == PCIE_RC_BAR_SIZE_64GB &&
		(state[0] & 0xfffff000U) == 0 && state[1] == RP1_SYSTEM_RAM_PCI_HI &&
		(state[2] & 0xfffff001U) == PCIE_MISC_UBUS_BAR_REMAP_ENABLE &&
		read_pcie64(offsets[4]) == 0 && (state[3] & PCIE_MISC_MISC_CTRL_SCB_ACCESS_EN)) {
		return 0;
	}
	for (size_t i = 0; i < 5; i++) {
		aarch64_poll_fail64("pcie2", "dma-readback", "BAR4_REMAP", pcie2 + offsets[i],
			read_pcie64(offsets[i]), -5);
	}
	for (size_t i = 0; i < 5; i++) { write_pcie64(offsets[i], before[i]); }
	a64_barrier64();
	return -2;
}

int pcie64_probe_rp1(uint32_t *vendor_device, uint32_t *class_revision,
	uint32_t *bar0, uint32_t *bar1, uint32_t *command_status,
	uint32_t *root_bus_numbers, uint32_t *root_memory_base,
	uint32_t *root_memory_limit)
{
	uint32_t buses, old_buses, window, base, limit, id, first_bar, peripheral_bar, command;
	uint64_t physical;
	int status = -3;

	pcie_ready64 = 0;
	pcie2 = arch64_phys_to_virt(PCIE2_BASE);
	if ((read_pcie64(PCIE_MISC_PCIE_STATUS) & (PCIE_STATUS_DL_ACTIVE | PCIE_STATUS_PHY_LINK_UP)) !=
		(PCIE_STATUS_DL_ACTIVE | PCIE_STATUS_PHY_LINK_UP)) { return -1; }
	/* Preserve firmware link setup and the unrelated secondary-latency byte. */
	old_buses = read_pcie64(0x18U);
	buses = (old_buses & 0xff000000U) | 0x00010100U;
	write_pcie64(0x18U, buses);
	a64_barrier64();
	if (root_bus_numbers != NULL) { *root_bus_numbers = read_pcie64(0x18U); }
	if ((read_pcie64(0x18U) & 0xffffffU) != 0x10100U) { goto fail; }
	window = read_pcie64(0x20U);
	base = (window & 0xfff0U) << 16;
	limit = (window & 0xfff00000U) | 0xfffffU;
	if (root_memory_base != NULL) { *root_memory_base = base; }
	if (root_memory_limit != NULL) { *root_memory_limit = limit; }
	if ((window & 0x000f000fU) != 0 || limit < base) { goto fail; }
	id = config_read32(RP1_BUS, RP1_DEVICE, RP1_FUNCTION, 0);
	if (vendor_device != NULL) { *vendor_device = id; }
	if (id != RP1_VENDOR_DEVICE) { status = -2; goto fail; }
	first_bar = config_read32(RP1_BUS, RP1_DEVICE, RP1_FUNCTION, 0x10U);
	peripheral_bar = config_read32(RP1_BUS, RP1_DEVICE, RP1_FUNCTION, 0x14U);
	command = config_read32(RP1_BUS, RP1_DEVICE, RP1_FUNCTION, 4U);
	if (bar0 != NULL) { *bar0 = first_bar; }
	if (bar1 != NULL) { *bar1 = peripheral_bar; }
	if (command_status != NULL) { *command_status = command; }
	if (class_revision != NULL) {
		*class_revision = config_read32(RP1_BUS, RP1_DEVICE, RP1_FUNCTION, 8U);
	}
	if ((first_bar & 7U) != 0 || !(command & PCI_COMMAND_MEMORY) ||
		rp1_bar_range64(peripheral_bar, base, limit, &physical) != 0 ||
		configure_outbound_window64(base, limit) != 0) { goto fail; }
	validated_bar64 = peripheral_bar; validated_base64 = base; validated_limit64 = limit;
	pcie_ready64 = 1;
	return 0;
fail:
	write_pcie64(0x18U, old_buses);
	a64_barrier64();
	return status;
}
