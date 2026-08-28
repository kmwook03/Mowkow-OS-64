/*
 * console64.c -- 한글 콘솔과 명령 해석기
 *
 * 화면에 글자를 찍는 일, 키를 받아 두벌식으로 조합하는 일, 명령 한 줄을
 * 해석하는 일이 여기 모여 있다. 콘솔은 시트 하나 위에 그려지므로 전체 화면
 * 이든 창 안이든 같은 코드로 동작한다.
 *
 * 두벌식 조합기는 커널 쪽인 이 파일이 갖고 있다(roadmap64.md 결정 11).
 * 앱은 완성된 글자와 조합 중인 글자를 SYS_TTY로 받을 뿐, 자모 상태는 보지
 * 않는다.
 */
#include <asmfunc64.h>
#include <bootinfo64.h>
#include <console64.h>
#include <fd64.h>
#include <fifo64.h>
#include <hangul64.h>
#include <keyboard64.h>
#include <memory64.h>
#include <mpport64.h>
#include <mtask64.h>
#include <gui64.h>
#include <process64.h>
#include <sheet64.h>
#include <syscall64.h>
#include <timer64.h>
#include <utf864.h>
#include <stddef.h>
#include <stdint.h>

/* 한글은 한 글자에 3바이트라 256이면 85자밖에 안 된다. 1024면 340자쯤 되고,
   .bss는 768바이트만 더 쓴다. console64.h의 CONSOLE64_LINE_MAX와 같아야 한다. */
#define CONSOLE_INPUT_MAX 1024
#define COLOR_BG_DEFAULT 0
/* 32비트 트리와 같은 값(COL8_FFFFFF). init_palette64가 15를 어두운 회색으로
   바꾸므로, 예전처럼 15를 쓰면 검정 배경에 어두운 회색 글씨가 된다. */
#define COLOR_FG_DEFAULT 7
#define FONT_W 8
#define FONT_H 16
#define HANGUL_W 16
extern const uint8_t hankaku64[4096];

#define REPL_QUEUE_SIZE 64
#define CONSOLE64_KEY_BUF 64
/*
 * 콘솔 태스크 스택. 기본 TASK64_STACK_SIZE(64KiB)로는 py가 예전보다 약해진다
 * -- 5단계 전에는 커널 메인 스택 128KiB에서 돌았다. MicroPython 파서는
 * 재귀에 스택 검사를 걸지 않으므로(guard는 런타임 경로에만 있다) 깊이 한계는
 * 순전히 스택 크기가 정한다.
 *
 * 스택을 키워 한계를 밀어낼 뿐 없애지는 못한다. 진짜 해결은 parse.c에 mp_cstack_check()를 넣는 것.
 * 그러나 해당 방식은 서브모듈을 건드린다.
 */
#define CONSOLE64_STACK_SIZE (256 * 1024)

/*
 * 콘솔 상태 전부. 예전에는 파일 스코프 전역이었지만 콘솔이 여러 개가 되면
 * (console_plan.md 6단계) 인스턴스마다 있어야 한다. 지금은 인스턴스가
 * 하나뿐이라 동작은 예전과 같다.
 *
 * 여기 없는 것 두 가지: 한글 글꼴은 시스템에 하나뿐인 자원이고, shift/ctrl은
 * 물리 키 상태라 콘솔이 아니라 키보드 계층(keyboard64.c)이 갖는다. 콘솔마다
 * 따로 두면 눌린 채 포커스가 옮겨갔을 때 한쪽에 눌림이 남는다.
 */
struct CONSOLE64 {
	/* 시트에 붙어 있으면 vram은 시트 버퍼의 내용 영역을 가리킨다. 붙어
	   있지 않으면(초기 부팅, 컴포지터 할당 실패) 예전처럼 LFB에 직접 쓴다. */
	uint8_t *vram;
	uint32_t stride;
	uint16_t width;
	uint16_t height;
	uint16_t cursor_x;
	uint16_t cursor_y;
	char input_line[CONSOLE_INPUT_MAX];
	uint16_t input_len;
	int lang_hangul;
	struct HANGUL64 composing;
	struct SHEET64 *sheet;
	uint16_t ox;
	uint16_t oy;
	char repl_queue[REPL_QUEUE_SIZE];
	uint32_t repl_queue_head;
	uint32_t repl_queue_tail;
	int repl_active;
	/* 콘솔마다 자기 태스크와 키 큐를 갖는다. 예전에는 커널 이벤트 루프가
	   직접 process_key를 불렀기 때문에 run/py가 도는 동안 마우스도 화면도
	   멈췄다 (console_plan.md 5단계). */
	struct TASK64 *task;
	struct FIFO64 keys;
	struct EVENT64 key_buf[CONSOLE64_KEY_BUF];
};

/* 콘솔 0은 부팅 콘솔이다 -- 전체 화면 토글과 화면 크기 버퍼를 가진 유일한
   콘솔이고, 나머지는 `new`로 뜨는 창 전용 콘솔이다. */
#define CONSOLE64_MAX 4
static struct CONSOLE64 console_table[CONSOLE64_MAX];
static struct CONSOLE64 *console_active = &console_table[0];

static const uint8_t *hangul_font;
static uint8_t color_fg = COLOR_FG_DEFAULT;
static uint8_t color_bg = COLOR_BG_DEFAULT;

