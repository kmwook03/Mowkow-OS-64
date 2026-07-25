#include <asmfunc64.h>
#include <bootinfo64.h>
#include <console64.h>
#include <fd64.h>
#include <fifo64.h>
#include <hangul64.h>
#include <memory64.h>
#include <mpport64.h>
#include <mtask64.h>
#include <process64.h>
#include <timer64.h>
#include <utf864.h>
#include <stddef.h>
#include <stdint.h>

#define CONSOLE_INPUT_MAX 256
#define COLOR_BG 0
#define COLOR_FG 15
#define FONT_W 8
#define FONT_H 16
#define HANGUL_W 16
#define KBD_STATUS_PORT 0x64
#define KBD_DATA_PORT   0x60

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
static struct HANGUL64 composing;
static const uint8_t *hangul_font;

#define REPL_QUEUE_SIZE 64
static char repl_queue[REPL_QUEUE_SIZE];
static uint32_t repl_queue_head;
static uint32_t repl_queue_tail;
static int repl_active;
static struct FIFO64 *repl_event_fifo;

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
				p[bit] = COLOR_FG;
			}
		}
	}
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
			console_vram[row * console_stride + col] = COLOR_BG;
		}
	}
	cursor_y = console_height - FONT_H;
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
	fill_rect(cursor_x, cursor_y, width, FONT_H, COLOR_BG);
	if (width == HANGUL_W && hangul_font != NULL) {
		unicode = utf8_to_unicode64(s, &decode_len);
		hangul64_draw_unicode(console_vram, console_stride, cursor_x, cursor_y,
			COLOR_FG, hangul_font, unicode);
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
	fill_rect(cursor_x, cursor_y, width, FONT_H, COLOR_BG);
	serial_putc('\b');
	serial_putc(' ');
	serial_putc('\b');
}

