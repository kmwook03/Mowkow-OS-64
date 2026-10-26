/* P0: a deliberately limited curses model exercised by unmodified nano.
 * Host libc/FILE/locale remain real dependencies, not success stubs. All
 * terminal writes go through the TTY-shaped fixture below, never ncurses.
 * Opaque system WINDOW is completed privately; no ncurses state is used.
 * P4 needs an architecture-neutral header and a real CRT backend.
 */
#define _XOPEN_SOURCE 700
#include "prototypes.h"
#undef NDEBUG
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <term.h>
#include <wchar.h>

#include "utf864.h"

/* Existing SYS_TTY packed-event kinds, audited by the Python runner. */
enum { P0_CHAR64 = 1, P0_PREEDIT64 = 2, P0_KEY64 = 3, P0_RESIZE64 = 4 };

#if !NCURSES_OPAQUE
#error "P0 host experiment requires opaque WINDOW declarations"
#endif
struct _win_st {
	short _maxy, _maxx, _begy, _begx, _cury, _curx;
	attr_t _attrs;
	int _delay, _use_keypad, _scroll;
};

struct CELL64 {
	uint32_t code;
	attr_t attr;
	int continuation;
};

struct WINDOW_MODEL64 {
	WINDOW api;
	struct CELL64 *cells;
};

int LINES = 25;
int COLS = 80;
WINDOW *stdscr;
WINDOW *curscr;
static struct CELL64 screen64[60][160];
static FILE *events64;
static FILE *trace64;
static unsigned windows64, moves64, writes64, flushes64, previews64, resizes64;
static unsigned korean_cells64, reverse_cells64, cursor_calls64;
static int ended64, pushed64 = ERR;
static int screen_row64, screen_col64, cursor64;
static uint32_t preedit64;
static int raw64, echo64 = 1, newline64 = 1;

static struct WINDOW_MODEL64 *model64(WINDOW *window)
{
	return (struct WINDOW_MODEL64 *) window;
}

static uint64_t buffer_hash64(void)
{
	uint64_t hash = 14695981039346656037ULL;

	if (openfile == NULL)
		return hash;
	for (linestruct *line = openfile->filetop; line != NULL; line = line->next) {
		for (const unsigned char *p = (const unsigned char *) line->data; *p; p++)
			hash = (hash ^ *p) * 1099511628211ULL;
		hash = (hash ^ '\n') * 1099511628211ULL;
	}
	return hash;
}

static void audit64(void)
{
	assert(windows64 != 0 && moves64 != 0 && writes64 != 0 && flushes64 != 0);
	assert(previews64 >= 2 && resizes64 != 0 && korean_cells64 != 0);
	assert(reverse_cells64 != 0 && cursor_calls64 != 0);
	assert(ended64 && !raw64);
	fprintf(trace64, "PASS windows=%u moves=%u writes=%u flushes=%u preedit=%u "
		"resize=%u hangul=%u reverse=%u cursor=%u\n", windows64, moves64,
		writes64, flushes64, previews64, resizes64, korean_cells64,
		reverse_cells64, cursor_calls64);
	assert(fclose(events64) == 0);
	assert(fclose(trace64) == 0);
}

WINDOW *newwin(int rows, int cols, int row, int col)
{
	struct WINDOW_MODEL64 *model;

	if (rows <= 0 || cols <= 0 || row < 0 || col < 0 ||
		rows > LINES - row || cols > COLS - col)
		return NULL;
	model = calloc(1, sizeof(*model));
	assert(model != NULL);
	model->cells = calloc((size_t) rows * (size_t) cols, sizeof(*model->cells));
	assert(model->cells != NULL);
	model->api._maxy = (short) (rows - 1);
	model->api._maxx = (short) (cols - 1);
	model->api._begy = (short) row;
	model->api._begx = (short) col;
	model->api._delay = -1;
	windows64++;
	return &model->api;
}

int delwin(WINDOW *window)
{
	if (window == NULL)
		return ERR;
	free(model64(window)->cells);
	free(window);
	return OK;
}

WINDOW *initscr(void)
{
	const char *path = getenv("P0_EVENTS");
	const char *trace = getenv("P0_TRACE");

	assert(path != NULL && trace != NULL);
	events64 = fopen(path, "r");
	trace64 = fopen(trace, "w");
	assert(events64 != NULL && trace64 != NULL);
	assert(atexit(audit64) == 0);
	stdscr = newwin(LINES, COLS, 0, 0);
	curscr = stdscr;
	return stdscr;
}

int wmove(WINDOW *window, int row, int col)
{
	if (window == NULL || row < 0 || col < 0 || row > window->_maxy || col > window->_maxx)
		return ERR;
	window->_cury = (short) row;
	window->_curx = (short) col;
	moves64++;
	return OK;
}

