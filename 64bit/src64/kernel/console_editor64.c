/* Cooked line editing, Hangul IME and raw/REPL input routing. */
#include "console_internal64.h"
#include <arch/platform64.h>
#include <keyboard64.h>
#include <keymap64.h>
#include <mtask64.h>
#include <syscall64.h>
#include <utf864.h>

void console_toggle_language64(struct CONSOLE64 *con)
{
	uint64_t state = platform_irq_save64();

	con->lang_hangul ^= 1;
	con->language_generation++;
	platform_irq_restore64(state);
}

int64_t console64_input_mode_con(struct CONSOLE64 *con)
{
	uint64_t state = platform_irq_save64();
	int64_t result = -22;

	if (con != NULL && con->slot_state == CONSOLE64_LIVE && !con->close_ready) {
		result = ((uint64_t) con->language_generation << 1) | (con->lang_hangul != 0);
	}
	platform_irq_restore64(state);
	return result;
}

static unsigned int composing_unicode64(struct CONSOLE64 *con)
{
	char utf8[4];
	int len;
	size_t consumed;
	uint32_t unicode;

	if (con->composing.state == 0) {
		return 0;
	}
	len = hangul64_compose_utf8(utf8, &con->composing);
	if (len <= 0) {
		return 0;
	}
	utf8_decode64(utf8, (size_t) len, &unicode, &consumed);
	return unicode;
}

static void raw_emit_preedit64(struct CONSOLE64 *con)
{
	console_raw_push64(con, TTY_KIND_PREEDIT, composing_unicode64(con));
}

static char translate_key64(struct CONSOLE64 *con, uint8_t scancode)
{
	return keymap64_translate(scancode, keyboard64_shift(),
		con->lang_hangul);
}

static int append_input64(struct CONSOLE64 *con, const char *s, int len)
{
	int i;

	if (con->raw_mode != 0) {
		/* raw 모드에서는 줄 버퍼 대신 앱의 큐로 간다. 에코도 하지 않으므로
		   0을 돌려 호출자가 화면에 그리지 않게 한다. */
		size_t consumed;
		uint32_t unicode;

		if (len <= 0) {
			return 0;
		}
		utf8_decode64(s, (size_t) len, &unicode, &consumed);
		console_raw_push64(con, TTY_KIND_CHAR, unicode);
		return 0;
	}
	if (con->input_len + len >= CONSOLE64_LINE_MAX) {
		/* 말없이 키를 무시하면 자판이 고장 난 것처럼 보인다. 한 줄에 한 번만
		   알린다 - 안 그러면 남은 타자마다 같은 말이 쏟아진다. */
		if (con->line_full_warned == 0) {
			con->line_full_warned = 1;
			console_puts_con64(con, "\n줄이 너무 깁니다\n");
		}
		return 0;
	}
	for (i = 0; i < len; i++) {
		con->input_line[con->input_len++] = s[i];
	}
	return 1;
}

/* hangul64_feed owns transitions; emit only committed bytes to COM1 and
 * reserve a screen slot for preedit. Raw mode preserves CHAR/PREEDIT ordering.
 */
