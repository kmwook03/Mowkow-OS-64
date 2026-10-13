/*
 * 시트(겹치기 처리) 컨트롤러 -- src/kernel/sheet.c의 64비트 이식.
 *
 * 32비트 원본과 다른 점
 *   1. VRAM 인덱싱에 ctl->stride를 쓴다. 원본은 화면 폭과 스캔라인 길이가
 *      같다고 가정하지만 VBE 모드에서는 보장되지 않는다.
 *   2. 4픽셀 묶음 최적화를 뺐다.
 */

#include <arch/platform64.h>
#include <memory64.h>
#include <sheet64.h>
#include <stddef.h>
#include <stdint.h>

#define SHEET64_USE 1

static void sheet64_refreshmap(struct SHTCTL64 *ctl, int64_t vx0, int64_t vy0,
	int64_t vx1, int64_t vy1, int32_t h0);
static void sheet64_refreshsub(struct SHTCTL64 *ctl, int64_t vx0, int64_t vy0,
	int64_t vx1, int64_t vy1, int32_t h0, int32_t h1);

static uint64_t sheet64_lock(void)
{
	return platform_irq_save64();
}

static void sheet64_unlock(uint64_t state)
{
	platform_irq_restore64(state);
}

struct SHTCTL64 *shtctl64_init(struct MEMMAN64 *man, uint8_t *vram, size_t vram_size,
	int32_t xsize, int32_t ysize, uint32_t stride, uint8_t bpp)
{
	struct SHTCTL64 *ctl;
	uintptr_t ctl_addr;
	uintptr_t map_addr;
	int32_t i;
	size_t map_size;
	struct FRAMEBUFFER64 framebuffer;

	if (man == NULL || framebuffer_init64(&framebuffer, vram, vram_size,
		xsize, ysize, stride, bpp) != 0 ||
		(size_t) xsize > SIZE_MAX / (size_t) ysize) { return NULL; }
	map_size = (size_t) xsize * (size_t) ysize;
	if (map_size > SIZE_MAX - 4095U) { return NULL; }

	ctl_addr = memman64_alloc_4k(man, sizeof (struct SHTCTL64));
	if (ctl_addr == 0) {
		return NULL;
	}
	map_addr = memman64_alloc_4k(man, map_size);
	if (map_addr == 0) {
		memman64_free_4k(man, ctl_addr, sizeof (struct SHTCTL64));
		return NULL;
	}

	ctl = (struct SHTCTL64 *) ctl_addr;
	ctl->framebuffer = framebuffer;
	ctl->map = (uint8_t *) map_addr;
	ctl->vram = vram;
	ctl->vram_size = vram_size;
	ctl->xsize = xsize;
	ctl->ysize = ysize;
	ctl->stride = stride;
	ctl->bpp = bpp;
	ctl->top = -1;
	for (i = 0; i < MAX_SHEETS64; i++) {
		ctl->sheets0[i] = (struct SHEET64) { 0 };
		ctl->sheets0[i].ctl = ctl;
		ctl->sheets0[i].task = NULL;
	}
	return ctl;
}

static struct SHEET64 *sheet64_alloc_nolock(struct SHTCTL64 *ctl)
{
	struct SHEET64 *sht;
	int32_t i;

	for (i = 0; i < MAX_SHEETS64; i++) {
		if (ctl->sheets0[i].flags == 0) {
			sht = &ctl->sheets0[i];
			*sht = (struct SHEET64) { .ctl = ctl };
			sht->flags = SHEET64_USE;
			sht->height = -1;
			sht->task = NULL;
			return sht;
		}
	}
	return NULL;
}

struct SURFACE64 sheet64_surface(const struct SHEET64 *sht)
{
	if (sht == NULL) {
		return (struct SURFACE64) { 0 };
	}
	return (struct SURFACE64) {
		sht->buf, sht->buf_size, sht->stride, sht->bxsize, sht->bysize,
		0, 0, sht->bxsize, sht->bysize
	};
}

/*
 * 원본의 4픽셀(32비트 워드) 묶음 최적화를 빼고 픽셀 단위로만 처리.
 * 스트라이드가 화면 폭과 다를 수 있어 워드 경계 가정이 깨지고,
 * 겹침 처리에서 가장 미묘한 부분이라 먼저 정확하게 옮기는 쪽을 택했다.
 * 800x600 전체 갱신이 눈에 띄게 느려지면 그때 되살린다.
 */
