#define _GNU_SOURCE
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "../src64/kernel/sheet64.c"
#include "../src64/kernel/gui64.c"
#include "../src64/kernel/console_render64.c"
#include "../src64/kernel/console_queue64.c"

const uint8_t hankaku64[4096] = { [65 * 16] = 0xff };
struct MEMMAN64 memman64;
static int allocations64, frees64, fail_at64, irq_depth64;

uint64_t platform_irq_save64(void)
{
	return (uint64_t) irq_depth64++;
}

void platform_irq_restore64(uint64_t state)
{
	assert(irq_depth64 == (int) state + 1);
	irq_depth64--;
}

uintptr_t memman64_alloc_4k(struct MEMMAN64 *man, size_t size)
{
	(void) man;
	allocations64++;
	if (allocations64 == fail_at64 || size > 1024 * 1024) { return 0; }
	return (uintptr_t) calloc(1, size);
}

int memman64_free_4k(struct MEMMAN64 *man, uintptr_t address, size_t size)
{
	(void) man;
	(void) size;
	frees64++;
	free((void *) address);
	return 0;
}

uint32_t graphic64_rgb32(uint8_t index)
{
	return (uint32_t) index * 0x010101U;
}

int keyboard64_shift(void) { return 0; }
int keyboard64_ctrl(void) { return 0; }
int keyboard64_alt(void) { return 0; }
int task_run64(struct TASK64 *task, int32_t level, int32_t priority)
{
	(void) task;
	(void) level;
	(void) priority;
	return 0;
}

static void destroy_ctl64(struct SHTCTL64 *ctl)
{
	memman64_free_4k(&memman64, (uintptr_t) ctl->map, 0);
	memman64_free_4k(&memman64, (uintptr_t) ctl, 0);
}

static void rectangles64(void)
{
	uint8_t bytes[80], expected[80];
	struct SURFACE64 surface = { bytes + 8, 7 * 5 + 5, 7, 5, 6, 1, 2, 4, 5 };
	static const int32_t edges[] = { INT_MIN, -8, -1, 0, 1, 3, 5, 8, INT_MAX };

	for (size_t a = 0; a < sizeof(edges) / sizeof(edges[0]); a++) {
		for (size_t b = 0; b < sizeof(edges) / sizeof(edges[0]); b++) {
			memset(bytes, 0xa5, sizeof(bytes));
			memset(expected, 0xa5, sizeof(expected));
			boxfill64(&surface, 7, edges[a], edges[a], edges[b], edges[b]);
			for (int y = 2; y < 5; y++) {
				for (int x = 1; x < 4; x++) {
					if (x >= edges[a] && x <= edges[b] &&
						y >= edges[a] && y <= edges[b]) {
						expected[8 + y * 7 + x] = 7;
					}
				}
			}
			assert(memcmp(bytes, expected, sizeof(bytes)) == 0);
		}
	}
	/* Tiny decorated windows must not reach either guards or row padding. */
	for (int height = 1; height <= 25; height++) {
		for (int width = 1; width <= 40; width++) {
			uint8_t pixels[32 * 48 + 16];
			struct SURFACE64 small = {
				pixels + 8, (size_t) (height - 1) * 48 + width,
				48, width, height, 0, 0, width, height
			};
			memset(pixels, 0xa5, sizeof(pixels));
			make_window64(&small, "AAAA터미널", 1);
			make_wtitle64(&small, "A", 0);
			for (size_t i = 0; i < sizeof(pixels); i++) {
				if (i < 8 || (i - 8) / 48 >= (size_t) height ||
					(i - 8) % 48 >= (size_t) width) {
					assert(pixels[i] == 0xa5);
				}
			}
		}
	}
	memset(bytes, 0xa5, sizeof(bytes));
	surface.size--;
	boxfill64(&surface, 7, INT_MIN, INT_MIN, INT_MAX, INT_MAX);
	make_window64(&surface, "A", 1);
	for (size_t i = 0; i < sizeof(bytes); i++) { assert(bytes[i] == 0xa5); }
	boxfill64(NULL, 0, 0, 0, 1, 1);
	change_wtitle64(NULL, 0);
}

