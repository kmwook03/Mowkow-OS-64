/* GUI lifetime faults and console transitions share the real editor/keyboard. */
#include "gui_lifecycle64_host.c"

#include <keyboard64.h>

static uint8_t hangul_font64[HANGUL64_FONT_SIZE];
static uint8_t status_font64[HANGUL64_FONT_SIZE];
static int commands64, wait_checks64;

void console_dispatch_command64(struct CONSOLE64 *con, const char *line)
{
	assert(con == console_self64() && line[0] != '\0');
	assert(!con->caret_wanted && !con->caret_drawn);
	commands64++;
}

static void select64(struct CONSOLE64 *con)
{
	uint64_t state = platform_irq_save64();

	focus_win(con->sheet);
	platform_irq_restore64(state);
	assert(gui64_focused_console() == con);
}

/* Match the kernel's routing order; consumption can be delayed until focus moves. */
static void route64(uint16_t key)
{
	struct CONSOLE64 *con = gui64_focused_console();

	assert(con != NULL && console64_post_input_key(con, key) == 0);
}

static void consume64(struct CONSOLE64 *con)
{
	uint16_t key;

	current64 = con->task;
	while (fifo64_status(&con->keys) != 0) {
		assert(console_pump_event64(con, &key) == 1);
		console64_process_key(con, key);
	}
	assert(irq64 == 0);
}

static void key64(struct CONSOLE64 *con, uint16_t key)
{
	assert(console64_post_input_key(con, key) == 0);
	consume64(con);
}

static void control64(struct CONSOLE64 *con, uint16_t key)
{
	key64(con, 0x1d);
	key64(con, key);
	key64(con, 0x9d);
}

static void expect_event64(struct CONSOLE64 *con, unsigned int kind,
	unsigned int payload, unsigned int mods)
{
	uint64_t event;

	current64 = con->task;
	event = console64_read_key();
	assert(TTY_KEY_KIND(event) == kind && TTY_KEY_PAYLOAD(event) == payload);
	assert(TTY_KEY_MODS(event) == mods);
}

enum ACTION64 {
	RAW_ON64, RAW_OFF64, REPL_ON64, REPL_OFF64, LETTER64,
	CTRL_C64, CTRL_D64, ENTER64, BACKSPACE64, ACTION_COUNT64
};

/* Independent observable model: mode precedence, output and reset boundaries. */
struct MODEL64 {
	int raw, repl;
	char line[4], repl_bytes[4];
	uint64_t events[4];
	size_t length, repl_count, event_count;
	int commands;
};

static void transition64(struct CONSOLE64 *con, struct MODEL64 *model, enum ACTION64 action)
{
	unsigned int payload, mods = 0;

	current64 = con->task;
	switch (action) {
	case RAW_ON64:
	case RAW_OFF64:
		model->raw = action == RAW_ON64;
		model->event_count = 0;
		console64_set_raw(model->raw);
		return;
	case REPL_ON64:
	case REPL_OFF64:
		model->repl = action == REPL_ON64;
		model->repl_count = 0;
		console64_repl_set_active(model->repl);
		return;
	case LETTER64: key64(con, 0x1e); payload = 'a'; break;
	case CTRL_C64: control64(con, 0x2e); payload = 3; mods = TTY_MOD_CTRL; break;
	case CTRL_D64: control64(con, 0x20); payload = 4; mods = TTY_MOD_CTRL; break;
	case ENTER64: key64(con, 0x1c); payload = '\n'; break;
	case BACKSPACE64: key64(con, 0x0e); payload = '\b'; break;
	default: assert(0); return;
	}
	if (model->raw) {
		model->events[model->event_count++] = payload |
			((uint64_t) TTY_KIND_CHAR << 32) | ((uint64_t) mods << 40);
	} else if (model->repl) {
		model->repl_bytes[model->repl_count++] = payload == '\n' ? '\r' : (char) payload;
	} else if (action == ENTER64) {
		model->commands += model->length != 0;
		model->length = 0;
	} else if (action == BACKSPACE64) {
		if (model->length != 0) { model->length--; }
	} else {
		model->line[model->length++] = action == CTRL_C64 ? 'c' :
			action == CTRL_D64 ? 'd' : 'a';
	}
}