static void put_cell64(WINDOW *window, uint32_t code, int width)
{
	struct CELL64 *cells = model64(window)->cells;
	int cols = window->_maxx + 1;
	int row = window->_cury;
	int col = window->_curx;

	if (width == 0)
		return;
	if (col + width > cols)
		return;
	cells[row * cols + col] = (struct CELL64) { code, window->_attrs, 0 };
	if (width == 2) {
		cells[row * cols + col + 1] = (struct CELL64) { 0, window->_attrs, 1 };
		korean_cells64 += code >= 0xac00 && code <= 0xd7a3;
	}
	if ((window->_attrs & A_REVERSE) != 0)
		reverse_cells64++;
	writes64++;
	if (col + width < cols)
		window->_curx = (short) (col + width);
}

int waddnstr(WINDOW *window, const char *text, int count)
{
	size_t remaining = 0;

	if (window == NULL || text == NULL)
		return ERR;
	while ((count < 0 || remaining < (size_t) count) && text[remaining] != '\0')
		remaining++;
	while (remaining != 0) {
		uint32_t code;
		size_t consumed;

		(void) utf8_decode64(text, remaining, &code, &consumed);
		int width = wcwidth((wchar_t) code);

		if (width < 0) {
			code = UTF64_REPLACEMENT;
			width = 1;
		}
		put_cell64(window, code, width);
		text += consumed;
		remaining -= consumed;
	}
	return OK;
}

int waddch(WINDOW *window, const chtype ch)
{
	attr_t saved = window->_attrs;

	window->_attrs |= ch & A_ATTRIBUTES;
	put_cell64(window, (uint32_t) (ch & A_CHARTEXT), 1);
	window->_attrs = saved;
	return OK;
}

int wclrtoeol(WINDOW *window)
{
	struct CELL64 *cells = model64(window)->cells;
	int cols = window->_maxx + 1;

	for (int col = window->_curx; col < cols; col++)
		cells[window->_cury * cols + col] = (struct CELL64) { ' ', window->_attrs, 0 };
	return OK;
}

int wattr_on(WINDOW *window, attr_t attrs, void *options)
{
	(void) options;
	window->_attrs |= attrs;
	return OK;
}

int wattr_off(WINDOW *window, attr_t attrs, void *options)
{
	(void) options;
	window->_attrs &= ~attrs;
	return OK;
}

int wnoutrefresh(WINDOW *window)
{
	const struct CELL64 *cells = model64(window)->cells;
	int cols = window->_maxx + 1;

	for (int row = 0; row <= window->_maxy; row++) {
		for (int col = 0; col < cols; col++)
			screen64[row + window->_begy][col + window->_begx] = cells[row * cols + col];
	}
	screen_row64 = window->_begy + window->_cury;
	screen_col64 = window->_begx + window->_curx;
	return OK;
}

int doupdate(void)
{
	/* A real backend emits dirty rows with tty_move/attr/write and flush.
	 * The fixture records complete cells; PREEDIT never enters these cells.
	 */
	flushes64++;
	if (preedit64 != 0) {
		assert(wcwidth((wchar_t) preedit64) == 2);
		assert(screen_row64 >= 0 && screen_col64 >= 0);
		assert(screen_row64 < LINES && screen_col64 + 1 < COLS);
		struct CELL64 *at = &screen64[screen_row64][screen_col64];
		struct CELL64 saved[2] = { at[0], at[1] };

		at[0] = (struct CELL64) { preedit64, A_UNDERLINE, 0 };
		at[1] = (struct CELL64) { 0, A_UNDERLINE, 1 };
		assert(at[0].code == preedit64 && at[1].continuation);
		previews64++;
		fprintf(trace64, "PREEDIT U+%04x row=%d col=%d cursor=%d\n",
			preedit64, screen_row64, screen_col64, cursor64);
		at[0] = saved[0];
		at[1] = saved[1];
	}
	return OK;
}

int wrefresh(WINDOW *window)
{
	if (ended64) {
		if (stdscr != NULL)
			delwin(stdscr);
		stdscr = newwin(LINES, COLS, 0, 0);
		curscr = stdscr;
		ended64 = 0;
		window = stdscr;
	}
	(void) wnoutrefresh(window);
	return doupdate();
}

int endwin(void)
{
	ended64 = 1;
	raw64 = 0;
	echo64 = newline64 = 1;
	return OK;
}

bool isendwin(void)
{
	return ended64 != 0;
}

int curs_set(int visibility)
{
	int previous = cursor64;

	if (visibility < 0 || visibility > 1)
		return ERR;
	cursor64 = visibility;
	cursor_calls64++;
	return previous;
}

