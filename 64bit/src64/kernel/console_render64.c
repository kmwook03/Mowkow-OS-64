/* Terminal pixels, UTF display, cursor and dirty-region/TTY output. */
#include "console_internal64.h"
#include <asmfunc64.h>
#include <arch/platform64.h>
#include <sheet64.h>
#include <mtask64.h>
#include <syscall64.h>
#include <utf864.h>
#include <window64.h>

extern const uint8_t hankaku64[4096];
/* The font has kernel lifetime; colors and the caret belong to each console. */
static const uint8_t *hangul_font;
static uint64_t cursor_ticks64;

static void console_cursor_restore64(struct CONSOLE64 *con);
static void console_cursor_show64(struct CONSOLE64 *con);

/* A renderer may be preempted after taking a surface snapshot. Lifecycle
 * operations reject busy views instead of freeing that snapshot's buffer.
 */
static int console_render_begin64(struct CONSOLE64 *con)
{
	uint64_t state = platform_irq_save64();
	int acquired = 0;

	if (con != NULL && con->slot_state != CONSOLE64_CLOSING && !con->close_ready &&
		con->render_users != UINT32_MAX) {
		if (con->render_users == 0) { console_cursor_restore64(con); }
		con->render_users++;
		acquired = 1;
	}
	platform_irq_restore64(state);
	return acquired;
}

static void console_render_end64(struct CONSOLE64 *con)
{
	uint64_t state = platform_irq_save64();

	con->render_users--;
	if (con->render_users == 0) { console_cursor_show64(con); }
	platform_irq_restore64(state);
}

static void console_flush64(struct CONSOLE64 *con, int32_t x, int32_t y, int32_t w, int32_t h)
{
	if (con->sheet == NULL) {
		return;
	}
	if (con->raw_mode != 0) {
		if (con->dirty_valid == 0) {
			con->dirty_x0 = x;
			con->dirty_y0 = y;
			con->dirty_x1 = x + w;
			con->dirty_y1 = y + h;
			con->dirty_valid = 1;
			return;
		}
		if (x < con->dirty_x0) { con->dirty_x0 = x; }
		if (y < con->dirty_y0) { con->dirty_y0 = y; }
		if (x + w > con->dirty_x1) { con->dirty_x1 = x + w; }
		if (y + h > con->dirty_y1) { con->dirty_y1 = y + h; }
		return;
	}
	sheet64_refresh(con->sheet, con->ox + x, con->oy + y,
		con->ox + x + w, con->oy + y + h);
}

static void console_flush_dirty64(struct CONSOLE64 *con)
{
	if (con->sheet == NULL || con->dirty_valid == 0) {
		return;
	}
	sheet64_refresh(con->sheet, con->ox + con->dirty_x0, con->oy + con->dirty_y0,
		con->ox + con->dirty_x1, con->oy + con->dirty_y1);
	con->dirty_valid = 0;
}

static void serial_putc64(char c)
{
#ifdef __aarch64__
	(void) c;
#else
	if (c == '\n') {
		serial_putc64('\r');
	}
	while ((io_in8(0x3f8 + 5) & 0x20) == 0) {
	}
	io_out8(0x3f8, (uint8_t) c);
#endif
}

static struct SURFACE64 console_surface64(const struct CONSOLE64 *con)
{
	struct SURFACE64 surface = {
		con->vram, con->vram_size, con->stride, con->width, con->height,
		0, 0, con->width, con->height
	};

	return surface;
}

/* All caret pixel access runs under irq-save, with no renderer in flight.
 * Restore before ANY body mutation (including scroll/preedit), then save the
 * new bottom scanline only at the outer render end. No allocation or sleep.
 */
static void console_cursor_refresh64(struct CONSOLE64 *con)
{
	if (con->sheet != NULL) {
		sheet64_refresh(con->sheet, con->ox + con->caret_saved_x,
			con->oy + con->caret_saved_y,
			con->ox + con->caret_saved_x + con->caret_saved_width,
			con->oy + con->caret_saved_y + 1);
	}
}

static void console_cursor_restore64(struct CONSOLE64 *con)
{
	if (!con->caret_drawn) { return; }
	for (uint16_t i = 0; i < con->caret_saved_width; i++) {
		con->vram[(size_t) con->caret_saved_y * con->stride +
			con->caret_saved_x + i] = con->caret_saved[i];
	}
	con->caret_drawn = 0;
	console_cursor_refresh64(con);
}

