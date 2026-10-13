#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "../src64/kernel/console_render64.c"
#include "../src64/kernel/console_queue64.c"
#include "../src64/kernel/console_editor64.c"

static uint64_t irq_depth64;
uint64_t platform_irq_save64(void) { return irq_depth64++; }
void platform_irq_restore64(uint64_t state)
{
	assert(irq_depth64 == state + 1);
	irq_depth64--;
}

#define CHECK64(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "Hangul check failed at line %d\n", __LINE__); \
		return 1; \
	} \
} while (0)

#define WIDTH64 512
#define HEIGHT64 64

const uint8_t hankaku64[4096] = { ['1' * 16] = 0x80 };
static uint8_t font64[11520];
static char serial64[8192];
static size_t serial_length64;
static int shift64;
static int ctrl64;

struct DISPLAY64 {
	uint8_t before[32];
	uint8_t pixels[WIDTH64 * HEIGHT64];
	uint8_t after[32];
};

uint8_t io_in8(uint16_t port)
{
	(void) port;
	return 0x20;
}

void io_out8(uint16_t port, uint8_t data)
{
	(void) port;
	if (serial_length64 < sizeof(serial64)) {
		serial64[serial_length64++] = (char) data;
	}
}

int keyboard64_shift(void)
{
	return shift64;
}

int keyboard64_ctrl(void)
{
	return ctrl64;
}

int keyboard64_alt(void)
{
	return 0;
}

int keyboard64_track_modifier(uint16_t key)
{
	(void) key;
	return 0;
}

void sheet64_refresh(struct SHEET64 *sheet, int32_t x0, int32_t y0,
	int32_t x1, int32_t y1)
{
	(void) sheet;
	(void) x0;
	(void) y0;
	(void) x1;
	(void) y1;
}

static void init_console64(struct CONSOLE64 *con, struct DISPLAY64 *display,
	uint16_t width, int raw)
{
	memset(con, 0, sizeof(*con));
	memset(display, 0xa5, sizeof(*display));
	memset(display->pixels, 0, sizeof(display->pixels));
	con->vram = display->pixels;
	con->vram_size = sizeof(display->pixels);
	con->stride = WIDTH64;
	con->width = width;
	con->height = HEIGHT64;
	con->raw_mode = raw;
	con->lang_hangul = 1;
	hangul64_init(&con->composing);
	color_fg = 7;
	color_bg = 0;
	shift64 = 0;
	ctrl64 = 0;
}

static int guards_valid64(const struct DISPLAY64 *display)
{
	for (size_t i = 0; i < sizeof(display->before); i++) {
		CHECK64(display->before[i] == 0xa5 && display->after[i] == 0xa5);
	}
	return 0;
}

static int line_matches64(const struct CONSOLE64 *con, const char *text)
{
	CHECK64(con->input_len == strlen(text));
	CHECK64(memcmp(con->input_line, text, strlen(text)) == 0);
	return 0;
}

static int pop_event64(struct CONSOLE64 *con, unsigned int kind, unsigned int payload)
{
	uint64_t event;

	CHECK64(console_raw_pop64(con, &event) == 1);
	CHECK64((unsigned int) event == payload);
	CHECK64(((event >> 32) & 0xff) == kind);
	return 0;
}