static void model_check64(struct CONSOLE64 *con, const struct MODEL64 *model)
{
	assert(con->raw_mode == model->raw && con->repl_active == model->repl);
	assert(con->input_len == model->length);
	assert(memcmp(con->input_line, model->line, model->length) == 0);
	assert(commands64 == model->commands && con->line_capture == 0);
	assert(con->raw_queue_tail == model->event_count && con->raw_queue_head == 0);
	assert(con->repl_queue_tail == model->repl_count && con->repl_queue_head == 0);
	assert(memcmp(con->raw_queue, model->events, model->event_count * sizeof(uint64_t)) == 0);
	assert(memcmp(con->repl_queue, model->repl_bytes, model->repl_count) == 0);
	assert(fifo64_status(&con->keys) == 0 && !keyboard64_ctrl());
}

static void mode_sequences64(void)
{
	struct CONSOLE64 *con = console64_active();

	con->lang_hangul = 0;
	/* All 729 three-action sequences, checking every prefix against the model. */
	for (int sequence = 0; sequence < ACTION_COUNT64 * ACTION_COUNT64 * ACTION_COUNT64;
		sequence++) {
		struct MODEL64 model = { 0 };
		int remaining = sequence;

		current64 = con->task;
		console64_set_raw(0);
		console64_repl_set_active(0);
		con->input_len = 0;
		commands64 = 0;
		for (int step = 0; step < 3; step++) {
			enum ACTION64 action = remaining % ACTION_COUNT64;

			remaining /= ACTION_COUNT64;
			transition64(con, &model, action);
			model_check64(con, &model);
		}
	}
	console64_set_raw(0);
	console64_repl_set_active(0);
	con->input_len = 0;
	puts("Console state: 729 mode/input sequences and 2187 prefixes: PASS");
}

static void capture64(void)
{
	struct CONSOLE64 *con = console64_active();
	char line[8];
	static const uint16_t keys[] = { 0x13, 0x25, 0x1d, 0x2e };

	current64 = con->task;
	console64_repl_set_active(1);
	con->lang_hangul = 1;
	for (int ending = 0; ending < 3; ending++) {
		memset(line, 0xa5, sizeof(line));
		for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
			uint16_t key = i == 3 ? (ending == 0 ? 0x1c : ending == 1 ? 0x2e : 0x20) :
				keys[i];
			assert(console64_post_input_key(con, key) == 0);
		}
		assert(console64_post_input_key(con, 0x9d) == 0);
		int64_t result = console64_read_line(line, 3);
		assert(result == (ending == 0 ? 3 : ending == 1 ? -1 : -2));
		assert(con->line_capture == 0 && con->input_len == 0 && con->composing.state == 0);
		assert(con->repl_queue_head == con->repl_queue_tail);
		if (ending == 0) { assert(memcmp(line, "가", 3) == 0); }
		else { assert((unsigned char) line[0] == 0xa5); }
		for (size_t i = 3; i < sizeof(line); i++) { assert((unsigned char) line[i] == 0xa5); }
		consume64(con); /* release after readline returned */
		assert(!keyboard64_ctrl());
	}
	control64(con, 0x2e);
	control64(con, 0x20);
	assert(console64_repl_getchar() == 3 && console64_repl_getchar() == 4);
	console64_repl_set_active(0);
	console64_set_raw(1);
	key64(con, 0x13);
	key64(con, 0x25);
	control64(con, 0x2e);
	control64(con, 0x20);
	expect_event64(con, TTY_KIND_PREEDIT, 0x3131, 0);
	expect_event64(con, TTY_KIND_PREEDIT, 0xac00, 0);
	expect_event64(con, TTY_KIND_CHAR, 0xac00, TTY_MOD_CTRL);
	expect_event64(con, TTY_KIND_CHAR, 3, TTY_MOD_CTRL);
	expect_event64(con, TTY_KIND_CHAR, 4, TTY_MOD_CTRL);
	key64(con, 0x13);
	console64_set_raw(0);
	assert(con->composing.state == 0 && con->raw_queue_head == con->raw_queue_tail);
	puts("Console state: readline/REPL/raw Ctrl-C/D and IME cleanup: PASS");
}