static void sheet64_refreshmap(struct SHTCTL64 *ctl, int64_t vx0, int64_t vy0,
	int64_t vx1, int64_t vy1, int32_t h0)
{
	int32_t h, bx, by, vx, vy;
	int64_t bx0, by0, bx1, by1;
	uint8_t *buf;
	uint8_t *map = ctl->map;
	uint8_t sid;
	struct SHEET64 *sht;

	if (vx0 < 0) { vx0 = 0; }
	if (vy0 < 0) { vy0 = 0; }
	if (vx1 > ctl->xsize) { vx1 = ctl->xsize; }
	if (vy1 > ctl->ysize) { vy1 = ctl->ysize; }
	if (vx0 >= vx1 || vy0 >= vy1) { return; }
	if (h0 < 0) { h0 = 0; }

	for (h = h0; h <= ctl->top; h++) {
		sht = ctl->sheets[h];
		sid = (uint8_t) (sht - ctl->sheets0);
		buf = sht->buf;
		bx0 = vx0 - sht->vx0;
		by0 = vy0 - sht->vy0;
		bx1 = vx1 - sht->vx0;
		by1 = vy1 - sht->vy0;
		if (bx0 < 0) { bx0 = 0; }
		if (by0 < 0) { by0 = 0; }
		if (bx1 > sht->bxsize) { bx1 = sht->bxsize; }
		if (by1 > sht->bysize) { by1 = sht->bysize; }

		if (bx0 >= bx1 || by0 >= by1) { continue; }
		for (by = (int32_t) by0; by < by1; by++) {
			vy = (int32_t) ((int64_t) sht->vy0 + by);
			for (bx = (int32_t) bx0; bx < bx1; bx++) {
				vx = (int32_t) ((int64_t) sht->vx0 + bx);
				if (sht->col_inv == -1 ||
						buf[(size_t) by * sht->stride + (size_t) bx] != (uint8_t) sht->col_inv) {
					map[(size_t) vy * (size_t) ctl->xsize + (size_t) vx] = sid;
				}
			}
		}
	}
}

static void sheet64_refreshsub(struct SHTCTL64 *ctl, int64_t vx0, int64_t vy0,
	int64_t vx1, int64_t vy1, int32_t h0, int32_t h1)
{
	int32_t h, by, vx, vy;
	int64_t bx0, by0, bx1, by1;
	uint8_t *buf;
	uint8_t *map = ctl->map;
	uint8_t sid;
	struct SHEET64 *sht;

	if (vx0 < 0) { vx0 = 0; }
	if (vy0 < 0) { vy0 = 0; }
	if (vx1 > ctl->xsize) { vx1 = ctl->xsize; }
	if (vy1 > ctl->ysize) { vy1 = ctl->ysize; }
	if (vx0 >= vx1 || vy0 >= vy1) { return; }
	if (h0 < 0) { h0 = 0; }

	if (h1 > ctl->top) { h1 = ctl->top; }
	for (h = h0; h <= h1; h++) {
		sht = ctl->sheets[h];
		buf = sht->buf;
		sid = (uint8_t) (sht - ctl->sheets0);
		bx0 = vx0 - sht->vx0;
		by0 = vy0 - sht->vy0;
		bx1 = vx1 - sht->vx0;
		by1 = vy1 - sht->vy0;
		if (bx0 < 0) { bx0 = 0; }
		if (by0 < 0) { by0 = 0; }
		if (bx1 > sht->bxsize) { bx1 = sht->bxsize; }
		if (by1 > sht->bysize) { by1 = sht->bysize; }

		if (bx0 >= bx1 || by0 >= by1) { continue; }
		for (by = (int32_t) by0; by < by1; by++) {
			vy = (int32_t) ((int64_t) sht->vy0 + by);
			vx = (int32_t) ((int64_t) sht->vx0 + bx0);
			framebuffer_span64(&ctl->framebuffer, vx, vy,
				buf + (size_t) by * sht->stride + (size_t) bx0,
				map + (size_t) vy * (size_t) ctl->xsize + (size_t) vx,
				sid, (int32_t) (bx1 - bx0));
		}
	}
	framebuffer_flush64(&ctl->framebuffer, vx0, vy0, vx1, vy1);
}

