#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall64.h>

#include "../src64/kernel/gui64.c"
#include "../src64/kernel/console64.c"

#define WIDTH64 320
#define HEIGHT64 240
#define STRIDE64 328
#define MAX_ALLOCATIONS64 64

struct MEMMAN64 memman64;
const uint8_t hankaku64[4096] = { ['A' * 16] = 0xff };
static struct TASK64 tasks64[32];
static struct TASK64 *current64 = &tasks64[0];
static void (*entries64[32])(void);
static uint8_t vram64[STRIDE64 * HEIGHT64];
static jmp_buf sleep_stop64;
static int irq64, sleep_checks64;
static void (*before_irq64)(void);
static void (*on_sleep64)(void);

static void inject_before_irq64(void)
{
	if (irq64 == 0 && before_irq64 != NULL) {
		void (*hook)(void) = before_irq64;

		before_irq64 = NULL;
		hook();
	}
}
static int alloc_calls64, sheet_calls64, bind_calls64;
static int fail_alloc64, fail_sheet64, fail_bind64;
static int fail_task_alloc64, fail_fifo64, fail_entry64, fail_run64, fail_kill64;
static int fail_free64, kill_free_fail64;
static uintptr_t fail_free_address64;
static size_t live64;
static int render_probe64, render_checked64;
static unsigned long screen_hash64(void);

struct ALLOCATION64 {
	uintptr_t address;
	size_t size;
};
static struct ALLOCATION64 allocations64[MAX_ALLOCATIONS64];

struct SHEET64 *fixture_sheet_alloc64(struct SHTCTL64 *ctl);
int fixture_sheet_setbuf64(struct SHEET64 *sheet, uint8_t *buf, size_t size,
	uint32_t stride, int32_t width, int32_t height, int32_t inverse);
int fixture_fifo_init64(struct FIFO64 *fifo, uint32_t size,
	struct EVENT64 *buf, struct TASK64 *task);

uint64_t platform_irq_save64(void)
{
	inject_before_irq64();
	int old = irq64;
	irq64 = 1;
	return old;
}
void platform_irq_restore64(uint64_t state) { irq64 = (int) state; }
void platform_irq_disable64(void)
{
	inject_before_irq64();
	irq64 = 1;
}
void platform_irq_enable64(void) { irq64 = 0; }
uint8_t io_in8(uint16_t port) { (void) port; return 0x20; }
void io_out8(uint16_t port, uint8_t value)
{
	(void) port;
	(void) value;
	if (render_probe64 && !render_checked64) {
		struct CONSOLE64 *con = console64_active();
		unsigned long image = screen_hash64();
		size_t resources = live64;

		render_checked64 = 1;
		assert(con->render_users != 0 && irq64 == 0);
		assert(gui_toggle_window_checked64() == -16);
		con->close_ready = 1;
		assert(gui_close_console_checked64(con) == -16);
		con->close_ready = 0;
		assert(screen_hash64() == image && live64 == resources);
	}
}
uint32_t graphic64_rgb32(uint8_t index) { return index; }

uintptr_t memman64_alloc_4k(struct MEMMAN64 *man, size_t size)
{
	(void) man;
	alloc_calls64++;
	if (alloc_calls64 == fail_alloc64) { return 0; }
	for (int i = 0; i < MAX_ALLOCATIONS64; i++) {
		if (allocations64[i].address != 0) { continue; }
		allocations64[i].address = (uintptr_t) calloc(1, (size + 4095U) & ~(size_t) 4095U);
		assert(allocations64[i].address != 0);
		allocations64[i].size = size;
		live64++;
		return allocations64[i].address;
	}
	assert(0);
	return 0;
}

int memman64_free_4k(struct MEMMAN64 *man, uintptr_t address, size_t size)
{
	(void) man;
	for (int i = 0; i < MAX_ALLOCATIONS64; i++) {
		if (allocations64[i].address != address) { continue; }
		assert(allocations64[i].size == size);
		if (fail_free64 || fail_free_address64 == address) { return -12; }
		free((void *) address);
		allocations64[i] = (struct ALLOCATION64) { 0 };
		live64--;
		return 0;
	}
	assert(0); /* double free or lost ownership */
	return -22;
}

