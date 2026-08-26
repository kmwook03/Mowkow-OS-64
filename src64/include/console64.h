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

/* 콘솔을 자기 태스크로 띄운다. task_init64 뒤에 불러야 한다.
   이때부터 run/py는 커널 이벤트 루프가 아니라 이 태스크에서 돈다. */
int console64_start_task(struct CONSOLE64 *con);

/* 콘솔을 하나 더 띄운다: 창 + 태스크 + 프롬프트. 자리가 없으면 NULL. */
struct CONSOLE64 *console64_create(void);

/* 스캔코드를 콘솔의 키 FIFO에 넣고, 자고 있으면 깨운다.
   커널 이벤트 루프가 포커스에 따라 부른다. */
void console64_post_key(struct CONSOLE64 *con, uint8_t scancode);
void console64_repl_set_active(int active);
int console64_repl_getchar(void);

#endif