static void apply_hangul_key64(struct CONSOLE64 *con, char key, int emit_preedit)
{
	struct HANGUL64_FEED_RESULT feed;
	size_t offset = 0;
	size_t length;
	uint32_t unicode;
	int had_preedit = con->composing.state != 0;
	int error;

	error = hangul64_feed(&con->composing, key, &feed);
	if (con->raw_mode != 0) {
		if (error == 0) {
			while (offset < feed.committed_length) {
				utf8_decode64(feed.committed + offset,
					feed.committed_length - offset, &unicode, &length);
				console_raw_push64(con, TTY_KIND_CHAR, unicode);
				offset += length;
			}
			if (feed.passthrough != '\0') {
				console_raw_push64(con, TTY_KIND_CHAR,
					(unsigned char) feed.passthrough);
			}
		}
		if (emit_preedit != 0 || error != 0) {
			raw_emit_preedit64(con);
		}
		return;
	}
	/* Preedit was never sent to COM1. Erase its screen slot without serial
	 * backspaces, then echo only committed text that fits in the line buffer.
	 */
	if (had_preedit != 0) {
		console_erase_preedit64(con);
	}
	if (error != 0) {
		return;
	}
	while (offset < feed.committed_length) {
		utf8_decode64(feed.committed + offset,
			feed.committed_length - offset, &unicode, &length);
		if (append_input64(con, feed.committed + offset, (int) length) != 0) {
			console_put_char64(con, feed.committed + offset, (int) length);
		}
		offset += length;
	}
	if (feed.passthrough != '\0' && append_input64(con, &feed.passthrough, 1) != 0) {
		console_put_char64(con, &feed.passthrough, 1);
	}
	if (con->composing.state != 0) {
		console_reserve_preedit64(con);
	}
}

static void flush_composing64(struct CONSOLE64 *con)
{
	if (con->composing.state != 0) {
		/* Preserve raw's commit-only protocol before mode/movement/shortcuts. */
		apply_hangul_key64(con, '\0', 0);
	}
}

static void not_korean64(struct CONSOLE64 *con, char key)
{
	flush_composing64(con);
	if (append_input64(con, &key, 1) != 0) {
		console_put_char64(con, &key, 1);
	}
}

static void process_hangul_key64(struct CONSOLE64 *con, char key)
{
	apply_hangul_key64(con, key, 1);
}

static int delete_composing64(struct CONSOLE64 *con)
{
	if (con->composing.state == 0) {
		return 0;
	}
	apply_hangul_key64(con, '\b', 1);
	return 1;
}

static void console_backspace64(struct CONSOLE64 *con)
{
	uint16_t width;
	uint16_t start;

	if (delete_composing64(con) != 0) {
		return;
	}
	if (con->input_len == 0) {
		return;
	}
	start = con->input_len - 1;
	while (start > 0 && (((uint8_t) con->input_line[start] & 0xc0) == 0x80)) {
		start--;
	}
	width = (con->input_len - start == 3) ? CONSOLE64_HANGUL_W : CONSOLE64_FONT_W;
	con->input_len = start;
	console_erase_prev64(con, width);
}

static int normalize_ext_key64(uint16_t *key)
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

uint64_t console64_read(char *dst, uint64_t len)
{
	return console64_read_con(console_self64(), dst, len);
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
	console_cursor_wait64(con, 1);
	for (;;) {
		key = console_wait_key64(con);
		if ((key & 0x80U) == 0) { console_cursor_activity64(con); }
		if (keyboard64_track_modifier(key) != 0) {
			continue;
		}
		if (normalize_ext_key64(&key) == 0) {
			continue;
		}
		if ((key & 0x80) != 0) {
			continue;
		}
		if (key == 0x1c) {
			dst[count++] = '\n';
			console_cursor_wait64(con, 0);
			console_put_char64(con, "\n", 1);
			return count;
		}
		if (key == 0x0e) {
			if (count > 0) {
				count--;
				console_erase_prev64(con, CONSOLE64_FONT_W);
			}
			continue;
		}
		c = keymap64_translate((uint8_t) key, keyboard64_shift(), 0);
		if (c == ' ' && keyboard64_shift()) {
			console_toggle_language64(con);
			continue;
		}
		if (c == '\0') {
			continue;
		}
		dst[count++] = c;
		console_put_char64(con, &c, 1);
		if (count == len) {
			console_cursor_wait64(con, 0);
			return count;
		}
	}
}

