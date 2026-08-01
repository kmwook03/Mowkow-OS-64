/*
 * 윈도우 그리기 -- src/kernel/window.c의 64비트 이식.
 *
 * 원본 window.c:72의 TODO("제목에 한글도 적을 수 있도록 해야함")를 여기서
 * 해결한다. putstr64가 UTF-8을 해석해 한글은 hangul64_draw_unicode로,
 * ASCII는 hankaku 폰트로 그린다.
 */

#include <console64.h>
#include <hangul64.h>
#include <stddef.h>
#include <stdint.h>
#include <utf864.h>
#include <window64.h>

#define FONT64_W 8
#define FONT64_H 16
#define HANGUL64_W 16

extern const uint8_t hankaku64[4096];

void boxfill64(uint8_t *buf, int32_t xsize, uint8_t color,
	int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
	int32_t x, y;

	for (y = y0; y <= y1; y++) {
		for (x = x0; x <= x1; x++) {
			buf[y * xsize + x] = color;
		}
	}
}

static void draw_ascii64(uint8_t *buf, uint32_t stride, int32_t x, int32_t y,
	uint8_t color, char c)
{
	const uint8_t *font;
	uint8_t d;
	int32_t row;
	int32_t bit;
	uint8_t *p;

	font = hankaku64 + (uint8_t) c * 16;
	for (row = 0; row < FONT64_H; row++) {
		d = font[row];
		p = buf + (uint32_t) (y + row) * stride + x;
		for (bit = 0; bit < FONT64_W; bit++) {
			if ((d & (0x80 >> bit)) != 0) {
				p[bit] = color;
			}
		}
	}
}

void putstr64(uint8_t *buf, uint32_t stride, int32_t x, int32_t y, uint8_t color,
	const char *s)
{
	const uint8_t *hangul_font = console64_hangul_font();
	unsigned int unicode;
	int len;

	while (*s != '\0') {
		len = utf8_byte_len64((unsigned char) *s);
		if (len <= 1) {
			draw_ascii64(buf, stride, x, y, color, *s);
			x += FONT64_W;
			s++;
			continue;
		}
		unicode = utf8_to_unicode64(s, &len);
		if (hangul_font != NULL &&
				((unicode >= 0xac00 && unicode <= 0xd7a3) ||
				 (unicode >= 0x3131 && unicode <= 0x3163))) {
			hangul64_draw_unicode(buf, stride, (uint16_t) x, (uint16_t) y,
				color, hangul_font, unicode);
			x += HANGUL64_W;
		}
		s += len;
	}
}

void make_window64(uint8_t *buf, int32_t xsize, int32_t ysize, const char *title, int act)
{
	boxfill64(buf, xsize, COL64_C6C6C6, 0,         0,         xsize - 1, 0        );
	boxfill64(buf, xsize, COL64_FFFFFF, 1,         1,         xsize - 2, 1        );
	boxfill64(buf, xsize, COL64_C6C6C6, 0,         0,         0,         ysize - 1);
	boxfill64(buf, xsize, COL64_FFFFFF, 1,         1,         1,         ysize - 2);
	boxfill64(buf, xsize, COL64_848484, xsize - 2, 1,         xsize - 2, ysize - 2);
	boxfill64(buf, xsize, COL64_000000, xsize - 1, 0,         xsize - 1, ysize - 1);
	boxfill64(buf, xsize, COL64_C6C6C6, 2,         2,         xsize - 3, ysize - 3);
	boxfill64(buf, xsize, COL64_848484, 1,         ysize - 2, xsize - 2, ysize - 2);
	boxfill64(buf, xsize, COL64_000000, 0,         ysize - 1, xsize - 1, ysize - 1);
	make_wtitle64(buf, xsize, title, act);
}

void make_wtitle64(uint8_t *buf, int32_t xsize, const char *title, int act)
{
	static const char closebtn[14][16] = {
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

	if (act != 0) {
		tc = COL64_FFFFFF;
		tbc = COL64_000084;
	} else {
		tc = COL64_C6C6C6;
		tbc = COL64_848484;
	}

	boxfill64(buf, xsize, tbc, 3, 3, xsize - 4, WINDOW64_TITLE_H - 1);
	putstr64(buf, (uint32_t) xsize, 24, 4, tc, title);

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
			buf[(5 + y) * xsize + (xsize - 21 + x)] = (uint8_t) c;
		}
	}
}

void change_wtitle64(struct SHEET64 *sht, int act)
{
	int32_t x, y, xsize = sht->bxsize;
	uint8_t c, tc_new, tbc_new, tc_old, tbc_old;
	uint8_t *buf = sht->buf;

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
	for (y = 3; y <= WINDOW64_TITLE_H - 1; y++) {
		for (x = 3; x <= xsize - 4; x++) {
			c = buf[y * xsize + x];
			if (c == tc_old && x <= xsize - 22) {
				c = tc_new;
			} else if (c == tbc_old) {
				c = tbc_new;
			}
			buf[y * xsize + x] = c;
		}
	}
	sheet64_refresh(sht, 3, 3, xsize, WINDOW64_TITLE_H);
}
