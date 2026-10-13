/* Reuse allocation/task fault injection with the production editor/keyboard. */
#define CONSOLE_STATE_MACHINE64
#define main lifecycle_checks64
#include "gui_lifecycle64_host.c"
#undef main

#include <keyboard64.h>

static const uint8_t hangul_font64[HANGUL64_FONT_SIZE];
static int commands64, wait_checks64;

void console_dispatch_command64(struct CONSOLE64 *con, const char *line)
{
	assert(con == console_self64() && line[0] != '\0');
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
	assert(lifecycle_checks64() == 0);
	initialize64();
	console64_set_hangul_font(hangul_font64);
	mode_sequences64();
	capture64();
	resize_waits64();
	focus_close64();
	return 0;
}