static void raw_process_key64(struct CONSOLE64 *con, uint16_t key)
{
	char c;

	if ((key & 0x80) != 0) {
		return;                     /* 브레이크 코드 */
	}
	if ((key & KEY64_EXT) != 0) {
		/* Commit at the old caret before the application moves or selects. */
		flush_composing64(con);
		if (key == KEY64_KPENTER) {
			console_raw_push64(con, TTY_KIND_CHAR, '\n');
		} else {
			console_raw_push64(con, TTY_KIND_KEY, key);
		}
		return;
	}
	if (key == 0x1c) {
		flush_composing64(con);
		console_raw_push64(con, TTY_KIND_CHAR, '\n');
		return;
	}
	if (key == 0x0e) {
		if (delete_composing64(con) == 0) {
			console_raw_push64(con, TTY_KIND_CHAR, '\b');
		}
		return;
	}
	if (key == 0x0f) {
		flush_composing64(con);
		console_raw_push64(con, TTY_KIND_CHAR, '\t');
		return;
	}
	if (key == 0x01) {
		flush_composing64(con);
		console_raw_push64(con, TTY_KIND_CHAR, 0x1b);
		return;
	}
	c = translate_key64(con, (uint8_t) key);
	if (c == '\0') {
		return;
	}
	if (keyboard64_shift() != 0 && c == ' ') {
		flush_composing64(con);
		console_toggle_language64(con);
		return;
	}
	/* App shortcuts remain usable in Hangul mode (nano uses Meta-U/Meta-E). */
	if (keyboard64_ctrl() != 0 || keyboard64_alt() != 0) {
		flush_composing64(con);
		if (keyboard64_ctrl() != 0 && c >= 'a' && c <= 'z') {
			c = (char) (c - 'a' + 1);
		} else if (keyboard64_ctrl() != 0 && c >= 'A' && c <= 'Z') {
			c = (char) (c - 'A' + 1);
		}
		console_raw_push64(con, TTY_KIND_CHAR, (unsigned char) c);
		return;
	}
	if (con->lang_hangul != 0) {
		process_hangul_key64(con, c);
	} else {
		not_korean64(con, c);         /* raw에서는 큐로 밀고 에코하지 않는다 */
	}
}

void console64_process_input_key(struct CONSOLE64 *con, uint16_t key)
{
	char c;

	if (con == NULL) { return; }
	if ((key & 0x80U) == 0) { console_cursor_activity64(con); }
	if (keyboard64_track_modifier(key) != 0) {
		return;
	}
	if (con->raw_mode != 0) {
		raw_process_key64(con, key);
		return;
	}
	if (normalize_ext_key64(&key) == 0 || (key & 0x80U) != 0) {
		return;
	}
	if (key == 0x1cU) {
		flush_composing64(con);
		console_put_char64(con, "\n", 1);
		con->input_len = 0;
		con->line_full_warned = 0;
		console_prompt64(con);
		return;
	}
	if (key == 0x0eU) {
		console_backspace64(con);
		return;
	}
	c = translate_key64(con, (uint8_t) key);
	if (c == '\0') {
		return;
	}
	if (keyboard64_shift() != 0 && c == ' ') {
		flush_composing64(con);
		console_toggle_language64(con);
		return;
	}
	if (con->lang_hangul != 0 && console64_hangul_font() != NULL) {
		process_hangul_key64(con, c);
	} else {
		not_korean64(con, c);
	}
}

static void console_submit_line64(struct CONSOLE64 *con)
{
	flush_composing64(con);
	con->input_line[con->input_len] = '\0';
	console_cursor_wait64(con, 0);
	console_newline64(con);
	if (con->input_len == 0) {
		console_prompt64(con);
		return;
	}
	console_dispatch_command64(con, con->input_line);
	con->input_len = 0;
	con->line_full_warned = 0;
	console_prompt64(con);
}

