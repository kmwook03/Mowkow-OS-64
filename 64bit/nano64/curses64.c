#include "include/nano_curses64.h"
#include <utf864.h>

#define NANO_WINDOWS64 8
#define NANO_CELLS64 65536U

struct NANO_CELL64 { uint32_t scalar64; attr_t attrs64; int tail64; };
struct NANO_WINDOW64 {
	int used64, rows64, cols64, row64, col64, y64, x64, nonblocking64, scrolling64;
	attr_t attrs64;
	struct NANO_CELL64 *cells64;
};

int LINES, COLS;
WINDOW *stdscr, *curscr;
static WINDOW windows64[NANO_WINDOWS64];
static struct NANO_CELL64 *screen64, *presented64;
static int screen_rows64, screen_cols64, cursor_row64, cursor_col64;
static int ended64 = 1, raw_mode64, pushed64 = ERR;
static int cursor_visible64 = 1;
static char input64[5];
static int input_at64, input_size64;
static unsigned int generation64;
static uint32_t preedit64;
static unsigned long held_event64;
static int has_held_event64;
extern volatile sig_atomic_t the_window_resized;

static int valid64(WINDOW *window)
{
	for (int i = 0; i < NANO_WINDOWS64; i++) {
		if (window == &windows64[i] && windows64[i].used64) { return 1; }
	}
	errno = EINVAL; return 0;
}

WINDOW *nano_newwin64(int rows, int cols, int row, int col)
{
	if (row < 0 || col < 0 || rows <= 0 || cols <= 0 || row > LINES || col > COLS ||
		rows > LINES - row || cols > COLS - col || (size_t) rows > NANO_CELLS64 / (size_t) cols) {
		errno = EINVAL; return NULL;
	}
	for (int i = 0; i < NANO_WINDOWS64; i++) {
		WINDOW *window = &windows64[i];

		if (window->used64) { continue; }
		struct NANO_CELL64 *cells = malloc((size_t) rows * (size_t) cols * sizeof(*cells));

		if (cells == NULL) { return NULL; }
		for (int j = 0; j < rows * cols; j++) { cells[j] = (struct NANO_CELL64) { ' ', 0, 0 }; }
		*window = (WINDOW) { 1, rows, cols, row, col, 0, 0, 0, 0, 0, cells };
		return window;
	}
	errno = EMFILE; return NULL;
}

int nano_delwin64(WINDOW *window)
{
	if (!valid64(window)) { return ERR; }
	free(window->cells64); window->used64 = 0;
	return OK;
}

static int geometry64(void)
{
	unsigned long size = tty_size();
	unsigned int rows = TTY_SIZE_ROWS(size), cols = TTY_SIZE_COLS(size);

	if (rows == 0 || cols == 0 || rows > NANO_CELLS64 / cols) { errno = EOVERFLOW; return ERR; }
	struct NANO_CELL64 *cells = malloc((size_t) rows * cols * sizeof(*cells));

	if (cells == NULL) { return ERR; }
	struct NANO_CELL64 *presented = malloc((size_t) rows * cols * sizeof(*presented));

	if (presented == NULL) { free(cells); return ERR; }
	for (size_t i = 0; i < (size_t) rows * cols; i++) {
		cells[i] = (struct NANO_CELL64) { ' ', 0, 0 };
		presented[i] = (struct NANO_CELL64) { 0, 0, 0 };
	}
	free(screen64); screen64 = cells;
	free(presented64); presented64 = presented;
	screen_rows64 = LINES = (int) rows;
	screen_cols64 = COLS = (int) cols;
	generation64 = TTY_SIZE_GEN(size);
	cursor_row64 = cursor_col64 = 0;
	return OK;
}

WINDOW *nano_initscr64(void)
{
	if (screen64 != NULL || geometry64() == ERR) { return NULL; }
	stdscr = nano_newwin64(LINES, COLS, 0, 0);
	if (stdscr == NULL) {
		free(screen64); screen64 = NULL; free(presented64); presented64 = NULL; return NULL;
	}
	curscr = stdscr; ended64 = 0;
	return stdscr;
}

int nano_wmove64(WINDOW *window, int row, int col)
{
	if (!valid64(window) || row < 0 || col < 0 ||
		row >= window->rows64 || col >= window->cols64) { return ERR; }
	window->y64 = row; window->x64 = col; return OK;
}