static void resize_before_wait64(void)
{
	assert(irq64 == 0 && gui_toggle_window_checked64() == 0);
}

static void unexpected_sleep64(void)
{
	assert(0); /* a RESIZE already queued before sleeping must be returned */
}

static void resize_while_asleep64(void)
{
	struct CONSOLE64 *con = console_self64();

	assert(con->raw_queue_head == con->raw_queue_tail);
	assert(fifo64_status(&con->keys) == 0);
	wait_checks64++;
	con->task->flags = TASK64_FLAGS_ALLOCATED;
	irq64 = 0; /* model the main task running while this consumer is asleep */
	assert(gui_toggle_window_checked64() == 0);
	assert(con->task->flags == TASK64_FLAGS_RUNNING);
	irq64 = 1;
}

static void resize_waits64(void)
{
	struct CONSOLE64 *con = console64_active();
	uint32_t generation;

	current64 = con->task;
	con->lang_hangul = 0;
	console64_set_raw(1);
	generation = con->size_generation;
	before_irq64 = resize_before_wait64;
	on_sleep64 = unexpected_sleep64;
	expect_event64(con, TTY_KIND_RESIZE, 0, 0);
	assert(before_irq64 == NULL && con->size_generation == generation + 1);
	on_sleep64 = resize_while_asleep64;
	expect_event64(con, TTY_KIND_RESIZE, 0, 0);
	assert(wait_checks64 == 1 && irq64 == 0);
	on_sleep64 = NULL;
	con->size_generation = UINT32_MAX;
	key64(con, 0x1e);
	assert(gui_toggle_window_checked64() == 0);
	key64(con, KEY64_LEFT);
	expect_event64(con, TTY_KIND_CHAR, 'a', 0);
	expect_event64(con, TTY_KIND_RESIZE, 0, 0);
	expect_event64(con, TTY_KIND_KEY, KEY64_LEFT, 0);
	uint64_t size = console64_size();
	assert(TTY_SIZE_GEN(size) == 0 && TTY_SIZE_COLS(size) == con->width / 8);
	assert(TTY_SIZE_ROWS(size) == con->height / 16);
	console64_set_raw(0);
	generation = con->size_generation;
	assert(gui_toggle_window_checked64() == 0 && con->size_generation == generation + 1);
	assert(con->raw_queue_head == con->raw_queue_tail);
	puts("Console state: RESIZE ordering, generation wrap and wait/wake interleavings: PASS");
}