static void console_cursor_show64(struct CONSOLE64 *con)
{
	struct SURFACE64 surface = console_surface64(con);
	uint32_t x = con->cursor_x, y = con->cursor_y, width = CONSOLE64_FONT_W;

	if (con->caret_drawn || !con->caret_wanted || !con->caret_phase ||
		!con->caret_focused || con->close_ready || con->slot_state == CONSOLE64_CLOSING ||
		con->dirty_valid || !surface64_valid(&surface) ||
		con->width < CONSOLE64_FONT_W || con->height < CONSOLE64_FONT_H) { return; }
	if (con->raw_mode) {
		if (!con->caret_valid) { return; }
		x = con->caret_x;
		y = con->caret_y;
		width = con->caret_cells * CONSOLE64_FONT_W;
	} else if (con->composing.state != 0 && x >= CONSOLE64_HANGUL_W) {
		x -= CONSOLE64_HANGUL_W;
		width = CONSOLE64_HANGUL_W;
	} else if (x + width > con->width) {
		/* Preview the next line without scrolling or emitting a serial LF.
		 * At the bottom edge retain the last cell until text causes a scroll.
		 */
		x = 0;
		if (y + 2 * CONSOLE64_FONT_H <= con->height) { y += CONSOLE64_FONT_H; }
		else { x = con->width - CONSOLE64_FONT_W; }
	}
	if (x >= con->width || y + CONSOLE64_FONT_H > con->height || width == 0 ||
		width > CONSOLE64_HANGUL_W) { return; }
	if (width > con->width - x) { width = con->width - x; }
	con->caret_saved_x = (uint16_t) x;
	con->caret_saved_y = (uint16_t) (y + CONSOLE64_FONT_H - 1);
	con->caret_saved_width = (uint16_t) width;
	for (uint16_t i = 0; i < width; i++) {
		size_t offset = (size_t) con->caret_saved_y * con->stride + x + i;

		con->caret_saved[i] = con->vram[offset];
		/* Invert the low palette bits to remain visible on selection colors. */
		con->vram[offset] ^= 7U;
	}
	con->caret_drawn = 1;
	console_cursor_refresh64(con);
}

static void console_cursor_sync64(struct CONSOLE64 *con)
{
	if (con->render_users != 0) { return; }
	/* Pin the same surface as normal rendering; the surrounding IRQ guard
	 * also prevents GUI rebind/close until save/restore and refresh complete.
	 */
	con->render_users++;
	console_cursor_restore64(con);
	console_cursor_show64(con);
	con->render_users--;
}

void console_cursor_activity64(struct CONSOLE64 *con)
{
	uint64_t state = platform_irq_save64();

	con->caret_activity = cursor_ticks64;
	con->caret_phase = 1;
	console_cursor_sync64(con);
	platform_irq_restore64(state);
}

void console_cursor_wait64(struct CONSOLE64 *con, int waiting)
{
	uint64_t state = platform_irq_save64();

	con->caret_wanted = waiting != 0;
	console_cursor_activity64(con);
	platform_irq_restore64(state);
}

void console_cursor_focus64(struct CONSOLE64 *con, int focused)
{
	uint64_t state = platform_irq_save64();

	if (con != NULL) {
		con->caret_focused = focused != 0;
		console_cursor_activity64(con);
	}
	platform_irq_restore64(state);
}

void console_cursor_tick64(struct CONSOLE64 *con, uint64_t ticks)
{
	uint64_t state = platform_irq_save64();
	int phase;

	cursor_ticks64 = ticks;
	/* Unsigned elapsed time handles wrap; late updates preserve phase. */
	phase = ((ticks - con->caret_activity) / 50U & 1U) == 0;
	if (con->caret_phase != phase || (con->close_ready && con->caret_drawn)) {
		con->caret_phase = phase;
		console_cursor_sync64(con);
	}
	platform_irq_restore64(state);
}

void console_cursor_detach64(struct CONSOLE64 *con)
{
	uint64_t state = platform_irq_save64();

	console_cursor_restore64(con);
	con->caret_valid = 0;
	platform_irq_restore64(state);
}