struct SHEET64 *sheet64_alloc(struct SHTCTL64 *ctl)
{
	return ++sheet_calls64 == fail_sheet64 ? NULL : fixture_sheet_alloc64(ctl);
}

int sheet64_setbuf(struct SHEET64 *sheet, uint8_t *buf, size_t size,
	uint32_t stride, int32_t width, int32_t height, int32_t inverse)
{
	if (++bind_calls64 == fail_bind64) { return -22; }
	return fixture_sheet_setbuf64(sheet, buf, size, stride, width, height, inverse);
}

int fifo64_init(struct FIFO64 *fifo, uint32_t size, struct EVENT64 *buf, struct TASK64 *task)
{
	if (fail_fifo64) { return -22; }
	return fixture_fifo_init64(fifo, size, buf, task);
}

struct TASK64 *task_now64(void) { return current64; }
struct TASK64 *task_alloc64(void)
{
	if (fail_task_alloc64) { return NULL; }
	for (int i = 1; i < 32; i++) {
		if (tasks64[i].flags == TASK64_FLAGS_UNUSED) {
			tasks64[i] = (struct TASK64) { .flags = TASK64_FLAGS_ALLOCATED };
			return &tasks64[i];
		}
	}
	return NULL;
}

int task_set_entry64(struct TASK64 *task, void (*entry)(void), uintptr_t stack, size_t size)
{
	assert(irq64 != 0);
	if (fail_entry64) { return -22; }
	task->stack_base = stack;
	task->stack_size = size;
	entries64[task - tasks64] = entry;
	return 0;
}

int task_run64(struct TASK64 *task, int level, int priority)
{
	(void) level;
	(void) priority;
	if (fail_run64) { return -28; }
	task->flags = TASK64_FLAGS_RUNNING;
	return 0;
}

int task_kill64(struct TASK64 *task)
{
	uintptr_t stack = task->stack_base;
	size_t size = task->stack_size;
	int status = 0;

	assert(irq64 != 0);
	if (task == current64 || fail_kill64) { return -22; }
	if (kill_free_fail64) { fail_free_address64 = stack; }
	*task = (struct TASK64) { 0 };
	if (stack != 0) { status = memman64_free_4k(&memman64, stack, size); }
	return status;
}

int task_sleep64(struct TASK64 *task)
{
	assert(irq64 != 0 && task == current64);
	if (on_sleep64 != NULL) { on_sleep64(); return 0; }
	assert(console_self64()->close_ready != 0);
	if (++sleep_checks64 == 2) { longjmp(sleep_stop64, 1); }
	return 0; /* emulate a spurious wake before the reaper runs */
}

static void no_failures64(void)
{
	fail_alloc64 = fail_sheet64 = fail_bind64 = 0;
	fail_task_alloc64 = fail_fifo64 = fail_entry64 = fail_run64 = fail_kill64 = 0;
	fail_free64 = kill_free_fail64 = 0;
	fail_free_address64 = 0;
}

static struct BOOTINFO64 info64(void)
{
	return (struct BOOTINFO64) { .vram = (uintptr_t) vram64, .scrnx = WIDTH64,
		.scrny = HEIGHT64, .bytes_per_scanline = STRIDE64, .framebuffer_type = 1, .bpp = 8,
		.reserved2 = sizeof(vram64) };
}

static unsigned long screen_hash64(void)
{
	unsigned long hash = 0;
	for (size_t i = 0; i < sizeof(vram64); i++) { hash = hash * 33 + vram64[i]; }
	return hash;
}