static void caret_pixels64(void)
{
	struct CONSOLE64 *con = console64_active();
	struct CONSOLE64 *other;
	uint8_t *original = malloc(con->vram_size);

	assert(original != NULL);
	current64 = con->task;
	console64_cursor_update(100);
	console64_set_raw(1);
	assert(console64_cursor_con(con, 0, 1) == 0);
	console64_move(1, 2);
	/* Distinct colors in every scanline, including padding. */
	for (size_t i = 0; i < con->vram_size; i++) { con->vram[i] = (uint8_t) (i % 16); }
	memcpy(original, con->vram, con->vram_size);
	assert(console64_cursor_con(con, 1, 2) == 0);
	assert(con->caret_drawn && con->caret_saved_width == 16);
	for (size_t i = 0; i < con->vram_size; i++) {
		size_t start = 31U * con->stride + 16U;
		uint8_t expected = original[i];

		if (i >= start && i < start + 16) { expected ^= 7U; }
		assert(con->vram[i] == expected);
	}
	console64_cursor_update(149);
	assert(con->caret_drawn);
	console64_cursor_update(150);
	assert(!con->caret_drawn && memcmp(original, con->vram, con->vram_size) == 0);
	console64_cursor_update(200);
	assert(con->caret_drawn);
	console64_cursor_update(350); /* missed periods preserve parity */
	assert(!con->caret_drawn);
	key64(con, KEY64_LEFT);
	assert(con->caret_drawn && con->caret_saved_x == 16 && con->caret_saved_y == 31);
	console64_cursor_update(UINT64_MAX - 20U);
	console_cursor_activity64(con);
	console64_cursor_update(28);
	assert(con->caret_drawn); /* 49 ticks across uint64 wrap */
	console64_cursor_update(29);
	assert(!con->caret_drawn);
	assert(memcmp(original, con->vram, con->vram_size) == 0);
	console64_move(1, 2);
	assert(con->caret_drawn);
	other = console64_create();
	assert(other != NULL && !con->caret_drawn && other->caret_drawn);
	assert(memcmp(original, con->vram, con->vram_size) == 0);
	select64(con);
	assert(con->caret_drawn && !other->caret_drawn);
	assert(console64_cursor_con(con, 3, 1) == -22);
	assert(console64_cursor_con(con, 1, 0) == -22);
	assert(console64_cursor_con(NULL, 1, 1) == -22);
	console64_move(1, con->width / 8U - 1U);
	assert(console64_cursor_con(con, 1, 2) == 0 && con->caret_saved_width == 8);
	assert(console64_cursor_con(con, 0, 1) == 0);
	assert(memcmp(original, con->vram, con->vram_size) == 0);
	console64_move(1, 2);
	console64_set_attr(0, 7);
	console64_write("AAAA", 4);
	assert(!con->caret_drawn && con->dirty_valid);
	assert(console64_cursor_con(con, 1, 1) == 0 && !con->caret_drawn);
	console64_flush();
	assert(con->caret_drawn && con->caret_saved_x == 16 && con->cursor_x == 48);
	assert(console64_cursor_con(con, 0, 1) == 0);
	for (size_t i = 0; i < 32; i++) { assert(con->vram[31U * con->stride + 16U + i] == 7); }
	assert(console64_cursor_con(con, 1, 1) == 0);
	assert(gui_toggle_window_checked64() == 0 && !con->caret_drawn && !con->caret_valid);
	console64_move(1, 2);
	console64_flush();
	assert(con->caret_drawn);
	/* Process cleanup targets the app's console even when another has focus. */
	select64(other);
	current64 = other->task;
	console64_set_attr(2, 9);
	console64_set_raw_con(con, 0);
	assert(!con->raw_mode && !con->caret_wanted && !con->caret_drawn);
	assert(other->color_fg == 2 && other->color_bg == 9);
	select64(con);
	current64 = con->task;
	console_prompt64(con);
	assert(con->caret_drawn);
	other->close_ready = 1;
	console64_reap_closed();
	free(original);
	puts("Console caret: exact pixels, 49/50 ticks, wrap, focus, raw/resize/exit: PASS");
}

static void caret_ime64(void)
{
	struct CONSOLE64 *con = console64_active();
	uint8_t *original = malloc(con->vram_size);
	uint32_t last_row = (con->height / 16U - 1U) * 16U;

	assert(original != NULL);
	console_cursor_wait64(con, 0);
	console_clear_screen64(con);
	con->lang_hangul = 1;
	console_cursor_wait64(con, 1);
	key64(con, 0x13);
	key64(con, 0x25);
	assert(con->caret_drawn && con->caret_saved_width == 16 && con->caret_saved_x == 0);
	assert(console64_cursor_con(con, 0, 1) == 0);
	memcpy(original, con->vram, con->vram_size);
	for (uint64_t tick = 0; tick < 500; tick += 50) {
		console64_cursor_update(tick);
		assert(console64_cursor_con(con, 1, 1) == 0);
		assert(console64_cursor_con(con, 0, 1) == 0);
		assert(memcmp(original, con->vram, con->vram_size) == 0);
	}
	console_cursor_wait64(con, 1);
	key64(con, 0x39); /* commit syllable with a space */
	assert(con->composing.state == 0 && con->input_len == 4);
	assert(memcmp(con->input_line, "가 ", 4) == 0 && con->caret_saved_width == 8);
	key64(con, 0x13);
	key64(con, 0x0e); /* cancel preedit */
	assert(con->composing.state == 0 && con->caret_saved_width == 8);
	console_cursor_wait64(con, 0);
	console_clear_screen64(con);
	console64_move(0, con->width / 8U - 1U);
	console64_write("A", 1);
	console_cursor_wait64(con, 1);
	assert(con->caret_saved_x == 0 && con->caret_saved_y == 31);
	key64(con, 0x13); /* two-cell reservation wraps without corrupting old row */
	assert(con->caret_saved_x == 0 && con->caret_saved_y == 31);
	key64(con, 0x0e);
	console_cursor_wait64(con, 0);
	console_clear_screen64(con);
	console64_move(last_row / 16U, 0);
	console64_write("A", 1);
	console_cursor_wait64(con, 1);
	console_newline64(con); /* scroll must move body, never the saved underline */
	assert(con->caret_drawn);
	console_cursor_wait64(con, 0);
	for (uint32_t x = 0; x < con->width; x++) {
		assert(con->vram[last_row * con->stride + x] == 0);
		assert(con->vram[(last_row + 15U) * con->stride + x] == 0);
	}
	con->input_len = 0;
	console_prompt64(con);
	free(original);
	puts("Console caret: Hangul preedit/commit/cancel, edge/wrap and scrolling: PASS");
}

