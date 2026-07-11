#ifndef MOWKOW64_BOOTINFO64_H
#define MOWKOW64_BOOTINFO64_H

#include <stdint.h>

struct BOOTINFO64 {
	uint8_t cyls;
	uint8_t leds;
	uint8_t vmode;
	uint8_t reserve;
	uint16_t scrnx;
	uint16_t scrny;
	uintptr_t vram;
};

#endif