void console64_process_key(struct CONSOLE64 *con, uint16_t key)
{
	char c;

	if (con == NULL) { return; }
	if ((key & 0x80U) == 0) { console_cursor_activity64(con); }

	if (keyboard64_track_modifier(key) != 0) {
		return;
	}
	if (con->raw_mode != 0) {
		raw_process_key64(con, key);
		return;
	}
	if (normalize_ext_key64(&key) == 0) {
		return;
	}
	if ((key & 0x80) != 0) {
		return;
	}
	if (con->line_capture) {
		if (key == 0x1c) {
			con->line_result = 0;
			con->line_done = 1;
			return;
		}
		if (keyboard64_ctrl() != 0) {
			c = translate_key64(con, (uint8_t) key);
			if (c == 'c' || c == 'C') {
				con->line_result = -1;
				con->line_done = 1;
				return;
			}
			if (c == 'd' || c == 'D') {
				con->line_result = -2;
				con->line_done = 1;
				return;
			}
		}
	}
	/* readline이 잡고 있는 동안에는 py REPL 안이라도 키가 줄 편집기로 가야
	   한다. 그래야 py REPL에서 부르든 스크립트에서 부르든 똑같이 동작한다. */
	if (con->repl_active && con->line_capture == 0) {
		if (key == 0x1c) {
			console_repl_push64(con, '\r');
			return;
		}
		if (key == 0x0e) {
			console_repl_push64(con, '\b');
			return;
		}
		c = translate_key64(con, (uint8_t) key);
		if (c == '\0') {
			return;
		}
		if (keyboard64_shift() && c == ' ') {
			console_toggle_language64(con);
			return;
		}
		if (keyboard64_ctrl() != 0) {
			if (c >= 'a' && c <= 'z') {
				c = (char) (c - 'a' + 1);
			} else if (c >= 'A' && c <= 'Z') {
				c = (char) (c - 'A' + 1);
			}
		}
		console_repl_push64(con, c);
		return;
	}
	if (key == 0x1c) {
		console_submit_line64(con);
		return;
	}
	if (key == 0x0e) {
		console_backspace64(con);
		return;
	}
	c = translate_key64(con, (uint8_t) key);
	if (c == '\0') {
		return;
	}
	if (keyboard64_shift() != 0 && c == ' ') {
		flush_composing64(con);
		console_toggle_language64(con);
		return;
	}
	if (con->lang_hangul != 0 && console64_hangul_font() != NULL) {
		process_hangul_key64(con, c);
	} else {
		not_korean64(con, c);
	}
}

struct HANGUL_CASE64 {
	const char *keys;
	unsigned int want;
};

int console64_hangul_smoke(void)
{
	static const struct HANGUL_CASE64 cases[] = {
		{ "dlT",  0xc788 },      /* 있 - 쌍시옷 받침 */
		{ "ekR",  0xb2e6 },      /* 닦 - 쌍기역 받침 */
		{ "qkR",  0xbc16 },      /* 밖 */
		{ "ruR",  0xacaa },      /* 겪 */
		{ "dksw", 0xc549 },      /* 앉 - 겹받침 ㄵ */
		{ "djqt", 0xc5c6 },      /* 없 - 겹받침 ㅄ */
		{ "ekE",  0x3138 },      /* 다 + ㄸ: ㄸ는 받침이 못 되니 새 글자 */
	};
	struct CONSOLE64 *con = console64_active();
	struct HANGUL64 saved_composing;
	uint32_t saved_head;
	uint32_t saved_tail;
	int saved_raw;
	uint32_t i;
	int j;
	int ok;

	saved_composing = con->composing;
	saved_head = con->raw_queue_head;
	saved_tail = con->raw_queue_tail;
	saved_raw = con->raw_mode;

	ok = 1;
	con->raw_mode = 1;
	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		hangul64_init(&con->composing);
		con->raw_queue_head = 0;
		con->raw_queue_tail = 0;
		for (j = 0; cases[i].keys[j] != '\0'; j++) {
			process_hangul_key64(con, cases[i].keys[j]);
		}
		if (composing_unicode64(con) != cases[i].want) {
			ok = 0;
			break;
		}
	}

	con->raw_mode = saved_raw;
	con->composing = saved_composing;
	con->raw_queue_head = saved_head;
	con->raw_queue_tail = saved_tail;
	return ok;
}

