/*
 * 바탕화면과 콘솔 창.
 *
 * 컴포지터는 항상 켜져 있고 콘솔은 그 위의 창 하나다. 부팅
 * 직후에는 콘솔 창이 화면 전체를 덮으므로 지금까지와 똑같아 보이고,
 * `xwindow`(또는 `창`) 명령이 바탕화면 위의 작은 창으로 바꾼다. 비디오 모드는
 * 바뀌지 않는다 -- 롱 모드에서는 VBE로 돌아갈 수 없다.
 */

#include "gui_internal64.h"
#include "console_internal64.h"
#include <arch/platform64.h>
#include <console64.h>
#include <graphic64.h>
#include <gui64.h>
#include <kstring64.h>
#include <memory64.h>
#include <mouse64.h>
#include <sheet64.h>
#include <stddef.h>
#include <stdint.h>
#include <window64.h>

/* 창 크기와 위치는 화면 비례다. 높이와 위치는 800x600일 때 예전의 고정값
   (400, (64,80))과 같은 값이 나오도록 고른 비율이고, 너비만 그 4/5에서
   다시 70%로 줄인 14/25다. */
#define GUI64_WIN_W_NUM 14
#define GUI64_WIN_W_DEN 25
#define GUI64_WIN_H_NUM 2
#define GUI64_WIN_H_DEN 3
#define GUI64_WIN_X_NUM 2
#define GUI64_WIN_X_DEN 25
#define GUI64_WIN_Y_NUM 2
#define GUI64_WIN_Y_DEN 15
/* 창 테두리 안쪽 내용 영역. 32비트 트리의 콘솔 창과 같은 여백. */
#define GUI64_PAD_X     8
#define GUI64_PAD_Y    28
#define GUI64_PAD_W    16
#define GUI64_PAD_H    36

#define GUI64_MODE_FULL   0
#define GUI64_MODE_WINDOW 1

#define GUI64_CURSOR_SIZE 16
#define GUI64_CURSOR_INV  99    /* 커서 시트의 투명색 */

#define GUI64_MAX_WINS 8
#define GUI64_TITLE_MAX 24

/* 작업 표시줄. 커서 바로 아래에 예약된 띠라서 창을 아무리 올려도 그 위로는
   못 간다 (raise_win). 전체 화면 모드에서는 숨는다 -- 콘솔 0의 전체 화면이
   작업 표시줄까지 덮는다는 게 계획의 결정이다. */
#define GUI64_BAR_H        28
#define GUI64_BAR_ENTRY_W 128
#define GUI64_BAR_ENTRY_H  22
#define GUI64_BAR_GAP       4

/* 바탕화면의 콘솔 실행 아이콘. */
#define GUI64_ICON_X  8
#define GUI64_ICON_Y  8
#define GUI64_ICON_W 48
#define GUI64_ICON_H 48

/* 타이틀 바 판정 (32비트 bootpack.c:349,356과 같은 범위). */
#define GUI64_TITLE_Y0  3
#define GUI64_TITLE_Y1 21
#define GUI64_CLOSE_Y0  5
#define GUI64_CLOSE_Y1 19

struct GUI64_WIN {
	struct SHEET64 *sht;
	uint8_t *buffer;
	size_t buffer_size;
	int published;
	int decorated;      /* 타이틀 바가 있으면 1 */
	int is_console;
	struct CONSOLE64 *console;  /* is_console일 때만 유효 */
	char title[GUI64_TITLE_MAX];    /* 작업 표시줄에 적을 이름 */
};

struct GUI_ALLOCATION64 {
	uintptr_t address;
	size_t size;
};

struct GUI_CONTEXT64 {
	struct SHTCTL64 *ctl;
	struct SHEET64 *back, *console, *cursor, *taskbar;
	uint8_t *back_buf, *console_buf, *taskbar_buf;
	size_t screen_size, console_buf_size, taskbar_buf_size;
	struct GUI64_WIN wins[GUI64_MAX_WINS];
	int32_t win_count;
	struct SHEET64 *key_win, *drag;
	int32_t mmx, mmy, btn_prev;
	uint8_t cursor_buf[GUI64_CURSOR_SIZE * GUI64_CURSOR_SIZE];
	struct MOUSE_DEC64 mdec;
	int32_t mx, my, scrnx, scrny, win_w, win_h, win_x, win_y;
	int mode, ready;
	/* Failed/post-commit frees retain ownership here until a later retry. */
	struct GUI_ALLOCATION64 retired[GUI64_MAX_WINS];
};

static struct GUI_CONTEXT64 gui_context64 = { .mmx = -1 };

