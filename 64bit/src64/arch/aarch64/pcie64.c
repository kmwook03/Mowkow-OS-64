/*
 * pcie64.c -- BCM2712 PCIe x4 / RP1 early probe
 *
 * config.txt의 pciex4_reset=0으로 firmware가 세운 링크를 보존한다. firmware가
 * PCI bus 번호까지 배정한다는 보장은 없으므로 root bridge를 0 -> 1로 설정한
 * 뒤 RP1 config-space를 읽는다. 링크가 내려가 있을 때 downstream window를
 * 읽으면 CPU abort가 날 수 있으므로 반드시 status를 먼저 검사한다.
 */
#include <arch/arch64.h>
#include <stdint.h>

#define PCIE2_BASE 0x1000120000ULL
#define PCIE2_OUTBOUND_BASE 0x1f00000000ULL

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

static volatile uint8_t *pcie2;

static uint32_t config_index64(uint32_t bus, uint32_t device,
	uint32_t function)
{
	return (bus << 20) | (device << 15) | (function << 12);
}

static uint32_t config_read32(uint32_t bus, uint32_t device,
	uint32_t function, uint32_t offset)
{
	*(volatile uint32_t *) (pcie2 + PCIE_EXT_CFG_INDEX) =
		config_index64(bus, device, function);
	__asm__ volatile ("dsb sy" ::: "memory");
	return *(volatile uint32_t *) (pcie2 + PCIE_EXT_CFG_DATA + offset);
}

static int configure_outbound_window64(uint32_t pci_base, uint32_t pci_limit)
{
	uint64_t cpu_base_mb;
	uint64_t cpu_limit_mb;
	uint32_t value;
	volatile uint16_t *root_command;

	if (pci_limit < pci_base || (pci_base & 0xfffffU) != 0 ||
			(pci_limit & 0xfffffU) != 0xfffffU) {
		return -1;
	}
	cpu_base_mb = PCIE2_OUTBOUND_BASE >> 20;
	cpu_limit_mb = cpu_base_mb + ((uint64_t) pci_limit - pci_base) / 0x100000ULL;

	/* BCM2712 CPU->PCIe window 0: translate the CPU outbound aperture to
	   the PCI range already assigned by firmware's root bridge setup. */
	*(volatile uint32_t *) (pcie2 + PCIE_MEM_WIN0_LO) = pci_base;
	*(volatile uint32_t *) (pcie2 + PCIE_MEM_WIN0_HI) = 0;
	value = *(volatile uint32_t *) (pcie2 + PCIE_MEM_WIN0_BASE_LIMIT);
	value &= ~(0xfff00000U | 0x0000fff0U);
	value |= ((uint32_t) cpu_base_mb & 0xfffU) << 4;
	value |= ((uint32_t) cpu_limit_mb & 0xfffU) << 20;
	*(volatile uint32_t *) (pcie2 + PCIE_MEM_WIN0_BASE_LIMIT) = value;
	value = *(volatile uint32_t *) (pcie2 + PCIE_MEM_WIN0_BASE_HI);
	value = (value & ~0xffU) | ((uint32_t) (cpu_base_mb >> 12) & 0xffU);
	*(volatile uint32_t *) (pcie2 + PCIE_MEM_WIN0_BASE_HI) = value;
	value = *(volatile uint32_t *) (pcie2 + PCIE_MEM_WIN0_LIMIT_HI);
	value = (value & ~0xffU) | ((uint32_t) (cpu_limit_mb >> 12) & 0xffU);
	*(volatile uint32_t *) (pcie2 + PCIE_MEM_WIN0_LIMIT_HI) = value;

	root_command = (volatile uint16_t *) (pcie2 + 0x04);
	*root_command |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
	__asm__ volatile ("dsb sy" ::: "memory");
	return 0;
}

