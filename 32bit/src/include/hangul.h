#ifndef _HANGUL_H_
#define _HANGUL_H_
// hangul.c

struct TASK;
struct CONSOLE;

struct HANGUL {
    int state;
    int cho, jung, jong;
    char buf[4];
};

void put_johab(unsigned char *vram, int xsize, int x, int y, char color, unsigned char *font, unsigned short code);
unsigned short utf8_to_johab(unsigned char *s);
unsigned char johab_to_utf8(unsigned char *dest, struct HANGUL hangul);
void putstr_utf8(unsigned char *vram, int xsize, int x, int y, char color, unsigned char *s);
void draw_composing_char(struct TASK *task, struct CONSOLE *cons, int x, int y);
void hangul_automata(struct CONSOLE *cons, struct TASK *task, int key, char *cmdline);
int hangul_automata_delete(struct CONSOLE *cons, struct TASK *task);
void flush_hangul_to_cmdline(struct CONSOLE *cons, struct TASK *task, char *cmdline);
void set_hangul(struct TASK *task, int state, int cho, int jung, int jong);

#endif  // _HANGUL_H_