static int gui_release_init64(void)
{
	uint8_t **buffers[] = { &gui_context64.taskbar_buf,
		&gui_context64.console_buf, &gui_context64.back_buf };
	size_t sizes[] = { gui_context64.taskbar_buf_size,
		gui_context64.console_buf_size, gui_context64.screen_size };
	struct SHEET64 *sheets[] = { gui_context64.taskbar, gui_context64.cursor,
		gui_context64.console, gui_context64.back };
	int status;

	for (size_t i = 0; i < sizeof(sheets) / sizeof(sheets[0]); i++) {
		sheet64_free(sheets[i]);
	}
	for (size_t i = 0; i < sizeof(buffers) / sizeof(buffers[0]); i++) {
		if (*buffers[i] == NULL) { continue; }
		status = memman64_free_4k(&memman64, (uintptr_t) *buffers[i], sizes[i]);
		if (status != 0) { return status; }
		*buffers[i] = NULL;
	}
	if (gui_context64.ctl != NULL) {
		if (gui_context64.ctl->map != NULL) {
			status = memman64_free_4k(&memman64, (uintptr_t) gui_context64.ctl->map,
				gui_context64.screen_size);
			if (status != 0) { return status; }
			gui_context64.ctl->map = NULL;
		}
		status = memman64_free_4k(&memman64, (uintptr_t) gui_context64.ctl,
			sizeof(struct SHTCTL64));
		if (status != 0) { return status; }
	}
	/* The decoder address is stable even if the hardware already uses it. */
	struct MOUSE_DEC64 decoder = gui_context64.mdec;
	gui_context64 = (struct GUI_CONTEXT64) { .mmx = -1, .mdec = decoder };
	return 0;
}

int gui_reclaim64(void)
{
	uint64_t irq_state = platform_irq_save64();
	int result = 0;

	if (!gui_context64.ready) {
		result = gui_release_init64();
		goto out;
	}
	for (int i = 0; i < GUI64_MAX_WINS; i++) {
		struct GUI_ALLOCATION64 *allocation = &gui_context64.retired[i];
		int status;

		if (allocation->address == 0) { continue; }
		status = memman64_free_4k(&memman64, allocation->address, allocation->size);
		if (status == 0) { *allocation = (struct GUI_ALLOCATION64) { 0 }; }
		else { result = status; }
	}
	if (gui_context64.taskbar == NULL && gui_context64.taskbar_buf != NULL) {
		int status = memman64_free_4k(&memman64, (uintptr_t) gui_context64.taskbar_buf,
			gui_context64.taskbar_buf_size);

		if (status == 0) { gui_context64.taskbar_buf = NULL; }
		else { result = status; }
	}
out:
	platform_irq_restore64(irq_state);
	return result;
}

static int gui_retire_slot64(void)
{
	(void) gui_reclaim64();
	for (int i = 0; i < GUI64_MAX_WINS; i++) {
		if (gui_context64.retired[i].address == 0) { return i; }
	}
	return -12;
}

static struct GUI64_WIN *find_console64(const struct CONSOLE64 *con)
{
	if (con == NULL) { return NULL; }
	for (int i = 0; i < gui_context64.win_count; i++) {
		if (gui_context64.wins[i].console == con) { return &gui_context64.wins[i]; }
	}
	return NULL;
}

int gui_has_console64(const struct CONSOLE64 *con)
{
	uint64_t irq_state = platform_irq_save64();
	int found = find_console64(con) != NULL;

	platform_irq_restore64(irq_state);
	return found;
}

/* src/drivers/graphic.c의 init_mouse_cursor8과 같은 비트맵. */
static void init_cursor_buf(void)
{
	static const char cursor[GUI64_CURSOR_SIZE][GUI64_CURSOR_SIZE + 1] = {
		"**************..",
		"*OOOOOOOOOOO*...",
		"*OOOOOOOOOO*....",
		"*OOOOOOOOO*.....",
		"*OOOOOOOO*......",
		"*OOOOOOO*.......",
		"*OOOOOOO*.......",
		"*OOOOOOOO*......",
		"*OOOO**OOO*.....",
		"*OOO*..*OOO*....",
		"*OO*....*OOO*...",
		"*O*......*OOO*..",
		"**........*OOO*.",
		"*..........*OOO*",
		"............*OO*",
		".............***"
	};
	int32_t x, y;

	for (y = 0; y < GUI64_CURSOR_SIZE; y++) {
		for (x = 0; x < GUI64_CURSOR_SIZE; x++) {
			if (cursor[y][x] == '*') {
				gui_context64.cursor_buf[y * GUI64_CURSOR_SIZE + x] = COL64_000000;
			} else if (cursor[y][x] == 'O') {
				gui_context64.cursor_buf[y * GUI64_CURSOR_SIZE + x] = COL64_FFFFFF;
			} else {
				gui_context64.cursor_buf[y * GUI64_CURSOR_SIZE + x] = GUI64_CURSOR_INV;
			}
		}
	}
}

static void set_title(struct GUI64_WIN *win, const char *title)
{
	/* Display titles deliberately keep the fitting prefix; rendering is bounded. */
	(void) str_copy64(win->title, sizeof(win->title), title);
}

/* 창을 맨 위로 올리되 작업 표시줄과 커서는 다시 그 위로 올린다. 예약된 띠를
   이렇게 지킨다 -- sheet64_updown은 사이에 낀 시트를 아래로 밀기 때문에
   "top-2로 올린다"만으로는 표시줄이 창 밑으로 내려간다. */
