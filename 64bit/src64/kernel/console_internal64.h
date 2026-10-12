#ifndef MOWKOW64_CONSOLE_INTERNAL64_H
#define MOWKOW64_CONSOLE_INTERNAL64_H

#include <console64.h>
#include <fifo64.h>
#include <hangul64.h>
#include <stddef.h>
#include <stdint.h>

#define CONSOLE64_BG_DEFAULT 0
#define CONSOLE64_FG_DEFAULT 7
#define CONSOLE64_FONT_W 8
#define CONSOLE64_FONT_H 16
#define CONSOLE64_HANGUL_W 16
#define CONSOLE64_REPL_QUEUE_SIZE 64
#define CONSOLE64_RAW_QUEUE_SIZE 64
#define CONSOLE64_KEY_BUF 64
#define CONSOLE64_MAX 4
/* Preserve the parser recursion budget of the existing console task. */
#define CONSOLE64_STACK_SIZE (256 * 1024)

/* Private console state. GUI/apps use the opaque public declaration.
 * The lifecycle owns slots/tasks; queue owns input transport; editor owns
 * line/IME transitions; renderer owns pixels/cursor/dirty regions. The current
 * subsystem runs on one core. GUI/lifecycle publication and teardown use
 * irq-save transactions; raw/REPL editing still belongs to the console task.
 */
enum CONSOLE_SLOT_STATE64 {
	CONSOLE64_FREE, CONSOLE64_RESERVED, CONSOLE64_LIVE, CONSOLE64_CLOSING
};

struct CONSOLE64 {
	enum CONSOLE_SLOT_STATE64 slot_state;
	uint32_t render_users;
	/* Startup/teardown retains a stack whose allocator return failed. */
	uintptr_t pending_stack;
	size_t pending_stack_size;
	/* 시트에 붙어 있으면 vram은 시트 버퍼의 내용 영역을 가리킨다. 붙어
	   있지 않으면(초기 부팅, 컴포지터 할당 실패) 예전처럼 LFB에 직접 쓴다. */
	uint8_t *vram;
	size_t vram_size;
	uint32_t stride;
	uint16_t width;
	uint16_t height;
	uint16_t cursor_x;
	uint16_t cursor_y;
	char input_line[CONSOLE64_LINE_MAX];
	uint16_t input_len;
	int lang_hangul;
	struct HANGUL64 composing;
	struct SHEET64 *sheet;
	uint16_t ox;
	uint16_t oy;
	char repl_queue[CONSOLE64_REPL_QUEUE_SIZE];
	uint32_t repl_queue_head;
	uint32_t repl_queue_tail;
	int repl_active;
	/*
	 * raw 모드: 콘솔 줄 편집기와 에코를 끄고 키 이벤트를 앱에게 그대로
	 * 넘긴다. 콘솔마다 따로 둔다 -- 한 창에서 나노를 띄웠다고 다른 창의
	 * 명령줄까지 raw가 되면 안 된다.
	 */
	int raw_mode;
	uint64_t raw_queue[CONSOLE64_RAW_QUEUE_SIZE];
	uint32_t raw_queue_head;
	uint32_t raw_queue_tail;
	uint32_t size_generation;
	/* raw 모드에서는 갱신 영역을 모았다가 TTY_FLUSH에서 한 번에 올린다.
	   글자마다 sheet64_refresh를 부르면 시트 더미를 수천 번 훑는다. */
	int32_t dirty_x0;
	int32_t dirty_y0;
	int32_t dirty_x1;
	int32_t dirty_y1;
	int dirty_valid;
	/*
	 * mowio.readline이 줄 편집기를 빌려 쓰는 동안 켜진다. 켜져 있으면
	 * Enter가 명령 실행 대신 줄을 호출자에게 돌려주고 Ctrl-C/Ctrl-D가
	 * 줄을 끝낸다.
	 */
	int line_capture;
	int line_done;
	int line_result;            /* 0 = 줄 완성, -1 = Ctrl-C, -2 = Ctrl-D */
	int line_full_warned;       /* 줄이 꽉 찼다고 이미 알렸는가 */
	/* 콘솔마다 자기 태스크와 키 큐를 갖는다. 예전에는 커널 이벤트 루프가
	   직접 process_key를 불렀기 때문에 run/py가 도는 동안 마우스도 화면도
	   멈췄다. */
	struct TASK64 *task;
	struct FIFO64 keys;
	struct EVENT64 key_buf[CONSOLE64_KEY_BUF];
	/* 창 닫기는 협조적이다. 컴포지터가 close_requested만 세우고 물러나면,
	   콘솔 태스크가 프롬프트로 돌아온 자리에서 close_ready를 세우고 잠든다.
	   실제로 부수는 것은 커널 메인 루프(console64_reap_closed)다. 돌고 있는
	   py/머꼬/앱을 그 자리에서 죽이면 그것들이 쥔 전역 자물쇠를 아무도 놓지
	   못해 재부팅 전까지 잠긴다. */
	int close_requested;
	int close_ready;
};

/* Checked GUI/lifecycle bridges run under irq-save and never sleep. */
int console_attach_sheet_checked64(struct CONSOLE64 *con, struct SHEET64 *sht,
	uint16_t ox, uint16_t oy, uint16_t w, uint16_t h);
int console_stop_checked64(struct CONSOLE64 *con);
void console_finish_destroy64(struct CONSOLE64 *con);
struct CONSOLE64 *console_self64(void);
void console_puts_con64(struct CONSOLE64 *con, const char *text);
void console_put_char64(struct CONSOLE64 *con, const char *text, int length);
void console_newline64(struct CONSOLE64 *con);
void console_erase_prev64(struct CONSOLE64 *con, uint16_t width);
void console_clear_screen64(struct CONSOLE64 *con);
void console_prompt64(struct CONSOLE64 *con);
void console_draw_preedit64(struct CONSOLE64 *con);
void console_erase_preedit64(struct CONSOLE64 *con);
/* line aliases the editor buffer; callees must snapshot it before readline. */
void console_dispatch_command64(struct CONSOLE64 *con, const char *line);
/* pump/wait run in task context; pump may sleep with IRQs disabled and restores
 * the existing enabled-IRQ return convention. FIFO handles producer locking.
 * Raw/REPL rings retain their existing single-consumer/full-ring policies.
 * TTY raw reads check both rings/FIFO and commit sleep under one irq-save
 * guard because GUI resize wakes the consumer without a FIFO keyboard event.
 */
int console_pump_event64(struct CONSOLE64 *con, uint16_t *key);
uint16_t console_wait_key64(struct CONSOLE64 *con);
void console_repl_push64(struct CONSOLE64 *con, char c);
int console_repl_pop64(struct CONSOLE64 *con, char *out);
void console_raw_push64(struct CONSOLE64 *con, unsigned int kind, unsigned int payload);
int console_raw_pop64(struct CONSOLE64 *con, uint64_t *out);

#endif
