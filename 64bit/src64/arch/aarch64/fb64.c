/* Pi 5 mailbox framebuffer and the early M2 text console. */
#include <arch/arch64.h>
#include <bootinfo64.h>
#include <framebuffer64.h>
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

static volatile uint32_t framebuffer_request[36] __attribute__((aligned(64)));
static struct BOOTINFO64 framebuffer_info;
static struct FRAMEBUFFER64 framebuffer64;
static uint32_t framebuffer_pixel_order;
static uint32_t cursor_x;
static uint32_t cursor_y;
static int framebuffer_ready;
static const uint8_t *active_hangul_font;

static uint32_t fb_color64(uint8_t red, uint8_t green, uint8_t blue)
{
	if (framebuffer_pixel_order == 0) {
		return ((uint32_t) blue << 16) | ((uint32_t) green << 8) | red;
	}
	return ((uint32_t) red << 16) | ((uint32_t) green << 8) | blue;
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
	framebuffer_flush64(&framebuffer64, 0, 0,
		framebuffer_info.scrnx, framebuffer_info.scrny);
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

	if (x >= framebuffer_info.scrnx || y >= framebuffer_info.scrny) { return; }
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
	framebuffer_flush64(&framebuffer64, x, y,
		(int64_t) x + bitmap_width * FB_SCALE, (int64_t) y + 16 * FB_SCALE);
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
	struct SURFACE64 surface = { bitmap, sizeof(bitmap), 16, 16, 16, 0, 0, 16, 16 };

	for (i = 0; i < sizeof bitmap; i++) {
		bitmap[i] = 0;
	}
	hangul64_draw_unicode(&surface, 0, 0, 1, active_hangul_font,
		HANGUL64_FONT_SIZE, unicode);
	draw_bitmap64(bitmap, 16, cursor_x, cursor_y);
	cursor_x += 16 * FB_SCALE;
}

/* Validate the fixed response layout before narrowing firmware values or
 * publishing a writable mapping. The GUI backend uses RGB pixel order.
 */
static int framebuffer_response64(const volatile uint32_t *m,
	struct BOOTINFO64 *info)
{
	static const uint32_t tags[][3] = {
		{ 2, TAG_SET_PHYSICAL_SIZE, 8 },
		{ 7, TAG_SET_VIRTUAL_SIZE, 8 },
		{ 12, TAG_SET_VIRTUAL_OFFSET, 8 },
		{ 17, TAG_SET_DEPTH, 4 },
		{ 21, TAG_SET_PIXEL_ORDER, 4 },
		{ 25, TAG_ALLOCATE_BUFFER, 8 },
		{ 30, TAG_GET_PITCH, 4 }
	};
	struct BOOTINFO64 candidate = { 0 };
	struct FRAMEBUFFER64 fb;
	uintptr_t physical;

	if (m == NULL || info == NULL || m[0] != 35U * 4U ||
		m[1] != 0x80000000U || m[34] != 0) { return -22; }
	for (size_t i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
		uint32_t index = tags[i][0];

		if (m[index] != tags[i][1] || m[index + 1] != tags[i][2] ||
			m[index + 2] != (0x80000000U | tags[i][2])) { return -22; }
	}
	if (m[20] != FB_DEPTH || m[24] != 1U || m[5] == 0 || m[6] == 0 ||
		m[5] > UINT16_MAX || m[6] > UINT16_MAX || m[33] > UINT16_MAX ||
		m[10] < m[5] || m[11] < m[6] || m[15] != 0 || m[16] != 0 ||
		m[29] == 0) { return -22; }
	physical = m[28] & 0x3fffffffU;
	/* TTBR1 maps the first GiB of RAM. Reject alias-only or out-of-map buffers. */
	if (physical == 0 || (physical & 4095U) != 0 ||
		m[29] > (1U << 30) - physical) { return -22; }
	candidate.scrnx = (uint16_t) m[5];
	candidate.scrny = (uint16_t) m[6];
	candidate.bytes_per_scanline = (uint16_t) m[33];
	candidate.bpp = FB_DEPTH;
	candidate.framebuffer_type = 1;
	candidate.reserved2 = m[29];
	candidate.vram = arch64_phys_to_virt(physical);
	if (framebuffer_boot64(&fb, &candidate) != 0) { return -22; }
	*info = candidate;
	return 0;
}

int arch64_fb_probe(struct BOOTINFO64 *bootinfo)
{
	volatile uint32_t *m = framebuffer_request;
	struct BOOTINFO64 candidate;

	if (bootinfo == NULL) { return -22; }

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
	if (framebuffer_response64(m, &candidate) != 0 ||
		framebuffer_boot64(&framebuffer64, &candidate) != 0) { return -3; }
	framebuffer_info = candidate;
	framebuffer_pixel_order = m[24];
	active_hangul_font = hangul_font64;
	*bootinfo = framebuffer_info;
	cursor_x = FB_MARGIN_X;
	cursor_y = FB_MARGIN_Y;
	framebuffer_ready = 1;
	fill64(fb_color64(0x00, 0x34, 0x3b));
	return 0;
}

void arch64_fb_set_hangul_font(const uint8_t *font)
{
	if (font != NULL) {
		active_hangul_font = font;
	}
}

void arch64_dbg_puts(const char *s)
{
	if (framebuffer_ready == 0 || s == NULL ||
		framebuffer_info.scrnx <= 2U * FB_MARGIN_X + 16U * FB_SCALE) {
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
		} else if (unicode == UTF64_REPLACEMENT) {
			/* U+FFFD is unavailable in the bitmap font. */
			draw_ascii64('?');
		} else if (length == 1) {
			draw_ascii64((unsigned char) *s);
		}
		s += length;
	}
}