static void erase_cell64(struct NANO_CELL64 *cells, int x, int cols, attr_t attrs)
{
	if (cells[x].tail64 && x > 0) { cells[x - 1] = (struct NANO_CELL64) { ' ', attrs, 0 }; }
	if (!cells[x].tail64 && nano_wcwidth64(cells[x].scalar64) == 2 && x + 1 < cols) {
		cells[x + 1] = (struct NANO_CELL64) { ' ', attrs, 0 };
	}
	cells[x] = (struct NANO_CELL64) { ' ', attrs, 0 };
}

static int cell64(WINDOW *window, uint32_t scalar)
{
	int width = nano_wcwidth64(scalar);
	int x = window->x64;
	struct NANO_CELL64 *row = window->cells64 + window->y64 * window->cols64;

	if (width == 0) { return OK; }
	if (width < 0) { scalar = UTF64_REPLACEMENT; width = 1; }
	if (x > window->cols64 - width) { return ERR; }
	for (int i = 0; i < width; i++) { erase_cell64(row, x + i, window->cols64, window->attrs64); }
	row[x] = (struct NANO_CELL64) { scalar, window->attrs64, 0 };
	if (width == 2) { row[x + 1] = (struct NANO_CELL64) { 0, window->attrs64, 1 }; }
	/* The limited adapter clips at the right edge; nano positions each row. */
	window->x64 = x + width;
	return OK;
}

int nano_waddnstr64(WINDOW *window, const char *text, int count)
{
	size_t remaining = 0;

	if (!valid64(window) || text == NULL) { return ERR; }
	while ((count < 0 || remaining < (size_t) count) && text[remaining] != '\0') { remaining++; }
	while (remaining != 0) {
		uint32_t scalar;
		size_t consumed;

		utf8_decode64(text, remaining, &scalar, &consumed);
		if (cell64(window, scalar) == ERR) { return ERR; }
		text += consumed; remaining -= consumed;
	}
	return OK;
}
int nano_waddch64(WINDOW *window, chtype byte)
{
	if (!valid64(window)) { return ERR; }
	attr_t saved = window->attrs64;

	window->attrs64 |= byte & A_ATTRIBUTES;
	int result = cell64(window, byte & A_CHARTEXT);
	window->attrs64 = saved; return result;
}
int nano_wclrtoeol64(WINDOW *window)
{
	if (!valid64(window)) { return ERR; }
	struct NANO_CELL64 *row = window->cells64 + window->y64 * window->cols64;

	for (int x = window->x64; x < window->cols64; x++) {
		erase_cell64(row, x, window->cols64, window->attrs64);
	}
	return OK;
}
int nano_wattr_on64(WINDOW *window, attr_t attrs, void *options)
{
	if (!valid64(window) || options != NULL) { return ERR; }
	window->attrs64 |= attrs; return OK;
}
int nano_wattr_off64(WINDOW *window, attr_t attrs, void *options)
{
	if (!valid64(window) || options != NULL) { return ERR; }
	window->attrs64 &= ~attrs; return OK;
}
int nano_wnoutrefresh64(WINDOW *window)
{
	if (!valid64(window) || screen64 == NULL || window->rows64 > screen_rows64 - window->row64 ||
		window->cols64 > screen_cols64 - window->col64) { return ERR; }
	for (int y = 0; y < window->rows64; y++) {
		memcpy(screen64 + (window->row64 + y) * screen_cols64 + window->col64,
			window->cells64 + y * window->cols64, (size_t) window->cols64 * sizeof(*screen64));
	}
	cursor_row64 = window->row64 + window->y64;
	cursor_col64 = window->col64 +
		(window->x64 < window->cols64 ? window->x64 : window->cols64 - 1);
	return OK;
}

static struct NANO_CELL64 output_cell64(int y, int x)
{
	int width = nano_wcwidth64(preedit64);

