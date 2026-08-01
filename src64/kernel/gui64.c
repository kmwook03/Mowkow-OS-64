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
#include <memory64.h>
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

/* 데모 창 -- step 9의 창 시스템 콜이 생기기 전까지 포커스 정책을 시험할
   두 번째 창이 필요해서 둔다. 창이 하나뿐이면 검증할 수 있는 게 없다. */
/* 콘솔 창(64,80)-(704,480) 아래쪽 빈 띠에 놓아 겹치지 않게 한다. */
#define GUI64_DEMO_W  300
#define GUI64_DEMO_H  105
#define GUI64_DEMO_X  450
#define GUI64_DEMO_Y  487

#define GUI64_MAX_WINS 4

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
	const uint8_t *colormap;    /* 활성일 때 걸 색표, 없으면 NULL */
};

/* 데모 창이 활성일 때 거는 색표: 기본 색 큐브를 뒤집는다.
   0-15는 예약이라 콘솔 글씨와 창 테두리는 그대로다 (decision 3c). */
static uint8_t demo_colormap[PALETTE64_APP_COUNT * 3];

static void build_demo_colormap(void)
{
	int32_t r, g, b, i;

	for (b = 0; b < 6; b++) {
		for (g = 0; g < 6; g++) {
			for (r = 0; r < 6; r++) {
				i = (r + g * 6 + b * 36) * 3;
				demo_colormap[i + 0] = (uint8_t) (255 - r * 51);
				demo_colormap[i + 1] = (uint8_t) (255 - g * 51);
				demo_colormap[i + 2] = (uint8_t) (255 - b * 51);
			}
		}
	}
}

static struct GUI64_WIN gui_wins[GUI64_MAX_WINS];
static int32_t gui_win_count;
static struct SHEET64 *gui_key_win;
static struct SHEET64 *gui_demo;
static uint8_t gui_demo_buf[GUI64_DEMO_W * GUI64_DEMO_H];
/* 드래그 상태: mmx < 0이면 드래그 중이 아님 (32비트와 같은 관례). */
static int32_t gui_mmx = -1;
static int32_t gui_mmy;
static int32_t gui_btn_prev;
static struct SHEET64 *gui_drag;
static struct SHEET64 *gui_cursor;
static uint8_t gui_cursor_buf[GUI64_CURSOR_SIZE * GUI64_CURSOR_SIZE];
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

struct SHEET64 *gui64_init(const struct BOOTINFO64 *boot_info)
{
	uintptr_t back_addr;
	uintptr_t console_addr;
	size_t screen_size;
	int32_t xsize;
	int32_t ysize;
	int32_t i;
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

	gui_demo = sheet64_alloc(gui_ctl);
	if (gui_demo == NULL) {
		return NULL;
	}
	sheet64_setbuf(gui_demo, gui_demo_buf, GUI64_DEMO_W, GUI64_DEMO_H, -1);
	make_window64(gui_demo_buf, GUI64_DEMO_W, GUI64_DEMO_H, "메모장", 0);
	/* 내용은 색 큐브(16-231)로만 그린다. 예약된 0-15만 쓰면 색표를 걸어도
	   아무것도 달라지지 않아 검증이 불가능하다. */
	boxfill64(gui_demo_buf, GUI64_DEMO_W, PALETTE64_APP_START + 215,
		4, GUI64_TITLE_Y1 + 2, GUI64_DEMO_W - 5, GUI64_DEMO_H - 5);
	putstr64(gui_demo_buf, GUI64_DEMO_W, 12, 30, PALETTE64_APP_START, "눌러서 활성화");
	for (i = 0; i < 5; i++) {
		boxfill64(gui_demo_buf, GUI64_DEMO_W,
			(uint8_t) (PALETTE64_APP_START + 1 + i),
			12 + i * 52, 58, 12 + i * 52 + 44, 92);
	}
	gui_demo->vx0 = GUI64_DEMO_X;
	gui_demo->vy0 = GUI64_DEMO_Y;

	gui_wins[0].sht = gui_console;
	gui_wins[0].decorated = 0;      /* 전체 화면일 땐 테두리가 없다 */
	gui_wins[0].is_console = 1;
	build_demo_colormap();
	gui_wins[0].colormap = NULL;
	gui_wins[1].sht = gui_demo;
	gui_wins[1].decorated = 1;
	gui_wins[1].is_console = 0;
	gui_wins[1].colormap = demo_colormap;
	gui_win_count = 2;
	gui_key_win = gui_console;