static void raise_win(struct SHEET64 *sht)
{
	sheet64_updown(sht, gui_context64.ctl->top);
	if (gui_context64.taskbar != NULL && gui_context64.taskbar->height >= 0) {
		sheet64_updown(gui_context64.taskbar, gui_context64.ctl->top);
	}
	if (gui_context64.cursor != NULL) {
		sheet64_updown(gui_context64.cursor, gui_context64.ctl->top);
	}
}

static void taskbar_entry_rect(int32_t i, int32_t *x0, int32_t *y0)
{
	*x0 = GUI64_BAR_GAP + i * (GUI64_BAR_ENTRY_W + GUI64_BAR_GAP);
	*y0 = (GUI64_BAR_H - GUI64_BAR_ENTRY_H) / 2;
}

static void taskbar_redraw(void)
{
	int32_t i, x0, y0;
	uint8_t bg, fg;
	struct SURFACE64 surface = sheet64_surface(gui_context64.taskbar);
	struct SURFACE64 text;

	if (gui_context64.taskbar == NULL || gui_context64.taskbar->height < 0) {
		return;
	}
	boxfill64(&surface, COL64_C6C6C6,
		0, 0, gui_context64.scrnx - 1, GUI64_BAR_H - 1);
	boxfill64(&surface, COL64_FFFFFF, 0, 0, gui_context64.scrnx - 1, 0);
	for (i = 0; i < gui_context64.win_count; i++) {
		taskbar_entry_rect(i, &x0, &y0);
		if (x0 + GUI64_BAR_ENTRY_W > gui_context64.scrnx) {
			break;
		}
		if (!gui_context64.wins[i].published) { continue; }
		if (gui_context64.wins[i].sht == gui_context64.key_win) {
			bg = COL64_000084;
			fg = COL64_FFFFFF;
		} else {
			bg = COL64_C6C6C6;
			fg = COL64_000000;
		}
		boxfill64(&surface, COL64_848484,
			x0, y0, x0 + GUI64_BAR_ENTRY_W - 1, y0 + GUI64_BAR_ENTRY_H - 1);
		boxfill64(&surface, bg,
			x0 + 1, y0 + 1, x0 + GUI64_BAR_ENTRY_W - 2, y0 + GUI64_BAR_ENTRY_H - 2);
		text = surface;
		text.x0 = x0 + 1;
		text.y0 = y0 + 1;
		text.x1 = x0 + GUI64_BAR_ENTRY_W - 1;
		text.y1 = y0 + GUI64_BAR_ENTRY_H - 1;
		putstr64(&text, x0 + 6, y0 + 3, fg, gui_context64.wins[i].title);
	}
	sheet64_refresh(gui_context64.taskbar, 0, 0, gui_context64.scrnx, GUI64_BAR_H);
}

static void taskbar_show(int on)
{
	if (gui_context64.taskbar == NULL) {
		return;
	}
	if (on != 0) {
		if (gui_context64.taskbar->height < 0) {
			sheet64_updown(gui_context64.taskbar, gui_context64.ctl->top);
			sheet64_updown(gui_context64.cursor, gui_context64.ctl->top);
		}
		taskbar_redraw();
	} else {
		sheet64_updown(gui_context64.taskbar, -1);
	}
}

/* 바탕화면 아이콘. 누르면 콘솔이 하나 뜬다 (`new` 명령과 같은 길). */
static void draw_desktop_icon(const struct SURFACE64 *surface)
{
	boxfill64(surface, COL64_848484,
		GUI64_ICON_X, GUI64_ICON_Y,
		GUI64_ICON_X + GUI64_ICON_W - 1, GUI64_ICON_Y + GUI64_ICON_H - 1);
	boxfill64(surface, COL64_C6C6C6,
		GUI64_ICON_X, GUI64_ICON_Y,
		GUI64_ICON_X + GUI64_ICON_W - 2, GUI64_ICON_Y + GUI64_ICON_H - 2);
	boxfill64(surface, COL64_000000,
		GUI64_ICON_X + 4, GUI64_ICON_Y + 4,
		GUI64_ICON_X + GUI64_ICON_W - 6, GUI64_ICON_Y + GUI64_ICON_H - 6);
	putstr64(surface, GUI64_ICON_X + 16, GUI64_ICON_Y + 16,
		COL64_FFFFFF, ">_");
	putstr64(surface, GUI64_ICON_X + 4,
		GUI64_ICON_Y + GUI64_ICON_H + 2, COL64_FFFFFF, "터미널");
}

struct SHEET64 *gui64_init(const struct BOOTINFO64 *boot_info)
{
	uint64_t irq_state = platform_irq_save64();
	struct SHEET64 *result = NULL;
	int32_t xsize, ysize;
	uint32_t stride;
	struct SURFACE64 surface;
	struct FRAMEBUFFER64 framebuffer;

