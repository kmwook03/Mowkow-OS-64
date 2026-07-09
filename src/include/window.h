// window.c
#ifndef _WINDOW_H_
#define _WINDOW_H_

struct SHEET;

void make_window8(unsigned char *buf, int xsize, int ysize, char *title, char act);
void make_wtitle8(unsigned char *buf, int xsize, char *title, char act);
void change_wtitle8(struct SHEET *sht, char act);
void putfonts8_asc_sht(struct SHEET *sht, int x, int y, int c, int b, char *s, int l);
void make_textbox8(struct SHEET *sht, int x0, int y0, int sx, int sy, int c);
void putfonts_sht(struct SHEET *sht, int x, int y, int c, int b, char *s, int l);

#endif // _WINDOW_H_
