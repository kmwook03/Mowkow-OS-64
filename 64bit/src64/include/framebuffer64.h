#ifndef MOWKOW64_FRAMEBUFFER64_H
#define MOWKOW64_FRAMEBUFFER64_H

#include <stddef.h>
#include <stdint.h>

struct BOOTINFO64;
struct FRAMEBUFFER_OPS64;

/* Immutable after init; size is the accessible capacity, pitch includes padding. */
struct FRAMEBUFFER64 {
	uint8_t *pixels;
	size_t size;
	uint32_t pitch;
	int32_t width, height;
	uint8_t bpp;
	const struct FRAMEBUFFER_OPS64 *ops;
};

/* Invalid descriptors return -22 and leave the destination unchanged. */
int framebuffer_init64(struct FRAMEBUFFER64 *fb, uint8_t *pixels, size_t size,
	int32_t width, int32_t height, uint32_t pitch, uint8_t bpp);
int framebuffer_boot64(struct FRAMEBUFFER64 *fb, const struct BOOTINFO64 *info);
/* Source and owner map each supply count bytes. Coordinates must be in bounds. */
int framebuffer_span64(const struct FRAMEBUFFER64 *fb, int32_t x, int32_t y,
	const uint8_t *source, const uint8_t *owners, uint8_t owner, int32_t count);
/* Exclusive rectangle endpoints; clips before row address calculation.
 * Task/IRQ safe, never sleeps. Platform may clean enclosing cache lines.
 */
void framebuffer_flush64(const struct FRAMEBUFFER64 *fb,
	int64_t x0, int64_t y0, int64_t x1, int64_t y1);

#endif
