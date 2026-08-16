/* PCI configuration space through the 0xCF8/0xCFC port pair. Enough to find a
   controller and read its BARs -- no enumeration, no resource assignment. */
#include <asmfunc64.h>
#include <pci64.h>
#include <stdint.h>

#define PCI_CONFIG_ADDRESS 0x0cf8
#define PCI_CONFIG_DATA    0x0cfc

static void io_out32(uint16_t port, uint32_t value)
{
	__asm__ volatile ("outl %0, %1" : : "a" (value), "Nd" (port));
}

static uint32_t io_in32(uint16_t port)
{
	uint32_t value;

	__asm__ volatile ("inl %1, %0" : "=a" (value) : "Nd" (port));
	return value;
}

uint32_t pci64_read32(uint32_t bdf, uint8_t offset)
{
	io_out32(PCI_CONFIG_ADDRESS, 0x80000000 | (bdf << 8) | (offset & 0xfc));
	return io_in32(PCI_CONFIG_DATA);
}

void pci64_write32(uint32_t bdf, uint8_t offset, uint32_t value)
{
	io_out32(PCI_CONFIG_ADDRESS, 0x80000000 | (bdf << 8) | (offset & 0xfc));
	io_out32(PCI_CONFIG_DATA, value);
}

uint32_t pci64_find_class(uint8_t class_code, uint8_t subclass)
{
	uint32_t bdf;
	uint32_t id;
	uint32_t classes;

	/* brute force over bus 0-255, device 0-31, function 0-7: a few thousand
	   port reads once at boot, versus a bridge walk that would need more
	   code than the scan saves */
	for (bdf = 0; bdf < 0x10000; bdf++) {
		id = pci64_read32(bdf, 0x00);
		if ((id & 0xffff) == 0xffff) {
			continue;
		}
		classes = pci64_read32(bdf, 0x08);
		if ((classes >> 24) == class_code && ((classes >> 16) & 0xff) == subclass) {
			return bdf;
		}
	}
	return PCI64_NONE;
}
