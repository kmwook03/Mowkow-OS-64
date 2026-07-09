#ifndef _COMPAT_H_
#define _COMPAT_H_

#include "tip.h"
#include "../include/apihan.h"

#define mvwaddstr(win, y, x, str) { wmove(win, y, x); waddstr(win, str); }
#define mvwaddch(win, y, x, ch)   { wmove(win, y, x); waddch(win, ch); }

#define TOPWIN_Y_OFFSET    0
#define EDIT_Y_OFFSET      2
#define BOTTOMWIN_Y_OFFSET (LINES - 3)
#define A_REVERSE         1

extern void *topwin;
extern void *edit;
extern void *bottomwin;
extern int current_x;
extern int current_y;
extern int editwineob;
extern int editwinrows;
extern int current_win;
extern int reverse_mode;
extern filestruct *fileage;
extern filestruct *filebot;
extern filestruct *current;
extern filestruct *cutbuffer;

int wgetch(void *win);
void *nano_malloc(int size);
void nano_free(void *ptr);
void *realloc(void *ptr, int size);
void wmove(void *win, int y, int x);
void waddstr(void *win, char *str);
void waddch(void *win, int c);
void wrefresh(void *win);
void wrefresh_rows(int start_row, int end_row) ;
void wattron(void *win, int attr);
void wattroff(void *win, int attr);
void endwin(int win);
void redrawin(int win);

typedef enum {
    TARGET_EDITOR,
    TARGET_STATUSBAR
} INPUT_TARGET;

typedef struct HAN_CONTEXT {
    INPUT_TARGET target;
    void *win;
    int *x_ptr;
} HAN_CONTEXT;

void nano_han_writer(const char *str, void *aux);

#endif  /* _COMPAT_H_ */
