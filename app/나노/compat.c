#include "compat.h"
#include "tip.h"
#include "proto.h"
#include "../include/apilib.h"
#include "../include/mylib.h"
#include <stdio.h>
#include <stdarg.h>


int current_win = -1;
int reverse_mode = 0;

extern void *topwin;
extern void *edit;
extern void *bottomwin;

extern int cur_x;
extern int cur_y;

extern char *winbuf_global;
extern int win_width_global;

extern struct filestruct *current;

extern void check_wrap(struct filestruct *inptr);

int ctrl_pressed = 0;

int wgetch(void *win)
{
    int key;

    for (;;) {
        key = api_getkey(1);

        if (key == 0x1D) { // Ctrl Key Pressed
            ctrl_pressed = 1;
            continue;
        }
        if (ctrl_pressed) {
            if (key >= 'a' && key <= 'z') {
                // Ctrl + a(97) -> 1 (Control_A)
                // 공식: 키값 - 'a' + 1
                ctrl_pressed = 0; // Reset Ctrl State
                return key - 'a' + 1; 
            }
            if (key == 'c' || key == 'C') {
                ctrl_pressed = 0; // Reset Ctrl State
                return 3; // Control_C
            }
        }

        if (key == 0x0A) {
            return 13;
        }
        
        ctrl_pressed = 0; // Reset Ctrl State if no Ctrl combination matched
        return key;
    }
}

void *nano_malloc(int size)
{
    int *p = (int *)api_malloc(size + 4);
    if (p == 0) return 0;

    *p = size;

    return (void *)(p + 1);
}

void nano_free(void *ptr) 
{
    if (ptr == 0) return;

    int *p = ((int *)ptr) - 1;

    int size = *p;
    api_free((char *)p, size + 4);
    return;
}

void *realloc(void *ptr, int size)
{
    if (ptr == 0) return nano_malloc(size);
    int *header = ((int *)ptr) - 1;
    int old_len = *header;
    void *new_ptr = nano_malloc(size);
    if (new_ptr == 0) return 0;

    char *src = (char *)ptr;
    char *dst = (char *)new_ptr;
    int copy_len = (old_len < size) ? old_len : size;

    int i;
    for (i=0; i<copy_len; i++) dst[i] = src[i];

    if (size > old_len) dst[copy_len] = 0; // Null Terminator
    
    nano_free(ptr);

    return new_ptr;
}

void wmove(void *win, int y, int x) 
{
    int base_y = 0;

    if (win == (void *)1) {
        base_y = TOPWIN_Y_OFFSET;
    } else if (win == (void *)2) {
        base_y = EDIT_Y_OFFSET;
    } else if (win == (void *)3) {
        base_y = BOTTOMWIN_Y_OFFSET;
    }

    cur_y = base_y + y;
    cur_x = x;
}

void waddstr(void *win, char *str)
{
    if (!str) return;

    int i = 0;
    int px, py;
    int bg_col = (reverse_mode) ? 7 : 0;  
    int txt_col = (reverse_mode) ? 0 : 7;

    while (str[i] != 0) {
        px = cur_x * 8 + 8;
        py = cur_y * 16 + 28;

        unsigned char c = (unsigned char)str[i];
        int char_len = 0;
        int visual_width = 0;

        char draw_buf[5] = {0, 0, 0, 0, 0};

        if (c < 0x80) {
            char_len = 1;
            visual_width = 1;
            draw_buf[0] = c;
        } else if ((c & 0xE0) == 0xC0) {
            char_len = 2;
            visual_width = 1;
        } else if ((c & 0xF0) == 0xE0) {
            char_len = 3;
            visual_width = 2;
        } else if ((c & 0xF8) == 0xF0) {
            char_len = 4;
            visual_width = 2;
        } else {
            // Invalid UTF-8, skip
            i++;
            continue;
        }

        int k;
        int incomplete = 0;
        for (k=0; k<char_len; k++) {
            if (str[i + k] == 0) {
                incomplete = 1;
                break;
            }
            draw_buf[k] = str[i + k];
        }

        if (incomplete) break;
        
        if (winbuf_global != 0) {
            int rect_h = 16;
            int rect_w = visual_width * 8;
            int h, w;
            for (h = 0; h < rect_h; h++) {
                int offset = (py + h) * win_width_global + px;
                for (w = 0; w < rect_w; w++) {
                    winbuf_global[offset + w] = (char)bg_col;
                }
            }
        }

        api_putstrwin(current_win, px, py, txt_col, char_len, draw_buf);

        cur_x += visual_width;
        i += char_len;
    }
}