	/* Composition is output-only: window and editor bytes stay untouched. */
	if (preedit64 != 0 && y == cursor_row64 && cursor_col64 <= screen_cols64 - width) {
		if (x == cursor_col64) { return (struct NANO_CELL64) { preedit64, A_REVERSE, 0 }; }
		if (width == 2 && x == cursor_col64 + 1) {
			return (struct NANO_CELL64) { 0, A_REVERSE, 1 };
		}
		/* Composition can cover the first half of the following wide glyph. */
		if (x == cursor_col64 + width && screen64[y * screen_cols64 + x].tail64) {
			return (struct NANO_CELL64) { ' ', 0, 0 };
		}
	}
	return screen64[y * screen_cols64 + x];
}

static int changed64(struct NANO_CELL64 cell, int y, int x)
{
	struct NANO_CELL64 previous = presented64[y * screen_cols64 + x];

	return cell.scalar64 != previous.scalar64 || cell.attrs64 != previous.attrs64 ||
		cell.tail64 != previous.tail64;
}

static int draw_run64(int y, int *column)
{
	char bytes[256];
	size_t used = 0;
	int first = *column, x = first;
	attr_t attrs = output_cell64(y, x).attrs64;

	while (x < screen_cols64) {
		struct NANO_CELL64 cell = output_cell64(y, x);
		char encoded[4];
		int length;

		if (cell.tail64 || cell.attrs64 != attrs) { break; }
		length = unicode_to_utf864(cell.scalar64, encoded, sizeof(encoded));
		if (length == 0) { errno = EILSEQ; return ERR; }
		if ((size_t) length > sizeof(bytes) - used) { break; }
		memcpy(bytes + used, encoded, (size_t) length); used += (size_t) length;
		x += nano_wcwidth64(cell.scalar64) == 2 ? 2 : 1;
	}
	tty_move(y, first);
	tty_attr((attrs & A_REVERSE) ? 0 : ((attrs & A_BOLD) ? 15 : 7),
		(attrs & A_REVERSE) ? 7 : 0);
	if (nano_write64(1, bytes, used) != (long) used) { return ERR; }
	for (int at = first; at < x; at++) {
		presented64[y * screen_cols64 + at] = output_cell64(y, at);
	}
	*column = x;
	return OK;
}

static int cursor_cells64(void)
{
	uint32_t scalar = preedit64 != 0 ? preedit64 :
		screen64[cursor_row64 * screen_cols64 + cursor_col64].scalar64;
	int width = nano_wcwidth64(scalar);

	return width == 2 && cursor_col64 + 1 < screen_cols64 ? 2 : 1;
}