static void validation64(void)
{
	_Alignas(4) uint8_t vram[128];
	struct SHTCTL64 *ctl;
	struct SHEET64 *sheet, saved;
	int before = allocations64;

	assert(shtctl64_init(NULL, vram, sizeof(vram), 4, 4, 4, 8) == NULL);
	assert(shtctl64_init(&memman64, NULL, 128, 4, 4, 4, 8) == NULL);
	assert(shtctl64_init(&memman64, vram, 128, -1, 4, 4, 8) == NULL);
	assert(shtctl64_init(&memman64, vram, 128, 4, 0, 4, 8) == NULL);
	assert(shtctl64_init(&memman64, vram, 128, 4, 4, 3, 8) == NULL);
	assert(shtctl64_init(&memman64, vram, 15, 4, 4, 4, 8) == NULL);
	assert(shtctl64_init(&memman64, vram, 128, 4, 4, 4, 16) == NULL);
	assert(shtctl64_init(&memman64, vram, 128, INT_MAX, 1, UINT32_MAX, 32) == NULL);
	assert(shtctl64_init(&memman64, vram, 128, 4, 4, 17, 32) == NULL);
	assert(shtctl64_init(&memman64, vram + 1, 127, 4, 4, 16, 32) == NULL);
	assert(shtctl64_init(&memman64, (uint8_t *) (UINTPTR_MAX - 3),
		SIZE_MAX, 4, 4, 4, 8) == NULL);
	assert(shtctl64_init(&memman64, vram, 128,
		INT_MAX, INT_MAX, UINT32_MAX, 8) == NULL);
	assert(allocations64 == before);
	fail_at64 = allocations64 + 2;
	before = frees64;
	assert(shtctl64_init(&memman64, vram, 16, 4, 4, 4, 8) == NULL);
	assert(frees64 == before + 1);
	fail_at64 = 0;
	ctl = shtctl64_init(&memman64, vram, 16, 4, 4, 4, 8);
	assert(ctl != NULL);
	sheet = sheet64_alloc(ctl);
	assert(sheet != NULL && sheet->buf == NULL && sheet->vx0 == 0);
	sheet64_updown(sheet, 0);
	assert(ctl->top == -1);
	assert(sheet64_setbuf(sheet, vram, 16, 4, 4, 4, -1) == 0);
	saved = *sheet;
	assert(sheet64_setbuf(sheet, vram, 15, 4, 4, 4, -1) == -22);
	assert(sheet64_setbuf(sheet, vram, 16, 3, 4, 4, -1) == -22);
	assert(sheet64_setbuf(sheet, vram, 16, 4, -1, 4, -1) == -22);
	assert(sheet64_setbuf(sheet, vram, 16, 4, 4, 4, 256) == -22);
	assert(sheet64_setbuf(sheet, NULL, 16, 4, 4, 4, -1) == -22);
	assert(sheet64_setbuf(sheet, (uint8_t *) (UINTPTR_MAX - 3),
		SIZE_MAX, 4, 4, 4, -1) == -22);
	assert(memcmp(sheet, &saved, sizeof(saved)) == 0);
	sheet64_free(sheet);
	sheet = sheet64_alloc(ctl);
	assert(sheet->buf == NULL && sheet->buf_size == 0 && sheet->stride == 0);
	sheet64_free(sheet);
	destroy_ctl64(ctl);
}

