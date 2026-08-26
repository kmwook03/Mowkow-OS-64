#ifndef MOWKOW64_CONSOLE64_H
#define MOWKOW64_CONSOLE64_H

#include <stdint.h>

struct BOOTINFO64;
struct FIFO64;
struct SHEET64;

/* 콘솔 인스턴스. 내부 구조는 console64.c만 안다. */
struct CONSOLE64;

/* 부팅 콘솔. 컴포지터가 없을 때(LFB 직접 그리기)와 MicroPython처럼
   인스턴스를 모르는 호출자를 위한 것이다. */
struct CONSOLE64 *console64_active(void);

void console64_set_hangul_font(const uint8_t *font);
const uint8_t *console64_hangul_font(void);
void console64_attach_sheet(struct CONSOLE64 *con, struct SHEET64 *sht,
	uint16_t ox, uint16_t oy, uint16_t w, uint16_t h);
void console64_init(const struct BOOTINFO64 *boot_info);
void console64_puts(const char *s);
void console64_prompt(void);
void console64_write(const char *s, uint64_t len);
uint64_t console64_read(char *dst, uint64_t len);
/* 콘솔을 지정하는 판. 앱의 stdout/stdin은 자기를 띄운 콘솔로 가야 한다. */
void console64_write_con(struct CONSOLE64 *con, const char *s, uint64_t len);
uint64_t console64_read_con(struct CONSOLE64 *con, char *dst, uint64_t len);
void console64_process_key(struct CONSOLE64 *con, uint8_t scancode);
void console64_set_event_fifo(struct FIFO64 *fifo);
void console64_repl_set_active(int active);
int console64_repl_getchar(void);

#endif
