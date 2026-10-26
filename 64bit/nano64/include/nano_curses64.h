#ifndef MOWKOW64_NANO_CURSES64_H
#define MOWKOW64_NANO_CURSES64_H

#include "nano_posix64.h"
#define WINDOW struct NANO_WINDOW64
struct NANO_WINDOW64;
typedef uint32_t chtype;
typedef uint32_t attr_t;
#define TRUE true
#define FALSE false
#define OK 0
#define ERR (-1)
#define A_NORMAL 0U
#define A_REVERSE 0x10000U
#define A_BOLD 0x20000U
#define A_ATTRIBUTES 0xffff0000U
#define A_CHARTEXT 0xffU
#define NCURSES_VERSION_MAJOR 0
#define NCURSES_VERSION_MINOR 0
#define NCURSES_VERSION_PATCH 0
#define KEY_MAX 0x1ff
#define KEY_F0 0x110
#define KEY_F(n) (KEY_F0 + (n))
#ifdef KEY_DOWN
#undef KEY_DOWN
#endif
#define KEY_DOWN 384
#ifdef KEY_UP
#undef KEY_UP
#endif
#define KEY_UP 385
#ifdef KEY_LEFT
#undef KEY_LEFT
#endif
#define KEY_LEFT 386
#ifdef KEY_RIGHT
#undef KEY_RIGHT
#endif
#define KEY_RIGHT 387
#ifdef KEY_HOME
#undef KEY_HOME
#endif
#define KEY_HOME 388
#ifdef KEY_BACKSPACE
#undef KEY_BACKSPACE
#endif
#define KEY_BACKSPACE 389
#ifdef KEY_DC
#undef KEY_DC
#endif
#define KEY_DC 390
#ifdef KEY_IC
#undef KEY_IC
#endif
#define KEY_IC 391
#ifdef KEY_NPAGE
#undef KEY_NPAGE
#endif
#define KEY_NPAGE 392
#ifdef KEY_PPAGE
#undef KEY_PPAGE
#endif
#define KEY_PPAGE 393
#ifdef KEY_END
#undef KEY_END
#endif
#define KEY_END 394
#ifdef KEY_ENTER
#undef KEY_ENTER
#endif
#define KEY_ENTER 395
#ifdef KEY_BTAB
#undef KEY_BTAB
#endif
#define KEY_BTAB 396
#ifdef KEY_BEG
#undef KEY_BEG
#endif
#define KEY_BEG 397
#ifdef KEY_EOL
#undef KEY_EOL
#endif
#define KEY_EOL 398
#ifdef KEY_CANCEL
#undef KEY_CANCEL
#endif
#define KEY_CANCEL 399
#ifdef KEY_A1
#undef KEY_A1
#endif
#define KEY_A1 400
#ifdef KEY_A3
#undef KEY_A3
#endif
#define KEY_A3 401
#ifdef KEY_B2
#undef KEY_B2
#endif
#define KEY_B2 402
#ifdef KEY_C1
#undef KEY_C1
#endif
#define KEY_C1 403
#ifdef KEY_C3
#undef KEY_C3
#endif
#define KEY_C3 404
#ifdef KEY_SBEG
#undef KEY_SBEG
#endif
#define KEY_SBEG 405
#ifdef KEY_SCANCEL
#undef KEY_SCANCEL
#endif
#define KEY_SCANCEL 406
#ifdef KEY_SDC
#undef KEY_SDC
#endif
#define KEY_SDC 407
#ifdef KEY_SEND
#undef KEY_SEND
#endif
#define KEY_SEND 408
#ifdef KEY_SF
#undef KEY_SF
#endif
#define KEY_SF 409
#ifdef KEY_SHOME
#undef KEY_SHOME
#endif
#define KEY_SHOME 410
#ifdef KEY_SIC
#undef KEY_SIC
#endif
#define KEY_SIC 411
#ifdef KEY_SLEFT
#undef KEY_SLEFT
#endif
#define KEY_SLEFT 412
#ifdef KEY_SNEXT
#undef KEY_SNEXT
#endif
#define KEY_SNEXT 413
#ifdef KEY_SPREVIOUS
#undef KEY_SPREVIOUS
#endif
#define KEY_SPREVIOUS 414
#ifdef KEY_SR
#undef KEY_SR
#endif
#define KEY_SR 415
#ifdef KEY_SRIGHT
#undef KEY_SRIGHT
#endif
#define KEY_SRIGHT 416
#ifdef KEY_SSUSPEND
#undef KEY_SSUSPEND
#endif
#define KEY_SSUSPEND 417
#ifdef KEY_SUSPEND
#undef KEY_SUSPEND
#endif
#define KEY_SUSPEND 418
#ifdef KEY_MOUSE
#undef KEY_MOUSE
#endif
#define KEY_MOUSE 419
#ifdef KEY_RESIZE
#undef KEY_RESIZE
#endif
#define KEY_RESIZE 420

