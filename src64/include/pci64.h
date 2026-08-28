#ifndef MOWKOW64_PCI64_H
#define MOWKOW64_PCI64_H

#include <stdint.h>

/* 버스:장치:기능을 (bus << 8) | (device << 3) | function 으로 묶은 값 */
#define PCI64_NONE 0xffffffff

#define PCI64_REG_COMMAND 0x04
#define PCI64_REG_BAR5 0x24
#define PCI64_COMMAND_IO 0x0001
#define PCI64_COMMAND_MEMORY 0x0002
#define PCI64_COMMAND_MASTER 0x0004

uint32_t pci64_read32(uint32_t bdf, uint8_t offset);
void pci64_write32(uint32_t bdf, uint8_t offset, uint32_t value);
/* 클래스/서브클래스가 맞는 첫 장치. 없으면 PCI64_NONE. */
uint32_t pci64_find_class(uint8_t class_code, uint8_t subclass);

#endif
