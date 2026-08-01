#ifndef MOWKOW64_WINDOW64_H
#define MOWKOW64_WINDOW64_H

#include <sheet64.h>
#include <stdint.h>

/*
 * 팔레트 인덱스. src/include/graphic.h의 COL8_*와 같은 배치이며,
 * 실제 DAC 설정은 아직 없다 (로드맵 Phase 3 step 8).
 */
#define COL64_000000    0
#define COL64_FF0000    1
#define COL64_00FF00    2
#define COL64_FFFF00    3
#define COL64_0000FF    4
#define COL64_FF00FF    5
#define COL64_00FFFF    6
#define COL64_FFFFFF    7
#define COL64_C6C6C6    8
#define COL64_840000    9
#define COL64_008400   10
#define COL64_848400   11
#define COL64_000084   12
#define COL64_840084   13
#define COL64_008484   14
#define COL64_848484   15

#define WINDOW64_TITLE_H 21

void boxfill64(uint8_t *buf, int32_t xsize, uint8_t color,
	int32_t x0, int32_t y0, int32_t x1, int32_t y1);
void putstr64(uint8_t *buf, uint32_t stride, int32_t x, int32_t y, uint8_t color,
	const char *s);
void make_window64(uint8_t *buf, int32_t xsize, int32_t ysize, const char *title, int act);
void make_wtitle64(uint8_t *buf, int32_t xsize, const char *title, int act);
void change_wtitle64(struct SHEET64 *sht, int act);

#endif