static int golden_cases64(void)
{
	static const struct {
		const char *keys;
		const char *text;
	} cases[] = {
		{ "r", "ㄱ" }, { "k", "ㅏ" }, { "rr", "ㄱㄱ" },
		{ "rk", "가" }, { "rhk", "과" }, { "rhl", "괴" },
		{ "rnj", "궈" }, { "rnl", "귀" }, { "rml", "긔" },
		{ "dlT", "있" }, { "ekR", "닦" }, { "qkR", "밖" }, { "ruR", "겪" },
		{ "dksw", "앉" }, { "djqt", "없" }, { "rkrt", "갃" },
		{ "rkrk", "가가" }, { "rkrtk", "각사" },
		{ "dkswk", "안자" }, { "djqtj", "업서" },
		{ "ekE", "다ㄸ" }, { "ekQ", "다ㅃ" }, { "ekW", "다ㅉ" },
		{ "rkk", "가ㅏ" }, { "rk1", "가1" }, { "rk ", "가 " },
		{ "rhk\b", "고" }, { "rhk\b\b", "ㄱ" }, { "rhk\b\b\b", "" },
		{ "rkrt\b", "각" }, { "rkrt\b\b", "가" },
		{ "rkrt\b\b\b", "ㄱ" }, { "rkrt\b\b\b\b", "" },
	};
	struct CONSOLE64 cooked;
	struct CONSOLE64 raw;
	struct CONSOLE64 reference;
	struct DISPLAY64 screen;
	struct DISPLAY64 raw_screen;
	struct DISPLAY64 expected;
	char committed[128];
	char encoded[4];
	uint64_t event;
	size_t length;
	int bytes;

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		for (unsigned int width = 32; width <= WIDTH64; width *= 16) {
			init_console64(&cooked, &screen, (uint16_t) width, 0);
			init_console64(&raw, &raw_screen, (uint16_t) width, 1);
			serial_length64 = 0;
			for (size_t j = 0; cases[i].keys[j] != '\0'; j++) {
				char key = cases[i].keys[j];

				if (key == '\b') {
					console_backspace64(&cooked);
					raw_process_key64(&raw, 0x0e);
				} else {
					process_hangul_key64(&cooked, key);
					process_hangul_key64(&raw, key);
				}
				CHECK64(memcmp(&cooked.composing, &raw.composing,
					sizeof(cooked.composing)) == 0);
			}
			flush_composing64(&cooked);
			flush_composing64(&raw);
			CHECK64(line_matches64(&cooked, cases[i].text) == 0);
			length = 0;
			while (console_raw_pop64(&raw, &event) != 0) {
				if (((event >> 32) & 0xff) == TTY_KIND_CHAR) {
					bytes = unicode_to_utf864((unsigned int) event, encoded, 4);
					CHECK64(bytes > 0 && length + (size_t) bytes < sizeof(committed));
					memcpy(committed + length, encoded, (size_t) bytes);
					length += (size_t) bytes;
				}
			}
			committed[length] = '\0';
			CHECK64(strcmp(committed, cases[i].text) == 0);
			CHECK64(raw.input_len == 0 && raw.cursor_x == 0 && raw.cursor_y == 0);
			init_console64(&reference, &expected, (uint16_t) width, 0);
			put_bytes64(&reference, cases[i].text, strlen(cases[i].text));
			CHECK64(memcmp(screen.pixels, expected.pixels, sizeof(screen.pixels)) == 0);
			CHECK64(cooked.cursor_x == reference.cursor_x &&
				cooked.cursor_y == reference.cursor_y);
			CHECK64(guards_valid64(&screen) == 0 && guards_valid64(&raw_screen) == 0);
		}
	}
	return 0;
}

