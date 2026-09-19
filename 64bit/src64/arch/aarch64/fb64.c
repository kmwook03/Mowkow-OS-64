/* Pi 5 mailbox framebuffer and the early M2 text console. */
#include <arch/arch64.h>
#include <bootinfo64.h>
#include <hangul64.h>
#include <stddef.h>
#include <stdint.h>
#include <utf864.h>

#define FB_WIDTH 1920U
#define FB_HEIGHT 1080U
#define FB_DEPTH 32U
#define FB_SCALE 2U
#define FB_MARGIN_X 64U
#define FB_MARGIN_Y 64U
#define CACHE_LINE_SIZE 64U
#define PROPERTY_CHANNEL 8U

#define TAG_SET_PHYSICAL_SIZE 0x00048003U
#define TAG_SET_VIRTUAL_SIZE 0x00048004U
#define TAG_SET_VIRTUAL_OFFSET 0x00048009U
#define TAG_SET_DEPTH 0x00048005U
#define TAG_SET_PIXEL_ORDER 0x00048006U
#define TAG_ALLOCATE_BUFFER 0x00040001U
#define TAG_GET_PITCH 0x00040008U

extern const uint8_t hankaku64[4096];
extern const uint8_t hangul_font64[11520];
int aarch64_mailbox_call(uint8_t channel, volatile uint32_t *message, size_t bytes);

static volatile uint32_t framebuffer_request[36] __attribute__((aligned(64)));
static struct BOOTINFO64 framebuffer_info;
static uint32_t framebuffer_pixel_order;
static uint32_t cursor_x;
static uint32_t cursor_y;
static int framebuffer_ready;

static uint32_t fb_color64(uint8_t red, uint8_t green, uint8_t blue)
{
	if (framebuffer_pixel_order == 0) {
		return ((uint32_t) blue << 16) | ((uint32_t) green << 8) | red;
	}
	return ((uint32_t) red << 16) | ((uint32_t) green << 8) | blue;
}

static void framebuffer_flush64(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
	uintptr_t first;
	uintptr_t last;
	uintptr_t address;
	uint32_t pitch;

	if (width == 0 || height == 0) {
		return;
	}
	pitch = framebuffer_info.bytes_per_scanline;
	first = framebuffer_info.vram + (uintptr_t) y * pitch + (uintptr_t) x * 4;
	last = framebuffer_info.vram + (uintptr_t) (y + height - 1) * pitch +
		(uintptr_t) (x + width) * 4;
	address = first & ~(uintptr_t) (CACHE_LINE_SIZE - 1);
	while (address < last) {
		__asm__ volatile ("dc cvac, %0" :: "r" (address) : "memory");
		address += CACHE_LINE_SIZE;
	}
	__asm__ volatile ("dsb sy" ::: "memory");
}

static void put_pixel64(uint32_t x, uint32_t y, uint32_t color)
{
	volatile uint32_t *row;

	if (x >= framebuffer_info.scrnx || y >= framebuffer_info.scrny) {
		return;
	}
	row = (volatile uint32_t *) (framebuffer_info.vram +
		(uintptr_t) y * framebuffer_info.bytes_per_scanline);
	row[x] = color;
}

static void fill64(uint32_t color)
{
	uint32_t x;
	uint32_t y;

	for (y = 0; y < framebuffer_info.scrny; y++) {
		volatile uint32_t *row = (volatile uint32_t *) (framebuffer_info.vram +
			(uintptr_t) y * framebuffer_info.bytes_per_scanline);
		for (x = 0; x < framebuffer_info.scrnx; x++) {
			row[x] = color;
		}
	}
	framebuffer_flush64(0, 0, framebuffer_info.scrnx, framebuffer_info.scrny);
}

static void draw_bitmap64(const uint8_t *bitmap, uint32_t bitmap_width,
	uint32_t x, uint32_t y)
{
	uint32_t foreground;
	uint32_t background;
	uint32_t bx;
	uint32_t by;
	uint32_t sx;
	uint32_t sy;

	foreground = fb_color64(0xff, 0xff, 0xff);
	background = fb_color64(0x00, 0x34, 0x3b);
	for (by = 0; by < 16; by++) {
		for (bx = 0; bx < bitmap_width; bx++) {
			uint32_t color = bitmap[by * bitmap_width + bx] != 0 ?
				foreground : background;
			for (sy = 0; sy < FB_SCALE; sy++) {
				for (sx = 0; sx < FB_SCALE; sx++) {
					put_pixel64(x + bx * FB_SCALE + sx,
						y + by * FB_SCALE + sy, color);
				}
			}
		}
	}
	framebuffer_flush64(x, y, bitmap_width * FB_SCALE, 16 * FB_SCALE);
}

