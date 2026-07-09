#ifndef _GRAPHIC_H_
#define _GRAPHIC_H_

// graphic.c
void init_palette(void);
void set_palette(int start, int end, unsigned char *rgb);
void boxfill8(unsigned char *vram, int xsize, unsigned char c, int x0, int y0, int x1, int y1);
void init_screen8(char *vram, int x, int y);
void putfont8(char *vram, int xsize, int x, int y, char c, char *font);
void putfonts8_asc(char *vram, int xsize, int x, int y, char c, unsigned char *s);
void putfont16(char *vram, int xsize, int x, int y, char c, unsigned char *s);
void init_mouse_cursor8(char *mouse, char bc);
void putblock8_8(char *vram, int vxsize, int pxsize,
                 int pysize, int px0, int py0, char *buf, int bxsize);
void putfont(char *vram, int xsize, int x, int y, char color, unsigned char *s, int len);
void putfonts(char *vram, int xsize, int x, int y, char c, unsigned char *s);

// color constants
#define COL8_000000    0  // black
#define COL8_FF0000    1  // bright red
#define COL8_00FF00    2  // bright green
#define COL8_FFFF00    3  // bright yellow
#define COL8_0000FF    4  // bright blue
#define COL8_FF00FF    5  // bright purple
#define COL8_00FFFF    6  // bright light blue
#define COL8_FFFFFF    7  // white
#define COL8_C6C6C6    8  // light gray
#define COL8_840000    9  // dark red
#define COL8_008400   10  // dark green
#define COL8_848400   11  // dark yellow
#define COL8_000084   12  // dark blue
#define COL8_840084   13  // dark purple
#define COL8_008484   14  // dark light blue
#define COL8_848484   15  // dark gray

#endif  // _GRAPHIC_H_
