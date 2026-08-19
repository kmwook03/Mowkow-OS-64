#ifndef MOWKOW64_CONSOLE64_H
#define MOWKOW64_CONSOLE64_H

#include <stdint.h>

struct BOOTINFO64;
struct FIFO64;
struct SHEET64;

void console64_set_hangul_font(const uint8_t *font);
const uint8_t *console64_hangul_font(void);
void console64_attach_sheet(struct SHEET64 *sht, uint16_t ox, uint16_t oy,
	uint16_t w, uint16_t h);
void console64_init(const struct BOOTINFO64 *boot_info);
void console64_puts(const char *s);
void console64_prompt(void);
void console64_write(const char *s, uint64_t len);
uint64_t console64_read(char *dst, uint64_t len);
void console64_process_key(uint16_t scancode);
void console64_set_raw(int on);
/* 한글 오토마타 회귀 확인. 통과하면 1. */
int console64_hangul_smoke(void);
int console64_is_raw(void);
uint64_t console64_read_key(void);
uint64_t console64_size(void);
void console64_move(uint32_t row, uint32_t col);
void console64_clear_cells(uint32_t row, uint32_t col, uint32_t rows, uint32_t cols);
void console64_set_attr(uint8_t fg, uint8_t bg);
void console64_flush(void);
void console64_set_event_fifo(struct FIFO64 *fifo);
void console64_repl_set_active(int active);
int console64_repl_getchar(void);
/* 명령줄 편집기(한글 조합 포함)로 한 줄을 읽는다. 바이트 수, Ctrl-C면 -1,
   Ctrl-D면 -2. 부르기 전에 input_line을 가리키는 인자는 복사해 두어야 한다. */
int64_t console64_read_line(char *dst, uint64_t max);
#define CONSOLE64_LINE_MAX 1024

#endif
