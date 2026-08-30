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
	uint16_t bytes_per_scanline;
	uint8_t bpp;
	uint8_t framebuffer_type;
	uint32_t reserved2;
	uintptr_t vram;
};

#endif
