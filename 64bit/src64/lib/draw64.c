#include <draw64.h>

int surface64_valid(const struct SURFACE64 *surface)
{
	size_t rows;
	size_t required;

	if (surface == NULL || surface->pixels == NULL ||
		surface->width <= 0 || surface->height <= 0 ||
		surface->stride < (uint32_t) surface->width ||
		surface->x0 > surface->x1 || surface->y0 > surface->y1) {
		return 0;
	}
	/* The final row need not include padding. Check before multiplying. */
	rows = (size_t) surface->height - 1;
	if (rows > (SIZE_MAX - (size_t) surface->width) / surface->stride) {
		return 0;
	}
	required = rows * surface->stride + (size_t) surface->width;
	return required <= surface->size &&
		required <= UINTPTR_MAX - (uintptr_t) surface->pixels;
}

void draw_rect64(const struct SURFACE64 *surface, uint8_t color,
	int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
	int64_t left = x0, top = y0;
	int64_t right = (int64_t) x1 + 1, bottom = (int64_t) y1 + 1;

	if (!surface64_valid(surface) || x0 > x1 || y0 > y1) {
		return;
	}
	if (left < 0) { left = 0; }
	if (top < 0) { top = 0; }
	if (left < surface->x0) { left = surface->x0; }
	if (top < surface->y0) { top = surface->y0; }
	if (right > surface->width) { right = surface->width; }
	if (bottom > surface->height) { bottom = surface->height; }
	if (right > surface->x1) { right = surface->x1; }
	if (bottom > surface->y1) { bottom = surface->y1; }
	if (left >= right || top >= bottom) { return; }
	for (int64_t y = top; y < bottom; y++) {
		for (int64_t x = left; x < right; x++) {
			surface->pixels[(size_t) y * surface->stride + (size_t) x] = color;
		}
	}
}

void draw_glyph64(const struct SURFACE64 *surface, int32_t x, int32_t y,
	uint8_t color, const uint8_t *glyph, size_t size, uint32_t width)
{
	uint32_t row, bit, row_bytes;
	int64_t px, py;

	if (!surface64_valid(surface) || glyph == NULL || (width != 8 && width != 16)) {
		return;
	}
	row_bytes = width / 8;
	if (size < 16 * row_bytes) {
		return;
	}
	for (row = 0; row < 16; row++) {
		py = (int64_t) y + row;
		if (py < 0 || py >= surface->height || py < surface->y0 || py >= surface->y1) {
			continue;
		}
		for (bit = 0; bit < width; bit++) {
			px = (int64_t) x + bit;
			if (px < 0 || px >= surface->width || px < surface->x0 || px >= surface->x1) {
				continue;
			}
			if (glyph[row * row_bytes + bit / 8] & (0x80U >> (bit % 8))) {
				surface->pixels[(size_t) py * surface->stride + (size_t) px] = color;
			}
		}
	}
}