	if (gui_context64.ready) { result = gui_context64.console; goto out; }
	if (gui_release_init64() != 0 || boot_info == NULL) { goto out; }
	if (framebuffer_boot64(&framebuffer, boot_info) != 0) { goto out; }
	xsize = framebuffer.width;
	ysize = framebuffer.height;
	stride = framebuffer.pitch;
	gui_context64.win_w = (xsize * GUI64_WIN_W_NUM / GUI64_WIN_W_DEN) & ~7;
	gui_context64.win_h = (ysize * GUI64_WIN_H_NUM / GUI64_WIN_H_DEN) & ~7;
	if (gui_context64.win_w < GUI64_PAD_W + 16 ||
		gui_context64.win_h < GUI64_PAD_H + 16) { goto fail; }
	gui_context64.scrnx = xsize;
	gui_context64.scrny = ysize;
	gui_context64.win_x = xsize * GUI64_WIN_X_NUM / GUI64_WIN_X_DEN;
	gui_context64.win_y = ysize * GUI64_WIN_Y_NUM / GUI64_WIN_Y_DEN;
	gui_context64.screen_size = (size_t) xsize * (size_t) ysize;
	gui_context64.console_buf_size = gui_context64.screen_size;
	gui_context64.taskbar_buf_size = (size_t) xsize * GUI64_BAR_H;
	gui_context64.ctl = shtctl64_init(&memman64, (uint8_t *) boot_info->vram,
		framebuffer.size,
		xsize, ysize, stride, boot_info->bpp);
	if (gui_context64.ctl == NULL) { goto fail; }
	gui_context64.back_buf = (uint8_t *) memman64_alloc_4k(&memman64,
		gui_context64.screen_size);
	if (gui_context64.back_buf == NULL) { goto fail; }
	gui_context64.console_buf = (uint8_t *) memman64_alloc_4k(&memman64,
		gui_context64.console_buf_size);
	if (gui_context64.console_buf == NULL) { goto fail; }
	gui_context64.back = sheet64_alloc(gui_context64.ctl);
	gui_context64.console = sheet64_alloc(gui_context64.ctl);
	if (gui_context64.back == NULL || gui_context64.console == NULL) { goto fail; }
	if (sheet64_setbuf(gui_context64.back, gui_context64.back_buf,
		gui_context64.screen_size, (uint32_t) xsize, xsize, ysize, -1) != 0 ||
		sheet64_setbuf(gui_context64.console, gui_context64.console_buf,
		gui_context64.console_buf_size, (uint32_t) xsize, xsize, ysize, -1) != 0) {
		goto fail;
	}
	surface = sheet64_surface(gui_context64.back);
	boxfill64(&surface, COL64_008484, 0, 0, xsize - 1, ysize - 1);
	draw_desktop_icon(&surface);
	surface = sheet64_surface(gui_context64.console);
	boxfill64(&surface, COL64_000000, 0, 0, xsize - 1, ysize - 1);
	gui_context64.cursor = sheet64_alloc(gui_context64.ctl);
	if (gui_context64.cursor == NULL) { goto fail; }
	init_cursor_buf();
	if (sheet64_setbuf(gui_context64.cursor, gui_context64.cursor_buf,
		sizeof(gui_context64.cursor_buf), GUI64_CURSOR_SIZE, GUI64_CURSOR_SIZE,
		GUI64_CURSOR_SIZE, GUI64_CURSOR_INV) != 0) { goto fail; }
	gui_context64.mx = (xsize - GUI64_CURSOR_SIZE) / 2;
	gui_context64.my = (ysize - GUI64_CURSOR_SIZE) / 2;
	gui_context64.cursor->vx0 = gui_context64.mx;
	gui_context64.cursor->vy0 = gui_context64.my;
	/* Preserve the optional taskbar policy, retaining any failed free. */
	gui_context64.taskbar_buf = (uint8_t *) memman64_alloc_4k(&memman64,
		gui_context64.taskbar_buf_size);
	if (gui_context64.taskbar_buf != NULL) {
		gui_context64.taskbar = sheet64_alloc(gui_context64.ctl);
	}
	if (gui_context64.taskbar != NULL && sheet64_setbuf(gui_context64.taskbar,
		gui_context64.taskbar_buf, gui_context64.taskbar_buf_size, (uint32_t) xsize,
		xsize, GUI64_BAR_H, -1) != 0) {
		sheet64_free(gui_context64.taskbar);
		gui_context64.taskbar = NULL;
	}
	if (gui_context64.taskbar != NULL) {
		gui_context64.taskbar->vy0 = ysize - GUI64_BAR_H;
	}
	gui_context64.wins[0] = (struct GUI64_WIN) {
		.sht = gui_context64.console, .buffer = gui_context64.console_buf,
		.buffer_size = gui_context64.console_buf_size, .published = 1, .is_console = 1
	};
	set_title(&gui_context64.wins[0], "터미널");
	gui_context64.win_count = 1;
	gui_context64.key_win = gui_context64.console;
	gui_context64.mode = GUI64_MODE_FULL;
	sheet64_updown(gui_context64.back, 0);
	sheet64_updown(gui_context64.console, 1);
	sheet64_updown(gui_context64.cursor, 2);
	gui_context64.ready = 1;
	(void) gui_reclaim64();
	result = gui_context64.console;
	goto out;
fail:
	(void) gui_release_init64();
out:
	platform_irq_restore64(irq_state);
	return result;
}

int gui64_available(void)
{
	return gui_context64.ready;
}

static struct GUI64_WIN *find_win(const struct SHEET64 *sht)
{
	int32_t i;

