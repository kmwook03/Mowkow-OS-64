/*
 * 윈도우 그리기 -- src/kernel/window.c의 64비트 이식.
 *
 * putstr64가 UTF-8을 해석해 한글은 hangul64_draw_unicode로,
 * ASCII는 hankaku 폰트로 그린다.
 */

#include <hangul64.h>
#include <stddef.h>
#include <stdint.h>
#include <utf864.h>
#include <window64.h>

#define FONT64_W 8
#define FONT64_H 16
#define HANGUL64_W 16

extern const uint8_t hankaku64[4096];
static const uint8_t *window_hangul_font;

void window64_set_hangul_font(const uint8_t *font)
{
	window_hangul_font = font;
}

void boxfill64(const struct SURFACE64 *surface, uint8_t color,
	int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
	draw_rect64(surface, color, x0, y0, x1, y1);
}

static void draw_ascii64(const struct SURFACE64 *surface, int32_t x, int32_t y,
	uint8_t color, char c)
{
	const uint8_t *font;

	font = hankaku64 + (uint8_t) c * 16;
	draw_glyph64(surface, x, y, color, font, FONT64_H, FONT64_W);
}

void putstr64(const struct SURFACE64 *surface, int32_t x, int32_t y, uint8_t color,
	const char *s)
{
	const uint8_t *hangul_font = window_hangul_font;
	unsigned int unicode;
	int len;
	int width;

	if (!surface64_valid(surface) || s == NULL) {
		return;
	}
	while (*s != '\0') {
		width = 0;
		unicode = utf8_to_unicode64(s, &len);
		if (unicode == UTF64_REPLACEMENT) {
			/* U+FFFD is unavailable in the bitmap font. */
			draw_ascii64(surface, x, y, color, '?');
			width = FONT64_W;
		} else if (len == 1) {
			draw_ascii64(surface, x, y, color, *s);
			width = FONT64_W;
		} else if (hangul_font != NULL &&
				((unicode >= 0xac00 && unicode <= 0xd7a3) ||
				 (unicode >= 0x3131 && unicode <= 0x3163))) {
			hangul64_draw_unicode(surface, x, y, color, hangul_font,
				HANGUL64_FONT_SIZE, unicode);
			width = HANGUL64_W;
		}
		if (x > INT32_MAX - width) {
			return;
		}
		x += width;
		s += len;
	}
}

void make_window64(const struct SURFACE64 *surface, const char *title, int act)
{
	int32_t xsize, ysize;

	if (!surface64_valid(surface)) {
		return;
	}
	xsize = surface->width;
	ysize = surface->height;
	boxfill64(surface, COL64_C6C6C6, 0,         0,         xsize - 1, 0        );
	boxfill64(surface, COL64_FFFFFF, 1,         1,         xsize - 2, 1        );
	boxfill64(surface, COL64_C6C6C6, 0,         0,         0,         ysize - 1);
	boxfill64(surface, COL64_FFFFFF, 1,         1,         1,         ysize - 2);
	boxfill64(surface, COL64_848484, xsize - 2, 1,         xsize - 2, ysize - 2);
	boxfill64(surface, COL64_000000, xsize - 1, 0,         xsize - 1, ysize - 1);
	boxfill64(surface, COL64_C6C6C6, 2,         2,         xsize - 3, ysize - 3);
	boxfill64(surface, COL64_848484, 1,         ysize - 2, xsize - 2, ysize - 2);
	boxfill64(surface, COL64_000000, 0,         ysize - 1, xsize - 1, ysize - 1);
	make_wtitle64(surface, title, act);
}

void make_wtitle64(const struct SURFACE64 *surface, const char *title, int act)
{
	static const char closebtn[14][17] = {
		"OOOOOOOOOOOOOOO@",
		"OQQQQQQQQQQQQQ$@",
		"OQQQQQQQQQQQQQ$@",
		"OQQQ@@QQQQ@@QQ$@",
		"OQQQQ@@QQ@@QQQ$@",
		"OQQQQQ@@@@QQQQ$@",
		"OQQQQQQ@@QQQQQ$@",
		"OQQQQQ@@@@QQQQ$@",
		"OQQQQ@@QQ@@QQQ$@",
		"OQQQ@@QQQQ@@QQ$@",
		"OQQQQQQQQQQQQQ$@",
		"OQQQQQQQQQQQQQ$@",
		"O$$$$$$$$$$$$$$@",
		"@@@@@@@@@@@@@@@@"
	};
	int32_t x, y;
	char c;
	uint8_t tc, tbc;
	struct SURFACE64 text;
	int32_t xsize;

	if (!surface64_valid(surface)) {
		return;
	}
	xsize = surface->width;
	text = *surface;
	if (text.x0 < 3) { text.x0 = 3; }
	if (text.y0 < 3) { text.y0 = 3; }
	if (text.x1 > xsize - 22) { text.x1 = xsize - 22; }
	if (text.y1 > WINDOW64_TITLE_H) { text.y1 = WINDOW64_TITLE_H; }

	if (act != 0) {
		tc = COL64_FFFFFF;
		tbc = COL64_000084;
	} else {
		tc = COL64_C6C6C6;
		tbc = COL64_848484;
	}

	boxfill64(surface, tbc, 3, 3, xsize - 4, WINDOW64_TITLE_H - 1);
	putstr64(&text, 24, 4, tc, title);

	for (y = 0; y < 14; y++) {
		for (x = 0; x < 16; x++) {
			c = closebtn[y][x];
			if (c == '@') {
				c = COL64_000000;
			} else if (c == '$') {
				c = COL64_848484;
			} else if (c == 'Q') {
				c = COL64_C6C6C6;
			} else {
				c = COL64_FFFFFF;
			}
			boxfill64(surface, (uint8_t) c, xsize - 21 + x, 5 + y,
				xsize - 21 + x, 5 + y);
		}
	}
}

void change_wtitle64(struct SHEET64 *sht, int act)
{
	int32_t x, y, xsize;
	struct SURFACE64 surface = sheet64_surface(sht);
	uint8_t c, tc_new, tbc_new, tc_old, tbc_old;
	uint8_t *buf = surface.pixels;

	if (!surface64_valid(&surface)) {
		return;
	}
	xsize = surface.width;

	if (act != 0) {
		tc_new = COL64_FFFFFF;
		tbc_new = COL64_000084;
		tc_old = COL64_C6C6C6;
		tbc_old = COL64_848484;
	} else {
		tc_new = COL64_C6C6C6;
		tbc_new = COL64_848484;
		tc_old = COL64_FFFFFF;
		tbc_old = COL64_000084;
	}
	for (y = 3; y < WINDOW64_TITLE_H && y < surface.height; y++) {
		for (x = 3; x <= xsize - 4; x++) {
			c = buf[(size_t) y * surface.stride + (size_t) x];
			if (c == tc_old && x <= xsize - 22) {
				c = tc_new;
			} else if (c == tbc_old) {
				c = tbc_new;
			}
			buf[(size_t) y * surface.stride + (size_t) x] = c;
		}
	}
	sheet64_refresh(sht, 3, 3, xsize, WINDOW64_TITLE_H);
}
