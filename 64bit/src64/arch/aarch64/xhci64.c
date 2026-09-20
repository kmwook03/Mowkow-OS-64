/* RP1's two DWC3 host blocks expose xHCI capability registers at offset 0. */
#include <arch/arch64.h>
#include <stdint.h>

#define RP1_USB0_BASE 0x200000U
#define RP1_USB1_BASE 0x300000U
#define XHCI_CAPLENGTH_HCIVERSION 0x00U
#define XHCI_HCSPARAMS1 0x04U

static int probe_controller64(uintptr_t base, uint32_t *capability,
	uint32_t *hcsparams1)
{
	volatile uint8_t *registers = (volatile uint8_t *) base;
	uint32_t cap;
	uint32_t params;
	uint32_t caplength;
	uint32_t version;

	cap = *(volatile uint32_t *)
		(registers + XHCI_CAPLENGTH_HCIVERSION);
	params = *(volatile uint32_t *) (registers + XHCI_HCSPARAMS1);
	*capability = cap;
	*hcsparams1 = params;
	if (cap == 0xdeaddeadU || cap == 0xffffffffU || params == 0xdeaddeadU ||
			params == 0xffffffffU) {
		return -1;
	}
	caplength = cap & 0xffU;
	version = cap >> 16;
	if (caplength < 0x20U || (caplength & 3U) != 0 || version < 0x0100U ||
			version >= 0x0200U) {
		return -2;
	}
	return 0;
}

int xhci64_probe_rp1(uintptr_t rp1_base, uint32_t capability[2],
	uint32_t hcsparams1[2])
{
	int status;

	if (capability == NULL || hcsparams1 == NULL) {
		return -1;
	}
	status = probe_controller64(rp1_base + RP1_USB0_BASE, &capability[0],
		&hcsparams1[0]);
	if (status != 0) {
		return -2;
	}
	status = probe_controller64(rp1_base + RP1_USB1_BASE, &capability[1],
		&hcsparams1[1]);
	if (status != 0) {
		return -3;
	}
	return 0;
}