static int append_input(const char *s, int len)
{
	int i;

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
	if (cursor_x < HANGUL_W || hangul_font == NULL) {
		return;
	}
	fill_rect(cursor_x - HANGUL_W, cursor_y, HANGUL_W, FONT_H, COLOR_BG);
	hangul64_draw_johab(console_vram, console_stride, cursor_x - HANGUL_W, cursor_y,
		COLOR_FG, hangul_font, hangul64_to_johab(&composing));
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
	if (cursor_x + HANGUL_W > console_width) {
		newline();
	}
	composing.state = state;
	composing.cho = cho;
	composing.jung = jung;
	composing.jong = jong;
	fill_rect(cursor_x, cursor_y, HANGUL_W, FONT_H, COLOR_BG);
	hangul64_draw_johab(console_vram, console_stride, cursor_x, cursor_y,
		COLOR_FG, hangul_font, hangul64_to_johab(&composing));
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

static int is_double_cho(int cho)
{
	return cho == 1 || cho == 4 || cho == 8 || cho == 10 || cho == 13;
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
		if (cho != -1 && is_double_cho(cho) != 0) {
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
		erase_prev_visual(HANGUL_W);
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

static void print_file_name(const struct FDINFO64 *finfo)
{
	uint16_t i;

	for (i = 0; i < 8; i++) {
		if (finfo->name[i] != ' ') {
			put_utf8_char((const char *) &finfo->name[i], 1);
		}
	}
	if (finfo->ext[0] != ' ') {
		put_utf8_char(".", 1);
		for (i = 0; i < 3; i++) {
			if (finfo->ext[i] != ' ') {
				put_utf8_char((const char *) &finfo->ext[i], 1);
			}
		}
	}
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

static void clear_screen(void)
{
	fill_rect(0, 0, console_width, console_height, COLOR_BG);
	cursor_x = 0;
	cursor_y = 0;
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
		console64_puts("commands: help clear ticks mem tasks ls 목록 type readme.txt run HELLO py py FILE.PY\n");
	} else if (str_eq(input_line, "clear")) {
		clear_screen();
	} else if (str_eq(input_line, "ticks")) {
		console64_puts("ticks ");
		print_uint64(timerctl64.count);
		console64_puts("\n");
	} else if (str_eq(input_line, "mem")) {
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
	} else if (str_eq(input_line, "tasks")) {
		console64_puts("switches ");
		print_uint64(taskctl64.switches);
		console64_puts(" current-level ");
		print_uint64(taskctl64.now_lv);
		console64_puts("\n");
	} else if (str_eq(input_line, "ls") || str_eq(input_line, "목록")) {
		uint32_t i;
		uint32_t count;
		const struct FDINFO64 *finfo;

		count = fd64_file_count();
		for (i = 0; i < count; i++) {
			finfo = fd64_file_at(i);
			if (finfo != NULL) {
				print_file_name(finfo);
				console64_puts("  ");
				print_uint64(finfo->size);
				console64_puts("\n");
			}
		}
		if (count == 0) {
			console64_puts("no files\n");
		}
	} else if (str_eq(input_line, "type readme.txt")) {
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
	} else if (str_starts_with(input_line, "run ")) {
		int status;

		status = process64_exec_file(input_line + 4, input_line + 4);
		console64_puts("exit ");
		print_uint64((uint64_t) status);
		console64_puts("\n");
	} else if (str_eq(input_line, "py")) {
		mpport_repl();
	} else if (str_starts_with(input_line, "py ")) {
		mpport_run_file(input_line + 3);
	} else {
		console64_puts("unknown command\n");
	}
	input_len = 0;
	prompt();
}

void console64_set_hangul_font(const uint8_t *font)
{
	hangul_font = font;
}

void console64_init(const struct BOOTINFO64 *boot_info)
{
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
	clear_screen();
	console64_puts("Mowkow OS x86_64 console\n");
	console64_puts("Hangul input is default. Shift+Space toggles English.\n");
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

uint64_t console64_read(char *dst, uint64_t len)
{
	uint64_t count;
	uint8_t scancode;
	char c;

	if (dst == 0 || len == 0) {
		return 0;
	}
	count = 0;
	for (;;) {
		while ((io_in8(KBD_STATUS_PORT) & 0x01) == 0) {
		}
		scancode = io_in8(KBD_DATA_PORT);
		if (scancode == 0x2a || scancode == 0x36) {
			shift_down = 1;
			continue;
		}
		if (scancode == 0xaa || scancode == 0xb6) {
			shift_down = 0;
			continue;
		}
		if ((scancode & 0x80) != 0) {
			continue;
		}
		if (scancode == 0x1c) {
			dst[count++] = '\n';
			put_utf8_char("\n", 1);
			return count;
		}
		if (scancode == 0x0e) {
			if (count > 0) {
				count--;
				erase_prev_visual(FONT_W);
			}
			continue;
		}
		c = shift_down != 0 ? keymap1[scancode] : keymap0[scancode];
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

void console64_process_key(uint8_t scancode)
{
	char c;

	if (scancode == 0x2a || scancode == 0x36) {
		shift_down = 1;
		return;
	}
	if (scancode == 0xaa || scancode == 0xb6) {
		shift_down = 0;
		return;
	}
	if (scancode == 0x1d) {
		ctrl_down = 1;
		return;
	}
	if (scancode == 0x9d) {
		ctrl_down = 0;
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

void console64_set_event_fifo(struct FIFO64 *fifo)
{
	repl_event_fifo = fifo;
}

void console64_repl_set_active(int active)
{
	repl_active = active;
	repl_queue_head = 0;
	repl_queue_tail = 0;
}

int console64_repl_getchar(void)
{
	struct EVENT64 event;
	char c;

	for (;;) {
		if (repl_queue_pop(&c)) {
			return (unsigned char) c;
		}
		io_cli();
		if (repl_event_fifo != NULL && fifo64_get(repl_event_fifo, &event) == 0) {
			io_sti();
			if (event.type == EVENT64_KEYBOARD) {
				console64_process_key((uint8_t) event.data);
			}
		} else {
			task_sleep64(task_now64());
			io_sti();
		}
	}
}
