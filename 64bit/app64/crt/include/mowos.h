#ifndef MOWKOW_APP64_MOWOS_H
#define MOWKOW_APP64_MOWOS_H

typedef unsigned long size_t;

#define O_CREAT 1
#define O_TRUNC 2

long write(int fd, const void *buf, size_t len);
long read(int fd, void *buf, size_t len);
int open(const char *path, int flags);
int close(int fd);
long seek(int fd, long offset, int whence);
void exit(int status) __attribute__((noreturn));
void *alloc(size_t size);
void free_alloc(void *ptr, size_t size);
void *malloc(size_t size);
void free(void *ptr);
unsigned long ticks(void);

/* raw 모드: 콘솔 줄 편집과 에코를 끄고 키 이벤트를 직접 받는다. */
void tty_raw(int on);
unsigned long tty_readkey(void);
unsigned long tty_size(void);
void tty_move(int row, int col);
void tty_clear(int row, int col, int rows, int cols);
void tty_attr(int fg, int bg);
/* raw 모드에서는 그린 것이 여기서야 화면에 올라간다 (curses의 doupdate). */
void tty_flush(void);

/* 칸 너비는 8픽셀. 한글 한 글자는 두 칸을 쓴다.
   세대 값이 달라졌으면 크기가 바뀌고 화면이 지워진 것이니 다시 그려야 한다. */
#define TTY_SIZE_COLS(v) ((unsigned int) ((v) & 0xffff))
#define TTY_SIZE_ROWS(v) ((unsigned int) (((v) >> 16) & 0xffff))
#define TTY_SIZE_GEN(v)  ((unsigned int) (((v) >> 32) & 0xffffffffUL))

#define TTY_KEY_PAYLOAD(v) ((unsigned int) ((v) & 0xffffffffUL))
#define TTY_KEY_KIND(v)    ((unsigned int) (((v) >> 32) & 0xff))
#define TTY_KEY_MODS(v)    ((unsigned int) (((v) >> 40) & 0xff))

#define TTY_KIND_CHAR    1
#define TTY_KIND_PREEDIT 2
#define TTY_KIND_KEY     3
#define TTY_KIND_RESIZE  4

#define TTY_MOD_SHIFT 0x01
#define TTY_MOD_CTRL  0x02
#define TTY_MOD_ALT   0x04

/* TTY_KIND_KEY의 payload 값 (커널 keyboard64.h와 같은 값) */
#define KEY_UP     0x148
#define KEY_DOWN   0x150
#define KEY_LEFT   0x14b
#define KEY_RIGHT  0x14d
#define KEY_HOME   0x147
#define KEY_END    0x14f
#define KEY_PGUP   0x149
#define KEY_PGDN   0x151
#define KEY_INSERT 0x152
#define KEY_DELETE 0x153

size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t size);
void *memcpy(void *dst, const void *src, size_t size);
void *memset(void *dst, int value, size_t size);
int memcmp(const void *a, const void *b, size_t size);
int puts(const char *s);

#endif