static void composition64(uint8_t bpp)
{
	_Alignas(4) uint8_t vram[16 + 24 * 6 + 16];
	uint8_t background[5 * 6], overlay[7 * 3];
	struct SHTCTL64 *ctl;
	struct SHEET64 *back, *front;
	static const int32_t positions[] = { INT_MIN, -4, -2, 0, 3, 4, 5, INT_MAX };
	uint32_t stride = bpp == 8 ? 8 : 24;
	size_t size = (size_t) stride * 5 + 5 * (bpp / 8);

	memset(vram, 0xa5, sizeof(vram));
	memset(background, 1, sizeof(background));
	memset(overlay, 0xa5, sizeof(overlay));
	for (int y = 0; y < 3; y++) {
		for (int x = 0; x < 4; x++) { overlay[y * 7 + x] = (x + y) % 3 + 2; }
	}
	ctl = shtctl64_init(&memman64, vram + 16, size, 5, 6, stride, bpp);
	assert(ctl != NULL);
	back = sheet64_alloc(ctl);
	front = sheet64_alloc(ctl);
	assert(sheet64_setbuf(back, background, sizeof(background), 5, 5, 6, -1) == 0);
	assert(sheet64_setbuf(front, overlay, sizeof(overlay), 7, 4, 3, 3) == 0);
	sheet64_updown(back, 0);
	sheet64_updown(front, 1);
	for (size_t a = 0; a < sizeof(positions) / sizeof(positions[0]); a++) {
		for (size_t b = 0; b < sizeof(positions) / sizeof(positions[0]); b++) {
			sheet64_slide(front, positions[a], positions[b]);
			sheet64_refresh(front, INT_MIN, INT_MIN, INT_MAX, INT_MAX);
			sheet64_refresh_all(ctl);
			for (int y = 0; y < 6; y++) {
				for (int x = 0; x < 5; x++) {
					int64_t bx = (int64_t) x - positions[a];
					int64_t by = (int64_t) y - positions[b];
					uint8_t color = 1;
					if (bx >= 0 && bx < 4 && by >= 0 && by < 3 &&
						overlay[by * 7 + bx] != 3) { color = overlay[by * 7 + bx]; }
					if (bpp == 8) {
						assert(vram[16 + y * stride + x] == color);
					} else {
						uint32_t value;
						memcpy(&value, vram + 16 + y * stride + x * 4, 4);
						assert(value == graphic64_rgb32(color));
					}
				}
			}
		}
	}
	for (size_t i = 0; i < sizeof(vram); i++) {
		if (i < 16 || (i - 16) / stride >= 6 || (i - 16) % stride >= 5 * (bpp / 8)) {
			assert(vram[i] == 0xa5);
		}
	}
	/* Raising an already visible sheet must clamp to top, including a full stack. */
	for (int i = 2; i < MAX_SHEETS64; i++) {
		struct SHEET64 *sheet = sheet64_alloc(ctl);
		assert(sheet64_setbuf(sheet, overlay, sizeof(overlay), 7, 4, 3, -1) == 0);
		sheet64_updown(sheet, INT_MAX);
	}
	assert(sheet64_alloc(ctl) == NULL);
	sheet64_updown(back, INT_MAX);
	assert(back->height == 255 && ctl->top == 255);
	for (int i = 0; i <= ctl->top; i++) { assert(ctl->sheets[i]->height == i); }
	sheet64_free(back);
	assert(ctl->top == 254);
	destroy_ctl64(ctl);
}

/* Reserve sparse rows to exercise byte offsets above UINT32_MAX without
 * allocating gigabytes of physical memory. Padding remains inaccessible.
 */
