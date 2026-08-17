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

#define CONSOLE_INPUT_MAX 256
#define COLOR_BG_DEFAULT 0
/* 32비트 트리와 같은 값(COL8_FFFFFF). init_palette64가 15를 어두운 회색으로
   바꾸므로, 예전처럼 15를 쓰면 검정 배경에 어두운 회색 글씨가 된다. */
#define COLOR_FG_DEFAULT 7
#define FONT_W 8
#define FONT_H 16
#define HANGUL_W 16

extern const uint8_t hankaku64[4096];

static uint8_t *console_vram;
static uint32_t console_stride;
static uint16_t console_width;
static uint16_t console_height;
static uint16_t cursor_x;
static uint16_t cursor_y;
static char input_line[CONSOLE_INPUT_MAX];
static uint16_t input_len;
static int lang_hangul;
static int shift_down;
static int ctrl_down;
static int alt_down;
static struct HANGUL64 composing;
static const uint8_t *hangul_font;
static uint8_t color_fg = COLOR_FG_DEFAULT;
static uint8_t color_bg = COLOR_BG_DEFAULT;

/* 시트에 붙어 있으면 console_vram은 시트 버퍼의 내용 영역을 가리킨다.
   붙어 있지 않으면(초기 부팅, 컴포지터 할당 실패) 예전처럼 LFB에 직접 쓴다. */
static struct SHEET64 *console_sheet;
static uint16_t console_ox;
static uint16_t console_oy;

/*
 * raw 모드에서는 갱신 영역을 모아 두었다가 TTY_FLUSH에서 한 번에 올린다.
 * 편집기가 화면을 다시 그릴 때 글자마다 sheet64_refresh를 부르면 시트 더미를
 * 3700번 훑게 된다. cooked 모드는 예전처럼 즉시 올린다.
 */
static int32_t dirty_x0;
static int32_t dirty_y0;
static int32_t dirty_x1;
static int32_t dirty_y1;
static int dirty_valid;
static int raw_mode;

static void console_flush(int32_t x, int32_t y, int32_t w, int32_t h)
{
	if (console_sheet == NULL) {
		return;
	}
	if (raw_mode != 0) {
		if (dirty_valid == 0) {
			dirty_x0 = x;
			dirty_y0 = y;
			dirty_x1 = x + w;
			dirty_y1 = y + h;
			dirty_valid = 1;
			return;
		}
		if (x < dirty_x0) { dirty_x0 = x; }
		if (y < dirty_y0) { dirty_y0 = y; }
		if (x + w > dirty_x1) { dirty_x1 = x + w; }
		if (y + h > dirty_y1) { dirty_y1 = y + h; }
		return;
	}
	sheet64_refresh(console_sheet, console_ox + x, console_oy + y,
		console_ox + x + w, console_oy + y + h);
}

static void console_flush_dirty(void)
{
	if (console_sheet == NULL || dirty_valid == 0) {
		return;
	}
	sheet64_refresh(console_sheet, console_ox + dirty_x0, console_oy + dirty_y0,
		console_ox + dirty_x1, console_oy + dirty_y1);
	dirty_valid = 0;
}

#define REPL_QUEUE_SIZE 64
static char repl_queue[REPL_QUEUE_SIZE];
static uint32_t repl_queue_head;
static uint32_t repl_queue_tail;
static int repl_active;
static struct FIFO64 *console_event_fifo;

/*
 * raw 모드: 콘솔 줄 편집기와 에코를 끄고, 키 이벤트를 앱에게 그대로 넘긴다.
 * 한글 오토마타는 커널에 그대로 둔다(roadmap64.md 결정 11). 조합 중인 음절은
 * 그리지 않고 PREEDIT 이벤트로 내보내 앱이 커서 자리에 직접 그리게 한다.
 */
#define RAW_QUEUE_SIZE 64
static uint64_t raw_queue[RAW_QUEUE_SIZE];
static uint32_t raw_queue_head;
static uint32_t raw_queue_tail;
static uint32_t size_generation;

static void raw_queue_push(unsigned int kind, unsigned int payload)
{
	uint32_t next;
	uint64_t mods;

	next = (raw_queue_tail + 1) % RAW_QUEUE_SIZE;
	if (next == raw_queue_head) {
		return;
	}
	mods = 0;
	if (shift_down != 0) {
		mods |= TTY_MOD_SHIFT;
	}
	if (ctrl_down != 0) {
		mods |= TTY_MOD_CTRL;
	}
	if (alt_down != 0) {
		mods |= TTY_MOD_ALT;
	}
	raw_queue[raw_queue_tail] = (uint64_t) payload |
		((uint64_t) kind << 32) | (mods << 40);
	raw_queue_tail = next;
}