/* 지금 도는 태스크의 콘솔. MicroPython처럼 인스턴스를 모르는 호출자가
   자기를 띄운 콘솔에 찍도록 해 준다. 콘솔 태스크가 아니면 부팅 콘솔. */
static struct CONSOLE64 *console_self(void)
{
	struct TASK64 *task = task_now64();

	int32_t i;

	if (task != NULL) {
		for (i = 0; i < CONSOLE64_MAX; i++) {
			if (console_table[i].task == task) {
				return &console_table[i];
			}
		}
	}
	return console_active;
}

/* 키가 올 때까지 잔다. 자는 동안 다른 태스크가 돈다 -- 이게 5단계의 요점이다.
   깨우기는 fifo64_put이 알아서 한다. */
static uint8_t console_wait_key(struct CONSOLE64 *con)
{
	struct EVENT64 event;

	for (;;) {
		io_cli();
		if (fifo64_get(&con->keys, &event) == 0) {
			io_sti();
			if (event.type == EVENT64_KEYBOARD) {
				return (uint8_t) event.data;
			}
		} else {
			task_sleep64(con->task);
			io_sti();
		}
	}
}

static void console_flush(struct CONSOLE64 *con, int32_t x, int32_t y, int32_t w, int32_t h)
{
	if (con->sheet == NULL) {
		return;
	}
	sheet64_refresh(con->sheet, con->ox + x, con->oy + y,
		con->ox + x + w, con->oy + y + h);
}


static void repl_queue_push(struct CONSOLE64 *con, char c)
{
	uint32_t next;

	next = (con->repl_queue_tail + 1) % REPL_QUEUE_SIZE;
	if (next == con->repl_queue_head) {
		return;
	}
	con->repl_queue[con->repl_queue_tail] = c;
	con->repl_queue_tail = next;
}

static int repl_queue_pop(struct CONSOLE64 *con, char *out)
{
	if (con->repl_queue_head == con->repl_queue_tail) {
		return 0;
	}
	*out = con->repl_queue[con->repl_queue_head];
	con->repl_queue_head = (con->repl_queue_head + 1) % REPL_QUEUE_SIZE;
	return 1;
}

