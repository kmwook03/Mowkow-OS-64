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
void console64_process_key(struct CONSOLE64 *con, uint16_t key);

/* raw 모드: 줄 편집과 에코를 끄고 키를 앱에게 그대로 넘긴다 (나노 등).
   모두 부르는 태스크의 콘솔에 걸린다. */
void console64_set_raw(int on);
int console64_is_raw(void);
uint64_t console64_read_key(void);
uint64_t console64_size(void);
void console64_move(uint32_t row, uint32_t col);
void console64_clear_cells(uint32_t row, uint32_t col, uint32_t rows, uint32_t cols);
void console64_set_attr(uint8_t fg, uint8_t bg);
void console64_flush(void);
/* 한글 오토마타 회귀 확인. 통과하면 1. */
int console64_hangul_smoke(void);

/* 콘솔을 자기 태스크로 띄운다. task_init64 뒤에 불러야 한다.
   이때부터 run/py는 커널 이벤트 루프가 아니라 이 태스크에서 돈다. */
int console64_start_task(struct CONSOLE64 *con);

/* 콘솔을 하나 더 띄운다: 창 + 태스크 + 프롬프트. 자리가 없으면 NULL. */
struct CONSOLE64 *console64_create(void);

/* 콘솔을 없앤다: 태스크를 죽이고 슬롯을 비운다. 창을 닫을 때 부른다. */
void console64_destroy(struct CONSOLE64 *con);

/* 스캔코드를 콘솔의 키 FIFO에 넣고, 자고 있으면 깨운다.
   커널 이벤트 루프가 포커스에 따라 부른다. */
void console64_post_key(struct CONSOLE64 *con, uint16_t key);
void console64_repl_set_active(int active);

/* 창 닫기 요청. 곧바로 부수지 않고 표시만 남긴다 -- 콘솔 태스크가 프롬프트로
   돌아오면 스스로 접히고, 그다음 console64_reap_closed가 실제로 없앤다. */
void console64_request_close(struct CONSOLE64 *con);

/* 접힌 콘솔을 거둔다. 커널 메인 루프가 부른다. */
void console64_reap_closed(void);
int console64_repl_getchar(void);
/* 명령줄 편집기(한글 조합 포함)로 한 줄을 읽는다. 바이트 수, Ctrl-C면 -1,
   Ctrl-D면 -2. 부르기 전에 input_line을 가리키는 인자는 복사해 두어야 한다. */
int64_t console64_read_line(char *dst, uint64_t max);
#define CONSOLE64_LINE_MAX 1024

#endif