void console64_set_raw_con(struct CONSOLE64 *con, int on)
{
	int was_raw;
	uint64_t state;

	if (con == NULL) {
		return;
	}
	state = platform_irq_save64();
	console_cursor_wait64(con, 0);
	was_raw = con->raw_mode;
	con->raw_mode = on != 0;
	con->caret_x = con->cursor_x;
	con->caret_y = con->cursor_y;
	con->caret_cells = 1;
	con->caret_valid = 1;
	con->raw_queue_head = 0;
	con->raw_queue_tail = 0;
	con->dirty_valid = 0;
	if (con->raw_mode == 0) {
		/* 앱이 조합 중인 채로 나가도, 색을 바꿔 놓고 나가도 콘솔이 그 상태를
		   물려받지 않게 한다. */
		hangul64_init(&con->composing);
		con->color_fg = CONSOLE64_FG_DEFAULT;
		con->color_bg = CONSOLE64_BG_DEFAULT;
		if (was_raw != 0) {
			/* 앱이 남긴 화면과 화면 한복판의 커서를 그대로 물려받으면
			   콘솔이 망가진 것처럼 보인다. */
			console_clear_screen64(con);
		}
	} else {
		console_cursor_wait64(con, 1);
	}
	platform_irq_restore64(state);
}

void console64_set_raw(int on)
{
	console64_set_raw_con(console_self64(), on);
}

int console64_is_raw(void)
{
	return console_self64()->raw_mode;
}

uint64_t console64_read_key(void)
{
	struct CONSOLE64 *con = console_self64();
	struct EVENT64 event;
	uint64_t out;
	uint64_t irq_state;

	for (;;) {
		/* GUI resize wakes this task without adding a keyboard FIFO event.
		 * Inspect both queues and commit sleep in one irq-save transaction,
		 * so a resize cannot arrive between an empty raw read and sleep.
		 */
		irq_state = platform_irq_save64();
		if (console_raw_pop64(con, &out) != 0) {
			platform_irq_restore64(irq_state);
			return out;
		}
		if (fifo64_get(&con->keys, &event) != 0) {
			(void) task_sleep64(con->task);
			platform_irq_restore64(irq_state);
			continue;
		}
		platform_irq_restore64(irq_state);
		if (event.type == EVENT64_KEYBOARD) {
			console64_process_key(con, (uint16_t) event.data);
		}
	}
}

/* The shared editor buffer is reused. Interpreter callers must snapshot
 * aliased command arguments before entering this blocking read.
 */
int64_t console64_read_line(char *dst, uint64_t max)
{
	struct CONSOLE64 *con = console_self64();
	uint64_t n;
	uint64_t i;

	con->line_capture = 1;
	con->line_done = 0;
	con->line_result = 0;
	con->input_len = 0;
	con->line_full_warned = 0;
	console_cursor_wait64(con, 1);
	while (con->line_done == 0) {
		console64_process_key(con, console_wait_key64(con));
	}
	console_cursor_wait64(con, 0);
	con->line_capture = 0;
	flush_composing64(con);
	console_newline64(con);
	if (con->line_result != 0) {
		con->input_len = 0;
		con->line_full_warned = 0;
		return con->line_result;
	}
	n = con->input_len;
	if (n > max) {
		n = max;
	}
	for (i = 0; i < n; i++) {
		dst[i] = con->input_line[i];
	}
	con->input_len = 0;
	con->line_full_warned = 0;
	return (int64_t) n;
}

int console64_repl_getchar(void)
{
	struct CONSOLE64 *con = console_self64();
	char c;

	console_cursor_wait64(con, 1);
	for (;;) {
		if (console_repl_pop64(con, &c)) {
			console_cursor_wait64(con, 0);
			return (unsigned char) c;
		}
		console64_process_key(con, console_wait_key64(con));
	}
}