static void toggle64(struct CONSOLE64 *con)
{
	key64(con, 0x2a);
	key64(con, 0x39);
	key64(con, 0xaa);
}

static void expect_status64(int language)
{
	/* Fixed H04.FNT raster expectations; do not build expected pixels with
	 * the renderer that the indicator uses.
	 */
	static const uint16_t glyphs[][16] = {
		{ 0, 0x1e0c, 0x330c, 0x61fc, 0x618c, 0x61fc, 0x330c, 0x1e0c,
			0, 0x0ff0, 0x381c, 0x300c, 0x381c, 0x0ff0, 0, 0 },
		{ 0, 0x0c18, 0x7f98, 0x1e18, 0x739f, 0x6198, 0x7398, 0x1e18,
			0, 0x3000, 0x3000, 0x3000, 0x3000, 0x1ff8, 0, 0 }
	};

	gui_update_status64();
	assert(gui_context64.status->height == gui_context64.ctl->top - 1);
	assert((gui_context64.status_mode < 0) == (language < 0));
	for (int y = 0; y < 28; y++) {
		for (int x = 0; x < 32; x++) {
			int expected = language < 0 ? 8 : 12;

			if (y == 0) { expected = 7; }
			if (language >= 0 && x >= 8 && x < 24 && y >= 6 && y < 22 &&
				(glyphs[language][y - 6] & (0x8000U >> (x - 8)))) { expected = 7; }
			assert(gui_context64.status_buf[y * 32 + x] == expected);
		}
	}
}

static void expect_fullscreen64(struct CONSOLE64 *con)
{
	gui_update_status64();
	assert(gui_context64.mode == GUI64_MODE_FULL);
	assert(gui_context64.status->height == -1 && gui_context64.taskbar->height == -1);
	assert(con->height == 240 && TTY_SIZE_ROWS(console64_size()) == 15);
	/* The old footer must show the black console, including the badge's pixels. */
	for (int y = 212; y < 240; y++) {
		for (int x = 0; x < 320; x++) {
			uint8_t owner = gui_context64.ctl->map[y * WIDTH64 + x];

			assert(con->vram[y * con->stride + x] == 0);
			if (owner == (uint8_t) (gui_context64.cursor - gui_context64.ctl->sheets0)) {
				continue;
			}
			assert(owner == (uint8_t) (con->sheet - gui_context64.ctl->sheets0));
			assert(vram64[y * STRIDE64 + x] == 0);
		}
	}
	verify64();
}

static void status_during_capture64(void)
{
	struct CONSOLE64 *con = console_self64();

	assert(con->line_capture && con->lang_hangul == 1);
	expect_status64(1);
	assert(console64_post_input_key(con, 0x1c) == 0);
}