static int raw_queue_pop(uint64_t *out)
{
	if (raw_queue_head == raw_queue_tail) {
		return 0;
	}
	*out = raw_queue[raw_queue_head];
	raw_queue_head = (raw_queue_head + 1) % RAW_QUEUE_SIZE;
	return 1;
}

/* 조합 중인 상태를 코드포인트 하나로. 비어 있으면 0. */
static unsigned int composing_unicode(void)
{
	char utf8[4];
	int len;
	int decode_len;

	if (composing.state == 0) {
		return 0;
	}
	len = hangul64_compose_utf8(utf8, &composing);
	if (len <= 0) {
		return 0;
	}
	return utf8_to_unicode64(utf8, &decode_len);
}

static void raw_emit_preedit(void)
{
	raw_queue_push(TTY_KIND_PREEDIT, composing_unicode());
}

static void repl_queue_push(char c)
{
	uint32_t next;

	next = (repl_queue_tail + 1) % REPL_QUEUE_SIZE;
	if (next == repl_queue_head) {
		return;
	}
	repl_queue[repl_queue_tail] = c;
	repl_queue_tail = next;
}

static int repl_queue_pop(char *out)
{
	if (repl_queue_head == repl_queue_tail) {
		return 0;
	}
	*out = repl_queue[repl_queue_head];
	repl_queue_head = (repl_queue_head + 1) % REPL_QUEUE_SIZE;
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

static char translate_key(uint8_t scancode)
{
	char c;

	if (lang_hangul != 0 && shift_down != 0) {
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
	return shift_down != 0 ? keymap1[scancode] : keymap0[scancode];
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

static void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t color)
{
	uint16_t px;
	uint16_t py;

	for (py = y; py < y + h && py < console_height; py++) {
		for (px = x; px < x + w && px < console_width; px++) {
			console_vram[(uint32_t) py * console_stride + px] = color;
		}
	}
	console_flush(x, y, w, h);
}

static void draw_ascii(uint16_t x, uint16_t y, char c)
{
	const uint8_t *font;
	uint8_t d;
	uint16_t row;
	uint16_t bit;
	uint8_t *p;

	font = hankaku64 + (uint8_t) c * 16;
	for (row = 0; row < FONT_H; row++) {
		d = font[row];
		p = console_vram + (uint32_t) (y + row) * console_stride + x;
		for (bit = 0; bit < FONT_W; bit++) {
			if ((d & (0x80 >> bit)) != 0) {
				p[bit] = color_fg;
			}
		}
	}
	console_flush(x, y, FONT_W, FONT_H);
}

static void scroll_if_needed(void)
{
	uint32_t row;
	uint32_t col;

	if (cursor_y + FONT_H <= console_height) {
		return;
	}
	for (row = FONT_H; row < console_height; row++) {
		for (col = 0; col < console_width; col++) {
			console_vram[(row - FONT_H) * console_stride + col] =
				console_vram[row * console_stride + col];
		}
	}
	for (row = console_height - FONT_H; row < console_height; row++) {
		for (col = 0; col < console_width; col++) {
			console_vram[row * console_stride + col] = color_bg;
		}
	}
	cursor_y = console_height - FONT_H;
	console_flush(0, 0, console_width, console_height);
}

static void newline(void)
{
	cursor_x = 0;
	cursor_y += FONT_H;
	serial_putc('\n');
	scroll_if_needed();
}

static void erase_prev_visual(uint16_t width);

static void put_utf8_char(const char *s, int len)
{
	unsigned int unicode;
	uint16_t width;
	int decode_len;
	int i;

	if (len == 1 && s[0] == '\n') {
		newline();
		return;
	}
	if (len == 1 && s[0] == '\r') {
		return;
	}
	if (len == 1 && s[0] == '\b') {
		erase_prev_visual(FONT_W);
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
	if (cursor_x + width > console_width) {
		newline();
	}
	fill_rect(cursor_x, cursor_y, width, FONT_H, color_bg);
	if (width == HANGUL_W && hangul_font != NULL) {
		unicode = utf8_to_unicode64(s, &decode_len);
		hangul64_draw_unicode(console_vram, console_stride, cursor_x, cursor_y,
			color_fg, hangul_font, unicode);
		console_flush(cursor_x, cursor_y, HANGUL_W, FONT_H);
	} else if (len == 1) {
		draw_ascii(cursor_x, cursor_y, s[0]);
	}
	cursor_x += width;
	for (i = 0; i < len; i++) {
		serial_putc(s[i]);
	}
}

static void put_bytes(const char *s, size_t n)
{
	size_t i;
	int len;

	i = 0;
	while (i < n) {
		len = utf8_byte_len64((unsigned char) s[i]);
		if (len <= 0 || i + (size_t) len > n) {
			len = 1;
		}
		put_utf8_char(s + i, len);
		i += (size_t) len;
	}
}

static void erase_prev_visual(uint16_t width)
{
	if (cursor_x < width) {
		return;
	}
	cursor_x -= width;
	fill_rect(cursor_x, cursor_y, width, FONT_H, color_bg);
	serial_putc('\b');
	serial_putc(' ');
	serial_putc('\b');
}

static int append_input(const char *s, int len)
{
	int i;

	if (raw_mode != 0) {
		/* raw 모드에서는 줄 버퍼 대신 앱의 큐로 간다. 에코도 하지 않으므로
		   0을 돌려 호출자가 화면에 그리지 않게 한다. */
		int decode_len;

		raw_queue_push(TTY_KIND_CHAR, utf8_to_unicode64(s, &decode_len));
		return 0;
	}
	if (input_len + len >= CONSOLE_INPUT_MAX) {
		return 0;
	}
	for (i = 0; i < len; i++) {
		input_line[input_len++] = s[i];
	}
	return 1;
}

static void draw_composing(void)
{
	if (raw_mode != 0) {
		raw_emit_preedit();
		return;
	}
	if (cursor_x < HANGUL_W || hangul_font == NULL) {
		return;
	}
	fill_rect(cursor_x - HANGUL_W, cursor_y, HANGUL_W, FONT_H, color_bg);
	hangul64_draw_johab(console_vram, console_stride, cursor_x - HANGUL_W, cursor_y,
		color_fg, hangul_font, hangul64_to_johab(&composing));
	console_flush(cursor_x - HANGUL_W, cursor_y, HANGUL_W, FONT_H);
}

static void flush_composing(void)
{
	char utf8[4];
	int len;

	if (composing.state == 0) {
		return;
	}
	len = hangul64_compose_utf8(utf8, &composing);
	if (len > 0) {
		append_input(utf8, len);
	}
	hangul64_init(&composing);
}

static void start_new_hangul(int state, int cho, int jung, int jong)
{
	flush_composing();
	if (raw_mode == 0 && cursor_x + HANGUL_W > console_width) {
		newline();
	}
	composing.state = state;
	composing.cho = cho;
	composing.jung = jung;
	composing.jong = jong;
	if (raw_mode != 0) {
		raw_emit_preedit();
		return;
	}
	fill_rect(cursor_x, cursor_y, HANGUL_W, FONT_H, color_bg);
	hangul64_draw_johab(console_vram, console_stride, cursor_x, cursor_y,
		color_fg, hangul_font, hangul64_to_johab(&composing));
	console_flush(cursor_x, cursor_y, HANGUL_W, FONT_H);
	cursor_x += HANGUL_W;
}

static void update_composing(int state, int cho, int jung, int jong)
{
	composing.state = state;
	composing.cho = cho;
	composing.jung = jung;
	composing.jong = jong;
	draw_composing();
}

static void not_korean(char key)
{
	char s[1];

	flush_composing();
	s[0] = key;
	if (append_input(s, 1) != 0) {
		put_utf8_char(s, 1);
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

static void process_hangul_key(char key)
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
	switch (composing.state) {
	case 0:
		if (cho != -1) {
			start_new_hangul(1, cho, -1, -1);
		} else if (jung != -1) {
			start_new_hangul(1, -1, jung, -1);
			flush_composing();
		} else {
			not_korean(key);
		}
		break;
	case 1:
		if (jung != -1 && composing.cho != -1) {
			update_composing(2, composing.cho, jung, -1);
		} else if (cho != -1) {
			start_new_hangul(1, cho, -1, -1);
		} else {
			not_korean(key);
		}
		break;
	case 2:
		if (cho != -1 && cho_cannot_be_jong(cho) != 0) {
			start_new_hangul(1, cho, -1, -1);
		} else if (jong != -1) {
			update_composing(3, composing.cho, composing.jung, jong);
		} else if (jung != -1) {
			complex = hangul64_composite_jung(composing.jung, jung);
			if (complex != -1) {
				update_composing(2, composing.cho, complex, -1);
			} else {
				start_new_hangul(1, -1, jung, -1);
				flush_composing();
			}
		} else if (cho != -1) {
			start_new_hangul(1, cho, -1, -1);
		} else {
			not_korean(key);
		}
		break;
	case 3:
		if (jung != -1) {
			next_cho = hangul64_jong_to_cho(composing.jong);
			update_composing(2, composing.cho, composing.jung, -1);
			if (next_cho != -1) {
				start_new_hangul(2, next_cho, jung, -1);
			}
		} else if (cho != -1) {
			complex = hangul64_composite_jong(composing.jong, cho);
			if (complex != -1) {
				update_composing(4, composing.cho, composing.jung, complex);
			} else {
				start_new_hangul(1, cho, -1, -1);
			}
		} else {
			not_korean(key);
		}
		break;
	case 4:
		if (jung != -1) {
			first_jong = hangul64_first_jong(composing.jong);
			second_jong = hangul64_second_jong(composing.jong);
			update_composing(3, composing.cho, composing.jung, first_jong);
			start_new_hangul(2, second_jong, jung, -1);
		} else if (cho != -1) {
			start_new_hangul(1, cho, -1, -1);
		} else {
			not_korean(key);
		}
		break;
	default:
		hangul64_init(&composing);
		break;
	}
}

static int delete_composing(void)
{
	int prev_jung;

	if (composing.state == 0) {
		return 0;
	}
	if (composing.state == 1) {
		hangul64_init(&composing);
		if (raw_mode != 0) {
			raw_emit_preedit();      /* payload 0 = 조합 취소 */
		} else {
			erase_prev_visual(HANGUL_W);
		}
	} else if (composing.state == 2) {
		prev_jung = hangul64_split_composite_jung(composing.jung);
		if (prev_jung != -1) {
			update_composing(2, composing.cho, prev_jung, -1);
		} else {
			update_composing(1, composing.cho, -1, -1);
		}
	} else if (composing.state == 3) {
		update_composing(2, composing.cho, composing.jung, -1);
	} else if (composing.state == 4) {
		update_composing(3, composing.cho, composing.jung,
			hangul64_first_jong(composing.jong));
	}
	return 1;
}

static void console_backspace(void)
{
	uint16_t width;
	uint16_t start;

	if (delete_composing() != 0) {
		return;
	}
	if (input_len == 0) {
		return;
	}
	start = input_len - 1;
	while (start > 0 && (((uint8_t) input_line[start] & 0xc0) == 0x80)) {
		start--;
	}
	width = (input_len - start == 3) ? HANGUL_W : FONT_W;
	input_len = start;
	erase_prev_visual(width);
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

/* 이름은 UTF-8이다(긴 이름이든 8.3이든). 바이트마다 put_utf8_char를 부르면
   3바이트 한글이 세 글자로 깨지므로, 모아서 put_bytes로 넘긴다. */
static void print_file_name(const char *name)
{
	uint16_t n;

	for (n = 0; name[n] != '\0'; n++) {
	}
	put_bytes(name, n);
}

static void print_uint64(uint64_t value)
{
	char buf[20];
	uint16_t i;

	if (value == 0) {
		put_utf8_char("0", 1);
		return;
	}
	i = 0;
	while (value != 0) {
		buf[i++] = (char) ('0' + (value % 10));
		value /= 10;
	}
	while (i > 0) {
		put_utf8_char(&buf[--i], 1);
	}
}

static void print_hex64(uint64_t value)
{
	uint16_t shift;
	uint8_t digit;
	int started;
	char c;

	console64_puts("0x");
	started = 0;
	for (shift = 60; shift > 0; shift -= 4) {
		digit = (uint8_t) ((value >> shift) & 0x0f);
		if (digit != 0 || started != 0) {
			c = (char) (digit < 10 ? '0' + digit : 'a' + digit - 10);
			put_utf8_char(&c, 1);
			started = 1;
		}
	}
	digit = (uint8_t) (value & 0x0f);
	c = (char) (digit < 10 ? '0' + digit : 'a' + digit - 10);
	put_utf8_char(&c, 1);
}

static void prompt(void)
{
	console64_puts("> ");
}

void console64_prompt(void)
{
	prompt();
}

static void clear_screen(void)
{
	fill_rect(0, 0, console_width, console_height, color_bg);
	cursor_x = 0;
	cursor_y = 0;
}

/* Loads and runs cmdline's first token as an app. Returns 0 when there is no
   such file, so the caller can report an unknown command instead. */
static int run_program(const char *cmdline)
{
	int status;

	status = process64_exec_file(cmdline, cmdline);
	if (status == -2) {
		return 0;
	}
	console64_puts("exit ");
	print_uint64((uint64_t) status);
	console64_puts("\n");
	return 1;
}

static void execute_command(void)
{
	flush_composing();
	input_line[input_len] = '\0';
	newline();
	if (input_len == 0) {
		prompt();
		return;
	}
	if (str_eq(input_line, "help")) {
		console64_puts("commands: help clear ticks mem tasks ls 목록 type readme.txt py py FILE.PY xwindow 창\n");
		console64_puts("apps: type the file name, e.g. HELLO or 나노 FILE.TXT\n");
	} else if (str_eq(input_line, "xwindow") || str_eq(input_line, "window") ||
			str_eq(input_line, "창")) {
		gui64_toggle_window();
	} else if (str_eq(input_line, "clear") || str_eq(input_line, "지우기")) {
		clear_screen();
	} else if (str_eq(input_line, "ticks")) {
		console64_puts("ticks ");
		print_uint64(timerctl64.count);
		console64_puts("\n");
	} else if (str_eq(input_line, "mem") || str_eq(input_line, "메모리")) {
		uintptr_t addr;

		console64_puts("free ");
		print_uint64(memman64_total(&memman64) / 1024);
		console64_puts(" KiB\n");
		addr = memman64_alloc_4k(&memman64, 4096);
		console64_puts("alloc4k ");
		print_hex64(addr);
		console64_puts("\n");
		if (addr != 0) {
			memman64_free_4k(&memman64, addr, 4096);
		}
	} else if (str_eq(input_line, "tasks") || str_eq(input_line, "태스크")) {
		console64_puts("switches ");
		print_uint64(taskctl64.switches);
		console64_puts(" current-level ");
		print_uint64(taskctl64.now_lv);
		console64_puts("\n");
	} else if (str_eq(input_line, "ls") || str_eq(input_line, "목록")) {
		uint32_t i;
		uint32_t count;
		struct FDINFO64 finfo;
		char name[FD64_NAME_MAX];

		count = fd64_file_count();
		for (i = 0; i < count; i++) {
			if (fd64_file_at(i, &finfo, name, sizeof(name)) != 0) {
				print_file_name(name);
				console64_puts("  ");
				print_uint64(finfo.size);
				console64_puts("\n");
			}
		}
		if (count == 0) {
			console64_puts("no files\n");
		}
	} else if (str_eq(input_line, "type readme.txt") || str_eq(input_line, "읽기 readme.txt")) {
		struct FDHANDLE64 fh;
		char buf[65];
		size_t n;

		if (fd64_open(&fh, "readme.txt") == 0) {
			console64_puts("file not found\n");
		} else {
			for (;;) {
				n = fd64_read(&fh, buf, sizeof(buf) - 1);
				if (n == 0) {
					break;
				}
				put_bytes(buf, n);
			}
			console64_puts("\n");
		}
	} else if (str_starts_with(input_line, "run ") || str_starts_with(input_line, "실행 ")) {
		const char *args;

		/* "실행 " is 7 bytes of UTF-8, "run " is 4: a fixed offset would cut
		   the Korean form mid-character. */
		args = input_line + (input_line[0] == 'r' ? 4 : 7);
		if (run_program(args) == 0) {
			console64_puts("file not found\n");
		}
	} else if (str_eq(input_line, "py") || str_eq(input_line, "파이썬")) {
		mpport_repl();
	} else if (str_starts_with(input_line, "py ")) {
		mpport_run_file(input_line + 3);
	} else if (run_program(input_line) == 0) {
		/* not a builtin and no such executable */
		console64_puts("unknown command\n");
	}
	input_len = 0;
	prompt();
}

void console64_set_hangul_font(const uint8_t *font)
{
	hangul_font = font;
}

const uint8_t *console64_hangul_font(void)
{
	return hangul_font;
}

void console64_attach_sheet(struct SHEET64 *sht, uint16_t ox, uint16_t oy,
	uint16_t w, uint16_t h)
{
	console_sheet = sht;
	console_ox = ox;
	console_oy = oy;
	console_vram = sht->buf + (uint32_t) oy * (uint32_t) sht->bxsize + ox;
	console_stride = (uint32_t) sht->bxsize;
	console_width = w;
	console_height = h;
	cursor_x = 0;
	cursor_y = 0;
	/* 크기가 바뀌었고 화면도 지워진다. raw 모드 앱은 TTY_READKEY에서 자고
	   있으므로, 깨워 주지 않으면 다음 키를 누를 때까지 빈 화면을 본다.
	   시그널 없는 SIGWINCH가 이 이벤트다. */
	size_generation++;
	if (raw_mode != 0) {
		raw_queue_push(TTY_KIND_RESIZE, 0);
	}
	clear_screen();
}

void console64_init(const struct BOOTINFO64 *boot_info)
{
	struct SHEET64 *sht;

	console_vram = (uint8_t *) boot_info->vram;
	console_width = boot_info->scrnx != 0 ? boot_info->scrnx : 800;
	console_height = boot_info->scrny != 0 ? boot_info->scrny : 600;
	console_stride = boot_info->bytes_per_scanline != 0 ?
		boot_info->bytes_per_scanline : console_width;
	cursor_x = 0;
	cursor_y = 0;
	input_len = 0;
	lang_hangul = 1;
	shift_down = 0;
	hangul64_init(&composing);
	/* 컴포지터가 뜨면 콘솔은 전체 화면 창 하나가 된다 (로드맵 decision 3b).
	   실패하면 지금까지처럼 LFB에 직접 그린다. */
	sht = gui64_init(boot_info);
	if (sht != NULL) {
		console64_attach_sheet(sht, 0, 0, console_width, console_height);
	} else {
		clear_screen();
	}
	// console64_puts("Mowkow OS x86_64 console\n");
	console64_puts("머꼬 OS x86_64 콘솔\n");
	// console64_puts("Hangul input is default. Shift+Space toggles English.\n");
	console64_puts("한글 입력이 기본입니다. Shift+Space로 영어 입력으로 전환합니다.\n");
	prompt();
}

void console64_puts(const char *s)
{
	while (*s != '\0') {
		int len;

		len = utf8_byte_len64((unsigned char) *s);
		if (len <= 0) {
			len = 1;
		}
		put_utf8_char(s, len);
		s += len;
	}
}

void console64_write(const char *s, uint64_t len)
{
	put_bytes(s, (size_t) len);
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
 * 키보드 이벤트 하나를 기다린다. FIFO가 비면 태스크를 재운다.
 * i8042 포트를 직접 돌려보지 않으므로 IRQ 핸들러와 스캔코드를 다투지 않고,
 * 기다리는 동안 다른 태스크가 돈다.
 */
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
	uint64_t count;
	uint16_t key;
	char c;

	if (dst == 0 || len == 0) {
		return 0;
	}
	count = 0;
	for (;;) {
		key = wait_key_event();
		if (normalize_ext_key(&key) == 0) {
			continue;
		}
		if (key == 0x2a || key == 0x36) {
			shift_down = 1;
			continue;
		}
		if (key == 0xaa || key == 0xb6) {
			shift_down = 0;
			continue;
		}
		if ((key & 0x80) != 0) {
			continue;
		}
		if (key == 0x1c) {
			dst[count++] = '\n';
			put_utf8_char("\n", 1);
			return count;
		}
		if (key == 0x0e) {
			if (count > 0) {
				count--;
				erase_prev_visual(FONT_W);
			}
			continue;
		}
		c = shift_down != 0 ? keymap1[key] : keymap0[key];
		if (c == '\0') {
			continue;
		}
		dst[count++] = c;
		put_utf8_char(&c, 1);
		if (count == len) {
			return count;
		}
	}
}

/*
 * raw 모드의 키 처리. 줄 편집도 에코도 하지 않고 이벤트만 쌓는다.
 * 한글은 여기서도 커널이 조합한다 - 완성되면 CHAR, 조합 중이면 PREEDIT.
 */
static void raw_process_key(uint16_t key)
{
	char c;

	if ((key & 0x80) != 0) {
		return;                     /* 브레이크 코드 */
	}
	if ((key & KEY64_EXT) != 0) {
		if (key == KEY64_KPENTER) {
			flush_composing();
			raw_queue_push(TTY_KIND_CHAR, '\n');
		} else {
			raw_queue_push(TTY_KIND_KEY, key);
		}
		return;
	}
	if (key == 0x1c) {
		flush_composing();
		raw_queue_push(TTY_KIND_CHAR, '\n');
		return;
	}
	if (key == 0x0e) {
		if (delete_composing() == 0) {
			raw_queue_push(TTY_KIND_CHAR, '\b');
		}
		return;
	}
	if (key == 0x0f) {
		flush_composing();
		raw_queue_push(TTY_KIND_CHAR, '\t');
		return;
	}
	if (key == 0x01) {
		flush_composing();
		raw_queue_push(TTY_KIND_CHAR, 0x1b);
		return;
	}
	c = translate_key(key);
	if (c == '\0') {
		return;
	}
	if (shift_down != 0 && c == ' ') {
		flush_composing();
		lang_hangul ^= 1;
		return;
	}
	if (ctrl_down != 0) {
		flush_composing();
		if (c >= 'a' && c <= 'z') {
			c = (char) (c - 'a' + 1);
		} else if (c >= 'A' && c <= 'Z') {
			c = (char) (c - 'A' + 1);
		}
		raw_queue_push(TTY_KIND_CHAR, (unsigned char) c);
		return;
	}
	if (lang_hangul != 0) {
		process_hangul_key(c);
	} else {
		not_korean(c);              /* raw에서는 큐로 밀고 에코하지 않는다 */
	}
}

void console64_process_key(uint16_t scancode)
{
	char c;

	if (scancode == KEY64_RCTRL || scancode == 0x1d) {
		ctrl_down = 1;
		return;
	}
	if (scancode == (KEY64_RCTRL | 0x80) || scancode == 0x9d) {
		ctrl_down = 0;
		return;
	}
	if (scancode == KEY64_RALT || scancode == 0x38) {
		alt_down = 1;
		return;
	}
	if (scancode == (KEY64_RALT | 0x80) || scancode == 0xb8) {
		alt_down = 0;
		return;
	}
	if (scancode == 0x2a || scancode == 0x36) {
		shift_down = 1;
		return;
	}
	if (scancode == 0xaa || scancode == 0xb6) {
		shift_down = 0;
		return;
	}
	if (raw_mode != 0) {
		raw_process_key(scancode);
		return;
	}
	if (normalize_ext_key(&scancode) == 0) {
		return;
	}
	if ((scancode & 0x80) != 0) {
		return;
	}
	if (repl_active) {
		if (scancode == 0x1c) {
			repl_queue_push('\r');
			return;
		}
		if (scancode == 0x0e) {
			repl_queue_push('\b');
			return;
		}
		c = translate_key(scancode);
		if (c == '\0') {
			return;
		}
		if (ctrl_down) {
			if (c >= 'a' && c <= 'z') {
				c = (char) (c - 'a' + 1);
			} else if (c >= 'A' && c <= 'Z') {
				c = (char) (c - 'A' + 1);
			}
		}
		repl_queue_push(c);
		return;
	}
	if (scancode == 0x1c) {
		execute_command();
		return;
	}
	if (scancode == 0x0e) {
		console_backspace();
		return;
	}
	c = translate_key(scancode);
	if (c == '\0') {
		return;
	}
	if (shift_down != 0 && c == ' ') {
		flush_composing();
		lang_hangul ^= 1;
		return;
	}
	if (lang_hangul != 0 && hangul_font != NULL) {
		process_hangul_key(c);
	} else {
		not_korean(c);
	}
}

/*
 * 한글 오토마타 회귀 확인. 부팅 때마다 COM1으로 결과를 알린다.
 *
 * raw 모드를 잠깐 켜서 조합 상태만 들여다본다 - raw에서는 start_new_hangul과
 * draw_composing이 화면에 그리지 않으므로 폰트도 VRAM도 필요 없다.
 * 넣는 값은 translate_key가 내놓는 문자, 즉 두벌식 자판 그대로다.
 *
 * 쌍자음 받침(ㄲ, ㅆ)이 한 번 막혀 있어서 있/닦을 아예 칠 수 없었다.
 * 겹받침(ㄵ, ㅄ)과 "받침이 못 되는 쌍자음"(ㄸ)도 같이 붙잡아 둔다.
 */
struct HANGUL_CASE {
	const char *keys;
	unsigned int want;
};

int console64_hangul_smoke(void)
{
	static const struct HANGUL_CASE cases[] = {
		{ "dlT",  0xc788 },      /* 있 - 쌍시옷 받침 */
		{ "ekR",  0xb2e6 },      /* 닦 - 쌍기역 받침 */
		{ "qkR",  0xbc16 },      /* 밖 */
		{ "ruR",  0xacaa },      /* 겪 */
		{ "dksw", 0xc549 },      /* 앉 - 겹받침 ㄵ */
		{ "djqt", 0xc5c6 },      /* 없 - 겹받침 ㅄ */
		{ "ekE",  0x3138 },      /* 다 + ㄸ: ㄸ는 받침이 못 되니 새 글자 */
	};
	struct HANGUL64 saved_composing;
	uint32_t saved_head;
	uint32_t saved_tail;
	int saved_raw;
	int saved_shift;
	uint32_t i;
	int j;
	int ok;

	saved_composing = composing;
	saved_head = raw_queue_head;
	saved_tail = raw_queue_tail;
	saved_raw = raw_mode;
	saved_shift = shift_down;

	ok = 1;
	raw_mode = 1;
	shift_down = 0;
	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		hangul64_init(&composing);
		raw_queue_head = 0;
		raw_queue_tail = 0;
		for (j = 0; cases[i].keys[j] != '\0'; j++) {
			process_hangul_key(cases[i].keys[j]);
		}
		if (composing_unicode() != cases[i].want) {
			ok = 0;
			break;
		}
	}

	raw_mode = saved_raw;
	shift_down = saved_shift;
	composing = saved_composing;
	raw_queue_head = saved_head;
	raw_queue_tail = saved_tail;
	return ok;
}

void console64_set_raw(int on)
{
	int was_raw;

	was_raw = raw_mode;
	raw_mode = on != 0;
	raw_queue_head = 0;
	raw_queue_tail = 0;
	dirty_valid = 0;
	if (raw_mode == 0) {
		/* 앱이 조합 중인 채로 나가도, 색을 바꿔 놓고 나가도 콘솔이 그 상태를
		   물려받지 않게 한다. */
		hangul64_init(&composing);
		color_fg = COLOR_FG_DEFAULT;
		color_bg = COLOR_BG_DEFAULT;
		if (was_raw != 0) {
			/* 앱이 남긴 화면과 화면 한복판의 커서를 그대로 물려받으면
			   콘솔이 망가진 것처럼 보인다. 앱의 마지막 화면을 버리고
			   깨끗한 상태에서 다시 시작한다. */
			clear_screen();
		}
	}
}

int console64_is_raw(void)
{
	return raw_mode;
}

/*
 * 칸 단위 크기와 세대 값. 칸 너비는 FONT_W(8픽셀)이라 한글 한 글자는 두 칸을
 * 차지한다. 세대 값이 달라졌으면 크기가 바뀌었고 화면도 지워진 것이다.
 */
uint64_t console64_size(void)
{
	uint64_t cols;
	uint64_t rows;

	cols = console_width / FONT_W;
	rows = console_height / FONT_H;
	return cols | (rows << 16) | ((uint64_t) size_generation << 32);
}

void console64_move(uint32_t row, uint32_t col)
{
	uint32_t max_col;
	uint32_t max_row;

	max_col = console_width / FONT_W;
	max_row = console_height / FONT_H;
	if (max_col == 0 || max_row == 0) {
		return;
	}
	if (col >= max_col) {
		col = max_col - 1;
	}
	if (row >= max_row) {
		row = max_row - 1;
	}
	cursor_x = (uint16_t) (col * FONT_W);
	cursor_y = (uint16_t) (row * FONT_H);
}

void console64_clear_cells(uint32_t row, uint32_t col, uint32_t rows, uint32_t cols)
{
	uint32_t max_col;
	uint32_t max_row;

	max_col = console_width / FONT_W;
	max_row = console_height / FONT_H;
	if (row >= max_row || col >= max_col) {
		return;
	}
	if (rows > max_row - row) {
		rows = max_row - row;
	}
	if (cols > max_col - col) {
		cols = max_col - col;
	}
	if (rows == 0 || cols == 0) {
		return;
	}
	fill_rect((uint16_t) (col * FONT_W), (uint16_t) (row * FONT_H),
		(uint16_t) (cols * FONT_W), (uint16_t) (rows * FONT_H), color_bg);
}

void console64_set_attr(uint8_t fg, uint8_t bg)
{
	color_fg = fg;
	color_bg = bg;
}

void console64_flush(void)
{
	console_flush_dirty();
}

uint64_t console64_read_key(void)
{
	uint64_t out;
	uint16_t key;

	for (;;) {
		/* 이벤트 하나마다 큐를 다시 본다. 마우스 처리가 RESIZE를 넣을 수
		   있으므로, 키를 기다리며 눌러앉으면 안 된다. */
		if (raw_queue_pop(&out) != 0) {
			return out;
		}
		if (pump_event(&key) != 0) {
			console64_process_key(key);
		}
	}
}

void console64_set_event_fifo(struct FIFO64 *fifo)
{
	console_event_fifo = fifo;
}

void console64_repl_set_active(int active)
{
	repl_active = active;
	repl_queue_head = 0;
	repl_queue_tail = 0;
}

int console64_repl_getchar(void)
{
	char c;

	for (;;) {
		if (repl_queue_pop(&c)) {
			return (unsigned char) c;
		}
		console64_process_key(wait_key_event());
	}
}
