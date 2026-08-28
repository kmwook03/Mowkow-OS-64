/*
 * 바탕화면과 콘솔 창 -- 로드맵 Phase 3 step 5.
 *
 * 컴포지터는 항상 켜져 있고 콘솔은 그 위의 창 하나다 (decision 3b). 부팅
 * 직후에는 콘솔 창이 화면 전체를 덮으므로 지금까지와 똑같아 보이고,
 * `xwindow`(또는 `창`) 명령이 바탕화면 위의 작은 창으로 바꾼다. 비디오 모드는
 * 바뀌지 않는다 -- 롱 모드에서는 VBE로 돌아갈 수 없다 (decision 2).
 */

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

#define GUI64_WIN_W   640
#define GUI64_WIN_H   400
#define GUI64_WIN_X    64
#define GUI64_WIN_Y    80
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

static struct SHTCTL64 *gui_ctl;
static struct SHEET64 *gui_back;
static struct SHEET64 *gui_console;
static uint8_t *gui_console_buf;
static size_t gui_console_buf_size;
struct GUI64_WIN {
	struct SHEET64 *sht;
	int decorated;      /* 타이틀 바가 있으면 1 */
	int is_console;
	struct CONSOLE64 *console;  /* is_console일 때만 유효 */
	char title[GUI64_TITLE_MAX];    /* 작업 표시줄에 적을 이름 */
};

static struct GUI64_WIN gui_wins[GUI64_MAX_WINS];
static int32_t gui_win_count;
static struct SHEET64 *gui_key_win;
/* 드래그 상태: mmx < 0이면 드래그 중이 아님 (32비트와 같은 관례). */
static int32_t gui_mmx = -1;
static int32_t gui_mmy;
static int32_t gui_btn_prev;
static struct SHEET64 *gui_drag;
static struct SHEET64 *gui_taskbar;
static uint8_t *gui_taskbar_buf;
static struct SHEET64 *gui_cursor;
static uint8_t gui_cursor_buf[GUI64_CURSOR_SIZE * GUI64_CURSOR_SIZE];
static struct MOUSE_DEC64 gui_mdec;
static int32_t gui_mx;
static int32_t gui_my;
static int32_t gui_scrnx;
static int32_t gui_scrny;
static int gui_mode;

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
				gui_cursor_buf[y * GUI64_CURSOR_SIZE + x] = COL64_000000;
			} else if (cursor[y][x] == 'O') {
				gui_cursor_buf[y * GUI64_CURSOR_SIZE + x] = COL64_FFFFFF;
			} else {
				gui_cursor_buf[y * GUI64_CURSOR_SIZE + x] = GUI64_CURSOR_INV;
			}
		}
	}
}

static void set_title(struct GUI64_WIN *win, const char *title)
{
	strncpy(win->title, title, GUI64_TITLE_MAX - 1);
	win->title[GUI64_TITLE_MAX - 1] = '\0';
}

/* 창을 맨 위로 올리되 작업 표시줄과 커서는 다시 그 위로 올린다. 예약된 띠를
   이렇게 지킨다 -- sheet64_updown은 사이에 낀 시트를 아래로 밀기 때문에
   "top-2로 올린다"만으로는 표시줄이 창 밑으로 내려간다. */
