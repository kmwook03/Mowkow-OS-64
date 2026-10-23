#include <arch/platform64.h>
#include <bootinfo64.h>
#include <framebuffer64.h>
#include <graphic64.h>

struct FRAMEBUFFER_OPS64 {
	void (*span)(uint8_t *destination, const uint8_t *source,
		const uint8_t *owners, uint8_t owner, int32_t count);
};

static void indexed_span64(uint8_t *destination, const uint8_t *source,
	const uint8_t *owners, uint8_t owner, int32_t count)
{
	for (int32_t x = 0; x < count; x++) {
		if (owners[x] == owner) { destination[x] = source[x]; }
	}
}

static void rgb_span64(uint8_t *destination, const uint8_t *source,
	const uint8_t *owners, uint8_t owner, int32_t count)
{
	uint32_t *row = (uint32_t *) (void *) destination;

	for (int32_t x = 0; x < count; x++) {
		if (owners[x] == owner) { row[x] = graphic64_rgb32(source[x]); }
	}
}

static const struct FRAMEBUFFER_OPS64 indexed_ops64 = { indexed_span64 };
static const struct FRAMEBUFFER_OPS64 rgb_ops64 = { rgb_span64 };

static void bgr_span64(uint8_t *destination, const uint8_t *source,
	const uint8_t *owners, uint8_t owner, int32_t count)
{
	uint32_t *row = (uint32_t *) (void *) destination;

	for (int32_t x = 0; x < count; x++) {
		if (owners[x] == owner) {
			uint32_t color = graphic64_rgb32(source[x]);

			row[x] = ((color & 0xffU) << 16) | (color & 0xff00U) |
				((color >> 16) & 0xffU);
		}
	}
}

static const struct FRAMEBUFFER_OPS64 bgr_ops64 = { bgr_span64 };

int framebuffer_init64(struct FRAMEBUFFER64 *fb, uint8_t *pixels, size_t size,
	int32_t width, int32_t height, uint32_t pitch, uint8_t bpp)
{
	size_t row_bytes, required;
	uint32_t bytes;

	if (fb == NULL || pixels == NULL || width <= 0 || height <= 0 ||
		(bpp != 8U && bpp != 32U)) { return -22; }
	bytes = bpp / 8U;
	if ((size_t) width > SIZE_MAX / bytes) { return -22; }
	row_bytes = (size_t) width * bytes;
	if (pitch < row_bytes ||
		(bpp == 32U && (((uintptr_t) pixels | pitch) & 3U) != 0) ||
		(size_t) (height - 1) > (SIZE_MAX - row_bytes) / pitch) { return -22; }
	required = (size_t) (height - 1) * pitch + row_bytes;
	if (required > size || size > UINTPTR_MAX - (uintptr_t) pixels) { return -22; }
	*fb = (struct FRAMEBUFFER64) {
		pixels, size, pitch, width, height, bpp,
		bpp == 8U ? &indexed_ops64 : &rgb_ops64
	};
	return 0;
}

int framebuffer_boot64(struct FRAMEBUFFER64 *fb, const struct BOOTINFO64 *info)
{
	size_t size;
	int status;

	if (info == NULL || info->framebuffer_type == 0 ||
		info->bytes_per_scanline == 0 || info->scrny == 0) { return -22; }
	/* x86 VBE supplies geometry but no allocation capacity; Pi supplies both. */
	if ((size_t) info->scrny > SIZE_MAX / info->bytes_per_scanline) { return -22; }
	size = info->reserved2 != 0 ? info->reserved2 :
		(size_t) info->bytes_per_scanline * info->scrny;
	if ((info->framebuffer_type & BOOTINFO_BGR64) != 0 && info->bpp != 32U) {
		return -22;
	}
	status = framebuffer_init64(fb, (uint8_t *) info->vram, size,
		info->scrnx, info->scrny, info->bytes_per_scanline, info->bpp);
	if (status == 0 && (info->framebuffer_type & BOOTINFO_BGR64) != 0) {
		fb->ops = &bgr_ops64;
	}
	return status;
}

int framebuffer_span64(const struct FRAMEBUFFER64 *fb, int32_t x, int32_t y,
	const uint8_t *source, const uint8_t *owners, uint8_t owner, int32_t count)
{
	uint8_t *destination;

	if (fb == NULL || fb->ops == NULL || source == NULL || owners == NULL ||
		x < 0 || y < 0 || y >= fb->height || count <= 0 ||
		x >= fb->width || count > fb->width - x) { return -22; }
	destination = fb->pixels + (size_t) y * fb->pitch + (size_t) x * (fb->bpp / 8U);
	fb->ops->span(destination, source, owners, owner, count);
	return 0;
}

void framebuffer_flush64(const struct FRAMEBUFFER64 *fb,
	int64_t x0, int64_t y0, int64_t x1, int64_t y1)
{
	size_t bytes, offset;

	if (fb == NULL || fb->ops == NULL) { return; }
	if (x0 < 0) { x0 = 0; }
	if (y0 < 0) { y0 = 0; }
	if (x1 > fb->width) { x1 = fb->width; }
	if (y1 > fb->height) { y1 = fb->height; }
	if (x0 >= x1 || y0 >= y1) { return; }
	bytes = (size_t) (x1 - x0) * (fb->bpp / 8U);
	for (int64_t y = y0; y < y1; y++) {
		offset = (size_t) y * fb->pitch + (size_t) x0 * (fb->bpp / 8U);
		platform_framebuffer_flush64((uintptr_t) fb->pixels + offset, bytes);
	}
	platform_framebuffer_sync64();
}