int console64_cursor_con(struct CONSOLE64 *con, int visible, unsigned int cells)
{
	uint64_t state = platform_irq_save64();
	int status = -22;

	if (con != NULL && (visible == 0 || visible == 1) && (cells == 1 || cells == 2) &&
		con->slot_state == CONSOLE64_LIVE && !con->close_ready) {
		con->caret_wanted = visible;
		con->caret_cells = (uint16_t) cells;
		console_cursor_activity64(con);
		status = 0;
	}
	platform_irq_restore64(state);
	return status;
}

void console_reserve_preedit64(struct CONSOLE64 *con)
{
	if (!console_render_begin64(con)) { return; }
	if (con->cursor_x + CONSOLE64_HANGUL_W > con->width) { console_newline64(con); }
	con->cursor_x += CONSOLE64_HANGUL_W;
	console_draw_preedit64(con);
	console_render_end64(con);
}

static void fill_rect64(struct CONSOLE64 *con, uint16_t x, uint16_t y,
	uint16_t w, uint16_t h, uint8_t color)
{
	struct SURFACE64 surface = console_surface64(con);

	if (w == 0 || h == 0) { return; }
	draw_rect64(&surface, color, x, y, (int32_t) x + w - 1, (int32_t) y + h - 1);
	console_flush64(con, x, y, w, h);
}

static void draw_ascii64(struct CONSOLE64 *con, uint16_t x, uint16_t y, char c)
{
	const uint8_t *font;
	struct SURFACE64 surface = console_surface64(con);

	font = hankaku64 + (uint8_t) c * 16;
	draw_glyph64(&surface, x, y, con->color_fg, font, CONSOLE64_FONT_H, CONSOLE64_FONT_W);
	console_flush64(con, x, y, CONSOLE64_FONT_W, CONSOLE64_FONT_H);
}

static void scroll_if_needed64(struct CONSOLE64 *con)
{
	uint32_t row;
	uint32_t col;
	struct SURFACE64 surface = console_surface64(con);

	if (!surface64_valid(&surface) || con->height < CONSOLE64_FONT_H) {
		con->cursor_y = 0;
		return;
	}
	if (con->cursor_y + CONSOLE64_FONT_H <= con->height) {
		return;
	}
	for (row = CONSOLE64_FONT_H; row < con->height; row++) {
		for (col = 0; col < con->width; col++) {
			con->vram[(size_t) (row - CONSOLE64_FONT_H) * con->stride + col] =
				con->vram[(size_t) row * con->stride + col];
		}
	}
	for (row = con->height - CONSOLE64_FONT_H; row < con->height; row++) {
		for (col = 0; col < con->width; col++) {
			con->vram[(size_t) row * con->stride + col] = con->color_bg;
		}
	}
	con->cursor_y = con->height - CONSOLE64_FONT_H;
	console_flush64(con, 0, 0, con->width, con->height);
}

/* Preedit is a visual reservation, so removing it must not echo backspaces. */
static void console_erase_preedit_unleased64(struct CONSOLE64 *con)
{
	if (con->cursor_x < CONSOLE64_HANGUL_W) {
		return;
	}
	con->cursor_x -= CONSOLE64_HANGUL_W;
	fill_rect64(con, con->cursor_x, con->cursor_y,
		CONSOLE64_HANGUL_W, CONSOLE64_FONT_H, con->color_bg);
}

static void console_draw_preedit_unleased64(struct CONSOLE64 *con)
{
	struct SURFACE64 surface = console_surface64(con);

	if (con->cursor_x < CONSOLE64_HANGUL_W || hangul_font == NULL) {
		return;
	}
	fill_rect64(con, con->cursor_x - CONSOLE64_HANGUL_W, con->cursor_y,
		CONSOLE64_HANGUL_W, CONSOLE64_FONT_H, con->color_bg);
	hangul64_draw_johab(&surface, con->cursor_x - CONSOLE64_HANGUL_W, con->cursor_y,
		con->color_fg, hangul_font, HANGUL64_FONT_SIZE, hangul64_to_johab(&con->composing));
	console_flush64(con, con->cursor_x - CONSOLE64_HANGUL_W, con->cursor_y,
		CONSOLE64_HANGUL_W, CONSOLE64_FONT_H);
}