static int protocol64(void)
{
	struct CONSOLE64 con;
	struct DISPLAY64 screen;
	uint64_t event;
	static const uint32_t preedit[] = { 0x3131, 0xac00, 0xac01, 0xac03 };
	const char *keys = "rkrt";

	init_console64(&con, &screen, WIDTH64, 1);
	serial_length64 = 0;
	for (size_t i = 0; i < 4; i++) {
		process_hangul_key64(&con, keys[i]);
		CHECK64(pop_event64(&con, TTY_KIND_PREEDIT, preedit[i]) == 0);
	}
	process_hangul_key64(&con, 'k');
	CHECK64(pop_event64(&con, TTY_KIND_CHAR, 0xac01) == 0);
	CHECK64(pop_event64(&con, TTY_KIND_PREEDIT, 0xc0ac) == 0);
	raw_process_key64(&con, 0x1c);
	CHECK64(pop_event64(&con, TTY_KIND_CHAR, 0xc0ac) == 0);
	CHECK64(pop_event64(&con, TTY_KIND_CHAR, '\n') == 0);
	CHECK64(console_raw_pop64(&con, &event) == 0);
	process_hangul_key64(&con, 'r');
	CHECK64(pop_event64(&con, TTY_KIND_PREEDIT, 0x3131) == 0);
	raw_process_key64(&con, 0x0e);
	CHECK64(pop_event64(&con, TTY_KIND_PREEDIT, 0) == 0);
	raw_process_key64(&con, 0x0e);
	CHECK64(pop_event64(&con, TTY_KIND_CHAR, '\b') == 0);
	CHECK64(console_raw_pop64(&con, &event) == 0 && serial_length64 == 0);
	process_hangul_key64(&con, 'r');
	process_hangul_key64(&con, 'k');
	con.raw_queue_head = con.raw_queue_tail;
	shift64 = 1;
	raw_process_key64(&con, 0x39);
	CHECK64(con.lang_hangul == 0 && con.composing.state == 0);
	CHECK64(pop_event64(&con, TTY_KIND_CHAR, 0xac00) == 0);
	CHECK64(console_raw_pop64(&con, &event) == 0);
	shift64 = 0;
	process_hangul_key64(&con, 'r');
	process_hangul_key64(&con, 'k');
	con.raw_queue_head = con.raw_queue_tail;
	ctrl64 = 1;
	raw_process_key64(&con, 0x2e);
	CHECK64(pop_event64(&con, TTY_KIND_CHAR, 0xac00) == 0);
	CHECK64(pop_event64(&con, TTY_KIND_CHAR, 3) == 0);
	CHECK64(console_raw_pop64(&con, &event) == 0);
	ctrl64 = 0;
	con.lang_hangul = 1;
	process_hangul_key64(&con, 'r');
	process_hangul_key64(&con, 'k');
	con.raw_queue_head = con.raw_queue_tail;
	process_hangul_key64(&con, 'k');
	CHECK64(pop_event64(&con, TTY_KIND_CHAR, 0xac00) == 0);
	CHECK64(pop_event64(&con, TTY_KIND_CHAR, 0x314f) == 0);
	CHECK64(pop_event64(&con, TTY_KIND_PREEDIT, 0) == 0);
	CHECK64(console_raw_pop64(&con, &event) == 0);
	for (size_t i = 0; i < 4; i++) {
		process_hangul_key64(&con, keys[i]);
		CHECK64(pop_event64(&con, TTY_KIND_PREEDIT, preedit[i]) == 0);
	}
	for (size_t i = 0; i < 4; i++) {
		raw_process_key64(&con, 0x0e);
		CHECK64(pop_event64(&con, TTY_KIND_PREEDIT, i == 3 ? 0 : preedit[2 - i]) == 0);
	}
	static const uint16_t flush_keys[] = { 0x0f, 0x01, KEY64_KPENTER };
	static const unsigned int payloads[] = { '\t', 0x1b, '\n' };

	for (size_t i = 0; i < sizeof(flush_keys) / sizeof(flush_keys[0]); i++) {
		process_hangul_key64(&con, 'r');
		process_hangul_key64(&con, 'k');
		con.raw_queue_head = con.raw_queue_tail;
		raw_process_key64(&con, flush_keys[i]);
		CHECK64(pop_event64(&con, TTY_KIND_CHAR, 0xac00) == 0);
		CHECK64(pop_event64(&con, TTY_KIND_CHAR, payloads[i]) == 0);
		CHECK64(console_raw_pop64(&con, &event) == 0);
	}
	return 0;
}