void waddch(void *win, int c) {
    char s[2];
    s[0] = (char)c;
    s[1] = 0; // Null Terminator
    waddstr(win, s);
}

void wrefresh(void *win) 
{
    api_refreshwin(current_win, 0, 0, 700, 450);
}

void wrefresh_rows(int start_row, int end_row) 
{
    int top_margin = 28; 
    int line_height = 16;
    
    int y0 = top_margin + start_row * line_height;
    int y1 = top_margin + (end_row + 1) * line_height; 

    if (y0 < 0) y0 = 0;
    if (y1 > 450) y1 = 450; 

    api_refreshwin(current_win, 0, y0, 700, y1);
}

void wattron(void *win, int attr) {
    if (attr == 1) reverse_mode = 1;
    return;
}

void wattroff(void *win, int attr) {
    if (attr == 1) reverse_mode = 0;
    return;
}

void endwin(int win) {
    api_closewin(win);
}

void redrawin(int win) {
    api_refreshwin(current_win, 0, 0, 700, 450);
}

/* 현재 커서 위치에 1바이트 삽입 (UTF-8 시퀀스 구성용) */
void insert_char_at_cursor(int key) {
    int len = (current->data == 0) ? 0 : my_strlen(current->data);
    current->data = realloc(current->data, len + 2);
    
    int i;
    // 데이터 밀기
    for(i = len; i >= current_x; i--) {
        current->data[i+1] = current->data[i];
    }
    
    current->data[current_x] = key;
    if (len == 0) current->data[1] = 0; // 널 문자 추가

    current_x++;
    return;
}

/* 현재 커서 앞의 1바이트 삭제 (백스페이스) */
void delete_char_at_cursor() {
    if (current_x <= 0) return;
    
    int len = my_strlen(current->data);
    
    // 데이터 당기기
    int i;
    for(i = current_x - 1; i < len; i++) {
        current->data[i] = current->data[i+1];
    }
    
    current->data = realloc(current->data, len); // 크기 줄임
    current_x--;
}

void nano_han_writer(const char *str, void *aux)
{
    HAN_CONTEXT *ctx = (HAN_CONTEXT *)aux;

    if (str[0] == 0x7F) {
        if (ctx->target == TARGET_EDITOR) {
            unsigned char last = (unsigned char)current->data[current_x - 1];
            if ((last & 0xC0) == 0x80) { 
                delete_char_at_cursor(); 
                delete_char_at_cursor(); 
            } else {
                delete_char_at_cursor(); 
            }
        } else {
            if (ctx->x_ptr && *(ctx->x_ptr) > 0) {
                (*(ctx->x_ptr))--;
                mvwaddstr(ctx->win, 0, *(ctx->x_ptr), " ");
                wmove(ctx->win, 0, *(ctx->x_ptr));
            }
        }
    } else {
        if (ctx->target == TARGET_EDITOR) {
            int i;
            for (i=0; str[i]!=0; i++) {
                insert_char_at_cursor((unsigned char)str[i]);
            }
            check_wrap(current);
        } else {
            if (ctx->x_ptr) {
                mvwaddstr(ctx->win, 0, *(ctx->x_ptr), (char *)str);
                int width = ((unsigned char)str[0] >= 0xE0) ? 2 : 1;
                *(ctx->x_ptr) += width;
            }
        }
    }

    if (ctx->target == TARGET_EDITOR) {
        update_line(current);
        wrefresh(edit);
    } else {
        wrefresh(ctx->win);
    }
}
