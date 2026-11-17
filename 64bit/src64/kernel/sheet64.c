/* Deferred tile compositor: sheet mutations publish damage, not framebuffer writes. */
#include <arch/platform64.h>
#include <memory64.h>
#include <kstring64.h>
#include <sheet64.h>
#include <stddef.h>
#include <stdint.h>

#define SHEET64_USE 1

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
	size_t map_size, tile_columns, tile_rows, tile_count;
	struct FRAMEBUFFER64 framebuffer;

	if (man == NULL || framebuffer_init64(&framebuffer, vram, vram_size,
		xsize, ysize, stride, bpp) != 0 ||
		(size_t) xsize > SIZE_MAX / (size_t) ysize) { return NULL; }
	map_size = (size_t) xsize * (size_t) ysize;
	tile_columns = ((size_t) xsize - 1) / SHEET64_TILE_WIDTH + 1;
	tile_rows = ((size_t) ysize - 1) / SHEET64_TILE_HEIGHT + 1;
	if (tile_columns > SIZE_MAX / tile_rows) { return NULL; }
	tile_count = tile_columns * tile_rows;
	if (tile_count > SIZE_MAX - map_size ||
		map_size + tile_count > SIZE_MAX - 4095U) { return NULL; }

	ctl_addr = memman64_alloc_4k(man, sizeof (struct SHTCTL64));
	if (ctl_addr == 0) {
		return NULL;
	}
	map_addr = memman64_alloc_4k(man, map_size + tile_count);
	if (map_addr == 0) {
		memman64_free_4k(man, ctl_addr, sizeof (struct SHTCTL64));
		return NULL;
	}

	ctl = (struct SHTCTL64 *) ctl_addr;
	ctl->framebuffer = framebuffer;
	ctl->map = (uint8_t *) map_addr;
	ctl->map_size = map_size + tile_count;
	ctl->dirty = ctl->map + map_size;
	ctl->tile_columns = tile_columns;
	ctl->tile_count = tile_count;
	ctl->dirty_count = 0;
	ctl->scan_tile = 0;
	ctl->presenting = 0;
	memset(ctl->dirty, 0, tile_count);
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

/* Damage coalesces per tile, including repeated cursor/window moves. */
static void sheet64_damage(struct SHTCTL64 *ctl, int64_t x0, int64_t y0,
	int64_t x1, int64_t y1)
{
	if (x0 < 0) { x0 = 0; }
	if (y0 < 0) { y0 = 0; }
	if (x1 > ctl->xsize) { x1 = ctl->xsize; }
	if (y1 > ctl->ysize) { y1 = ctl->ysize; }
	if (x0 >= x1 || y0 >= y1) { return; }
	for (size_t y = (size_t) y0 / SHEET64_TILE_HEIGHT;
		y <= (size_t) (y1 - 1) / SHEET64_TILE_HEIGHT; y++) {
		for (size_t x = (size_t) x0 / SHEET64_TILE_WIDTH;
			x <= (size_t) (x1 - 1) / SHEET64_TILE_WIDTH; x++) {
			size_t tile = y * ctl->tile_columns + x;

			if (ctl->dirty[tile] == 0) {
				ctl->dirty[tile] = 1;
				ctl->dirty_count++;
			}
		}
	}
}

static void sheet64_damage_sheet(struct SHEET64 *sht)
{
	if (sht->height >= 0) {
		sheet64_damage(sht->ctl, sht->vx0, sht->vy0,
			(int64_t) sht->vx0 + sht->bxsize, (int64_t) sht->vy0 + sht->bysize);
	}
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
	if (old == height) { return; }
	sheet64_damage_sheet(sht);
	sht->height = height;

	if (old > height) {
		if (height >= 0) {
			for (h = old; h > height; h--) {
				ctl->sheets[h] = ctl->sheets[h - 1];
				ctl->sheets[h]->height = h;
			}
			ctl->sheets[height] = sht;
		} else {
			if (ctl->top > old) {
				for (h = old; h < ctl->top; h++) {
					ctl->sheets[h] = ctl->sheets[h + 1];
					ctl->sheets[h]->height = h;
				}
			}
			ctl->top--;
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
	}
	sheet64_damage_sheet(sht);
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
		sheet64_damage(sht->ctl, (int64_t) sht->vx0 + bx0, (int64_t) sht->vy0 + by0,
			(int64_t) sht->vx0 + bx1, (int64_t) sht->vy0 + by1);
	}
}

static void sheet64_slide_nolock(struct SHEET64 *sht, int32_t vx0, int32_t vy0)
{
	if (sht->vx0 == vx0 && sht->vy0 == vy0) { return; }
	sheet64_damage_sheet(sht);
	sht->vx0 = vx0;
	sht->vy0 = vy0;
	sheet64_damage_sheet(sht);
}

static void sheet64_refresh_all_nolock(struct SHTCTL64 *ctl)
{
	sheet64_damage(ctl, 0, 0, ctl->xsize, ctl->ysize);
}

static void sheet64_free_nolock(struct SHEET64 *sht)
{
	if (sht->height >= 0) {
		sheet64_updown_nolock(sht, -1);
	}
	sht->flags = 0;
}

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
	sheet64_damage_sheet(sht);
	sht->buf = buf;
	sht->buf_size = size;
	sht->stride = stride;
	sht->bxsize = xsize;
	sht->bysize = ysize;
	sht->col_inv = col_inv;
	sheet64_damage_sheet(sht);
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