static void large_stride64(void)
{
	size_t page = (size_t) sysconf(_SC_PAGESIZE);
	uint32_t stride = UINT32_C(2147483648);
	size_t extent = (size_t) stride * 2 + page;
	uint8_t *rows = mmap(NULL, extent, PROT_NONE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	uint8_t compact[3] = { 1, 2, 3 };
	struct SHTCTL64 *ctl;
	struct SHEET64 *sheet;

	assert(rows != MAP_FAILED);
	for (size_t y = 0; y < 3; y++) {
		assert(mprotect(rows + y * stride, page, PROT_READ | PROT_WRITE) == 0);
	}
	ctl = shtctl64_init(&memman64, rows, (size_t) stride * 2 + 1, 1, 3, stride, 8);
	assert(ctl != NULL);
	sheet = sheet64_alloc(ctl);
	assert(sheet64_setbuf(sheet, compact, sizeof(compact), 1, 1, 3, -1) == 0);
	sheet64_updown(sheet, 0);
	for (size_t y = 0; y < 3; y++) { assert(rows[y * stride] == y + 1); }
	destroy_ctl64(ctl);
	memset(compact, 0, sizeof(compact));
	ctl = shtctl64_init(&memman64, compact, sizeof(compact), 1, 3, 1, 8);
	assert(ctl != NULL);
	sheet = sheet64_alloc(ctl);
	assert(sheet64_setbuf(sheet, rows, (size_t) stride * 2 + 1, stride, 1, 3, -1) == 0);
	sheet64_updown(sheet, 0);
	for (size_t y = 0; y < 3; y++) { assert(compact[y] == y + 1); }
	destroy_ctl64(ctl);
	assert(munmap(rows, extent) == 0);
}

static void guarded_console64(void)
{
	size_t page = (size_t) sysconf(_SC_PAGESIZE);
	uint8_t *memory = mmap(NULL, 3 * page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	uint8_t *pixels;
	struct SHTCTL64 *ctl;
	struct SHEET64 *sheet;
	struct CONSOLE64 con = { 0 }, saved;
	struct SURFACE64 surface;

	assert(memory != MAP_FAILED);
	assert(mprotect(memory + page, page, PROT_READ | PROT_WRITE) == 0);
	pixels = memory + 2 * page - (23 * 19 + 17);
	memset(pixels, 0xa5, 23 * 19 + 17);
	ctl = shtctl64_init(&memman64, pixels, 23 * 19 + 17, 17, 20, 23, 8);
	assert(ctl != NULL);
	sheet = sheet64_alloc(ctl);
	assert(sheet64_setbuf(sheet, pixels, 23 * 19 + 17, 23, 17, 20, -1) == 0);
	surface = sheet64_surface(sheet);
	make_window64(&surface, "AAA", 1);
	change_wtitle64(sheet, 0);
	console64_attach_sheet(&con, sheet, 1, 1, 16, 19);
	assert(con.stride == 23 && con.vram_size == 23 * 18 + 16);
	fill_rect64(&con, 0, 0, UINT16_MAX, UINT16_MAX, 7);
	for (int y = 1; y < 20; y++) {
		for (int x = 1; x < 17; x++) { assert(pixels[y * 23 + x] == 7); }
	}
	con.cursor_y = 19;
	scroll_if_needed64(&con);
	for (int y = 1; y < 4; y++) { assert(pixels[y * 23 + 1] == 7); }
	for (int y = 4; y < 20; y++) { assert(pixels[y * 23 + 1] == color_bg); }
	saved = con;
	console64_attach_sheet(&con, sheet, 2, 1, 16, 19);
	assert(memcmp(&con, &saved, sizeof(con)) == 0);
	con.height = 1;
	con.cursor_y = UINT16_MAX;
	scroll_if_needed64(&con);
	assert(con.cursor_y == 0);
	con.vram_size = 1;
	fill_rect64(&con, 0, 0, 16, 1, 99);
	assert(pixels[24] == 7);
	/* Hit testing must subtract in int64 and use source stride. */
	gui_context64.ctl = ctl;
	sheet64_updown(sheet, 0);
	ctl->top = 1; /* hit_test excludes the top cursor slot */
	int32_t bx, by;
	assert(hit_test(2, 2, &bx, &by) == sheet && bx == 2 && by == 2);
	sheet->vx0 = INT_MIN;
	sheet->vy0 = INT_MAX;
	assert(hit_test(INT_MAX, INT_MIN, &bx, &by) == NULL);
	gui_context64.ctl = NULL;
	destroy_ctl64(ctl);
	assert(munmap(memory, 3 * page) == 0);
}

int main(void)
{
	rectangles64();
	validation64();
	composition64(8);
	composition64(32);
	large_stride64();
	guarded_console64();
	assert(irq_depth64 == 0);
	puts("Graphics rectangles/windows, sheet geometry/stride, clipping and guard pages: PASS");
	return 0;
}

void platform_framebuffer_flush64(uintptr_t address, size_t bytes)
{
	(void) address;
	(void) bytes;
}

void platform_framebuffer_sync64(void)
{
}