static void sheet64_updown_nolock(struct SHEET64 *sht, int32_t height)
{
	struct SHTCTL64 *ctl = sht->ctl;
	int32_t h, old = sht->height;

	/* A visible sheet cannot occupy the unused top+1 slot. */
	int32_t maximum = old >= 0 ? ctl->top : ctl->top + 1;

	if (height > maximum) {
		height = maximum;
	}
	if (height < -1) {
		height = -1;
	}
	sht->height = height;

	if (old > height) {
		if (height >= 0) {
			for (h = old; h > height; h--) {
				ctl->sheets[h] = ctl->sheets[h - 1];
				ctl->sheets[h]->height = h;
			}
			ctl->sheets[height] = sht;
			sheet64_refreshmap(ctl, sht->vx0, sht->vy0,
				(int64_t) sht->vx0 + sht->bxsize, (int64_t) sht->vy0 + sht->bysize, height + 1);
			sheet64_refreshsub(ctl, sht->vx0, sht->vy0,
				(int64_t) sht->vx0 + sht->bxsize, (int64_t) sht->vy0 + sht->bysize, height + 1, old);
		} else {
			if (ctl->top > old) {
				for (h = old; h < ctl->top; h++) {
					ctl->sheets[h] = ctl->sheets[h + 1];
					ctl->sheets[h]->height = h;
				}
			}
			ctl->top--;
			sheet64_refreshmap(ctl, sht->vx0, sht->vy0,
				(int64_t) sht->vx0 + sht->bxsize, (int64_t) sht->vy0 + sht->bysize, 0);
			sheet64_refreshsub(ctl, sht->vx0, sht->vy0,
				(int64_t) sht->vx0 + sht->bxsize, (int64_t) sht->vy0 + sht->bysize, 0, old - 1);
		}
	} else if (old < height) {
		if (old >= 0) {
			for (h = old; h < height; h++) {
				ctl->sheets[h] = ctl->sheets[h + 1];
				ctl->sheets[h]->height = h;
			}
			ctl->sheets[height] = sht;
		} else {
			for (h = ctl->top; h >= height; h--) {
				ctl->sheets[h + 1] = ctl->sheets[h];
				ctl->sheets[h + 1]->height = h + 1;
			}
			ctl->sheets[height] = sht;
			ctl->top++;
		}
		sheet64_refreshmap(ctl, sht->vx0, sht->vy0,
			(int64_t) sht->vx0 + sht->bxsize, (int64_t) sht->vy0 + sht->bysize, height);
		sheet64_refreshsub(ctl, sht->vx0, sht->vy0,
			(int64_t) sht->vx0 + sht->bxsize, (int64_t) sht->vy0 + sht->bysize, height, height);
	}
}

static void sheet64_refresh_nolock(struct SHEET64 *sht, int32_t bx0,
	int32_t by0, int32_t bx1, int32_t by1)
{
	if (bx0 < 0) { bx0 = 0; }
	if (by0 < 0) { by0 = 0; }
	if (bx1 > sht->bxsize) { bx1 = sht->bxsize; }
	if (by1 > sht->bysize) { by1 = sht->bysize; }
	if (bx0 >= bx1 || by0 >= by1) { return; }
	if (sht->height >= 0) {
		sheet64_refreshsub(sht->ctl, (int64_t) sht->vx0 + bx0, (int64_t) sht->vy0 + by0,
			(int64_t) sht->vx0 + bx1, (int64_t) sht->vy0 + by1, sht->height, sht->height);
	}
}

static void sheet64_slide_nolock(struct SHEET64 *sht, int32_t vx0, int32_t vy0)
{
	struct SHTCTL64 *ctl = sht->ctl;
	int32_t old_vx0 = sht->vx0, old_vy0 = sht->vy0;

	sht->vx0 = vx0;
	sht->vy0 = vy0;
	if (sht->height >= 0) {
		sheet64_refreshmap(ctl, old_vx0, old_vy0,
			(int64_t) old_vx0 + sht->bxsize, (int64_t) old_vy0 + sht->bysize, 0);
		sheet64_refreshmap(ctl, vx0, vy0,
			(int64_t) vx0 + sht->bxsize, (int64_t) vy0 + sht->bysize, sht->height);
		sheet64_refreshsub(ctl, old_vx0, old_vy0,
			(int64_t) old_vx0 + sht->bxsize, (int64_t) old_vy0 + sht->bysize, 0, sht->height - 1);
		sheet64_refreshsub(ctl, vx0, vy0,
			(int64_t) vx0 + sht->bxsize, (int64_t) vy0 + sht->bysize, sht->height, sht->height);
	}
}