static void console_newline_unleased64(struct CONSOLE64 *con)
{
	con->cursor_x = 0;
	con->cursor_y += CONSOLE64_FONT_H;
	serial_putc64('\n');
	scroll_if_needed64(con);
}

static void console_put_char_unleased64(struct CONSOLE64 *con, const char *s, int len)
{
	uint32_t unicode;
	uint16_t width;
	size_t consumed;
	int error;
	int i;

	if (len <= 0) {
		return;
	}
	error = utf8_decode64(s, (size_t) len, &unicode, &consumed);
	if (len == 1 && s[0] == '\n') {
		console_newline64(con);
		return;
	}
	if (len == 1 && s[0] == '\r') {
		return;
	}
	if (len == 1 && s[0] == '\b') {
		console_erase_prev64(con, CONSOLE64_FONT_W);
		return;
	}
	width = CONSOLE64_FONT_W;
	if ((unicode >= 0xac00 && unicode <= 0xd7a3) ||
		(unicode >= 0x3131 && unicode <= 0x3163)) {
		width = CONSOLE64_HANGUL_W;
	}
	if (con->cursor_x + width > con->width) {
		console_newline64(con);
	}
	if (con->raw_mode && con->cursor_x == con->caret_x &&
		con->cursor_y == con->caret_y) {
		con->caret_cells = width / CONSOLE64_FONT_W;
	}
	fill_rect64(con, con->cursor_x, con->cursor_y, width, CONSOLE64_FONT_H, con->color_bg);
	if (width == CONSOLE64_HANGUL_W && hangul_font != NULL) {
		struct SURFACE64 surface = console_surface64(con);

		hangul64_draw_unicode(&surface, con->cursor_x, con->cursor_y,
			con->color_fg, hangul_font, HANGUL64_FONT_SIZE, unicode);
		console_flush64(con, con->cursor_x, con->cursor_y, CONSOLE64_HANGUL_W, CONSOLE64_FONT_H);
	} else if (unicode == UTF64_REPLACEMENT) {
		/* The bitmap font has no U+FFFD glyph; use its visible fallback. */
		draw_ascii64(con, con->cursor_x, con->cursor_y, '?');
	} else if (len == 1) {
		draw_ascii64(con, con->cursor_x, con->cursor_y, s[0]);
	}
	con->cursor_x += width;
	if (error != 0) {
		serial_putc64((char) 0xef);
		serial_putc64((char) 0xbf);
		serial_putc64((char) 0xbd);
		return;
	}
	for (i = 0; i < len; i++) {
		serial_putc64(s[i]);
	}
}

static void console_puts_con_unleased64(struct CONSOLE64 *con, const char *s)
{
	while (*s != '\0') {
		int len;

		utf8_to_unicode64(s, &len);
		console_put_char64(con, s, len);
		s += len;
	}
}

static void put_bytes64(struct CONSOLE64 *con, const char *s, size_t n)
{
	size_t i;
	size_t consumed;
	uint32_t unicode;

	i = 0;
	while (i < n) {
		utf8_decode64(s + i, n - i, &unicode, &consumed);
		console_put_char64(con, s + i, (int) consumed);
		i += consumed;
	}
}

static void console_erase_prev_unleased64(struct CONSOLE64 *con, uint16_t width)
{
	if (con->cursor_x < width) {
		return;
	}
	con->cursor_x -= width;
	if (con->raw_mode && con->cursor_x == con->caret_x &&
		con->cursor_y == con->caret_y) {
		con->caret_cells = width / CONSOLE64_FONT_W;
	}
	fill_rect64(con, con->cursor_x, con->cursor_y, width, CONSOLE64_FONT_H, con->color_bg);
	serial_putc64('\b');
	serial_putc64(' ');
	serial_putc64('\b');
}

static void console_prompt_unleased64(struct CONSOLE64 *con)
{
	console_puts_con64(con, "> ");
	console_cursor_wait64(con, 1);
}

void console64_prompt(void)
{
	struct CONSOLE64 *con = console_self64();

	console_prompt64(con);
}

static void console_clear_screen_unleased64(struct CONSOLE64 *con)
{
	fill_rect64(con, 0, 0, con->width, con->height, con->color_bg);
	con->cursor_x = 0;
	con->cursor_y = 0;
}

