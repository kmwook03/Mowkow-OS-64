/*
 * pci64.c -- 0xCF8/0xCFC 포트 쌍으로 읽는 PCI 설정 공간
 *
 * 컨트롤러를 찾고 BAR를 읽는 데까지만 한다. 장치 목록을 만들지도, 자원을
 * 나눠 주지도 않는다. 지금 필요한 것은 AHCI 컨트롤러 하나를 찾는 일뿐이다.
 */
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

	/* 버스 0-255, 장치 0-31, 기능 0-7을 그냥 다 훑는다. 부팅 때 한 번 포트를
	   몇천 번 읽을 뿐이고, 브리지를 따라가는 코드는 그렇게 아낀 시간보다
	   길다. */
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