	if (sht == NULL) { return NULL; }
	for (i = 0; i < gui_context64.win_count; i++) {
		if (gui_context64.wins[i].sht == sht) {
			return &gui_context64.wins[i];
		}
	}
	return NULL;
}

int gui_toggle_window_checked64(void)
{
	uint64_t irq_state = platform_irq_save64();
	struct GUI64_WIN *win = find_win(gui_context64.console);
	struct SHEET64 old_sheet;
	struct SURFACE64 surface;
	struct GUI_ALLOCATION64 *allocation;
	int32_t width, height;
	uint16_t ox, oy, w, h;
	int slot, status = -22;

	if (!gui_context64.ready || win == NULL || win->console == NULL) { goto out; }
	if (win->console->render_users != 0 || win->console->close_ready ||
		win->console->slot_state == CONSOLE64_CLOSING) {
		status = -16;
		goto out;
	}
	slot = gui_retire_slot64();
	if (slot < 0) { status = slot; goto out; }
	allocation = &gui_context64.retired[slot];
	allocation->size = gui_context64.console_buf_size;
	allocation->address = memman64_alloc_4k(&memman64, allocation->size);
	if (allocation->address == 0) {
		*allocation = (struct GUI_ALLOCATION64) { 0 };
		status = -12;
		goto out;
	}
	width = gui_context64.mode == GUI64_MODE_FULL ? gui_context64.win_w :
		gui_context64.scrnx;
	height = gui_context64.mode == GUI64_MODE_FULL ? gui_context64.win_h :
		gui_context64.scrny;
	ox = gui_context64.mode == GUI64_MODE_FULL ? GUI64_PAD_X : 0;
	oy = gui_context64.mode == GUI64_MODE_FULL ? GUI64_PAD_Y : 0;
	w = (uint16_t) (width - (ox != 0 ? GUI64_PAD_W : 0));
	h = (uint16_t) (height - (oy != 0 ? GUI64_PAD_H : 0));
	surface = (struct SURFACE64) { (uint8_t *) allocation->address, allocation->size,
		(uint32_t) width, width, height, 0, 0, width, height };
	boxfill64(&surface, COL64_000000, 0, 0, width - 1, height - 1);
	if (ox != 0) {
		make_window64(&surface, "터미널", gui_context64.key_win == gui_context64.console);
	}
	old_sheet = *gui_context64.console;
	status = sheet64_setbuf(gui_context64.console, surface.pixels, surface.size,
		surface.stride, width, height, -1);
	if (status != 0) { goto release; }
	status = console_attach_sheet_checked64(win->console, gui_context64.console, ox, oy, w, h);
	if (status != 0) {
		*gui_context64.console = old_sheet;
		goto release;
	}
	/* No failing operation remains: publish once, then retire the old buffer. */
	gui_context64.console_buf = surface.pixels;
	win->buffer = surface.pixels;
	win->buffer_size = surface.size;
	gui_context64.console->vx0 = ox != 0 ? gui_context64.win_x : 0;
	gui_context64.console->vy0 = oy != 0 ? gui_context64.win_y : 0;
	gui_context64.mode = ox != 0 ? GUI64_MODE_WINDOW : GUI64_MODE_FULL;
	win->decorated = ox != 0;
	if (gui_context64.drag == gui_context64.console) {
		gui_context64.drag = NULL;
		gui_context64.mmx = -1;
	}
	allocation->address = (uintptr_t) old_sheet.buf;
	allocation->size = old_sheet.buf_size;
	taskbar_show(ox != 0);
	sheet64_refresh_all(gui_context64.ctl);
	taskbar_redraw();
release:
	(void) gui_reclaim64();
out:
	platform_irq_restore64(irq_state);
	return status;
}

void gui64_toggle_window(void)
{
	(void) gui_toggle_window_checked64();
}

static void keywin_off(struct SHEET64 *sht)
{
	struct GUI64_WIN *win = find_win(sht);

	if (win != NULL) { console_cursor_focus64(win->console, 0); }
	if (win != NULL && win->decorated != 0) {
		change_wtitle64(sht, 0);
	}
}

static void keywin_on(struct SHEET64 *sht)
{
	struct GUI64_WIN *win = find_win(sht);

	if (win != NULL) { console_cursor_focus64(win->console, 1); }
	if (win != NULL && win->decorated != 0) {
		change_wtitle64(sht, 1);
	}
}

static void focus_win(struct SHEET64 *sht)
{
	raise_win(sht);
	if (sht != gui_context64.key_win) {
		keywin_off(gui_context64.key_win);
		gui_context64.key_win = sht;
		keywin_on(gui_context64.key_win);
	}
	taskbar_redraw();
}

static void taskbar_press(int32_t bx, int32_t by)
{
	int32_t i, x0, y0;

	for (i = 0; i < gui_context64.win_count; i++) {
		if (!gui_context64.wins[i].published) { continue; }
		taskbar_entry_rect(i, &x0, &y0);
		if (x0 <= bx && bx < x0 + GUI64_BAR_ENTRY_W &&
				y0 <= by && by < y0 + GUI64_BAR_ENTRY_H) {
			focus_win(gui_context64.wins[i].sht);
			return;
		}
	}
}