int nano_doupdate64(void)
{
	if (screen64 == NULL || ended64) { return ERR; }
	if (TTY_SIZE_GEN(tty_size()) != generation64) { return OK; }
	/* Hide the kernel underline while submitting body pixels. The final move
	 * publishes the application caret, independently of the output position.
	 */
	if (tty_cursor(0, 1) < 0) { errno = EIO; return ERR; }
	for (int y = 0; y < screen_rows64; y++) {
		int dirty = 0;

		for (int x = 0; x < screen_cols64 && !dirty; x++) {
			dirty = changed64(output_cell64(y, x), y, x);
		}
		if (!dirty) { continue; }
		/* Repaint changed rows in bounded attribute runs. This also preserves
		 * complete status lines in the serial drawing trace used by tools.
		 */
		for (int x = 0; x < screen_cols64;) {
			struct NANO_CELL64 cell = output_cell64(y, x);

			if (cell.tail64) { x++; continue; }
			if (draw_run64(y, &x) == ERR) { return ERR; }
		}
	}
	tty_move(cursor_row64, cursor_col64);
	if (tty_cursor(cursor_visible64, cursor_cells64()) < 0) { errno = EIO; return ERR; }
	tty_flush();
	return OK;
}
int nano_wrefresh64(WINDOW *window)
{
	if (ended64) {
		if (geometry64() == ERR) { return ERR; }
		if (stdscr != NULL) { nano_delwin64(stdscr); }
		stdscr = nano_newwin64(LINES, COLS, 0, 0); curscr = stdscr;
		if (stdscr == NULL) { return ERR; }
		window = stdscr; ended64 = 0;
	}
	return nano_wnoutrefresh64(window) == ERR ? ERR : nano_doupdate64();
}
int nano_endwin64(void)
{
	tty_cursor(0, 1);
	tty_raw(0); raw_mode64 = 0; ended64 = 1; preedit64 = 0; return OK;
}
bool nano_isendwin64(void) { return ended64 != 0; }
int nano_wscrl64(WINDOW *window, int count)
{
	if (!valid64(window) || !window->scrolling64 ||
		count < -window->rows64 || count > window->rows64) { return ERR; }
	for (int row = 0; row < window->rows64; row++) {
		int target = count < 0 ? window->rows64 - row - 1 : row;
		int source = target + count;

		for (int x = 0; x < window->cols64; x++) {
			window->cells64[target * window->cols64 + x] = source >= 0 && source < window->rows64 ?
				window->cells64[source * window->cols64 + x] : (struct NANO_CELL64) { ' ', 0, 0 };
		}
	}
	return OK;
}
int nano_wredrawln64(WINDOW *window, int row, int count)
{
	return valid64(window) && row >= 0 && count >= 0 && row <= window->rows64 &&
		count <= window->rows64 - row ? OK : ERR; /* Every refresh repaints. */
}
int nano_mvwprintw64(WINDOW *window, int row, int col, const char *format, ...)
{
	char text[1024]; va_list args;
	va_start(args, format);
	int length = nano_vsnprintf64(text, sizeof(text), format, args);
	va_end(args);
	if (length < 0 || (size_t) length >= sizeof(text) ||
		nano_wmove64(window, row, col) == ERR) { return ERR; }
	return nano_waddnstr64(window, text, length);
}
int nano_nodelay64(WINDOW *window, bool enabled)
{
	if (!valid64(window)) { return ERR; } window->nonblocking64 = enabled; return OK;
}
int nano_keypad64(WINDOW *window, bool enabled)
{
	if (!valid64(window) || !enabled) { errno = ENOSYS; return ERR; } return OK;
}
int nano_scrollok64(WINDOW *window, bool enabled)
{
	if (!valid64(window)) { return ERR; } window->scrolling64 = enabled; return OK;
}
int nano_ungetch64(int code)
{
	if (code < 0 || pushed64 != ERR) { return ERR; } pushed64 = code; return OK;
}
static int key64(unsigned int payload, unsigned int mods)
{
	/* Extended key codes are nano 9.3's definitions.h fallback values. */
	if (mods & (TTY_MOD_CTRL | TTY_MOD_ALT)) {
		int code;

		switch (payload) {
		case 0x14b: code = 1; break;
		case 0x14d: code = 2; break;
		case 0x148: code = 3; break;
		case 0x150: code = 4; break;
		case 0x147: code = 5; break;
		case 0x14f: code = 6; break;
		case 0x149: code = 7; break;
		case 0x151: code = 8; break;
		case 0x152: code = 12; break;
		case 0x153: code = 13; break;
		default: return ERR;
		}
		return 0x400 + code + (mods & TTY_MOD_ALT ? 0x20 : 0) +
			(mods & TTY_MOD_SHIFT ? 0x10 : 0);
	}
	if (mods & TTY_MOD_SHIFT) {
		switch (payload) {
		case 0x148: return 0x453;
		case 0x150: return 0x454;
		case 0x14b: return KEY_SLEFT;
		case 0x14d: return KEY_SRIGHT;
		case 0x147: return KEY_SHOME;
		case 0x14f: return KEY_SEND;
		case 0x149: return KEY_SPREVIOUS;
		case 0x151: return KEY_SNEXT;
		case 0x153: return KEY_SDC;
		default: return ERR;
		}
	}
	switch (payload) {
	case 0x148: return mods & TTY_MOD_CTRL ? 0x403 : KEY_UP;
	case 0x150: return mods & TTY_MOD_CTRL ? 0x404 : KEY_DOWN;
	case 0x14b: return mods & TTY_MOD_CTRL ? 0x401 : KEY_LEFT;
	case 0x14d: return mods & TTY_MOD_CTRL ? 0x402 : KEY_RIGHT;
	case 0x147: return KEY_HOME;
	case 0x14f: return KEY_END;
	case 0x149: return KEY_PPAGE;
	case 0x151: return KEY_NPAGE;
	case 0x152: return KEY_IC;
	case 0x153: return KEY_DC;
	default: return ERR;
	}
}
int nano_wgetch64(WINDOW *window)
{
	if (!valid64(window)) { return ERR; }
	if (pushed64 != ERR) { int code = pushed64; pushed64 = ERR; return code; }
	if (input_at64 < input_size64) { return (unsigned char) input64[input_at64++]; }
	if (window->nonblocking64) { return ERR; } /* Drain private bytes only. */
	if (nano_wrefresh64(window) == ERR) { return ERR; }
	for (;;) {
		unsigned long event = has_held_event64 ? held_event64 : tty_readkey();
		unsigned int kind = TTY_KEY_KIND(event), payload = TTY_KEY_PAYLOAD(event);
		unsigned int mods = TTY_KEY_MODS(event);

		has_held_event64 = 0;
		preedit64 = 0;
		if (kind == TTY_KIND_RESIZE || TTY_SIZE_GEN(tty_size()) != generation64) {
			/* A generation change can accompany a real key; replay that key
			 * after regenerate_screen rebuilds the surfaces.
			 */
			if (kind != TTY_KIND_RESIZE) { held_event64 = event; has_held_event64 = 1; }
			the_window_resized = 1; return ERR;
		}
		if (kind == TTY_KIND_PREEDIT) {
			if (nano_wcwidth64(payload) > 0) { preedit64 = payload; }
			if (nano_doupdate64() == ERR) { return ERR; } continue;
		}
		if (nano_doupdate64() == ERR) { return ERR; }
		if (kind == TTY_KIND_KEY) {
			int code = key64(payload, mods);

			if (code != ERR) { return code; } continue;
		}
		if (kind != TTY_KIND_CHAR) { errno = EIO; return ERR; }
		/* The raw TTY sends LF for Enter; nano's nonl input expects CR.
		 * Convert before Ctrl-key decoding so Ctrl-J remains a distinct key. */
		if (payload == '\n' && !(mods & TTY_MOD_CTRL)) { payload = '\r'; }
		if (payload == '\t' && (mods & TTY_MOD_SHIFT)) { return KEY_BTAB; }
		if ((mods & TTY_MOD_CTRL) && payload >= '@' && payload <= 127) { payload &= 31; }
		input_size64 = unicode_to_utf864(payload, input64 + 1, 4);
		if (input_size64 == 0) { errno = EILSEQ; return ERR; }
		/* A shortcut can commit the IME's preceding Unicode text while its
		 * modifier is held. That committed text is not a Meta key sequence.
		 */
		if ((mods & TTY_MOD_ALT) && payload < 128) {
			input64[0] = 27; input_size64++; input_at64 = 1; return 27;
		}
		input_at64 = 2; input_size64++; return (unsigned char) input64[1];
	}
}
int nano_raw64(void) { tty_raw(1); raw_mode64 = 1; return OK; }
int nano_noecho64(void) { return raw_mode64 ? OK : ERR; }
int nano_nonl64(void) { return raw_mode64 ? OK : ERR; }
int nano_curs_set64(int visible)
{
	if (visible < 0 || visible > 2) { errno = EINVAL; return ERR; }
	int previous = cursor_visible64;

	/* Both curses visible styles use the OS's blinking underline. */
	if (tty_cursor(visible != 0, screen64 == NULL ? 1 : cursor_cells64()) < 0) {
		errno = EIO; return ERR;
	}
	cursor_visible64 = visible != 0;
	return previous;
}
int nano_halfdelay64(int tenths) { (void) tenths; errno = ENOSYS; return ERR; }
int nano_napms64(int milliseconds) { (void) milliseconds; errno = ENOSYS; return ERR; }
int nano_typeahead64(int fd) { if (fd == -1) { return OK; } errno = ENOSYS; return ERR; }
int nano_beep64(void) { errno = ENOSYS; return ERR; }
int nano_window_row64(WINDOW *window) { return valid64(window) ? window->y64 : ERR; }
int nano_window_col64(WINDOW *window)
{
	if (!valid64(window)) { return ERR; }
	return window->x64 < window->cols64 ? window->x64 : window->cols64 - 1;
}
int nano_window_rows64(WINDOW *window) { return valid64(window) ? window->rows64 : ERR; }
int nano_window_cols64(WINDOW *window) { return valid64(window) ? window->cols64 : ERR; }
