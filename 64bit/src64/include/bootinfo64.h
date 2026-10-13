#ifndef MOWKOW64_BOOTINFO64_H
#define MOWKOW64_BOOTINFO64_H

#include <stddef.h>
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

_Static_assert(offsetof(struct BOOTINFO64, scrnx) == 4,
	"BOOTINFO64.scrnx offset must match loader64.asm");
_Static_assert(offsetof(struct BOOTINFO64, bytes_per_scanline) == 8,
	"BOOTINFO64 stride offset must match loader64.asm");
_Static_assert(offsetof(struct BOOTINFO64, reserved2) == 12,
	"BOOTINFO64.reserved2 offset must match loader64.asm");
_Static_assert(offsetof(struct BOOTINFO64, vram) == 16,
	"BOOTINFO64.vram offset must match loader64.asm");
_Static_assert(sizeof(struct BOOTINFO64) == 24,
	"BOOTINFO64 size must match loader64.asm");

#endif
