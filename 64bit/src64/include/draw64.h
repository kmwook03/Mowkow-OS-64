#ifndef MOWKOW64_DRAW64_H
#define MOWKOW64_DRAW64_H

#include <stddef.h>
#include <stdint.h>

/* 8bpp destination; clip endpoints are exclusive, in surface coordinates.
 * size counts bytes accessible from pixels, including any row padding.
 */
struct SURFACE64 {
	uint8_t *pixels;
	size_t size;
	uint32_t stride;
	int32_t width, height;
	int32_t x0, y0, x1, y1;
};

int surface64_valid(const struct SURFACE64 *surface);
/* Packed MSB-first rows, one byte for 8 pixels or two for 16 pixels.
 * Invalid descriptors are a no-op. Coordinates may be negative.
 */
void draw_glyph64(const struct SURFACE64 *surface, int32_t x, int32_t y,
	uint8_t color, const uint8_t *glyph, size_t size, uint32_t width);

#endif