static void draw_ascii64(unsigned char character)
{
	uint8_t bitmap[8 * 16];
	const uint8_t *glyph;
	uint32_t x;
	uint32_t y;

	glyph = hankaku64 + (uint32_t) character * 16;
	for (y = 0; y < 16; y++) {
		for (x = 0; x < 8; x++) {
			bitmap[y * 8 + x] = (uint8_t) ((glyph[y] >> (7 - x)) & 1);
		}
	}
	draw_bitmap64(bitmap, 8, cursor_x, cursor_y);
	cursor_x += 8 * FB_SCALE;
}

static void draw_hangul64(unsigned int unicode)
{
	uint8_t bitmap[16 * 16];
	uint32_t i;

	for (i = 0; i < sizeof bitmap; i++) {
		bitmap[i] = 0;
	}
	hangul64_draw_unicode(bitmap, 16, 0, 0, 1, hangul_font64, unicode);
	draw_bitmap64(bitmap, 16, cursor_x, cursor_y);
	cursor_x += 16 * FB_SCALE;
}

int arch64_fb_probe(struct BOOTINFO64 *bootinfo)
{
	volatile uint32_t *m = framebuffer_request;
	uint32_t framebuffer_address;

	m[0] = 35 * 4;
	m[1] = 0;
	m[2] = TAG_SET_PHYSICAL_SIZE; m[3] = 8; m[4] = 8;
	m[5] = FB_WIDTH; m[6] = FB_HEIGHT;
	m[7] = TAG_SET_VIRTUAL_SIZE; m[8] = 8; m[9] = 8;
	m[10] = FB_WIDTH; m[11] = FB_HEIGHT;
	m[12] = TAG_SET_VIRTUAL_OFFSET; m[13] = 8; m[14] = 8;
	m[15] = 0; m[16] = 0;
	m[17] = TAG_SET_DEPTH; m[18] = 4; m[19] = 4; m[20] = FB_DEPTH;
	m[21] = TAG_SET_PIXEL_ORDER; m[22] = 4; m[23] = 4; m[24] = 1;
	m[25] = TAG_ALLOCATE_BUFFER; m[26] = 8; m[27] = 4;
	m[28] = 4096; m[29] = 0;
	m[30] = TAG_GET_PITCH; m[31] = 4; m[32] = 0; m[33] = 0;
	m[34] = 0;

	if (aarch64_mailbox_call(PROPERTY_CHANNEL, m, m[0]) != 0) {
		return -2;
	}
	if (m[1] != 0x80000000U || m[20] != FB_DEPTH || m[28] == 0 ||
		m[29] == 0 || m[33] == 0 || m[5] == 0 || m[6] == 0 ||
		m[5] > UINT16_MAX || m[6] > UINT16_MAX || m[33] > UINT16_MAX) {
		return -3;
	}

	framebuffer_address = m[28] & 0x3fffffffU;
	framebuffer_info.cyls = 0;
	framebuffer_info.leds = 0;
	framebuffer_info.vmode = 0;
	framebuffer_info.reserve = 0;
	framebuffer_info.scrnx = (uint16_t) m[5];
	framebuffer_info.scrny = (uint16_t) m[6];
	framebuffer_info.bytes_per_scanline = (uint16_t) m[33];
	framebuffer_info.bpp = FB_DEPTH;
	framebuffer_info.framebuffer_type = 1;
	framebuffer_info.reserved2 = m[29];
	framebuffer_info.vram = (uintptr_t) framebuffer_address;
	framebuffer_pixel_order = m[24];
	*bootinfo = framebuffer_info;
	cursor_x = FB_MARGIN_X;
	cursor_y = FB_MARGIN_Y;
	framebuffer_ready = 1;
	fill64(fb_color64(0x00, 0x34, 0x3b));
	return 0;
}

void arch64_dbg_puts(const char *s)
{
	if (framebuffer_ready == 0 || s == NULL) {
		return;
	}
	while (*s != '\0') {
		unsigned int unicode;
		int length;
		uint32_t width;

		if (*s == '\n') {
			cursor_x = FB_MARGIN_X;
			cursor_y += 16 * FB_SCALE + 8;
			s++;
			continue;
		}
		unicode = utf8_to_unicode64(s, &length);
		if (length <= 0) {
			length = 1;
		}
		width = (unicode >= 0xac00 && unicode <= 0xd7a3) ?
			16 * FB_SCALE : 8 * FB_SCALE;
		if (cursor_x + width > framebuffer_info.scrnx - FB_MARGIN_X) {
			cursor_x = FB_MARGIN_X;
			cursor_y += 16 * FB_SCALE + 8;
		}
		if (cursor_y + 16 * FB_SCALE > framebuffer_info.scrny) {
			return;
		}
		if (unicode >= 0xac00 && unicode <= 0xd7a3) {
			draw_hangul64(unicode);
		} else if (length == 1) {
			draw_ascii64((unsigned char) *s);
		}
		s += length;
	}
}