int pcie64_enable_rp1_dma(uint32_t state[4])
{
	uint32_t misc;

	if (state == NULL) {
		return -1;
	}
	/* RP1 bus masters address system RAM at PCIe 0x10_00000000 + physical.
	   BCM2712 RC BAR4 translates that 64 GiB PCI aperture to CPU address 0. */
	misc = *(volatile uint32_t *) (pcie2 + PCIE_MISC_MISC_CTRL);
	*(volatile uint32_t *) (pcie2 + PCIE_MISC_MISC_CTRL) =
		misc | PCIE_MISC_MISC_CTRL_SCB_ACCESS_EN;
	*(volatile uint32_t *) (pcie2 + PCIE_MISC_RC_BAR4_CONFIG_LO) =
		PCIE_RC_BAR_SIZE_64GB;
	*(volatile uint32_t *) (pcie2 + PCIE_MISC_RC_BAR4_CONFIG_HI) =
		RP1_SYSTEM_RAM_PCI_HI;
	*(volatile uint32_t *) (pcie2 + PCIE_MISC_UBUS_BAR4_REMAP_HI) = 0;
	*(volatile uint32_t *) (pcie2 + PCIE_MISC_UBUS_BAR4_REMAP_LO) =
		PCIE_MISC_UBUS_BAR_REMAP_ENABLE;
	__asm__ volatile ("dsb sy" ::: "memory");

	state[0] = *(volatile uint32_t *)
		(pcie2 + PCIE_MISC_RC_BAR4_CONFIG_LO);
	state[1] = *(volatile uint32_t *)
		(pcie2 + PCIE_MISC_RC_BAR4_CONFIG_HI);
	state[2] = *(volatile uint32_t *)
		(pcie2 + PCIE_MISC_UBUS_BAR4_REMAP_LO);
	state[3] = *(volatile uint32_t *) (pcie2 + PCIE_MISC_MISC_CTRL);
	if ((state[0] & 0x1fU) != PCIE_RC_BAR_SIZE_64GB ||
			state[1] != RP1_SYSTEM_RAM_PCI_HI ||
			(state[2] & PCIE_MISC_UBUS_BAR_REMAP_ENABLE) == 0 ||
			(state[3] & PCIE_MISC_MISC_CTRL_SCB_ACCESS_EN) == 0) {
		return -2;
	}
	return 0;
}

int pcie64_probe_rp1(uint32_t *vendor_device, uint32_t *class_revision,
	uint32_t *bar0, uint32_t *bar1, uint32_t *command_status,
	uint32_t *root_bus_numbers, uint32_t *root_memory_base,
	uint32_t *root_memory_limit)
{
	uint32_t buses;
	uint32_t memory_base_limit;
	uint32_t memory_base;
	uint32_t memory_limit;
	uint32_t status;
	uint32_t id;

	pcie2 = (volatile uint8_t *) arch64_phys_to_virt(PCIE2_BASE);
	status = *(volatile uint32_t *) (pcie2 + PCIE_MISC_PCIE_STATUS);
	if ((status & (PCIE_STATUS_DL_ACTIVE | PCIE_STATUS_PHY_LINK_UP)) !=
			(PCIE_STATUS_DL_ACTIVE | PCIE_STATUS_PHY_LINK_UP)) {
		return -1;
	}
	/* Type-1 header: primary=0, secondary=1, subordinate=1. Preserve the
	   secondary-latency byte because it is unrelated to enumeration. */
	buses = *(volatile uint32_t *) (pcie2 + 0x18);
	if (((buses >> 8) & 0xffffU) != 0x0101U || (buses & 0xffU) != 0) {
		buses = (buses & 0xff000000U) | 0x00010100U;
		*(volatile uint32_t *) (pcie2 + 0x18) = buses;
		__asm__ volatile ("dsb sy" ::: "memory");
		buses = *(volatile uint32_t *) (pcie2 + 0x18);
	}
	if (root_bus_numbers != NULL) {
		*root_bus_numbers = buses;
	}
	/* Type-1 memory base/limit describes the PCI address accepted by the
	   firmware-configured downstream window. Bits 15:4 become address 31:20. */
	memory_base_limit = *(volatile uint32_t *) (pcie2 + 0x20);
	memory_base = (memory_base_limit & 0x0000fff0U) << 16;
	memory_limit = (memory_base_limit & 0xfff00000U) | 0x000fffffU;
	if (root_memory_base != NULL) {
		*root_memory_base = memory_base;
	}
	if (root_memory_limit != NULL) {
		*root_memory_limit = memory_limit;
	}
	id = config_read32(RP1_BUS, RP1_DEVICE, RP1_FUNCTION, 0x00);
	if (vendor_device != NULL) {
		*vendor_device = id;
	}
	if (id == 0xffffffffU || id == 0 || id != RP1_VENDOR_DEVICE) {
		return -2;
	}
	if (class_revision != NULL) {
		*class_revision = config_read32(RP1_BUS, RP1_DEVICE,
			RP1_FUNCTION, 0x08);
	}
	if (command_status != NULL) {
		*command_status = config_read32(RP1_BUS, RP1_DEVICE,
			RP1_FUNCTION, 0x04);
	}
	if (bar0 != NULL) {
		*bar0 = config_read32(RP1_BUS, RP1_DEVICE, RP1_FUNCTION, 0x10);
	}
	if (bar1 != NULL) {
		*bar1 = config_read32(RP1_BUS, RP1_DEVICE, RP1_FUNCTION, 0x14);
	}
	if (configure_outbound_window64(memory_base, memory_limit) != 0) {
		return -3;
	}
	return 0;
}