extern int LINES, COLS;
extern WINDOW *stdscr, *curscr;
int nano_window_row64(WINDOW *window);
int nano_window_col64(WINDOW *window);
int nano_window_rows64(WINDOW *window);
int nano_window_cols64(WINDOW *window);
#define getcury(w) nano_window_row64(w)
#define getcurx(w) nano_window_col64(w)
#define getmaxy(w) nano_window_rows64(w)
#define getmaxx(w) nano_window_cols64(w)
#define getyx(w, y, x) do { (y) = getcury(w); (x) = getcurx(w); } while (0)
#define refresh() nano_wrefresh64(stdscr)
#define waddstr(w, s) nano_waddnstr64(w, s, -1)
#define mvwaddstr(w, y, x, s) mvwaddnstr(w, y, x, s, -1)
#define mvwaddnstr(w, y, x, s, n) (nano_wmove64(w, y, x) == ERR ? ERR : nano_waddnstr64(w, s, n))
#define mvwaddch(w, y, x, c) (nano_wmove64(w, y, x) == ERR ? ERR : nano_waddch64(w, c))
#define wattron(w, a) nano_wattr_on64(w, a, NULL)
#define wattroff(w, a) nano_wattr_off64(w, a, NULL)
#define newwin nano_newwin64
WINDOW *nano_newwin64(int rows, int cols, int row, int col);
#define delwin nano_delwin64
int nano_delwin64(WINDOW *window);
#define initscr nano_initscr64
WINDOW *nano_initscr64(void);
#define wmove nano_wmove64
int nano_wmove64(WINDOW *window, int row, int col);
#define waddnstr nano_waddnstr64
int nano_waddnstr64(WINDOW *window, const char *text, int count);
#define waddch nano_waddch64
int nano_waddch64(WINDOW *window, chtype byte);
#define wclrtoeol nano_wclrtoeol64
int nano_wclrtoeol64(WINDOW *window);
#define wattr_on nano_wattr_on64
int nano_wattr_on64(WINDOW *window, attr_t attrs, void *options);
#define wattr_off nano_wattr_off64
int nano_wattr_off64(WINDOW *window, attr_t attrs, void *options);
#define wnoutrefresh nano_wnoutrefresh64
int nano_wnoutrefresh64(WINDOW *window);
#define wrefresh nano_wrefresh64
int nano_wrefresh64(WINDOW *window);
#define doupdate nano_doupdate64
int nano_doupdate64(void);
#define endwin nano_endwin64
int nano_endwin64(void);
#define isendwin nano_isendwin64
bool nano_isendwin64(void);
#define wscrl nano_wscrl64
int nano_wscrl64(WINDOW *window, int count);
#define wredrawln nano_wredrawln64
int nano_wredrawln64(WINDOW *window, int row, int count);
#define mvwprintw nano_mvwprintw64
int nano_mvwprintw64(WINDOW *window, int row, int col, const char *format, ...);
#define nodelay nano_nodelay64
int nano_nodelay64(WINDOW *window, bool enabled);
#define keypad nano_keypad64
int nano_keypad64(WINDOW *window, bool enabled);
#define scrollok nano_scrollok64
int nano_scrollok64(WINDOW *window, bool enabled);
#define ungetch nano_ungetch64
int nano_ungetch64(int code);
#define wgetch nano_wgetch64
int nano_wgetch64(WINDOW *window);
#define raw nano_raw64
int nano_raw64(void);
#define noecho nano_noecho64
int nano_noecho64(void);
#define nonl nano_nonl64
int nano_nonl64(void);
#define curs_set nano_curs_set64
int nano_curs_set64(int visible);
#define halfdelay nano_halfdelay64
int nano_halfdelay64(int tenths);
#define napms nano_napms64
int nano_napms64(int milliseconds);
#define typeahead nano_typeahead64
int nano_typeahead64(int fd);
#define beep nano_beep64
int nano_beep64(void);

#endif