static void verify64(void)
{
	assert(irq64 == 0);
	assert(gui_context64.ready);
	for (int h = 0; h <= gui_context64.ctl->top; h++) {
		struct SHEET64 *sheet = gui_context64.ctl->sheets[h];
		assert(sheet->height == h && sheet->flags != 0 && sheet->buf != NULL);
	}
	for (int i = 0; i < gui_context64.win_count; i++) {
		struct GUI64_WIN *win = &gui_context64.wins[i];
		assert(win->buffer == win->sht->buf && win->buffer_size == win->sht->buf_size);
		assert(win->published || win->sht->height == -1);
	}
	for (int y = 0; y < HEIGHT64; y++) {
		for (int x = WIDTH64; x < STRIDE64; x++) { assert(vram64[y * STRIDE64 + x] == 0xa5); }
	}
}

static void cleanup64(void)
{
	no_failures64();
	current64 = &tasks64[0];
	while (gui_context64.win_count > 0) {
		struct CONSOLE64 *con = gui_context64.wins[gui_context64.win_count - 1].console;
		con->close_ready = 1;
		assert(gui_close_console_checked64(con) == 0);
	}
	gui_context64.ready = 0;
	assert(gui_reclaim64() == 0 && live64 == 0);
	for (int i = 1; i < 32; i++) { assert(tasks64[i].flags == TASK64_FLAGS_UNUSED); }
}

static void initialize64(void)
{
	struct BOOTINFO64 info = info64();
	struct SHEET64 *sheet;

	no_failures64();
	memset(vram64, 0xa5, sizeof(vram64));
	sheet = gui64_init(&info);
	assert(sheet != NULL);
	console64_init_on_sheet(&info, sheet);
	assert(console64_start_task(console64_active()) == 0);
	verify64();
}

static void init_failures64(void)
{
	struct BOOTINFO64 info = info64();
	for (int kind = 0; kind < 3; kind++) {
		for (int stage = 1; stage <= (kind == 0 ? 5 : 4); stage++) {
			struct SHEET64 *sheet;
			no_failures64();
			if (kind == 0) { fail_alloc64 = alloc_calls64 + stage; }
			if (kind == 1) { fail_sheet64 = sheet_calls64 + stage; }
			if (kind == 2) { fail_bind64 = bind_calls64 + stage; }
			sheet = gui64_init(&info);
			if ((kind == 0 && stage == 5) || (kind != 0 && stage == 4)) {
				assert(sheet != NULL && gui_context64.taskbar == NULL);
				console64_init_on_sheet(&info, sheet);
				cleanup64();
			} else { assert(sheet == NULL && live64 == 0 && !gui64_available()); }
			no_failures64();
			sheet = gui64_init(&info);
			assert(sheet != NULL); /* retry must never reuse dangling context state */
			console64_init_on_sheet(&info, sheet);
			cleanup64();
		}
	}
	no_failures64();
	fail_sheet64 = sheet_calls64 + 3;
	fail_free64 = 1;
	assert(gui64_init(&info) == NULL && live64 != 0 && !gui64_available());
	no_failures64();
	assert(gui_reclaim64() == 0 && live64 == 0);
}

static void create_failures64(void)
{
	struct SHEET64 *focus = gui_context64.key_win;
	size_t baseline = live64;
	unsigned long image = screen_hash64();
	for (int stage = 0; stage < 8; stage++) {
		no_failures64();
		if (stage == 0) { fail_alloc64 = alloc_calls64 + 1; }
		if (stage == 1) { fail_sheet64 = sheet_calls64 + 1; }
		if (stage == 2) { fail_bind64 = bind_calls64 + 1; }
		if (stage == 3) { fail_task_alloc64 = 1; }
		if (stage == 4) { fail_alloc64 = alloc_calls64 + 2; }
		if (stage == 5) { fail_fifo64 = 1; }
		if (stage == 6) { fail_entry64 = 1; }
		if (stage == 7) { fail_run64 = 1; }
		assert(console64_create() == NULL);
		assert(gui_context64.win_count == 1 && gui_context64.key_win == focus);
		assert(live64 == baseline && screen_hash64() == image);
		assert(console_table[1].slot_state == CONSOLE64_FREE);
		verify64();
	}
	no_failures64();
	fail_entry64 = fail_kill64 = 1;
	assert(console64_create() == NULL);
	assert(console_table[1].slot_state == CONSOLE64_CLOSING);
	assert(gui_context64.win_count == 2 && !gui_context64.wins[1].published);
	assert(screen_hash64() == image);
	taskbar_press(GUI64_BAR_GAP + GUI64_BAR_ENTRY_W + GUI64_BAR_GAP + 2, 5);
	assert(gui_context64.key_win == focus); /* unpublished cleanup is not clickable */
	no_failures64();
	console64_reap_closed();
	assert(live64 == baseline && gui_context64.win_count == 1);
}