/* 새 창은 콘솔 0 자리에서 조금씩 어긋나게 놓는다.
   완전히 겹치면 눈으로 확인할 수 없다. */
#define GUI64_CASCADE 24

int gui_prepare_console64(struct CONSOLE64 *con, const char *title)
{
	struct GUI_ALLOCATION64 *allocation;
	struct SHEET64 *sht;
	struct SURFACE64 surface;
	int retire_slot, status = -22;

	if (!gui_context64.ready || con == NULL || find_console64(con) != NULL ||
		gui_context64.win_count >= GUI64_MAX_WINS) { return -22; }
	retire_slot = gui_retire_slot64();
	if (retire_slot < 0) { return retire_slot; }
	allocation = &gui_context64.retired[retire_slot];
	allocation->size = (size_t) gui_context64.win_w * gui_context64.win_h;
	allocation->address = memman64_alloc_4k(&memman64, allocation->size);
	if (allocation->address == 0) {
		*allocation = (struct GUI_ALLOCATION64) { 0 };
		return -12;
	}
	sht = sheet64_alloc(gui_context64.ctl);
	if (sht == NULL) { status = -12; goto release; }
	status = sheet64_setbuf(sht, (uint8_t *) allocation->address, allocation->size,
		(uint32_t) gui_context64.win_w, gui_context64.win_w, gui_context64.win_h, -1);
	if (status != 0) { goto free_sheet; }
	surface = sheet64_surface(sht);
	make_window64(&surface, title, 1);
	status = console_attach_sheet_checked64(con, sht, GUI64_PAD_X, GUI64_PAD_Y,
		(uint16_t) (gui_context64.win_w - GUI64_PAD_W),
		(uint16_t) (gui_context64.win_h - GUI64_PAD_H));
	if (status != 0) { goto free_sheet; }
	int slot = gui_context64.win_count;
	int step = slot * GUI64_CASCADE;
	sht->vx0 = gui_context64.win_x + step;
	sht->vy0 = gui_context64.win_y + step;
	gui_context64.wins[slot] = (struct GUI64_WIN) {
		.sht = sht, .buffer = surface.pixels, .buffer_size = surface.size,
		.decorated = 1, .is_console = 1, .console = con
	};
	set_title(&gui_context64.wins[slot], title);
	gui_context64.win_count++;
	*allocation = (struct GUI_ALLOCATION64) { 0 };
	return 0;
free_sheet:
	sheet64_free(sht);
release:
	(void) gui_reclaim64();
	return status;
}

void gui_publish_console64(struct CONSOLE64 *con)
{
	struct GUI64_WIN *win = find_console64(con);

	if (win == NULL || win->published) { return; }
	win->published = 1;
	raise_win(win->sht);
	keywin_off(gui_context64.key_win);
	gui_context64.key_win = win->sht;
	keywin_on(gui_context64.key_win);
	taskbar_redraw();
}

int gui64_open_console_window(struct CONSOLE64 *con, const char *title)
{
	uint64_t irq_state = platform_irq_save64();
	int status = gui_prepare_console64(con, title);

	if (status == 0) { gui_publish_console64(con); }
	platform_irq_restore64(irq_state);
	return status == 0 ? 0 : -1;
}

static void gui64_bind_console_locked64(struct SHEET64 *sht, struct CONSOLE64 *con)
{
	struct GUI64_WIN *win = find_win(sht);

	if (win != NULL) {
		win->console = con;
		console_cursor_focus64(con, gui_context64.key_win == sht);
	}
}

static struct CONSOLE64 *gui64_focused_console_locked64(void)
{
	struct GUI64_WIN *win;

	if (!gui_context64.ready) {
		/* 컴포지터가 못 떴다 -- 콘솔이 LFB에 직접 그리는 상태다.
		   창이 없으니 포커스도 없고, 키는 부팅 콘솔로 간다. */
		return console64_active();
	}
	if (gui_context64.win_count == 0) {
		return NULL;
	}
	win = find_win(gui_context64.key_win != NULL ? gui_context64.key_win : gui_context64.console);
	if (win == NULL || !win->published || win->is_console == 0) {
		return NULL;
	}
	return win->console;
}

/* 클릭 지점에서 가장 위에 있는 시트를 찾는다. 커서 시트는 항상 맨 위이므로
   top-1부터 훑는다 (32비트 bootpack.c:337-343). 바탕화면(높이 0)까지 내려가는
   건 거기 실행 아이콘이 있기 때문이다 -- 배경인지는 호출자가 가려낸다. */
static struct SHEET64 *hit_test(int32_t mx, int32_t my, int32_t *bx, int32_t *by)
{
	struct SHEET64 *sht;
	int32_t j;
	int64_t x, y;

	for (j = gui_context64.ctl->top - 1; j >= 0; j--) {
		sht = gui_context64.ctl->sheets[j];
		x = (int64_t) mx - sht->vx0;
		y = (int64_t) my - sht->vy0;
		if (0 <= x && x < sht->bxsize && 0 <= y && y < sht->bysize) {
			if (sht->col_inv == -1 ||
					sht->buf[(size_t) y * sht->stride + (size_t) x] != (uint8_t) sht->col_inv) {
				*bx = (int32_t) x;
				*by = (int32_t) y;
				return sht;
			}
		}
	}
	return NULL;
}

