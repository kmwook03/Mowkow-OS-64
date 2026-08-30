#ifndef MOWKOW64_SHEET64_H
#define MOWKOW64_SHEET64_H

#include <memory64.h>
#include <stdint.h>

#define MAX_SHEETS64 256

struct SHTCTL64;
struct TASK64;

struct SHEET64 {
	uint8_t *buf;                   /* 시트 데이터 버퍼 */
	int32_t bxsize, bysize;         /* 버퍼 크기 */
	int32_t vx0, vy0;               /* 화면상 위치 */
	int32_t col_inv;                /* 투명색 (-1이면 투명색 없음) */
	int32_t height;                 /* 높이, -1이면 숨김 */
	int32_t flags;
	struct SHTCTL64 *ctl;
	struct TASK64 *task;
};

struct SHTCTL64 {
	uint8_t *vram;
	uint8_t *map;                   /* 픽셀별 시트 ID, xsize 간격 */
	int32_t xsize, ysize;
	uint32_t stride;                /* VRAM 한 행의 바이트 수 (xsize와 다를 수 있음) */
	int32_t top;
	struct SHEET64 *sheets[MAX_SHEETS64];
	struct SHEET64 sheets0[MAX_SHEETS64];
};

struct SHTCTL64 *shtctl64_init(struct MEMMAN64 *man, uint8_t *vram,
	int32_t xsize, int32_t ysize, uint32_t stride);
struct SHEET64 *sheet64_alloc(struct SHTCTL64 *ctl);
void sheet64_setbuf(struct SHEET64 *sht, uint8_t *buf, int32_t xsize, int32_t ysize,
	int32_t col_inv);
void sheet64_updown(struct SHEET64 *sht, int32_t height);
void sheet64_refresh(struct SHEET64 *sht, int32_t bx0, int32_t by0, int32_t bx1, int32_t by1);
void sheet64_slide(struct SHEET64 *sht, int32_t vx0, int32_t vy0);
void sheet64_free(struct SHEET64 *sht);
void sheet64_refresh_all(struct SHTCTL64 *ctl);

#endif