	sheet64_updown(gui_back, 0);
	sheet64_updown(gui_demo, 1);
	sheet64_updown(gui_console, 2);
	sheet64_updown(gui_cursor, 3);    /* 커서는 항상 맨 위 */
	return gui_console;
}

void gui64_toggle_window(void)
{
	if (gui_ctl == NULL || gui_console == NULL) {
		return;
	}

	if (gui_mode == GUI64_MODE_FULL) {
		sheet64_setbuf(gui_console, gui_console_buf, GUI64_WIN_W, GUI64_WIN_H, -1);
		make_window64(gui_console_buf, GUI64_WIN_W, GUI64_WIN_H, "머꼬 콘솔", 1);
		boxfill64(gui_console_buf, GUI64_WIN_W, COL64_000000,
			GUI64_PAD_X, GUI64_PAD_Y,
			GUI64_WIN_W - GUI64_PAD_W + GUI64_PAD_X - 1,
			GUI64_WIN_H - GUI64_PAD_H + GUI64_PAD_Y - 1);
		gui_console->vx0 = GUI64_WIN_X;
		gui_console->vy0 = GUI64_WIN_Y;
		gui_mode = GUI64_MODE_WINDOW;
		gui_wins[0].decorated = 1;
		console64_attach_sheet(gui_console, GUI64_PAD_X, GUI64_PAD_Y,
			GUI64_WIN_W - GUI64_PAD_W, GUI64_WIN_H - GUI64_PAD_H);
	} else {
		sheet64_setbuf(gui_console, gui_console_buf, gui_scrnx, gui_scrny, -1);
		gui_console->vx0 = 0;
		gui_console->vy0 = 0;
		gui_mode = GUI64_MODE_FULL;
		gui_wins[0].decorated = 0;
		console64_attach_sheet(gui_console, 0, 0,
			(uint16_t) gui_scrnx, (uint16_t) gui_scrny);
	}

	/* 시트 크기가 바뀌었으니 map 전체가 낡았다. slide로는 부족하다. */
	sheet64_refresh_all(gui_ctl);
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
	/* 색표 교체는 포커스를 따라간다. 창이 아니라 컴포지터가 되돌린다 --
	   색표를 건 채 사라진 창이 화면을 망가뜨리면 안 된다. */
	if (win != NULL && win->colormap != NULL) {
		palette64_install(win->colormap);
	} else {
		palette64_restore();
	}
}

int gui64_console_has_focus(void)
{
	return gui_key_win == NULL || gui_key_win == gui_console;
}

/* 클릭 지점에서 가장 위에 있는 창을 찾는다. 배경(높이 0)은 제외하고,
   커서 시트는 항상 맨 위이므로 top-1부터 훑는다 (32비트 bootpack.c:337-343). */
static struct SHEET64 *hit_test(int32_t mx, int32_t my, int32_t *bx, int32_t *by)
{
	struct SHEET64 *sht;
	int32_t j, x, y;

	for (j = gui_ctl->top - 1; j > 0; j--) {
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

static void handle_close(struct SHEET64 *sht)
{
	struct GUI64_WIN *win = find_win(sht);

	if (win == NULL) {
		return;
	}
	if (win->is_console != 0) {
		/* 32비트에서는 콘솔 창을 숨기지만, 여기서는 콘솔이 하나뿐이라
		   숨기면 키를 받을 곳이 없어진다. 전체 화면으로 되돌린다. */
		if (gui_mode == GUI64_MODE_WINDOW) {
			gui64_toggle_window();
			/* 전환하면 화면이 지워진다. xwindow 명령으로 왔을 때는
			   execute_command가 곧바로 프롬프트를 찍지만, 이 경로는
			   마우스 이벤트라 아무도 찍어 주지 않는다. 그대로 두면
			   빈 화면만 남아 멈춘 것처럼 보인다. */
			console64_prompt();
		}
		return;
	}
	keywin_off(sht);
	sheet64_updown(sht, -1);
	gui_key_win = gui_console;
	keywin_on(gui_key_win);
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
	sheet64_updown(sht, gui_ctl->top - 1);
	if (sht != gui_key_win) {
		keywin_off(gui_key_win);
		gui_key_win = sht;
		keywin_on(gui_key_win);
	}
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
	sheet64_updown(gui_ctl->sheets[1], gui_ctl->top - 1);
}
