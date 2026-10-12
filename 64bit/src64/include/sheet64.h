#ifndef MOWKOW64_SHEET64_H
#define MOWKOW64_SHEET64_H

#include <memory64.h>
#include <draw64.h>
#include <framebuffer64.h>
#include <stdint.h>

#define MAX_SHEETS64 256

struct SHTCTL64;
struct TASK64;

struct SHEET64 {
	uint8_t *buf;                   /* 시트 데이터 버퍼 */
	size_t buf_size;
	uint32_t stride;                /* 8bpp buffer row bytes, including padding */
	int32_t bxsize, bysize;         /* 버퍼 크기 */
	int32_t vx0, vy0;               /* 화면상 위치 */
	int32_t col_inv;                /* 투명색 (-1이면 투명색 없음) */
	int32_t height;                 /* 높이, -1이면 숨김 */
	int32_t flags;
	struct SHTCTL64 *ctl;
	struct TASK64 *task;
};

struct SHTCTL64 {
	struct FRAMEBUFFER64 framebuffer;
	uint8_t *vram;
	size_t vram_size;
	uint8_t *map;                   /* 픽셀별 시트 ID, xsize 간격 */
	int32_t xsize, ysize;
	uint32_t stride;                /* VRAM 한 행의 바이트 수 (xsize와 다를 수 있음) */
	uint8_t bpp;                    /* 출력 VRAM: 8bpp indexed 또는 32bpp RGB */
	int32_t top;
	struct SHEET64 *sheets[MAX_SHEETS64];
	struct SHEET64 sheets0[MAX_SHEETS64];
};

struct SHTCTL64 *shtctl64_init(struct MEMMAN64 *man, uint8_t *vram, size_t vram_size,
	int32_t xsize, int32_t ysize, uint32_t stride, uint8_t bpp);
struct SHEET64 *sheet64_alloc(struct SHTCTL64 *ctl);
/* Invalid buffer descriptors return -22 without changing the sheet.
 * Caller owns buf and must keep size bytes live until hidden/rebound/freed.
 * Task/IRQ safe; operations serialize with irq-save and never sleep.
 */
int sheet64_setbuf(struct SHEET64 *sht, uint8_t *buf, size_t size, uint32_t stride,
	int32_t xsize, int32_t ysize, int32_t col_inv);
struct SURFACE64 sheet64_surface(const struct SHEET64 *sht);
void sheet64_updown(struct SHEET64 *sht, int32_t height);
void sheet64_refresh(struct SHEET64 *sht, int32_t bx0, int32_t by0, int32_t bx1, int32_t by1);
void sheet64_slide(struct SHEET64 *sht, int32_t vx0, int32_t vy0);
void sheet64_free(struct SHEET64 *sht);
void sheet64_refresh_all(struct SHTCTL64 *ctl);

#endif