/* Resolve front to back, skipping covered rows and stopping at full coverage.
 * Only copied indices escape the guard; close/rebind can release old buffers
 * even when presentation is preempted immediately after this snapshot.
 */
static void sheet64_snapshot(struct SHTCTL64 *ctl, int32_t x, int32_t y,
	int32_t width, int32_t height, uint8_t *pixels)
{
	uint32_t covered[SHEET64_TILE_HEIGHT] = { 0 };
	uint32_t wanted = UINT32_MAX >> (32 - width);
	int remaining = height;

	memset(pixels, 0, SHEET64_TILE_WIDTH * SHEET64_TILE_HEIGHT);
	for (int row = 0; row < height; row++) {
		memset(ctl->map + (size_t) (y + row) * ctl->xsize + x, 0, (size_t) width);
	}
	for (int h = ctl->top; h >= 0 && remaining != 0; h--) {
		const struct SHEET64 *sht = ctl->sheets[h];
		int64_t left = (int64_t) sht->vx0 - x;
		int64_t top = (int64_t) sht->vy0 - y;
		int64_t right = left + sht->bxsize, bottom = top + sht->bysize;
		uint8_t sid = (uint8_t) (sht - ctl->sheets0);

		if (left < 0) { left = 0; }
		if (top < 0) { top = 0; }
		if (right > width) { right = width; }
		if (bottom > height) { bottom = height; }
		if (left >= right || top >= bottom) { continue; }
		for (int row = (int) top; row < bottom; row++) {
			const uint8_t *source;
			uint8_t *output, *owners;

			if (covered[row] == wanted) { continue; }
			source = sht->buf + (size_t) ((int64_t) y + row - sht->vy0) * sht->stride +
				(size_t) ((int64_t) x + left - sht->vx0);
			output = pixels + row * SHEET64_TILE_WIDTH;
			owners = ctl->map + (size_t) (y + row) * ctl->xsize + x;
			if (sht->col_inv == -1 && covered[row] == 0 && left == 0 && right == width) {
				memcpy(output, source, (size_t) width);
				memset(owners, sid, (size_t) width);
				covered[row] = wanted;
			} else {
				for (int col = (int) left; col < right; col++) {
					uint32_t bit = 1U << col;
					uint8_t color;

					if ((covered[row] & bit) != 0) { continue; }
					color = source[col - (int) left];
					if (sht->col_inv >= 0 && color == sht->col_inv) { continue; }
					output[col] = color;
					owners[col] = sid;
					covered[row] |= bit;
				}
			}
			if (covered[row] == wanted) { remaining--; }
		}
	}
}

int sheet64_present(struct SHTCTL64 *ctl, size_t tile_budget)
{
	uint8_t pixels[SHEET64_TILE_WIDTH * SHEET64_TILE_HEIGHT];
	const uint8_t owners[SHEET64_TILE_WIDTH] = { 0 };
	size_t scanned = 0, drawn = 0;
	uint64_t flags;
	int pending;

	if (ctl == NULL) { return 0; }
	flags = sheet64_lock();
	if (ctl->presenting) {
		sheet64_unlock(flags);
		return 1;
	}
	ctl->presenting = 1;
	sheet64_unlock(flags);
	while (scanned < ctl->tile_count && drawn < tile_budget) {
		size_t tile = SIZE_MAX;
		int32_t x = 0, y = 0, width = 0, height = 0;

		flags = sheet64_lock();
		if (ctl->dirty_count == 0) {
			sheet64_unlock(flags);
			break;
		}
		/* Bound sparse-damage searches as well as pixel work per IRQ guard. */
		for (int search = 0; search < 64 && scanned < ctl->tile_count; search++) {
			size_t candidate = ctl->scan_tile;

			ctl->scan_tile = candidate + 1 == ctl->tile_count ? 0 : candidate + 1;
			scanned++;
			if (ctl->dirty[candidate] != 0) {
				tile = candidate;
				break;
			}
		}
		if (tile != SIZE_MAX) {
			ctl->dirty[tile] = 0;
			ctl->dirty_count--;
			x = (int32_t) ((tile % ctl->tile_columns) * SHEET64_TILE_WIDTH);
			y = (int32_t) ((tile / ctl->tile_columns) * SHEET64_TILE_HEIGHT);
			width = ctl->xsize - x;
			height = ctl->ysize - y;
			if (width > SHEET64_TILE_WIDTH) { width = SHEET64_TILE_WIDTH; }
			if (height > SHEET64_TILE_HEIGHT) { height = SHEET64_TILE_HEIGHT; }
			sheet64_snapshot(ctl, x, y, width, height, pixels);
		}
		sheet64_unlock(flags);
		if (tile == SIZE_MAX) { continue; }
		for (int row = 0; row < height; row++) {
			framebuffer_span64(&ctl->framebuffer, x, y + row,
				pixels + row * SHEET64_TILE_WIDTH, owners, 0, width);
		}
		framebuffer_flush64(&ctl->framebuffer, x, y, (int64_t) x + width, (int64_t) y + height);
		drawn++;
	}
	flags = sheet64_lock();
	ctl->presenting = 0;
	pending = ctl->dirty_count != 0;
	sheet64_unlock(flags);
	return pending;
}