/* 숨긴 창 다음으로 키를 받을 창. 보이는 창 중 가장 위. 하나도 없으면
   바탕화면이 받는다 -- 즉 아무 데도 가지 않는다. 숨은 콘솔에 몰래 넣지
   않는 게 중요하다. */
static struct SHEET64 *focus_after_hide(const struct SHEET64 *hidden)
{
	struct SHEET64 *sht;
	int32_t j;

	for (j = gui_context64.ctl->top - 1; j > 0; j--) {
		sht = gui_context64.ctl->sheets[j];
		if (sht != hidden && sht != gui_context64.cursor && find_win(sht) != NULL &&
			find_win(sht)->published) {
			return sht;
		}
	}
	return gui_context64.back;
}

static void remove_win(struct GUI64_WIN *win)
{
	int32_t i;

	for (i = (int32_t) (win - gui_context64.wins); i < gui_context64.win_count - 1; i++) {
		gui_context64.wins[i] = gui_context64.wins[i + 1];
	}
	gui_context64.win_count--;
	gui_context64.wins[gui_context64.win_count] = (struct GUI64_WIN) { 0 };
}

int gui_close_console_checked64(struct CONSOLE64 *con)
{
	uint64_t irq_state = platform_irq_save64();
	struct GUI64_WIN *win = find_console64(con);
	struct SHEET64 *sht;
	int slot, status = -2;
	int focused, primary;

	if (win == NULL) { goto out; }
	if (win->published && con->task != NULL && con->close_ready == 0) {
		status = -16;
		goto out;
	}
	slot = gui_retire_slot64();
	if (slot < 0) { status = slot; goto out; }
	status = console_stop_checked64(con);
	if (status != 0) { goto out; }
	sht = win->sht;
	focused = gui_context64.key_win == sht;
	primary = gui_context64.console == sht;
	/* Quiesce the task before either sheet visibility or ownership changes. */
	sheet64_updown(sht, -1);
	if (focused) { gui_context64.key_win = focus_after_hide(sht); }
	if (gui_context64.drag == sht) {
		gui_context64.drag = NULL;
		gui_context64.mmx = -1;
	}
	gui_context64.retired[slot] = (struct GUI_ALLOCATION64) {
		.address = (uintptr_t) win->buffer, .size = win->buffer_size
	};
	sheet64_free(sht);
	remove_win(win);
	if (primary) {
		gui_context64.console = NULL;
		gui_context64.console_buf = NULL;
		gui_context64.console_buf_size = 0;
		gui_context64.mode = GUI64_MODE_WINDOW;
		taskbar_show(1);
	}
	console_finish_destroy64(con);
	if (focused) { keywin_on(gui_context64.key_win); }
	taskbar_redraw();
	(void) gui_reclaim64();
	status = 0;
out:
	platform_irq_restore64(irq_state);
	return status;
}

void gui64_close_console(struct CONSOLE64 *con)
{
	(void) gui_close_console_checked64(con);
}

/* 닫기 버튼. 콘솔 창은 여기서 부수지 않는다 -- py나 머꼬나 앱이 돌고 있으면
   그 태스크가 쥔 전역 자물쇠(MicroPython 하나, 앱 자리 하나)를 놓지 못한 채
   죽어서 재부팅 전까지 잠긴다. 표시만 남기면 콘솔이 프롬프트로 돌아온 뒤
   스스로 접히고, 그때 커널이 gui64_close_console로 돌아온다. */
static void handle_close(struct SHEET64 *sht)
{
	struct GUI64_WIN *win = find_win(sht);

	if (win == NULL) {
		return;
	}
	if (win->is_console != 0) {
		console64_request_close(win->console);
		return;
	}
	(void) gui_close_console_checked64(win->console);
}

static void on_press(int32_t mx, int32_t my)
{
	struct SHEET64 *sht;
	int32_t bx = 0;
	int32_t by = 0;

	sht = hit_test(mx, my, &bx, &by);
	if (sht == NULL) {
		return;
	}
	if (sht == gui_context64.taskbar) {
		taskbar_press(bx, by);
		return;
	}
	if (sht == gui_context64.back) {
		/* 바탕화면은 포커스를 가져가지 않는다. 아이콘만 반응한다. */
		if (GUI64_ICON_X <= bx && bx < GUI64_ICON_X + GUI64_ICON_W &&
				GUI64_ICON_Y <= by && by < GUI64_ICON_Y + GUI64_ICON_H) {
			console64_create();
		}
		return;
	}
	raise_win(sht);
	if (sht != gui_context64.key_win) {
		keywin_off(gui_context64.key_win);
		gui_context64.key_win = sht;
		keywin_on(gui_context64.key_win);
	}
	taskbar_redraw();
	if (find_win(sht) == NULL || find_win(sht)->decorated == 0) {
		return;
	}
	if (sht->bxsize - 21 <= bx && bx < sht->bxsize - 5 &&
			GUI64_CLOSE_Y0 <= by && by < GUI64_CLOSE_Y1) {
		handle_close(sht);
		return;
	}
	if (GUI64_TITLE_Y0 <= bx && bx < sht->bxsize - GUI64_TITLE_Y0 &&
			GUI64_TITLE_Y0 <= by && by < GUI64_TITLE_Y1) {
		gui_context64.mmx = mx;
		gui_context64.mmy = my;
		gui_context64.drag = sht;
	}
}