static void status_indicator64(void)
{
	struct CONSOLE64 *con = console64_active();
	struct CONSOLE64 *other, *third;
	uint8_t snapshot[32 * 28];
	char line[4];
	FILE *font = fopen("common/font/H04.FNT", "rb");

	assert(font != NULL && fread(status_font64, 1, sizeof(status_font64), font) ==
		sizeof(status_font64));
	assert(fclose(font) == 0);
	console64_set_hangul_font(status_font64);
	current64 = con->task;
	if (gui_context64.mode != GUI64_MODE_FULL) { assert(gui_toggle_window_checked64() == 0); }
	con->lang_hangul = 1;
	expect_fullscreen64(con);
	toggle64(con);
	expect_fullscreen64(con);
	assert(gui_toggle_window_checked64() == 0);
	assert(TTY_SIZE_COLS(console64_size()) == 20 && TTY_SIZE_ROWS(console64_size()) == 6);
	expect_status64(0);
	assert(gui_toggle_window_checked64() == 0);
	toggle64(con); /* Hidden mode changes must appear when the footer returns. */
	expect_fullscreen64(con);
	assert(gui_toggle_window_checked64() == 0);
	expect_status64(1);
	toggle64(con);
	console64_set_raw(1);
	toggle64(con);
	expect_status64(1);
	console64_set_raw(0);
	toggle64(con);
	assert(console64_post_input_key(con, 0x2a) == 0);
	assert(console64_post_input_key(con, 0x39) == 0);
	assert(console64_post_input_key(con, 0xaa) == 0);
	on_sleep64 = status_during_capture64;
	assert(console64_read_line(line, sizeof(line)) == 0);
	on_sleep64 = NULL;
	assert(irq64 == 0);
	other = console64_create();
	third = console64_create();
	assert(other != NULL && third != NULL);
	expect_status64(1);
	select64(con);
	toggle64(other); /* Delayed background consumption never changes the badge. */
	expect_status64(1);
	assert(console64_input_mode_con(other) % 2 == 0);
	select64(other);
	expect_status64(0);
	assert(gui64_handle_key(0x57) == 1 && gui64_handle_key(0xd7) == 0);
	assert(gui64_focused_console() == other); /* F11 preserves keyboard ownership. */
	expect_status64(0);
	select64(con);
	/* Third taskbar entry cannot fit left of the badge; its undrawn prefix
	 * must not accept clicks in the remaining 20-pixel gap.
	 */
	gui64_mouse_event(270 - gui_context64.mx, 226 - gui_context64.my, 1);
	gui64_mouse_event(0, 0, 0);
	assert(gui64_focused_console() == con);
	memcpy(snapshot, gui_context64.status_buf, sizeof(snapshot));
	gui64_mouse_event(308 - gui_context64.mx, 226 - gui_context64.my, 1);
	gui64_mouse_event(0, 0, 0);
	assert(gui64_focused_console() == con && gui_context64.drag == NULL);
	assert(memcmp(snapshot, gui_context64.status_buf, sizeof(snapshot)) == 0);
	assert(gui_context64.ctl->map[226 * WIDTH64 + 308] ==
		(uint8_t) (gui_context64.cursor - gui_context64.ctl->sheets0));
	gui64_mouse_event(-308, -226, 0);
	assert(vram64[226 * STRIDE64 + 308] == snapshot[14 * 32 + 20]);
	/* A drag cannot move application content into the reserved footer. */
	gui64_mouse_event(con->sheet->vx0 + 32 - gui_context64.mx,
		con->sheet->vy0 + 10 - gui_context64.my, 1);
	gui64_mouse_event(0, INT32_MAX, 1);
	gui64_mouse_event(0, 0, 0);
	assert(con->sheet->vy0 + con->sheet->bysize <= 212);
	current64 = &tasks64[0];
	other->close_ready = third->close_ready = 1;
	console64_reap_closed();
	select64(con);
	current64 = con->task;
	console_prompt64(con);
	console64_set_hangul_font(hangul_font64);
	puts("GUI input mode: golden glyphs, cooked/raw/readline, owners, footer/mouse/F11: PASS");
}

static void small_status64(void)
{
	struct BOOTINFO64 info = info64();
	struct SHEET64 *sheet;
	struct CONSOLE64 *con;

	info.scrnx = 64;
	info.scrny = 112;
	memset(vram64, 0xa5, sizeof(vram64));
	sheet = gui64_init(&info);
	assert(sheet != NULL && sheet->bysize == 112);
	console64_init_on_sheet(&info, sheet);
	con = console64_active();
	assert(console64_start_task(con) == 0);
	current64 = con->task;
	assert(TTY_SIZE_COLS(console64_size()) == 8 && TTY_SIZE_ROWS(console64_size()) == 7);
	assert(gui_toggle_window_checked64() == 0);
	assert(TTY_SIZE_COLS(console64_size()) == 2 && TTY_SIZE_ROWS(console64_size()) == 1);
	verify64();
	cleanup64();
	info.scrny = 100;
	assert(gui64_init(&info) == NULL && !gui64_available() && live64 == 0);
	console64_init_on_sheet(&info, NULL);
	assert(console64_active()->height == 100); /* direct fallback reserves no fake footer */
	console64_destroy(console64_active());
	puts("GUI input mode: 64x112 layout, 64x100 fallback and complete cleanup: PASS");
}