static const char keymap0[128] = {
	[0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
	[0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
	[0x0a] = '9', [0x0b] = '0', [0x0c] = '-', [0x0d] = '=',
	[0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
	[0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
	[0x18] = 'o', [0x19] = 'p', [0x1a] = '[', [0x1b] = ']',
	[0x1e] = 'a', [0x1f] = 's', [0x20] = 'd', [0x21] = 'f',
	[0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
	[0x26] = 'l', [0x27] = ';', [0x28] = '\'',
	[0x2b] = '\\', [0x2c] = 'z', [0x2d] = 'x', [0x2e] = 'c',
	[0x2f] = 'v', [0x30] = 'b', [0x31] = 'n', [0x32] = 'm',
	[0x33] = ',', [0x34] = '.', [0x35] = '/', [0x39] = ' ',
};

static const char keymap1[128] = {
	[0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
	[0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*',
	[0x0a] = '(', [0x0b] = ')', [0x0c] = '_', [0x0d] = '+',
	[0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R',
	[0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I',
	[0x18] = 'O', [0x19] = 'P', [0x1a] = '{', [0x1b] = '}',
	[0x1e] = 'A', [0x1f] = 'S', [0x20] = 'D', [0x21] = 'F',
	[0x22] = 'G', [0x23] = 'H', [0x24] = 'J', [0x25] = 'K',
	[0x26] = 'L', [0x27] = ':', [0x28] = '"',
	[0x2b] = '|', [0x2c] = 'Z', [0x2d] = 'X', [0x2e] = 'C',
	[0x2f] = 'V', [0x30] = 'B', [0x31] = 'N', [0x32] = 'M',
	[0x33] = '<', [0x34] = '>', [0x35] = '?', [0x39] = ' ',
};

static char translate_key(struct CONSOLE64 *con, uint8_t scancode)
{
	char c;

	if (con->lang_hangul != 0 && keyboard64_shift() != 0) {
		switch (scancode) {
		case 0x10: return 'Q'; /* ㅃ */
		case 0x11: return 'W'; /* ㅉ */
		case 0x12: return 'E'; /* ㄸ */
		case 0x13: return 'R'; /* ㄲ */
		case 0x14: return 'T'; /* ㅆ */
		case 0x18: return 'O'; /* ㅒ */
		case 0x19: return 'P'; /* ㅖ */
		default:
			c = keymap0[scancode];
			if ((c >= 'a' && c <= 'z') || c == ' ') {
				return c;
			}
			return keymap1[scancode];
		}
	}
	return keyboard64_shift() != 0 ? keymap1[scancode] : keymap0[scancode];
}

static void serial_putc(char c)
{
	if (c == '\n') {
		serial_putc('\r');
	}
	while ((io_in8(0x3f8 + 5) & 0x20) == 0) {
	}
	io_out8(0x3f8, (uint8_t) c);
}

static void fill_rect(struct CONSOLE64 *con, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t color)
{
	uint16_t px;
	uint16_t py;

	for (py = y; py < y + h && py < con->height; py++) {
		for (px = x; px < x + w && px < con->width; px++) {
			con->vram[(uint32_t) py * con->stride + px] = color;
		}
	}
	console_flush(con, x, y, w, h);
}

static void draw_ascii(struct CONSOLE64 *con, uint16_t x, uint16_t y, char c)
{
	const uint8_t *font;
	uint8_t d;
	uint16_t row;
	uint16_t bit;
	uint8_t *p;

	font = hankaku64 + (uint8_t) c * 16;
	for (row = 0; row < FONT_H; row++) {
		d = font[row];
		p = con->vram + (uint32_t) (y + row) * con->stride + x;
		for (bit = 0; bit < FONT_W; bit++) {
			if ((d & (0x80 >> bit)) != 0) {
				p[bit] = color_fg;
			}
		}
	}
	console_flush(con, x, y, FONT_W, FONT_H);
}

static void scroll_if_needed(struct CONSOLE64 *con)
{
	uint32_t row;
	uint32_t col;

	if (con->cursor_y + FONT_H <= con->height) {
		return;
	}
	for (row = FONT_H; row < con->height; row++) {
		for (col = 0; col < con->width; col++) {
			con->vram[(row - FONT_H) * con->stride + col] =
				con->vram[row * con->stride + col];
		}
	}
	for (row = con->height - FONT_H; row < con->height; row++) {
		for (col = 0; col < con->width; col++) {
			con->vram[row * con->stride + col] = COLOR_BG;
		}
	}
	con->cursor_y = con->height - FONT_H;
	console_flush(con, 0, 0, con->width, con->height);
}

static void newline(struct CONSOLE64 *con)
{
	con->cursor_x = 0;
	con->cursor_y += FONT_H;
	serial_putc('\n');
	scroll_if_needed(con);
}

static void erase_prev_visual(struct CONSOLE64 *con, uint16_t width);

static void put_utf8_char(struct CONSOLE64 *con, const char *s, int len)
{
	unsigned int unicode;
	uint16_t width;
	int decode_len;
	int i;

	if (len == 1 && s[0] == '\n') {
		newline(con);
		return;
	}
	if (len == 1 && s[0] == '\r') {
		return;
	}
	if (len == 1 && s[0] == '\b') {
		erase_prev_visual(con, FONT_W);
		return;
	}
	width = FONT_W;
	if (len == 3) {
		unicode = utf8_to_unicode64(s, &decode_len);
		if ((unicode >= 0xac00 && unicode <= 0xd7a3) ||
				(unicode >= 0x3131 && unicode <= 0x3163)) {
			width = HANGUL_W;
		}
	}
	if (con->cursor_x + width > con->width) {
		newline(con);
	}
	fill_rect(con, con->cursor_x, con->cursor_y, width, FONT_H, COLOR_BG);
	if (width == HANGUL_W && hangul_font != NULL) {
		unicode = utf8_to_unicode64(s, &decode_len);
		hangul64_draw_unicode(con->vram, con->stride, con->cursor_x, con->cursor_y,
			COLOR_FG, hangul_font, unicode);
		console_flush(con, con->cursor_x, con->cursor_y, HANGUL_W, FONT_H);
	} else if (len == 1) {
		draw_ascii(con, con->cursor_x, con->cursor_y, s[0]);
	}
	con->cursor_x += width;
	for (i = 0; i < len; i++) {
		serial_putc(s[i]);
	}
}

static void puts_con(struct CONSOLE64 *con, const char *s)
{
	while (*s != '\0') {
		int len;

		len = utf8_byte_len64((unsigned char) *s);
		if (len <= 0) {
			len = 1;
		}
		put_utf8_char(con, s, len);
		s += len;
	}
}

static void put_bytes(struct CONSOLE64 *con, const char *s, size_t n)
{
	size_t i;
	int len;

	i = 0;
	while (i < n) {
		len = utf8_byte_len64((unsigned char) s[i]);
		if (len <= 0 || i + (size_t) len > n) {
			len = 1;
		}
		put_utf8_char(con, s + i, len);
		i += (size_t) len;
	}
}

static void erase_prev_visual(struct CONSOLE64 *con, uint16_t width)
{
	if (con->cursor_x < width) {
		return;
	}
	con->cursor_x -= width;
	fill_rect(con, con->cursor_x, con->cursor_y, width, FONT_H, COLOR_BG);
	serial_putc('\b');
	serial_putc(' ');
	serial_putc('\b');
}

static int append_input(struct CONSOLE64 *con, const char *s, int len)
{
	int i;

	if (con->input_len + len >= CONSOLE_INPUT_MAX) {
		return 0;
	}
	for (i = 0; i < len; i++) {
		con->input_line[con->input_len++] = s[i];
	}
	return 1;
}

static void draw_composing(struct CONSOLE64 *con)
{
	if (con->cursor_x < HANGUL_W || hangul_font == NULL) {
		return;
	}
	fill_rect(con, con->cursor_x - HANGUL_W, con->cursor_y, HANGUL_W, FONT_H, COLOR_BG);
	hangul64_draw_johab(con->vram, con->stride, con->cursor_x - HANGUL_W, con->cursor_y,
		COLOR_FG, hangul_font, hangul64_to_johab(&con->composing));
	console_flush(con, con->cursor_x - HANGUL_W, con->cursor_y, HANGUL_W, FONT_H);
}

static void flush_composing(struct CONSOLE64 *con)
{
	char utf8[4];
	int len;

	if (con->composing.state == 0) {
		return;
	}
	len = hangul64_compose_utf8(utf8, &con->composing);
	if (len > 0) {
		append_input(con, utf8, len);
	}
	hangul64_init(&con->composing);
}

static void start_new_hangul(struct CONSOLE64 *con, int state, int cho, int jung, int jong)
{
	flush_composing(con);
	if (con->cursor_x + HANGUL_W > con->width) {
		newline(con);
	}
	con->composing.state = state;
	con->composing.cho = cho;
	con->composing.jung = jung;
	con->composing.jong = jong;
	fill_rect(con, con->cursor_x, con->cursor_y, HANGUL_W, FONT_H, COLOR_BG);
	hangul64_draw_johab(con->vram, con->stride, con->cursor_x, con->cursor_y,
		COLOR_FG, hangul_font, hangul64_to_johab(&con->composing));
	console_flush(con, con->cursor_x, con->cursor_y, HANGUL_W, FONT_H);
	con->cursor_x += HANGUL_W;
}

static void update_composing(struct CONSOLE64 *con, int state, int cho, int jung, int jong)
{
	con->composing.state = state;
	con->composing.cho = cho;
	con->composing.jung = jung;
	con->composing.jong = jong;
	draw_composing(con);
}

static void not_korean(struct CONSOLE64 *con, char key)
{
	char s[1];

	flush_composing(con);
	s[0] = key;
	if (append_input(con, s, 1) != 0) {
		put_utf8_char(con, s, 1);
	}
}

/*
 * 받침이 될 수 없는 쌍자음: ㄸ(4), ㅃ(8), ㅉ(13). 이 셋은 앞 글자에 붙이지
 * 못하므로 새 글자를 열어야 한다.
 *
 * ㄲ(1)과 ㅆ(10)은 여기 들어가면 안 된다. 둘 다 멀쩡한 받침이라(종성 2번과
 * 20번), 쌍자음이라는 이유로 함께 막으면 있, 닦, 밖, 겪 같은 글자를 아예 칠
 * 수 없게 된다.
 */
static int cho_cannot_be_jong(int cho)
{
	return cho == 4 || cho == 8 || cho == 13;
}

static void process_hangul_key(struct CONSOLE64 *con, char key)
{
	int cho;
	int jung;
	int jong;
	int complex;
	int next_cho;
	int first_jong;
	int second_jong;

	cho = hangul64_key_to_cho(key);
	jung = hangul64_key_to_jung(key);
	jong = hangul64_key_to_jong(key);
	switch (con->composing.state) {
	case 0:
		if (cho != -1) {
			start_new_hangul(con, 1, cho, -1, -1);
		} else if (jung != -1) {
			start_new_hangul(con, 1, -1, jung, -1);
			flush_composing(con);
		} else {
			not_korean(con, key);
		}
		break;
	case 1:
		if (jung != -1 && con->composing.cho != -1) {
			update_composing(con, 2, con->composing.cho, jung, -1);
		} else if (cho != -1) {
			start_new_hangul(con, 1, cho, -1, -1);
		} else {
			not_korean(con, key);
		}
		break;
	case 2:
		if (cho != -1 && is_double_cho(cho) != 0) {
			start_new_hangul(con, 1, cho, -1, -1);
		} else if (jong != -1) {
			update_composing(con, 3, con->composing.cho, con->composing.jung, jong);
		} else if (jung != -1) {
			complex = hangul64_composite_jung(con->composing.jung, jung);
			if (complex != -1) {
				update_composing(con, 2, con->composing.cho, complex, -1);
			} else {
				start_new_hangul(con, 1, -1, jung, -1);
				flush_composing(con);
			}
		} else if (cho != -1) {
			start_new_hangul(con, 1, cho, -1, -1);
		} else {
			not_korean(con, key);
		}
		break;
	case 3:
		if (jung != -1) {
			next_cho = hangul64_jong_to_cho(con->composing.jong);
			update_composing(con, 2, con->composing.cho, con->composing.jung, -1);
			if (next_cho != -1) {
				start_new_hangul(con, 2, next_cho, jung, -1);
			}
		} else if (cho != -1) {
			complex = hangul64_composite_jong(con->composing.jong, cho);
			if (complex != -1) {
				update_composing(con, 4, con->composing.cho, con->composing.jung, complex);
			} else {
				start_new_hangul(con, 1, cho, -1, -1);
			}
		} else {
			not_korean(con, key);
		}
		break;
	case 4:
		if (jung != -1) {
			first_jong = hangul64_first_jong(con->composing.jong);
			second_jong = hangul64_second_jong(con->composing.jong);
			update_composing(con, 3, con->composing.cho, con->composing.jung, first_jong);
			start_new_hangul(con, 2, second_jong, jung, -1);
		} else if (cho != -1) {
			start_new_hangul(con, 1, cho, -1, -1);
		} else {
			not_korean(con, key);
		}
		break;
	default:
		hangul64_init(&con->composing);
		break;
	}
}

static int delete_composing(struct CONSOLE64 *con)
{
	int prev_jung;

	if (con->composing.state == 0) {
		return 0;
	}
	if (con->composing.state == 1) {
		hangul64_init(&con->composing);
		erase_prev_visual(con, HANGUL_W);
	} else if (con->composing.state == 2) {
		prev_jung = hangul64_split_composite_jung(con->composing.jung);
		if (prev_jung != -1) {
			update_composing(con, 2, con->composing.cho, prev_jung, -1);
		} else {
			update_composing(con, 1, con->composing.cho, -1, -1);
		}
	} else if (con->composing.state == 3) {
		update_composing(con, 2, con->composing.cho, con->composing.jung, -1);
	} else if (con->composing.state == 4) {
		update_composing(con, 3, con->composing.cho, con->composing.jung,
			hangul64_first_jong(con->composing.jong));
	}
	return 1;
}

static void console_backspace(struct CONSOLE64 *con)
{
	uint16_t width;
	uint16_t start;

	if (delete_composing(con) != 0) {
		return;
	}
	if (con->input_len == 0) {
		return;
	}
	start = con->input_len - 1;
	while (start > 0 && (((uint8_t) con->input_line[start] & 0xc0) == 0x80)) {
		start--;
	}
	width = (con->input_len - start == 3) ? HANGUL_W : FONT_W;
	con->input_len = start;
	erase_prev_visual(con, width);
}

static int str_eq(const char *a, const char *b)
{
	while (*a != '\0' && *b != '\0') {
		if (*a != *b) {
			return 0;
		}
		a++;
		b++;
	}
	return *a == *b;
}

static int str_starts_with(const char *s, const char *prefix)
{
	while (*prefix != '\0') {
		if (*s != *prefix) {
			return 0;
		}
		s++;
		prefix++;
	}
	return 1;
}

static void print_file_name(struct CONSOLE64 *con, const struct FDINFO64 *finfo)
{
	uint16_t n;

	for (i = 0; i < 8; i++) {
		if (finfo->name[i] != ' ') {
			put_utf8_char(con, (const char *) &finfo->name[i], 1);
		}
	}
	if (finfo->ext[0] != ' ') {
		put_utf8_char(con, ".", 1);
		for (i = 0; i < 3; i++) {
			if (finfo->ext[i] != ' ') {
				put_utf8_char(con, (const char *) &finfo->ext[i], 1);
			}
		}
	}
	put_bytes(name, n);
}

static void print_uint64(struct CONSOLE64 *con, uint64_t value)
{
	char buf[20];
	uint16_t i;

	if (value == 0) {
		put_utf8_char(con, "0", 1);
		return;
	}
	i = 0;
	while (value != 0) {
		buf[i++] = (char) ('0' + (value % 10));
		value /= 10;
	}
	while (i > 0) {
		put_utf8_char(con, &buf[--i], 1);
	}
}

static void print_hex64(struct CONSOLE64 *con, uint64_t value)
{
	uint16_t shift;
	uint8_t digit;
	int started;
	char c;

	puts_con(con, "0x");
	started = 0;
	for (shift = 60; shift > 0; shift -= 4) {
		digit = (uint8_t) ((value >> shift) & 0x0f);
		if (digit != 0 || started != 0) {
			c = (char) (digit < 10 ? '0' + digit : 'a' + digit - 10);
			put_utf8_char(con, &c, 1);
			started = 1;
		}
	}
	digit = (uint8_t) (value & 0x0f);
	c = (char) (digit < 10 ? '0' + digit : 'a' + digit - 10);
	put_utf8_char(con, &c, 1);
}

static void prompt(struct CONSOLE64 *con)
{
	puts_con(con, "> ");
}

void console64_prompt(void)
{
	struct CONSOLE64 *con = console_self();

	prompt(con);
}

static void clear_screen(struct CONSOLE64 *con)
{
	fill_rect(con, 0, 0, con->width, con->height, COLOR_BG);
	con->cursor_x = 0;
	con->cursor_y = 0;
}

static void execute_command(struct CONSOLE64 *con)
{
	flush_composing(con);
	con->input_line[con->input_len] = '\0';
	newline(con);
	if (con->input_len == 0) {
		prompt(con);
		return;
	}
	if (str_eq(con->input_line, "help")) {
		puts_con(con, "commands: help clear ticks mem tasks ls 목록 type readme.txt run HELLO py py FILE.PY xwindow 창 new 새창\n");
	} else if (str_eq(con->input_line, "xwindow") || str_eq(con->input_line, "window") ||
			str_eq(con->input_line, "창")) {
		/* 전체 화면 토글은 콘솔 0 전용이다 -- 화면 크기 버퍼를 가진 건
		   콘솔 0뿐이다 (console_plan.md 결정). */
		if (con != console_active) {
			puts_con(con, "fullscreen is console 0 only\n");
		} else {
			gui64_toggle_window();
		}
	} else if (str_eq(con->input_line, "clear") || str_eq(con->input_line, "지우기")) {
		clear_screen(con);
	} else if (str_eq(con->input_line, "ticks")) {
		puts_con(con, "ticks ");
		print_uint64(con, timerctl64.count);
		puts_con(con, "\n");
	} else if (str_eq(con->input_line, "mem") || str_eq(con->input_line, "메모리")) {
		uintptr_t addr;

		puts_con(con, "free ");
		print_uint64(con, memman64_total(&memman64) / 1024);
		puts_con(con, " KiB\n");
		addr = memman64_alloc_4k(&memman64, 4096);
		puts_con(con, "alloc4k ");
		print_hex64(con, addr);
		puts_con(con, "\n");
		if (addr != 0) {
			memman64_free_4k(&memman64, addr, 4096);
		}
	} else if (str_eq(con->input_line, "new") || str_eq(con->input_line, "새창")) {
		if (console64_create() == NULL) {
			puts_con(con, "no free console slot\n");
		}
	} else if (str_eq(con->input_line, "tasks") || str_eq(con->input_line, "태스크")) {
		uint32_t i;

		puts_con(con, "switches ");
		print_uint64(con, taskctl64.switches);
		puts_con(con, " current-level ");
		print_uint64(con, taskctl64.now_lv);
		puts_con(con, "\n");
		/* 숨긴 콘솔이 살아 있는지 보려면 태스크별 전환 수가 필요하다. */
		for (i = 0; i < MAX_TASKS64; i++) {
			if (taskctl64.tasks0[i].flags == TASK64_FLAGS_UNUSED) {
				continue;
			}
			puts_con(con, "  task ");
			print_uint64(con, i);
			puts_con(con, " lv ");
			print_uint64(con, taskctl64.tasks0[i].level);
			puts_con(con, " switches ");
			print_uint64(con, taskctl64.tasks0[i].switches);
			puts_con(con, "\n");
		}
	} else if (str_eq(con->input_line, "ls") || str_eq(con->input_line, "목록")) {
		uint32_t i;
		uint32_t count;
		struct FDINFO64 finfo;
		char name[FD64_NAME_MAX];

		count = fd64_file_count();
		for (i = 0; i < count; i++) {
			finfo = fd64_file_at(i);
			if (finfo != NULL) {
				print_file_name(con, finfo);
				puts_con(con, "  ");
				print_uint64(con, finfo->size);
				puts_con(con, "\n");
			}
		}
		if (count == 0) {
			puts_con(con, "no files\n");
		}
	} else if (str_eq(con->input_line, "type readme.txt") || str_eq(con->input_line, "읽기 readme.txt")) {
		struct FDHANDLE64 fh;
		char buf[65];
		size_t n;

		if (fd64_open(&fh, "readme.txt") == 0) {
			puts_con(con, "file not found\n");
		} else {
			for (;;) {
				n = fd64_read(&fh, buf, sizeof(buf) - 1);
				if (n == 0) {
					break;
				}
				put_bytes(con, buf, n);
			}
			puts_con(con, "\n");
		}
	} else if (str_starts_with(con->input_line, "run ") || str_starts_with(con->input_line, "실행 ")) {
		int status;

		status = process64_exec_file(con->input_line + 4, con->input_line + 4, con);
		if (status == -8) {
			puts_con(con, "another program is running\n");
		} else {
			puts_con(con, "exit ");
			print_uint64(con, (uint64_t) status);
			puts_con(con, "\n");
		}
	} else if (str_eq(con->input_line, "py") || str_eq(con->input_line, "파이썬")) {
		mpport_repl();
	} else if (str_starts_with(con->input_line, "py ")) {
		mpport_run_file(con->input_line + 3);
	} else {
		puts_con(con, "unknown command\n");
	}
	con->input_len = 0;
	prompt(con);
}

struct CONSOLE64 *console64_active(void)
{
	return console_active;
}

void console64_set_hangul_font(const uint8_t *font)
{
	hangul_font = font;
}

const uint8_t *console64_hangul_font(void)
{
	return hangul_font;
}

void console64_attach_sheet(struct CONSOLE64 *con, struct SHEET64 *sht,
	uint16_t ox, uint16_t oy, uint16_t w, uint16_t h)
{
	con->sheet = sht;
	con->ox = ox;
	con->oy = oy;
	con->vram = sht->buf + (uint32_t) oy * (uint32_t) sht->bxsize + ox;
	con->stride = (uint32_t) sht->bxsize;
	con->width = w;
	con->height = h;
	con->cursor_x = 0;
	con->cursor_y = 0;
	clear_screen(con);
}

void console64_init(const struct BOOTINFO64 *boot_info)
{
	struct SHEET64 *sht;
	struct CONSOLE64 *con = console_active;

	con->vram = (uint8_t *) boot_info->vram;
	con->width = boot_info->scrnx != 0 ? boot_info->scrnx : 800;
	con->height = boot_info->scrny != 0 ? boot_info->scrny : 600;
	con->stride = boot_info->bytes_per_scanline != 0 ?
		boot_info->bytes_per_scanline : con->width;
	con->cursor_x = 0;
	con->cursor_y = 0;
	con->input_len = 0;
	con->lang_hangul = 1;
	hangul64_init(&con->composing);
	/* 컴포지터가 뜨면 콘솔은 전체 화면 창 하나가 된다 (로드맵 decision 3b).
	   실패하면 지금까지처럼 LFB에 직접 그린다. */
	sht = gui64_init(boot_info);
	if (sht != NULL) {
		gui64_bind_console(sht, con);
		console64_attach_sheet(con, sht, 0, 0, con->width, con->height);
	} else {
		clear_screen(con);
	}
	console64_puts("머꼬 OS x86_64 콘솔\n");
	console64_puts("한글 입력이 기본입니다. Shift+Space로 영어 입력으로 전환합니다.\n");
	prompt(con);
}

void console64_puts(const char *s)
{
	puts_con(console_self(), s);
}

void console64_write(const char *s, uint64_t len)
{
	console64_write_con(console_self(), s, len);
}

void console64_write_con(struct CONSOLE64 *con, const char *s, uint64_t len)
{
	put_bytes(con, s, (size_t) len);
}

/*
 * 확장 키를 콘솔이 아는 코드로 바꾼다. 소비자가 없는 키면 0을 돌려준다.
 */
static int normalize_ext_key(uint16_t *key)
{
	if ((*key & KEY64_EXT) == 0) {
		return 1;
	}
	if (*key == KEY64_KPENTER) {
		*key = 0x1c;                /* 키패드 Enter는 Enter와 같게 */
		return 1;
	}
	if (*key == (KEY64_EXT | 0x35)) {
		*key = 0x35;                /* 키패드 / 는 그냥 '/' */
		return 1;
	}
	/* 화살표, Home/End, Delete 등: 아직 콘솔에 소비자가 없다. */
	return 0;
}

/*
 * FIFO에서 이벤트 하나만 처리한다. 키보드 스캔코드였으면 *key에 담고 1.
 * 비어 있으면 태스크를 재운다.
 *
 * 한 번에 하나씩 돌려주는 이유: 마우스로 창 모드를 바꾸면 그 처리 도중
 * console64_attach_sheet가 raw 큐에 TTY_KIND_RESIZE를 넣는다. 키보드
 * 이벤트가 올 때까지 여기서 계속 돌면 그 RESIZE는 다음 타자를 칠 때까지
 * 큐에 갇힌다 - 앱은 지워진 화면을 그대로 보고 있게 된다.
 */
static int pump_event(uint16_t *key)
{
	struct EVENT64 event;

	io_cli();
	if (console_event_fifo == NULL || fifo64_get(console_event_fifo, &event) != 0) {
		task_sleep64(task_now64());
		io_sti();
		return 0;
	}
	io_sti();
	/* 마우스와 F11은 앱이 콘솔을 쥐고 있어도 살아 있어야 한다.
	   메인 루프와 같은 처리기를 쓴다. */
	if (gui64_handle_system_event(&event) != 0) {
		return 0;
	}
	if (event.type == EVENT64_KEYBOARD) {
		*key = (uint16_t) event.data;
		return 1;
	}
	return 0;
}

static uint16_t wait_key_event(void)
{
	uint16_t key;

	for (;;) {
		if (pump_event(&key) != 0) {
			return key;
		}
	}
}

uint64_t console64_read(char *dst, uint64_t len)
{
	return console64_read_con(console_self(), dst, len);
}

uint64_t console64_read_con(struct CONSOLE64 *con, char *dst, uint64_t len)
{
	uint64_t count;
	uint16_t key;
	char c;

	if (dst == 0 || len == 0) {
		return 0;
	}
	count = 0;
	for (;;) {
		scancode = console_wait_key(con);
		if (keyboard64_track_modifier(scancode) != 0) {
			continue;
		}
		if ((key & 0x80) != 0) {
			continue;
		}
		if (key == 0x1c) {
			dst[count++] = '\n';
			put_utf8_char(con, "\n", 1);
			return count;
		}
		if (key == 0x0e) {
			if (count > 0) {
				count--;
				erase_prev_visual(con, FONT_W);
			}
			continue;
		}
		c = keyboard64_shift() != 0 ? keymap1[scancode] : keymap0[scancode];
		if (c == '\0') {
			continue;
		}
		dst[count++] = c;
		put_utf8_char(con, &c, 1);
		if (count == len) {
			return count;
		}
	}
}

void console64_process_key(struct CONSOLE64 *con, uint8_t scancode)
{
	char c;

	if (keyboard64_track_modifier(scancode) != 0) {
		return;
	}
	if ((scancode & 0x80) != 0) {
		return;
	}
	if (con->repl_active) {
		if (scancode == 0x1c) {
			repl_queue_push(con, '\r');
			return;
		}
		if (scancode == 0x0e) {
			repl_queue_push(con, '\b');
			return;
		}
		c = translate_key(con, scancode);
		if (c == '\0') {
			return;
		}
		if (keyboard64_ctrl() != 0) {
			if (c >= 'a' && c <= 'z') {
				c = (char) (c - 'a' + 1);
			} else if (c >= 'A' && c <= 'Z') {
				c = (char) (c - 'A' + 1);
			}
		}
		repl_queue_push(con, c);
		return;
	}
	if (scancode == 0x1c) {
		execute_command(con);
		return;
	}
	if (scancode == 0x0e) {
		console_backspace(con);
		return;
	}
	c = translate_key(con, scancode);
	if (c == '\0') {
		return;
	}
	if (keyboard64_shift() != 0 && c == ' ') {
		flush_composing(con);
		con->lang_hangul ^= 1;
		return;
	}
	if (con->lang_hangul != 0 && hangul_font != NULL) {
		process_hangul_key(con, c);
	} else {
		not_korean(con, c);
	}
}

static void console_task_main(void)
{
	struct CONSOLE64 *con = console_self();

	for (;;) {
		console64_process_key(con, console_wait_key(con));
	}
}

struct CONSOLE64 *console64_create(void)
{
	static const char base[] = "터미널";
	char title[sizeof(base) + 2];
	struct CONSOLE64 *con;
	int32_t slot;
	uint32_t i;

	con = NULL;
	for (slot = 1; slot < CONSOLE64_MAX; slot++) {
		if (console_table[slot].task == NULL) {
			con = &console_table[slot];
			break;
		}
	}
	if (con == NULL) {
		return NULL;
	}
	con->cursor_x = 0;
	con->cursor_y = 0;
	con->input_len = 0;
	con->lang_hangul = 1;
	con->repl_active = 0;
	con->repl_queue_head = 0;
	con->repl_queue_tail = 0;
	hangul64_init(&con->composing);

	for (i = 0; i < sizeof(base) - 1; i++) {
		title[i] = base[i];
	}
	title[i] = '\0';

	if (gui64_open_console_window(con, title) != 0) {
		return NULL;
	}
	if (console64_start_task(con) != 0) {
		/* 창은 남는다. 콘솔 상한이 4라 실제로 닿을 일은 없고,
		   창을 되돌리려면 컴포지터에 파괴 경로가 필요하다. */
		return NULL;
	}
	prompt(con);
	return con;
}

/* 콘솔을 없앤다. 창을 닫을 때 컴포지터가 부른다 -- 태스크를 죽이고 슬롯을
   비워야 `new`가 그 번호를 다시 쓴다. task == NULL이 빈 슬롯 표시다.
   시트는 이미 없어졌으므로 vram/크기를 지워 늦게 오는 출력이 해제된
   버퍼를 건드리지 않게 한다. */
void console64_destroy(struct CONSOLE64 *con)
{
	if (con == NULL || con->task == NULL) {
		return;
	}
	task_kill64(con->task);
	con->task = NULL;
	con->keys.task = NULL;
	con->sheet = NULL;
	con->vram = NULL;
	con->width = 0;
	con->height = 0;
	con->repl_active = 0;
}

void console64_post_key(struct CONSOLE64 *con, uint8_t scancode)
{
	struct EVENT64 event;

	event.type = EVENT64_KEYBOARD;
	event.data = scancode;
	fifo64_put(&con->keys, event);
}

int console64_start_task(struct CONSOLE64 *con)
{
	struct TASK64 *task;
	uintptr_t stack;

	task = task_alloc64();
	stack = memman64_alloc_4k(&memman64, CONSOLE64_STACK_SIZE);
	if (task == NULL || stack == 0) {
		return -1;
	}
	/* console_self()가 태스크로 콘솔을 찾으므로 돌리기 전에 이어 둔다. */
	con->task = task;
	fifo64_init(&con->keys, CONSOLE64_KEY_BUF, con->key_buf, task);
	if (task_set_entry64(task, console_task_main, stack, CONSOLE64_STACK_SIZE) != 0) {
		con->task = NULL;
		return -1;
	}
	task_run64(task, 0, 2);
	return 0;
}

void console64_repl_set_active(int active)
{
	/* REPL을 켜는 건 MicroPython을 돌리는 그 콘솔이다. console_active로
	   두면 콘솔 1에서 py를 띄웠는데 콘솔 0이 REPL 모드가 된다. */
	struct CONSOLE64 *con = console_self();

	con->repl_active = active;
	con->repl_queue_head = 0;
	con->repl_queue_tail = 0;
}

/*
 * 명령줄 편집기를 그대로 빌려서 한 줄을 읽어 온다. 완성된 줄의 바이트 수,
 * Ctrl-C면 -1, Ctrl-D면 -2를 돌려준다(mowkow_porting.md 결정 8).
 *
 * 편집 버퍼(input_line)는 명령줄과 같은 것을 쓴다. 그리기와 백스페이스가
 * 전부 그 버퍼를 보고 있어서 따로 두면 코드가 갈라진다. 대신 부르는 쪽은
 * input_line을 가리키는 문자열(execute_command가 넘긴 인자 같은 것)을
 * 여기 오기 전에 자기 쪽으로 복사해 두어야 한다.
 */
int64_t console64_read_line(char *dst, uint64_t max)
{
	uint64_t n;
	uint64_t i;

	line_capture = 1;
	line_done = 0;
	line_result = 0;
	input_len = 0;
	line_full_warned = 0;
	while (line_done == 0) {
		console64_process_key(wait_key_event());
	}
	line_capture = 0;
	flush_composing();
	newline();
	if (line_result != 0) {
		input_len = 0;
		line_full_warned = 0;
		return line_result;
	}
	n = input_len;
	if (n > max) {
		n = max;
	}
	for (i = 0; i < n; i++) {
		dst[i] = input_line[i];
	}
	input_len = 0;
	line_full_warned = 0;
	return (int64_t) n;
}

int console64_repl_getchar(void)
{
	struct CONSOLE64 *con = console_self();
	char c;

	for (;;) {
		if (repl_queue_pop(con, &c)) {
			return (unsigned char) c;
		}
		console64_process_key(con, console_wait_key(con));
	}
}