int wscrl(WINDOW *window, int count)
{
	struct CELL64 *cells = model64(window)->cells;
	int rows = window->_maxy + 1, cols = window->_maxx + 1;

	for (int row = 0; row < rows; row++) {
		int source = count < 0 ? rows - row - 1 + count : row + count;
		int target = count < 0 ? rows - row - 1 : row;

		for (int col = 0; col < cols; col++)
			cells[target * cols + col] = source >= 0 && source < rows ?
				cells[source * cols + col] : (struct CELL64) { ' ', 0, 0 };
	}
	return OK;
}

int wredrawln(WINDOW *window, int row, int count)
{
	if (row < 0 || count < 0 || row + count > window->_maxy + 1)
		return ERR;
	/* This model always recopies every row at wnoutrefresh. */
	return OK;
}

int mvwprintw(WINDOW *window, int row, int col, const char *format, ...)
{
	char text[1024];
	va_list args;

	va_start(args, format);
	int len = vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	if (len < 0 || (size_t) len >= sizeof(text) || wmove(window, row, col) == ERR)
		return ERR;
	return waddnstr(window, text, len);
}

int nodelay(WINDOW *window, bool enabled)
{
	window->_delay = enabled ? 0 : -1;
	return OK;
}

int keypad(WINDOW *window, bool enabled)
{
	window->_use_keypad = enabled;
	return OK;
}

int scrollok(WINDOW *window, bool enabled)
{
	window->_scroll = enabled;
	return OK;
}

int ungetch(int code)
{
	if (pushed64 != ERR)
		return ERR;
	pushed64 = code;
	return OK;
}

int wgetch(WINDOW *window)
{
	static char bytes[4];
	static int byte_next, byte_count;
	unsigned kind, payload;

	if (pushed64 != ERR) {
		int code = pushed64;

		pushed64 = ERR;
		return code;
	}
	if (byte_next < byte_count)
		return (unsigned char) bytes[byte_next++];
	if (window->_delay == 0)
		return ERR;
	/* wgetch refreshes its input window, including the latest cursor move. */
	(void) wrefresh(window);
	for (;;) {
		assert(fscanf(events64, "%u %x", &kind, &payload) == 2);
		preedit64 = 0;
		if (kind == P0_PREEDIT64) {
			uint64_t before = buffer_hash64();
			void *undo = openfile->undotop;
			size_t size = openfile->totsize;

			preedit64 = payload;
			doupdate();
			assert(before == buffer_hash64());
			assert(undo == openfile->undotop && size == openfile->totsize);
			continue;
		}
		if (kind == 5) {
			static const char *const expected[] = { "", "한글", "", "한글", "한글", "가글" };

			assert(payload > 0 && payload < sizeof(expected) / sizeof(expected[0]));
			assert(strcmp(openfile->filetop->data, expected[payload]) == 0);
			if (payload == 1) {
				assert(screen64[1][0].code == 0xd55c && screen64[1][1].continuation);
				assert(screen64[1][2].code == 0xae00 && screen64[1][3].continuation);
			}
			if (payload == 4)
				assert(openfile->current_x == 0);
			fprintf(trace64, "CHECK %u text=%s x=%zu bytes=%zu\n", payload,
				openfile->filetop->data, openfile->current_x, openfile->totsize);
			fflush(trace64);
			continue;
		}
		if (kind == P0_RESIZE64) {
			COLS = (int) (payload & 0xffff);
			LINES = (int) (payload >> 16);
			assert(COLS >= 20 && COLS <= 160 && LINES >= 6 && LINES <= 60);
			the_window_resized = 1;
			resizes64++;
			return ERR;
		}
		if (kind == P0_KEY64) {
			switch (payload) {
			case 0x14b: return KEY_LEFT;
			case 0x14d: return KEY_RIGHT;
			default: abort();
			}
		}
		assert(kind == P0_CHAR64 && raw64 && !echo64 && !newline64);
		byte_count = unicode_to_utf864(payload, bytes, sizeof(bytes));
		assert(byte_count > 0);
		byte_next = 1;
		return (unsigned char) bytes[0];
	}
}

/* These calls affect the input model instead of changing a hosted terminal.
 * Unsupported terminfo/key-definition/delay requests return errors.
 */
int raw(void) { raw64 = 1; return OK; }
int noecho(void) { echo64 = 0; return OK; }
int nonl(void) { newline64 = 0; return OK; }
int halfdelay(int tenths) { (void) tenths; errno = ENOSYS; return ERR; }
int typeahead(int fd) { return fd == -1 ? OK : ERR; }
int set_escdelay(int delay) { (void) delay; errno = ENOSYS; return ERR; }
int define_key(const char *sequence, int code)
{
	(void) sequence;
	(void) code;
	return ERR;
}
int key_defined(const char *sequence) { (void) sequence; return 0; }
char *tigetstr(const char *name) { (void) name; return NULL; }
char *tgetstr(const char *name, char **area) { (void) name; (void) area; return NULL; }
int beep(void) { fputc('\a', stderr); return OK; }
int napms(int delay) { (void) delay; errno = ENOSYS; return ERR; }