void console64_set_hangul_font(const uint8_t *font)
{
	hangul_font = font;
	window64_set_hangul_font(font);
}

const uint8_t *console64_hangul_font(void)
{
	return hangul_font;
}

int console_attach_sheet_checked64(struct CONSOLE64 *con, struct SHEET64 *sht,
	uint16_t ox, uint16_t oy, uint16_t w, uint16_t h)
{
	struct SURFACE64 surface = sheet64_surface(sht);
	size_t offset;
	uint64_t irq_state;
	int status;

	if (con == NULL || !surface64_valid(&surface) || w == 0 || h == 0 ||
		(uint32_t) ox + w > (uint32_t) surface.width ||
		(uint32_t) oy + h > (uint32_t) surface.height) {
		return -22;
	}
	irq_state = platform_irq_save64();
	if (con->slot_state == CONSOLE64_CLOSING || con->close_ready != 0 ||
		con->render_users != 0) {
		platform_irq_restore64(irq_state);
		return -16;
	}
	if (con->raw_mode != 0) {
		if (con->raw_queue_head >= CONSOLE64_RAW_QUEUE_SIZE ||
			con->raw_queue_tail >= CONSOLE64_RAW_QUEUE_SIZE) {
			platform_irq_restore64(irq_state);
			return -22;
		}
		if ((con->raw_queue_tail + 1) % CONSOLE64_RAW_QUEUE_SIZE ==
			con->raw_queue_head) {
			platform_irq_restore64(irq_state);
			return -28;
		}
		/* The task cannot execute until the complete GUI transaction commits. */
		if (con->task != NULL) {
			status = task_run64(con->task, -1, 0);
			if (status != 0) {
				platform_irq_restore64(irq_state);
				return status;
			}
		}
	}
	console_cursor_detach64(con);
	offset = (size_t) oy * surface.stride + ox;
	con->sheet = sht;
	con->ox = ox;
	con->oy = oy;
	con->vram = surface.pixels + offset;
	con->vram_size = surface.size - offset;
	con->stride = surface.stride;
	con->width = w;
	con->height = h;
	con->cursor_x = 0;
	con->cursor_y = 0;
	/* 크기가 바뀌었고 화면도 지워진다. raw 모드 앱은 TTY_READKEY에서 자고
	   있으므로 깨워 주지 않으면 다음 키를 누를 때까지 빈 화면을 본다.
	   시그널 없는 SIGWINCH가 이 이벤트다. */
	con->size_generation++;
	con->dirty_valid = 0;
	if (con->raw_mode != 0) {
		console_raw_push64(con, TTY_KIND_RESIZE, 0);
	}
	console_clear_screen64(con);
	platform_irq_restore64(irq_state);
	return 0;
}

void console64_attach_sheet(struct CONSOLE64 *con, struct SHEET64 *sht,
	uint16_t ox, uint16_t oy, uint16_t w, uint16_t h)
{
	(void) console_attach_sheet_checked64(con, sht, ox, oy, w, h);
}

void console64_puts(const char *s)
{
	console_puts_con64(console_self64(), s);
}

void console64_write(const char *s, uint64_t len)
{
	console64_write_con(console_self64(), s, len);
}

static void console64_write_con_unleased64(struct CONSOLE64 *con, const char *s, uint64_t len)
{
	put_bytes64(con, s, (size_t) len);
}

uint64_t console64_size(void)
{
	struct CONSOLE64 *con = console_self64();
	uint64_t cols;
	uint64_t rows;
	uint64_t state = platform_irq_save64();
	uint64_t result;

	cols = con->width / CONSOLE64_FONT_W;
	rows = con->height / CONSOLE64_FONT_H;
	result = cols | (rows << 16) | ((uint64_t) con->size_generation << 32);
	platform_irq_restore64(state);
	return result;
}