static void resize_failures64(void)
{
	struct CONSOLE64 *con = console64_active();
	struct SHEET64 saved = *gui_context64.console;
	struct CONSOLE64 before = *con;
	size_t baseline = live64;
	unsigned long image = screen_hash64();

	con->render_users = 1;
	assert(gui_toggle_window_checked64() == -16);
	assert(screen_hash64() == image && live64 == baseline);
	con->render_users = 0;
	fail_alloc64 = alloc_calls64 + 1;
	assert(gui_toggle_window_checked64() == -12);
	no_failures64();
	fail_bind64 = bind_calls64 + 1;
	assert(gui_toggle_window_checked64() == -22);
	no_failures64();
	assert(memcmp(&before, con, sizeof(before)) == 0);
	assert(memcmp(&saved, gui_context64.console, sizeof(saved)) == 0);
	assert(screen_hash64() == image && live64 == baseline);
	con->raw_mode = 1;
	con->raw_queue_head = 0;
	con->raw_queue_tail = CONSOLE64_RAW_QUEUE_SIZE - 1;
	before = *con;
	assert(gui_toggle_window_checked64() == -28);
	assert(memcmp(&before, con, sizeof(before)) == 0 && screen_hash64() == image);
	con->raw_queue_tail = 0;
	before = *con;
	fail_run64 = 1;
	assert(gui_toggle_window_checked64() == -28);
	assert(memcmp(&before, con, sizeof(before)) == 0 && screen_hash64() == image);
	no_failures64();
	uint32_t generation = con->size_generation;
	assert(gui_toggle_window_checked64() == 0);
	assert(con->size_generation == generation + 1 && con->sheet == gui_context64.console);
	uint64_t event;
	assert(console_raw_pop64(con, &event) == 1 && (event >> 32 & 0xff) == TTY_KIND_RESIZE);
	uintptr_t old_buffer = (uintptr_t) con->sheet->buf;
	fail_free_address64 = old_buffer;
	assert(gui_toggle_window_checked64() == 0 && live64 == baseline + 1);
	assert(gui_context64.retired[0].address == old_buffer);
	no_failures64();
	assert(gui_reclaim64() == 0 && live64 == baseline);
	con->raw_mode = 0;
	verify64();
}