/* 시트 크기가 바뀐 뒤처럼 map 전체가 낡았을 때 화면을 통째로 다시 그린다.
   sheet64_slide는 이동 전 영역을 '새' 크기로 계산하므로 축소 시에는 부족하다. */
static void sheet64_refresh_all_nolock(struct SHTCTL64 *ctl)
{
	if (ctl->top < 0) {
		return;
	}
	sheet64_refreshmap(ctl, 0, 0, ctl->xsize, ctl->ysize, 0);
	sheet64_refreshsub(ctl, 0, 0, ctl->xsize, ctl->ysize, 0, ctl->top);
}

static void sheet64_free_nolock(struct SHEET64 *sht)
{
	if (sht->height >= 0) {
		sheet64_updown_nolock(sht, -1);
	}
	sht->flags = 0;
}

/*
 * 컴포지터는 재진입 불가다. sheet64_updown은 ctl->sheets[]와 모든 시트의
 * height를 새로 쓰고, refreshmap/refreshsub는 그동안 그 배열을 읽는다.
 * 콘솔마다 태스크가 하나씩 생기면 PIT 선점이 그 한가운데를
 * 갈라놓는다. 전환 원인이 PIT IRQ뿐이라 인터럽트를 막으면 충분하다.
 * rflags 저장/복원은 이미 꺼진 문맥에서 불러도 안전하게 하려는 것이다.
 * 규칙: 이 구간 안에서는 task_sleep64를 부르지 않는다.
 */
struct SHEET64 *sheet64_alloc(struct SHTCTL64 *ctl)
{
	uint64_t flags;
	struct SHEET64 *sht;

	if (ctl == NULL) { return NULL; }
	flags = sheet64_lock();
	sht = sheet64_alloc_nolock(ctl);
	sheet64_unlock(flags);
	return sht;
}

int sheet64_setbuf(struct SHEET64 *sht, uint8_t *buf, size_t size, uint32_t stride,
	int32_t xsize, int32_t ysize, int32_t col_inv)
{
	struct SURFACE64 surface = {
		buf, size, stride, xsize, ysize, 0, 0, xsize, ysize
	};
	uint64_t flags;

	if (sht == NULL || sht->ctl == NULL || sht->flags == 0 ||
		col_inv < -1 || col_inv > 255 || !surface64_valid(&surface)) {
		return -22;
	}
	flags = sheet64_lock();
	sht->buf = buf;
	sht->buf_size = size;
	sht->stride = stride;
	sht->bxsize = xsize;
	sht->bysize = ysize;
	sht->col_inv = col_inv;
	sheet64_unlock(flags);
	return 0;
}

void sheet64_updown(struct SHEET64 *sht, int32_t height)
{
	uint64_t flags;

	if (sht == NULL || sht->flags == 0 || sht->ctl == NULL) { return; }
	if (sht->buf == NULL) { return; }
	flags = sheet64_lock();
	sheet64_updown_nolock(sht, height);
	sheet64_unlock(flags);
}

void sheet64_refresh(struct SHEET64 *sht, int32_t bx0, int32_t by0, int32_t bx1, int32_t by1)
{
	uint64_t flags;

	if (sht == NULL || sht->flags == 0 || sht->ctl == NULL) { return; }
	if (sht->buf == NULL) { return; }
	flags = sheet64_lock();
	sheet64_refresh_nolock(sht, bx0, by0, bx1, by1);
	sheet64_unlock(flags);
}

void sheet64_slide(struct SHEET64 *sht, int32_t vx0, int32_t vy0)
{
	uint64_t flags;

	if (sht == NULL || sht->flags == 0 || sht->ctl == NULL) { return; }
	if (sht->buf == NULL) { return; }
	flags = sheet64_lock();
	sheet64_slide_nolock(sht, vx0, vy0);
	sheet64_unlock(flags);
}

void sheet64_refresh_all(struct SHTCTL64 *ctl)
{
	uint64_t flags;

	if (ctl == NULL) { return; }
	flags = sheet64_lock();
	sheet64_refresh_all_nolock(ctl);
	sheet64_unlock(flags);
}

void sheet64_free(struct SHEET64 *sht)
{
	uint64_t flags;

	if (sht == NULL || sht->flags == 0 || sht->ctl == NULL) { return; }
	flags = sheet64_lock();
	sheet64_free_nolock(sht);
	sheet64_unlock(flags);
}