static void input_mode64(void)
{
	struct CONSOLE64 *con = console64_active();
	struct CONSOLE64 *other = console64_create();
	char line[4];
	static const uint16_t switches[] = { 0x2a, 0x39, 0xaa, 0x1c };

	assert(other != NULL);
	con->language_generation = UINT32_MAX;
	con->lang_hangul = 1;
	assert(console64_input_mode_con(con) == 0x1ffffffffLL);
	toggle64(con); /* a background console consumes its own queued keys */
	assert(console64_input_mode_con(con) == 0 && console64_input_mode_con(other) == 1);
	console64_set_raw(1);
	toggle64(con);
	assert(console64_input_mode_con(con) == 3);
	console64_set_raw(0);
	for (size_t i = 0; i < sizeof(switches) / sizeof(switches[0]); i++) {
		assert(console64_post_input_key(con, switches[i]) == 0);
	}
	assert(console64_read_line(line, sizeof(line)) == 0);
	assert(console64_input_mode_con(con) == 4 && !con->caret_wanted && !con->caret_drawn);
	for (size_t i = 0; i < sizeof(switches) / sizeof(switches[0]); i++) {
		assert(console64_post_input_key(con, switches[i]) == 0);
	}
	assert(console64_read_con(con, line, sizeof(line)) == 1 && line[0] == '\n');
	assert(console64_input_mode_con(con) == 7 && !con->caret_wanted);
	console64_repl_set_active(1);
	for (size_t i = 0; i < sizeof(switches) / sizeof(switches[0]); i++) {
		assert(console64_post_input_key(con, switches[i]) == 0);
	}
	assert(console64_repl_getchar() == '\r');
	assert(console64_input_mode_con(con) == 8 && !con->caret_wanted);
	console64_repl_set_active(0);
	assert(console64_input_mode_con(NULL) == -22);
	other->close_ready = 1;
	assert(console64_input_mode_con(other) == -22);
	console64_reap_closed();
	select64(con);
	current64 = con->task;
	console_prompt64(con);
	puts("Console input mode: per-owner cooked/raw/readline/REPL, generation wrap: PASS");
}

static void close_during_capture64(void)
{
	struct CONSOLE64 *con = console_self64();
	struct TASK64 *owner = current64;
	size_t resources = live64;

	assert(con->line_capture && !con->close_ready);
	console64_request_close(con);
	current64 = &tasks64[0];
	console64_reap_closed();
	assert(con->slot_state == CONSOLE64_LIVE && live64 == resources);
	current64 = owner;
	assert(console64_post_input_key(con, 0x1d) == 0);
	assert(console64_post_input_key(con, 0x2e) == 0);
	assert(console64_post_input_key(con, 0x9d) == 0);
}

static void capture_close64(struct CONSOLE64 *con)
{
	char line[8];

	current64 = con->task;
	on_sleep64 = close_during_capture64;
	assert(console64_read_line(line, sizeof(line)) == -1);
	assert(con->close_requested && !con->close_ready && !con->line_capture);
	on_sleep64 = NULL;
	consume64(con); /* release the physical Ctrl before this task parks */
	assert(console64_post_input_key(con, 0x1e) == 0);
	sleep_checks64 = 0;
	if (setjmp(sleep_stop64) == 0) { entries64[current64 - tasks64](); assert(0); }
	assert(con->close_ready && fifo64_status(&con->keys) == 1);
	irq64 = 0;
	current64 = &tasks64[0];
	console64_reap_closed();
	assert(con->slot_state == CONSOLE64_FREE);
}