static void close_failures64(void)
{
	struct CONSOLE64 *first = console64_create();
	struct CONSOLE64 *second = console64_create();
	assert(first != NULL && second != NULL);
	struct SHEET64 *focused = gui_context64.key_win;
	struct GUI64_WIN *win = find_console64(first);
	struct SHEET64 *closing_sheet = win->sht;
	size_t baseline = live64;
	unsigned long image = screen_hash64();
	assert(gui_close_console_checked64(first) == -16); /* unsafe live interpreter */
	first->close_ready = 1;
	first->render_users = 1;
	assert(gui_close_console_checked64(first) == -16);
	assert(live64 == baseline && screen_hash64() == image);
	first->render_users = 0;
	fail_kill64 = 1;
	assert(gui_close_console_checked64(first) == -22);
	assert(live64 == baseline && screen_hash64() == image && gui_context64.key_win == focused);
	no_failures64();
	gui_context64.drag = win->sht;
	gui_context64.mmx = 0;
	assert(gui_close_console_checked64(first) == 0);
	assert(gui_context64.drag == NULL && gui_context64.mmx == -1);
	assert(gui_context64.key_win == focused && gui64_focused_console() == second);
	/* A stale drag pointer must not move a newly reused sheet. */
	struct SHEET64 *reused = sheet64_alloc(gui_context64.ctl);
	assert(reused == closing_sheet);
	uint8_t replacement[16 * 16] = { 0 };
	assert(sheet64_setbuf(reused, replacement, sizeof(replacement), 16, 16, 16, -1) == 0);
	gui_context64.btn_prev = 1;
	gui64_mouse_event(3, 2, 1);
	assert(reused->vx0 == 0 && reused->vy0 == 0);
	sheet64_free(reused);
	second->close_ready = 1;
	kill_free_fail64 = 1;
	assert(gui_close_console_checked64(second) != 0);
	assert(second->task == NULL && second->pending_stack != 0);
	assert(second->slot_state == CONSOLE64_CLOSING && gui_has_console64(second));
	no_failures64();
	assert(gui_close_console_checked64(second) == 0);
	struct CONSOLE64 *next = console64_create();
	assert(next == first && next->slot_state == CONSOLE64_LIVE);
	next->close_ready = 1;
	uintptr_t buffer = (uintptr_t) next->sheet->buf;
	fail_free_address64 = buffer;
	assert(gui_close_console_checked64(next) == 0 && next->slot_state == CONSOLE64_FREE);
	assert(gui_reclaim64() != 0);
	no_failures64();
	assert(gui_reclaim64() == 0);
	verify64();
}

static void retirement_limit64(void)
{
	struct CONSOLE64 *con = console64_active();
	size_t baseline = live64;

	con->raw_mode = 0;
	fail_free64 = 1;
	for (int i = 0; i < GUI64_MAX_WINS; i++) {
		assert(gui_toggle_window_checked64() == 0);
	}
	assert(live64 == baseline + GUI64_MAX_WINS);
	struct SHEET64 before = *con->sheet;
	uint32_t generation = con->size_generation;
	unsigned long image = screen_hash64();
	assert(gui_toggle_window_checked64() == -12);
	assert(memcmp(&before, con->sheet, sizeof(before)) == 0);
	assert(con->size_generation == generation && screen_hash64() == image);
	assert(console64_create() == NULL && live64 == baseline + GUI64_MAX_WINS);
	no_failures64();
	assert(gui_reclaim64() == 0 && live64 == baseline);
	verify64();
}

static void close_park64(void)
{
	struct CONSOLE64 *con = console64_create();
	assert(con != NULL);
	for (int i = 0; i < CONSOLE64_KEY_BUF; i++) {
		assert(console64_post_input_key(con, 0x1e) == 0);
	}
	console64_request_close(con);
	assert(con->keys.overruns == 1 && con->close_requested);
	current64 = con->task;
	sleep_checks64 = 0;
	if (setjmp(sleep_stop64) == 0) { entries64[current64 - tasks64](); assert(0); }
	assert(con->close_ready && fifo64_status(&con->keys) == CONSOLE64_KEY_BUF);
	assert(console64_post_input_key(con, 0x30) != 0);
	irq64 = 0; /* longjmp stands in for context switching, not IRQ return */
	current64 = &tasks64[0];
	console64_reap_closed();
	assert(con->slot_state == CONSOLE64_FREE);
}

static void lifecycle_checks64(void)
{
	tasks64[0].flags = TASK64_FLAGS_RUNNING;
	init_failures64();
	initialize64();
	render_probe64 = 1;
	console64_write_con(console64_active(), "A", 1);
	assert(render_checked64 && console64_active()->render_users == 0);
	render_probe64 = 0;
	create_failures64();
	resize_failures64();
	close_failures64();
	retirement_limit64();
	close_park64();
	cleanup64();
	puts("GUI lifecycle: context, failure atomicity, ownership/retry, resize and close: PASS");
}

void platform_framebuffer_flush64(uintptr_t address, size_t bytes)
{
	(void) address;
	(void) bytes;
}

void platform_framebuffer_sync64(void)
{
}
