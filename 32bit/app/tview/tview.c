#include "../include/apilib.h"
#include <stdio.h>

int strtol(char *s, char **endp, int base);
char *skipspace(char *p);
void textview(int win, int w, int h, int xskip, char *p, int tab);
char *lineview(int win, int w, int y, int xskip, unsigned char *p, int tab);

void HariMain(void)
{
	// static char winbuf[1024 * 757];
	// static char txtbuf[240 * 1024];
	char *winbuf = (char *) api_malloc(1024 * 757);
	char *txtbuf = (char *) api_malloc(240 * 1024);
	
	int w = 100, h = 30, t = 4, spd_x = 1, spd_y = 1;
	int win, i, j, xskip = 0;
	char s[30], *p, *q = 0, *r = 0;

	api_cmdline(s, 30);
	for (p = s; (unsigned char)*p > ' '; p++) { }	
	for (; (unsigned char)*p != 0; ) {
		p = skipspace(p);
		if (*p == '-') {
			if (p[1] == 'w') {
				w = strtol(p + 2, &p, 0);
				if (w < 20) {
					w = 20;
				}
				if (w > 126) {
					w = 126;
				}
			} else if (p[1] == 'h') {
				h = strtol(p + 2, &p, 0);
				if (h < 1) {
					h = 1;
				}
				if (h > 45) {
					h = 45;
				}
			} else if (p[1] == 't') {
				t = strtol(p + 2, &p, 0);
				if (t < 1) {
					t = 4;
				}
			} else {
err:
				api_putstr(" >tview file [-w30 -h10 -t4]\n");
				api_end();
			}
		} else {	
			if (q != 0) {
				goto err;
			}
			q = p;
			for (; (unsigned char)*p > ' '; p++) { }
			r = p;
		}
	}
	if (q == 0) {
		goto err;
	}

	win = api_openwin(winbuf, w * 8 + 16, h * 16 + 37, -1, "tview");
	api_boxfilwin(win, 6, 27, w * 8 + 9, h * 16 + 30, 7);

	*r = 0;
	i = api_fopen(q, 0);
	if (i == 0) {
		api_putstr("file open error.\n");
		api_end();
	}
	j = api_fsize(i, 0);
	if (j >= 240 * 1024 - 1) {
		j = 240 * 1024 - 2;
	}
	txtbuf[0] = 0x0a; 
	api_fread(txtbuf + 1, j, i);
	api_fclose(i);
	txtbuf[j + 1] = 0;
	q = txtbuf + 1;
	for (p = txtbuf + 1; (unsigned char)*p != 0; p++) {
		if (*p != 0x0d) {
			*q = *p;
			q++;
		}
	}
	*q = 0;


	p = txtbuf + 1;
	for (;;) {
		textview(win, w, h, xskip, p, t);
		i = api_getkey(1);
		if (i == 'Q' || i == 'q') {
			api_end();
		}
		if ('A' <= i && i <= 'F') {
			spd_x = 1 << (i - 'A');	
		}
		if ('a' <= i && i <= 'f') {
			spd_y = 1 << (i - 'a');
		}
		if (i == '<' && t > 1) {
			t /= 2;
		}
		if (i == '>' && t < 256) {
			t *= 2;
		}
		if (i == '4') {
			for (;;) {
				xskip -= spd_x;
				if (xskip < 0) {
					xskip = 0;
				}
				if (api_getkey(0) != '4') {
					break;
				}
			}
		}
		if (i == '6') {
			for (;;) {
				xskip += spd_x;
				if (api_getkey(0) != '6') {
					break;
				}
			}
		}
		if (i == '8') {
			for (;;) {
				for (j = 0; j < spd_y; j++) {
					if (p == txtbuf + 1) {
						break;
					}
					for (p--; p[-1] != 0x0a; p--) { }
				}
				if (api_getkey(0) != '8') {
					break;
				}
			}
		}
		if (i == '2') {
			for (;;) {
				for (j = 0; j < spd_y; j++) {
					for (q = p; *q != 0 && *q != 0x0a; q++) { }
					if (*q == 0) {
						break;
					}
					p = q + 1;
				}
				if (api_getkey(0) != '2') {
					break;
				}
			}
		}
	}
}

char *skipspace(char *p)
{
    for (; *p==' '; p++) { }
    return p;
}

void textview(int win, int w, int h, int xskip, char *p, int tab)
{
    int i;
    api_boxfilwin(win + 1, 8, 29, w * 8 + 7, h * 16 + 28, 7);
    for (i=0; i<h; i++) {
        p = lineview(win, w, i * 16 + 29, xskip, (unsigned char *)p, tab);
    }
    api_refreshwin(win, 8, 29, w * 8 + 8, h * 16 + 29);
    return;
}

char *lineview(int win, int w, int y, int xskip, unsigned char *p, int tab)
{
    int vx = -xskip;    // Visual X
    int buffer_idx = 0; 
    char s[512];        

    int char_width; 
    int char_bytes; 
    int i;

    for (;;) {
        if (*p == 0) {
            break;
        }
        if (*p == 0x0a) { 
            p++;          
            break;        
        }

        if (*p == 0x09) { // 탭
            int space_len = tab - ((vx + xskip) % tab);
            char_width = space_len;
            char_bytes = 1;
        } else if ((*p & 0x80) == 0) { // ASCII
            char_width = 1;
            char_bytes = 1;
        } else if ((*p & 0xE0) == 0xE0) { // 한글(3byte)
            char_width = 2;
            char_bytes = 3;
        } else if ((*p & 0xC0) == 0xC0) { // 2byte 문자
            char_width = 1;
            char_bytes = 2;
        } else {
            char_width = 1;
            char_bytes = 1;
        }

        if (vx + char_width > w) {
            break; 
        }

        if (*p == 0x09) {
            for (i = 0; i < char_width; i++) {
                if (vx >= 0 && vx < w) {
                    s[buffer_idx++] = ' ';
                }
                vx++;
            }
            p++;
        } else {
            if (vx >= 0) {
                for (i = 0; i < char_bytes; i++) {
                    s[buffer_idx++] = p[i];
                }
            }
            vx += char_width;
            p += char_bytes;
        }
    }

    if (buffer_idx > 0) {
        s[buffer_idx] = 0;
        api_putstrwin(win + 1, 8, y, 0, buffer_idx, s);
    }

    return (char *)p; 
}