static void console64_move_unleased64(uint32_t row, uint32_t col)
{
	struct CONSOLE64 *con = console_self64();
	uint32_t max_col;
	uint32_t max_row;

	max_col = con->width / CONSOLE64_FONT_W;
	max_row = con->height / CONSOLE64_FONT_H;
	if (max_col == 0 || max_row == 0) {
		return;
	}
	if (col >= max_col) {
		col = max_col - 1;
	}
	if (row >= max_row) {
		row = max_row - 1;
	}
	con->cursor_x = (uint16_t) (col * CONSOLE64_FONT_W);
	con->cursor_y = (uint16_t) (row * CONSOLE64_FONT_H);
	con->caret_x = con->cursor_x;
	con->caret_y = con->cursor_y;
	con->caret_valid = 1;
	con->caret_cells = 1;
	console_cursor_activity64(con);
}

static void console64_clear_cells_unleased64(uint32_t row, uint32_t col,
	uint32_t rows, uint32_t cols)
{
	struct CONSOLE64 *con = console_self64();
	uint32_t max_col;
	uint32_t max_row;

	max_col = con->width / CONSOLE64_FONT_W;
	max_row = con->height / CONSOLE64_FONT_H;
	if (row >= max_row || col >= max_col) {
		return;
	}
	if (rows > max_row - row) {
		rows = max_row - row;
	}
	if (cols > max_col - col) {
		cols = max_col - col;
	}
	if (rows == 0 || cols == 0) {
		return;
	}
	fill_rect64(con, (uint16_t) (col * CONSOLE64_FONT_W), (uint16_t) (row * CONSOLE64_FONT_H),
		(uint16_t) (cols * CONSOLE64_FONT_W), (uint16_t) (rows * CONSOLE64_FONT_H), con->color_bg);
}

void console64_set_attr(uint8_t fg, uint8_t bg)
{
	struct CONSOLE64 *con = console_self64();
	uint64_t state = platform_irq_save64();

	con->color_fg = fg;
	con->color_bg = bg;
	platform_irq_restore64(state);
}

static void console64_flush_unleased64(void)
{
	console_flush_dirty64(console_self64());
}

void console_put_char64(struct CONSOLE64 *con, const char *s, int len)
{
	if (!console_render_begin64(con)) { return; }
	console_put_char_unleased64(con, s, len);
	console_render_end64(con);
}

void console_puts_con64(struct CONSOLE64 *con, const char *s)
{
	if (!console_render_begin64(con)) { return; }
	console_puts_con_unleased64(con, s);
	console_render_end64(con);
}

void console_newline64(struct CONSOLE64 *con)
{
	if (!console_render_begin64(con)) { return; }
	console_newline_unleased64(con);
	console_render_end64(con);
}

void console_erase_prev64(struct CONSOLE64 *con, uint16_t width)
{
	if (!console_render_begin64(con)) { return; }
	console_erase_prev_unleased64(con, width);
	console_render_end64(con);
}

void console_clear_screen64(struct CONSOLE64 *con)
{
	if (!console_render_begin64(con)) { return; }
	console_clear_screen_unleased64(con);
	console_render_end64(con);
}

void console_prompt64(struct CONSOLE64 *con)
{
	if (!console_render_begin64(con)) { return; }
	console_prompt_unleased64(con);
	console_render_end64(con);
}

void console_draw_preedit64(struct CONSOLE64 *con)
{
	if (!console_render_begin64(con)) { return; }
	console_draw_preedit_unleased64(con);
	console_render_end64(con);
}

void console_erase_preedit64(struct CONSOLE64 *con)
{
	if (!console_render_begin64(con)) { return; }
	console_erase_preedit_unleased64(con);
	console_render_end64(con);
}

void console64_write_con(struct CONSOLE64 *con, const char *s, uint64_t len)
{
	if (!console_render_begin64(con)) { return; }
	console64_write_con_unleased64(con, s, len);
	console_render_end64(con);
}

void console64_move(uint32_t row, uint32_t col)
{
	struct CONSOLE64 *con = console_self64();

	if (!console_render_begin64(con)) { return; }
	console64_move_unleased64(row, col);
	console_render_end64(con);
}

void console64_clear_cells(uint32_t row, uint32_t col, uint32_t rows, uint32_t cols)
{
	struct CONSOLE64 *con = console_self64();

	if (!console_render_begin64(con)) { return; }
	console64_clear_cells_unleased64(row, col, rows, cols);
	console_render_end64(con);
}

void console64_flush(void)
{
	struct CONSOLE64 *con = console_self64();

	if (!console_render_begin64(con)) { return; }
	console64_flush_unleased64();
	console_render_end64(con);
}