static void raise_win(struct SHEET64 *sht)
{
	sheet64_updown(sht, gui_ctl->top);
	if (gui_taskbar != NULL && gui_taskbar->height >= 0) {
		sheet64_updown(gui_taskbar, gui_ctl->top);
	}
	if (gui_cursor != NULL) {
		sheet64_updown(gui_cursor, gui_ctl->top);
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

	if (gui_taskbar == NULL || gui_taskbar->height < 0) {
		return;
	}
	boxfill64(gui_taskbar_buf, gui_scrnx, COL64_C6C6C6,
		0, 0, gui_scrnx - 1, GUI64_BAR_H - 1);
	boxfill64(gui_taskbar_buf, gui_scrnx, COL64_FFFFFF, 0, 0, gui_scrnx - 1, 0);
	for (i = 0; i < gui_win_count; i++) {
		taskbar_entry_rect(i, &x0, &y0);
		if (x0 + GUI64_BAR_ENTRY_W > gui_scrnx) {
			break;
		}
		if (gui_wins[i].sht == gui_key_win) {
			bg = COL64_000084;
			fg = COL64_FFFFFF;
		} else {
			bg = COL64_C6C6C6;
			fg = COL64_000000;
		}
		boxfill64(gui_taskbar_buf, gui_scrnx, COL64_848484,
			x0, y0, x0 + GUI64_BAR_ENTRY_W - 1, y0 + GUI64_BAR_ENTRY_H - 1);
		boxfill64(gui_taskbar_buf, gui_scrnx, bg,
			x0 + 1, y0 + 1, x0 + GUI64_BAR_ENTRY_W - 2, y0 + GUI64_BAR_ENTRY_H - 2);
		putstr64(gui_taskbar_buf, (uint32_t) gui_scrnx, x0 + 6, y0 + 3, fg,
			gui_wins[i].title);
	}
	sheet64_refresh(gui_taskbar, 0, 0, gui_scrnx, GUI64_BAR_H);
}

static void taskbar_show(int on)
{
	if (gui_taskbar == NULL) {
		return;
	}
	if (on != 0) {
		if (gui_taskbar->height < 0) {
			sheet64_updown(gui_taskbar, gui_ctl->top);
			sheet64_updown(gui_cursor, gui_ctl->top);
		}
		taskbar_redraw();
	} else {
		sheet64_updown(gui_taskbar, -1);
	}
}

/* 바탕화면 아이콘. 누르면 콘솔이 하나 뜬다 (`new` 명령과 같은 길). */
static void draw_desktop_icon(uint8_t *buf, int32_t stride)
{
	boxfill64(buf, stride, COL64_848484,
		GUI64_ICON_X, GUI64_ICON_Y,
		GUI64_ICON_X + GUI64_ICON_W - 1, GUI64_ICON_Y + GUI64_ICON_H - 1);
	boxfill64(buf, stride, COL64_C6C6C6,
		GUI64_ICON_X, GUI64_ICON_Y,
		GUI64_ICON_X + GUI64_ICON_W - 2, GUI64_ICON_Y + GUI64_ICON_H - 2);
	boxfill64(buf, stride, COL64_000000,
		GUI64_ICON_X + 4, GUI64_ICON_Y + 4,
		GUI64_ICON_X + GUI64_ICON_W - 6, GUI64_ICON_Y + GUI64_ICON_H - 6);
	putstr64(buf, (uint32_t) stride, GUI64_ICON_X + 16, GUI64_ICON_Y + 16,
		COL64_FFFFFF, ">_");
	putstr64(buf, (uint32_t) stride, GUI64_ICON_X + 4,
		GUI64_ICON_Y + GUI64_ICON_H + 2, COL64_FFFFFF, "터미널");
}

struct SHEET64 *gui64_init(const struct BOOTINFO64 *boot_info)
{
	uintptr_t back_addr;
	uintptr_t console_addr;
	uintptr_t bar_addr;
	size_t screen_size;
	int32_t xsize;
	int32_t ysize;
	uint32_t stride;

	xsize = boot_info->scrnx != 0 ? boot_info->scrnx : 800;
	ysize = boot_info->scrny != 0 ? boot_info->scrny : 600;
	stride = boot_info->bytes_per_scanline != 0 ?
		boot_info->bytes_per_scanline : (uint32_t) xsize;
	screen_size = (size_t) xsize * (size_t) ysize;

	gui_ctl = shtctl64_init(&memman64, (uint8_t *) boot_info->vram,
		xsize, ysize, stride);
	if (gui_ctl == NULL) {
		return NULL;
	}
	back_addr = memman64_alloc_4k(&memman64, screen_size);
	console_addr = memman64_alloc_4k(&memman64, screen_size);
	if (back_addr == 0 || console_addr == 0) {
		return NULL;
	}

	gui_scrnx = xsize;
	gui_scrny = ysize;
	gui_console_buf = (uint8_t *) console_addr;
	gui_console_buf_size = screen_size;

	gui_back = sheet64_alloc(gui_ctl);
	gui_console = sheet64_alloc(gui_ctl);
	if (gui_back == NULL || gui_console == NULL) {
		return NULL;
	}

	sheet64_setbuf(gui_back, (uint8_t *) back_addr, xsize, ysize, -1);
	boxfill64((uint8_t *) back_addr, xsize, COL64_008484, 0, 0, xsize - 1, ysize - 1);
	draw_desktop_icon((uint8_t *) back_addr, xsize);
	gui_back->vx0 = 0;
	gui_back->vy0 = 0;

	sheet64_setbuf(gui_console, gui_console_buf, xsize, ysize, -1);
	gui_console->vx0 = 0;
	gui_console->vy0 = 0;
	gui_mode = GUI64_MODE_FULL;

	gui_cursor = sheet64_alloc(gui_ctl);
	if (gui_cursor == NULL) {
		return NULL;
	}
	init_cursor_buf();
	sheet64_setbuf(gui_cursor, gui_cursor_buf, GUI64_CURSOR_SIZE, GUI64_CURSOR_SIZE,
		GUI64_CURSOR_INV);
	gui_mx = (xsize - GUI64_CURSOR_SIZE) / 2;
	gui_my = (ysize - GUI64_CURSOR_SIZE) / 2;
	gui_cursor->vx0 = gui_mx;
	gui_cursor->vy0 = gui_my;

	/* 작업 표시줄은 못 만들어도 GUI는 뜬다 -- 숨긴 창을 되살릴 길이 없을
	   뿐이다. 그래서 실패해도 NULL을 돌려주지 않는다. */
	bar_addr = memman64_alloc_4k(&memman64, (size_t) xsize * (size_t) GUI64_BAR_H);
	if (bar_addr != 0) {
		gui_taskbar = sheet64_alloc(gui_ctl);
	}
	if (gui_taskbar != NULL) {
		gui_taskbar_buf = (uint8_t *) bar_addr;
		sheet64_setbuf(gui_taskbar, gui_taskbar_buf, xsize, GUI64_BAR_H, -1);
		gui_taskbar->vx0 = 0;
		gui_taskbar->vy0 = ysize - GUI64_BAR_H;
	}

	gui_wins[0].sht = gui_console;
	gui_wins[0].decorated = 0;      /* 전체 화면일 땐 테두리가 없다 */
	gui_wins[0].is_console = 1;
	set_title(&gui_wins[0], "터미널");
	gui_win_count = 1;
	gui_key_win = gui_console;

	sheet64_updown(gui_back, 0);
	sheet64_updown(gui_console, 1);
	sheet64_updown(gui_cursor, 2);    /* 커서는 항상 맨 위 */
	return gui_console;
}

static struct GUI64_WIN *find_win(const struct SHEET64 *sht)
{
	int32_t i;

	for (i = 0; i < gui_win_count; i++) {
		if (gui_wins[i].sht == sht) {
			return &gui_wins[i];
		}
	}
	return NULL;
}

void gui64_toggle_window(void)
{
	struct GUI64_WIN *win;

	if (gui_ctl == NULL || gui_console == NULL) {
		return;
	}
	/* 콘솔 0의 창을 닫았으면 표에서 빠져 토글할 게 없다. */
	win = find_win(gui_console);
	if (win == NULL) {
		return;
	}

	if (gui_mode == GUI64_MODE_FULL) {
		sheet64_setbuf(gui_console, gui_console_buf, GUI64_WIN_W, GUI64_WIN_H, -1);
		make_window64(gui_console_buf, GUI64_WIN_W, GUI64_WIN_H, "터미널", 1);
		boxfill64(gui_console_buf, GUI64_WIN_W, COL64_000000,
			GUI64_PAD_X, GUI64_PAD_Y,
			GUI64_WIN_W - GUI64_PAD_W + GUI64_PAD_X - 1,
			GUI64_WIN_H - GUI64_PAD_H + GUI64_PAD_Y - 1);
		gui_console->vx0 = GUI64_WIN_X;
		gui_console->vy0 = GUI64_WIN_Y;
		gui_mode = GUI64_MODE_WINDOW;
		win->decorated = 1;
		taskbar_show(1);
		console64_attach_sheet(win->console, gui_console,
			GUI64_PAD_X, GUI64_PAD_Y,
			GUI64_WIN_W - GUI64_PAD_W, GUI64_WIN_H - GUI64_PAD_H);
	} else {
		sheet64_setbuf(gui_console, gui_console_buf, gui_scrnx, gui_scrny, -1);
		gui_console->vx0 = 0;
		gui_console->vy0 = 0;
		gui_mode = GUI64_MODE_FULL;
		win->decorated = 0;
		taskbar_show(0);
		console64_attach_sheet(win->console, gui_console, 0, 0,
			(uint16_t) gui_scrnx, (uint16_t) gui_scrny);
	}

	/* 시트 크기가 바뀌었으니 map 전체가 낡았다. slide로는 부족하다. */
	sheet64_refresh_all(gui_ctl);
	taskbar_redraw();
}

static void keywin_off(struct SHEET64 *sht)
{
	struct GUI64_WIN *win = find_win(sht);

	if (win != NULL && win->decorated != 0) {
		change_wtitle64(sht, 0);
	}
}

static void keywin_on(struct SHEET64 *sht)
{
	struct GUI64_WIN *win = find_win(sht);

	if (win != NULL && win->decorated != 0) {
		change_wtitle64(sht, 1);
	}
}

static void focus_win(struct SHEET64 *sht)
{
	raise_win(sht);
	if (sht != gui_key_win) {
		keywin_off(gui_key_win);
		gui_key_win = sht;
		keywin_on(gui_key_win);
	}
	taskbar_redraw();
}

static void taskbar_press(int32_t bx, int32_t by)
{
	int32_t i, x0, y0;

	for (i = 0; i < gui_win_count; i++) {
		taskbar_entry_rect(i, &x0, &y0);
		if (x0 <= bx && bx < x0 + GUI64_BAR_ENTRY_W &&
				y0 <= by && by < y0 + GUI64_BAR_ENTRY_H) {
			focus_win(gui_wins[i].sht);
			return;
		}
	}
}

/* 새 창은 콘솔 0 자리에서 조금씩 어긋나게 놓는다. 완전히 겹치면 두 개가
   떠 있는지 눈으로 확인할 수 없다. */
#define GUI64_CASCADE 24

int gui64_open_console_window(struct CONSOLE64 *con, const char *title)
{
	struct SHEET64 *sht;
	uintptr_t buf;
	int32_t slot;
	int32_t step;

	if (gui_ctl == NULL || gui_win_count >= GUI64_MAX_WINS) {
		return -1;
	}
	buf = memman64_alloc_4k(&memman64, (size_t) GUI64_WIN_W * (size_t) GUI64_WIN_H);
	if (buf == 0) {
		return -1;
	}
	sht = sheet64_alloc(gui_ctl);
	if (sht == NULL) {
		memman64_free_4k(&memman64, buf,
			(size_t) GUI64_WIN_W * (size_t) GUI64_WIN_H);
		return -1;
	}
	sheet64_setbuf(sht, (uint8_t *) buf, GUI64_WIN_W, GUI64_WIN_H, -1);
	make_window64((uint8_t *) buf, GUI64_WIN_W, GUI64_WIN_H, title, 1);
	boxfill64((uint8_t *) buf, GUI64_WIN_W, COL64_000000,
		GUI64_PAD_X, GUI64_PAD_Y,
		GUI64_WIN_W - GUI64_PAD_W + GUI64_PAD_X - 1,
		GUI64_WIN_H - GUI64_PAD_H + GUI64_PAD_Y - 1);

	slot = gui_win_count;
	step = slot * GUI64_CASCADE;
	sht->vx0 = GUI64_WIN_X + step;
	sht->vy0 = GUI64_WIN_Y + step;
	gui_wins[slot].sht = sht;
	gui_wins[slot].decorated = 1;
	gui_wins[slot].is_console = 1;
	gui_wins[slot].console = con;
	set_title(&gui_wins[slot], title);
	gui_win_count++;

	console64_attach_sheet(con, sht, GUI64_PAD_X, GUI64_PAD_Y,
		GUI64_WIN_W - GUI64_PAD_W, GUI64_WIN_H - GUI64_PAD_H);
	raise_win(sht);
	keywin_off(gui_key_win);
	gui_key_win = sht;
	keywin_on(gui_key_win);
	taskbar_redraw();
	return 0;
}

void gui64_bind_console(struct SHEET64 *sht, struct CONSOLE64 *con)
{
	struct GUI64_WIN *win = find_win(sht);

	if (win != NULL) {
		win->console = con;
	}
}

struct CONSOLE64 *gui64_focused_console(void)
{
	struct GUI64_WIN *win;

	if (gui_ctl == NULL || gui_win_count == 0) {
		/* 컴포지터가 못 떴다 -- 콘솔이 LFB에 직접 그리는 상태다.
		   창이 없으니 포커스도 없고, 키는 부팅 콘솔로 간다. */
		return console64_active();
	}
	win = find_win(gui_key_win != NULL ? gui_key_win : gui_console);
	if (win == NULL || win->is_console == 0) {
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
	int32_t j, x, y;

	for (j = gui_ctl->top - 1; j >= 0; j--) {
		sht = gui_ctl->sheets[j];
		x = mx - sht->vx0;
		y = my - sht->vy0;
		if (0 <= x && x < sht->bxsize && 0 <= y && y < sht->bysize) {
			if (sht->col_inv == -1 ||
					sht->buf[y * sht->bxsize + x] != (uint8_t) sht->col_inv) {
				*bx = x;
				*by = y;
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

	for (j = gui_ctl->top - 1; j > 0; j--) {
		sht = gui_ctl->sheets[j];
		if (sht != hidden && sht != gui_cursor && find_win(sht) != NULL) {
			return sht;
		}
	}
	return gui_back;
}

static void remove_win(struct GUI64_WIN *win)
{
	int32_t i;

	for (i = (int32_t) (win - gui_wins); i < gui_win_count - 1; i++) {
		gui_wins[i] = gui_wins[i + 1];
	}
	gui_win_count--;
}

/* 창과 콘솔을 같이 없앤다 -- 작업 표시줄 항목도, 태스크도, 콘솔 슬롯도
   사라진다. 슬롯이 비어야 다음 `new`가 그 번호를 다시 쓴다. */
static void destroy_window(struct SHEET64 *sht)
{
	struct GUI64_WIN *win = find_win(sht);

	if (win == NULL) {
		return;
	}
	keywin_off(sht);
	sheet64_updown(sht, -1);
	gui_key_win = focus_after_hide(sht);
	if (sht != gui_console) {
		memman64_free_4k(&memman64, (uintptr_t) sht->buf,
			(size_t) GUI64_WIN_W * (size_t) GUI64_WIN_H);
	}
	sheet64_free(sht);
	if (win->is_console != 0) {
		console64_destroy(win->console);
	}
	remove_win(win);
	keywin_on(gui_key_win);
	taskbar_redraw();
}

void gui64_close_console(struct CONSOLE64 *con)
{
	int32_t i;

	for (i = 0; i < gui_win_count; i++) {
		if (gui_wins[i].is_console != 0 && gui_wins[i].console == con) {
			destroy_window(gui_wins[i].sht);
			return;
		}
	}
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
	destroy_window(sht);
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
	if (sht == gui_taskbar) {
		taskbar_press(bx, by);
		return;
	}
	if (sht == gui_back) {
		/* 바탕화면은 포커스를 가져가지 않는다. 아이콘만 반응한다. */
		if (GUI64_ICON_X <= bx && bx < GUI64_ICON_X + GUI64_ICON_W &&
				GUI64_ICON_Y <= by && by < GUI64_ICON_Y + GUI64_ICON_H) {
			console64_create();
		}
		return;
	}
	raise_win(sht);
	if (sht != gui_key_win) {
		keywin_off(gui_key_win);
		gui_key_win = sht;
		keywin_on(gui_key_win);
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
		gui_mmx = mx;
		gui_mmy = my;
		gui_drag = sht;
	}
}

void gui64_mouse_event(int32_t dx, int32_t dy, int32_t btn)
{
	int32_t left;

	if (gui_cursor == NULL) {
		return;
	}
	gui_mx += dx;
	gui_my += dy;
	if (gui_mx < 0) { gui_mx = 0; }
	if (gui_my < 0) { gui_my = 0; }
	if (gui_mx > gui_scrnx - 1) { gui_mx = gui_scrnx - 1; }
	if (gui_my > gui_scrny - 1) { gui_my = gui_scrny - 1; }
	sheet64_slide(gui_cursor, gui_mx, gui_my);

	left = btn & 0x01;
	if (left != 0 && gui_btn_prev == 0) {
		on_press(gui_mx, gui_my);
	} else if (left != 0 && gui_mmx >= 0 && gui_drag != NULL) {
		/* 드래그: x는 32비트와 같이 4픽셀 단위로 맞춘다. */
		int32_t nx = (gui_drag->vx0 + (gui_mx - gui_mmx) + 2) & ~3;
		int32_t ny = gui_drag->vy0 + (gui_my - gui_mmy);
		gui_mmx = gui_mx;
		gui_mmy = gui_my;
		sheet64_slide(gui_drag, nx, ny);
	} else if (left == 0) {
		gui_mmx = -1;
		gui_drag = NULL;
	}
	gui_btn_prev = left;
}

/* F11: 맨 아래 창을 커서 바로 아래까지 올린다 (32비트 bootpack.c:283-284). */
void gui64_raise_bottom_window(void)
{
	if (gui_ctl == NULL || gui_ctl->top < 2) {
		return;
	}
	raise_win(gui_ctl->sheets[1]);
}

/*
 * 마우스와 창 관리 키는 콘솔이 어떤 모드든 똑같이 동작해야 한다.
 * 커널 메인 루프와 raw 모드의 키 대기 루프가 둘 다 이 함수를 먼저 부른다.
 * 마우스 디코더가 여기 있는 이유: 예전에는 kernel64.c의 static이라
 * 메인 루프만 접근할 수 있었고, raw 모드에서 마우스가 그대로 멈췄다.
 */
int gui64_handle_system_event(const struct EVENT64 *event)
{
	if (event->type == EVENT64_MOUSE) {
		if (mouse64_decode(&gui_mdec, (uint8_t) event->data) != 0) {
			gui64_mouse_event(gui_mdec.x, gui_mdec.y, gui_mdec.btn);
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
	return &gui_mdec;
}