static void focus_close64(void)
{
	struct CONSOLE64 *boot = console64_active();
	struct CONSOLE64 *first = console64_create();
	struct CONSOLE64 *second = console64_create();
	struct CONSOLE64 *third = console64_create();
	assert(first && second && third && console64_create() == NULL);
	current64 = boot->task;
	boot->lang_hangul = 0;
	console64_set_raw(1);
	first->lang_hangul = 0;
	second->lang_hangul = 0;
	select64(first);
	route64(0x1e);
	select64(second);
	route64(0x30);
	consume64(second);
	consume64(first);
	assert(first->input_len == 1 && first->input_line[0] == 'a');
	assert(second->input_len == 1 && second->input_line[0] == 'b');
	assert(boot->raw_queue_head == boot->raw_queue_tail);
	/* The physical modifier is shared across focus; releases go to the new owner. */
	select64(boot);
	route64(KEY64_RCTRL);
	consume64(boot);
	select64(second);
	route64(KEY64_RCTRL | 0x80);
	consume64(second);
	assert(!keyboard64_ctrl());
	select64(boot);
	route64(0x1e);
	consume64(boot);
	expect_event64(boot, TTY_KIND_CHAR, 'a', 0);
	select64(second);
	uint32_t generation = boot->size_generation;
	assert(gui_toggle_window_checked64() == 0);
	assert(gui64_focused_console() == second && first->raw_mode == 0);
	assert(boot->size_generation == generation + 1);
	expect_event64(boot, TTY_KIND_RESIZE, 0, 0);
	current64 = &tasks64[0];
	console64_request_close(first);
	console64_request_close(first);
	assert(fifo64_status(&first->keys) == 1); /* idempotent request */
	console64_reap_closed();
	assert(first->slot_state == CONSOLE64_LIVE && gui64_focused_console() == second);
	assert(gui_close_console_checked64(first) == -16); /* editor still running */
	select64(first);
	route64(0x30); /* input while a running app has not reached the safe close point */
	select64(second);
	first->close_ready = 1;
	assert(console64_post_input_key(first, 0x2e) != 0);
	fail_kill64 = 1;
	console64_reap_closed();
	assert(first->close_ready && first->slot_state == CONSOLE64_LIVE);
	assert(gui64_focused_console() == second);
	no_failures64();
	console64_reap_closed();
	assert(first->slot_state == CONSOLE64_FREE && gui64_focused_console() == second);
	struct CONSOLE64 *reused = console64_create();
	assert(reused == first && fifo64_status(&reused->keys) == 0);
	assert(!reused->raw_mode && !reused->repl_active && !reused->close_requested);
	select64(second);
	second->close_ready = 1;
	console64_reap_closed();
	assert(second->slot_state == CONSOLE64_FREE && gui64_focused_console() != second);
	assert(gui64_focused_console() != NULL);
	capture_close64(third);
	assert(gui64_focused_console() != third);
	/* Close the primary raw console while another console owns focus. */
	select64(reused);
	console64_request_close(boot);
	console64_reap_closed();
	assert(boot->task != NULL && !boot->close_ready);
	boot->close_ready = 1;
	console64_reap_closed();
	assert(boot->slot_state == CONSOLE64_FREE && gui64_focused_console() == reused);
	reused->close_ready = 1;
	console64_reap_closed();
	assert(gui_context64.win_count == 0 && gui64_focused_console() == NULL);
	assert(gui_context64.key_win == gui_context64.back);
	assert(gui_context64.status_mode < 0 && gui_context64.status_owner == NULL);
	reused = console64_create();
	assert(reused == first && gui64_focused_console() == reused);
	assert(reused->size_generation == 1 && fifo64_status(&reused->keys) == 0);
	verify64();
	cleanup64();
	assert(gui64_focused_console() == console64_active()); /* compositor fallback */
	puts("Console state: four-console routing, modifiers, close/retry and slot reuse: PASS");
}

int main(void)
{
	lifecycle_checks64();
	initialize64();
	memset(hangul_font64, 0xff, sizeof(hangul_font64));
	console64_set_hangul_font(hangul_font64);
	mode_sequences64();
	capture64();
	resize_waits64();
	caret_pixels64();
	caret_ime64();
	input_mode64();
	status_indicator64();
	focus_close64();
	small_status64();
	return 0;
}