static int editor_boundaries64(void)
{
	struct CONSOLE64 con;
	struct CONSOLE64 other;
	struct DISPLAY64 screen;
	struct DISPLAY64 other_screen;
	struct HANGUL64 saved;
	struct HANGUL64_FEED_RESULT feed;

	init_console64(&con, &screen, WIDTH64, 0);
	init_console64(&other, &other_screen, WIDTH64, 0);
	serial_length64 = 0;
	process_hangul_key64(&con, 'r');
	process_hangul_key64(&con, 'k');
	CHECK64(serial_length64 == 0 && con.input_len == 0 && con.cursor_x == CONSOLE64_HANGUL_W);
	saved = con.composing;
	process_hangul_key64(&other, 's');
	CHECK64(memcmp(&saved, &con.composing, sizeof(saved)) == 0);
	console_backspace64(&con);
	CHECK64(serial_length64 == 0 && composing_unicode64(&con) == 0x3131);
	console_backspace64(&con);
	CHECK64(serial_length64 == 0 && con.cursor_x == 0);
	process_hangul_key64(&con, 'r');
	process_hangul_key64(&con, 'k');
	flush_composing64(&con);
	CHECK64(serial_length64 == 3 && memcmp(serial64, "가", 3) == 0);
	CHECK64(line_matches64(&con, "가") == 0);
	console_backspace64(&con);
	CHECK64(con.input_len == 0 && con.cursor_x == 0);
	CHECK64(memcmp(serial64 + 3, "\b \b", 3) == 0);
	CHECK64(composing_unicode64(&other) == 0x3134 && other.input_len == 0);
	/* Exercise the actual cooked scancode path used by the AArch64 console. */
	console64_process_input_key(&con, 0x13);
	console64_process_input_key(&con, 0x25);
	shift64 = 1;
	console64_process_input_key(&con, 0x39);
	CHECK64(con.lang_hangul == 0 && con.composing.state == 0);
	CHECK64(line_matches64(&con, "가") == 0);
	shift64 = 0;
	console64_process_input_key(&con, 0x13);
	CHECK64(line_matches64(&con, "가r") == 0);
	CHECK64(other.composing.cho == 2);

	init_console64(&con, &screen, WIDTH64, 0);
	memset(con.input_line, 'x', sizeof(con.input_line));
	con.input_len = CONSOLE64_LINE_MAX - 4;
	process_hangul_key64(&con, 'r');
	process_hangul_key64(&con, 'k');
	process_hangul_key64(&con, 'k');
	CHECK64(con.input_len == CONSOLE64_LINE_MAX - 1 && con.line_full_warned == 1);
	CHECK64(memcmp(con.input_line + CONSOLE64_LINE_MAX - 4, "가", 3) == 0);
	CHECK64(con.input_line[CONSOLE64_LINE_MAX - 1] == 'x');
	CHECK64(con.composing.state == 0 && guards_valid64(&screen) == 0);
	init_console64(&con, &screen, WIDTH64, 0);
	memset(con.input_line, 'x', sizeof(con.input_line));
	con.input_len = CONSOLE64_LINE_MAX - 2;
	process_hangul_key64(&con, 'r');
	process_hangul_key64(&con, 'k');
	flush_composing64(&con);
	CHECK64(con.input_len == CONSOLE64_LINE_MAX - 2 && con.line_full_warned == 1);
	CHECK64(con.input_line[CONSOLE64_LINE_MAX - 2] == 'x');
	CHECK64(con.composing.state == 0 && guards_valid64(&screen) == 0);
	CHECK64(hangul64_feed(NULL, 'r', &feed) == -1);
	CHECK64(hangul64_feed(&con.composing, 'r', NULL) == -1);
	for (int state = -1; state <= 5; state += 6) {
		for (size_t i = 0; i < 3; i++) {
			con.composing.state = state;
			CHECK64(hangul64_feed(&con.composing, "r\b\0"[i], &feed) == -2);
			CHECK64(con.composing.state == 0 && feed.committed_length == 0);
		}
	}
	return 0;
}

int main(void)
{
	FILE *font = fopen("common/font/H04.FNT", "rb");

	CHECK64(font != NULL);
	CHECK64(fread(font64, 1, sizeof(font64), font) == sizeof(font64));
	CHECK64(fclose(font) == 0);
	hangul_font = font64;
	CHECK64(golden_cases64() == 0);
	CHECK64(protocol64() == 0);
	CHECK64(editor_boundaries64() == 0);
	puts("Hangul cooked/raw composition, backspace, display and TTY protocol: PASS");
	return 0;
}