static int32_t gui64_clip_coordinate(int64_t value, int32_t low, int32_t high)
{
	if (value < low) { return low; }
	if (value > high) { return high; }
	return (int32_t) value;
}

static void gui64_mouse_event_locked64(int32_t dx, int32_t dy, int32_t btn)
{
	int32_t left;

	if (gui_context64.cursor == NULL) {
		return;
	}
	gui_context64.mx = gui64_clip_coordinate((int64_t) gui_context64.mx + dx,
		0, gui_context64.scrnx - 1);
	gui_context64.my = gui64_clip_coordinate((int64_t) gui_context64.my + dy,
		0, gui_context64.scrny - 1);
	sheet64_slide(gui_context64.cursor, gui_context64.mx, gui_context64.my);

	left = btn & 0x01;
	if (left != 0 && gui_context64.btn_prev == 0) {
		on_press(gui_context64.mx, gui_context64.my);
	} else if (left != 0 && gui_context64.mmx >= 0 && gui_context64.drag != NULL) {
		/* 드래그: x는 32비트와 같이 4픽셀 단위로 맞춘다. */
		int32_t nx = gui64_clip_coordinate(
			((int64_t) gui_context64.drag->vx0 + gui_context64.mx - gui_context64.mmx + 2) & ~(int64_t) 3,
			INT32_MIN, INT32_MAX);
		int32_t ny = gui64_clip_coordinate(
			(int64_t) gui_context64.drag->vy0 + gui_context64.my - gui_context64.mmy, INT32_MIN, INT32_MAX);
		gui_context64.mmx = gui_context64.mx;
		gui_context64.mmy = gui_context64.my;
		sheet64_slide(gui_context64.drag, nx, ny);
	} else if (left == 0) {
		gui_context64.mmx = -1;
		gui_context64.drag = NULL;
	}
	gui_context64.btn_prev = left;
}

/* F11: 맨 아래 창을 커서 바로 아래까지 올린다 (32비트 bootpack.c:283-284). */
static void gui64_raise_bottom_window_locked64(void)
{
	if (gui_context64.ctl == NULL || gui_context64.ctl->top < 2) {
		return;
	}
	struct GUI64_WIN *win = find_win(gui_context64.ctl->sheets[1]);
	if (win != NULL && win->published) { raise_win(win->sht); }
}

/*
 * 마우스와 창 관리 키는 콘솔이 어떤 모드든 똑같이 동작해야 한다.
 * 커널 메인 루프와 raw 모드의 키 대기 루프가 둘 다 이 함수를 먼저 부른다.
 * 마우스 디코더가 여기 있는 이유: 예전에는 kernel64.c의 static이라
 * 메인 루프만 접근할 수 있었고, raw 모드에서 마우스가 그대로 멈췄다.
 */
static int gui64_handle_system_event_locked64(const struct EVENT64 *event)
{
	if (event->type == EVENT64_MOUSE) {
		if (event->data == MOUSE64_DATA_ERROR) {
			mouse64_decode_error(&gui_context64.mdec);
		} else if (mouse64_decode(&gui_context64.mdec, (uint8_t) event->data) != 0) {
			gui64_mouse_event(gui_context64.mdec.x, gui_context64.mdec.y, gui_context64.mdec.btn);
		}
		return 1;
	}
	if (event->type == EVENT64_KEYBOARD && event->data == 0x57) {   /* F11 */
		gui64_raise_bottom_window();
		return 1;
	}
	return 0;
}

struct MOUSE_DEC64 *gui64_mouse_dec(void)
{
	return &gui_context64.mdec;
}

void gui64_bind_console(struct SHEET64 *sht, struct CONSOLE64 *con)
{
	uint64_t irq_state = platform_irq_save64();
	gui64_bind_console_locked64(sht, con);
	platform_irq_restore64(irq_state);
}

struct CONSOLE64 *gui64_focused_console(void)
{
	uint64_t irq_state = platform_irq_save64();
	struct CONSOLE64 *result = gui64_focused_console_locked64();

	platform_irq_restore64(irq_state);
	return result;
}

void gui64_mouse_event(int32_t dx, int32_t dy, int32_t btn)
{
	uint64_t irq_state = platform_irq_save64();
	gui64_mouse_event_locked64(dx, dy, btn);
	platform_irq_restore64(irq_state);
}

void gui64_raise_bottom_window(void)
{
	uint64_t irq_state = platform_irq_save64();
	gui64_raise_bottom_window_locked64();
	platform_irq_restore64(irq_state);
}

int gui64_handle_system_event(const struct EVENT64 *event)
{
	uint64_t irq_state = platform_irq_save64();
	int result = gui64_handle_system_event_locked64(event);

	platform_irq_restore64(irq_state);
	return result;
}
