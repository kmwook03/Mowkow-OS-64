#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src64/kernel/console_internal64.h"
#include "../src64/kernel/gui_internal64.h"
#include <arch/platform64.h>
#include <bootinfo64.h>
#include <fd64.h>
#include <gui64.h>
#include <keyboard64.h>
#include <kstring64.h>
#include <memory64.h>
#include <mpport64.h>
#include <mtask64.h>
#include <process64.h>
#include <sheet64.h>
#include <syscall64.h>
#include <timer64.h>
#include <window64.h>

#define WIDTH64 128
#define HEIGHT64 64
#define STRIDE64 136
#define BYTES64 (STRIDE64 * HEIGHT64)

const uint8_t hankaku64[4096] = { ['A' * 16] = 0xff };
struct MEMMAN64 memman64;
struct TASKCTL64 taskctl64;
struct TIMERCTL64 timerctl64;
static struct TASK64 *current64;
static struct SHEET64 sheets64[CONSOLE64_MAX];
static uint8_t pixels64[CONSOLE64_MAX][BYTES64 + 32];
static const uint8_t font64[HANGUL64_FONT_SIZE];
static int shift64, ctrl64, irq64, sleeps64, wakes64, refreshes64;
static int opens64, closes64, toggles64, executions64, interpreters64;
static size_t allocations64, frees64;
static struct CONSOLE64 *owner64;
static char path64[CONSOLE64_LINE_MAX + 5], argv64[CONSOLE64_LINE_MAX + 5];
static char captured64[16], captured_path64[64];
static int64_t captured_length64;
static char serial64[32768];
static size_t serial_length64;

uint8_t io_in8(uint16_t port) { (void) port; return 0x20; }
void io_out8(uint16_t port, uint8_t value)
{
	(void) port;
	assert(serial_length64 + 1 < sizeof(serial64));
	serial64[serial_length64++] = (char) value;
	serial64[serial_length64] = '\0';
}
uint64_t platform_irq_save64(void) { int old = irq64; irq64 = 1; return old; }
void platform_irq_restore64(uint64_t value) { irq64 = (int) value; }
void platform_irq_disable64(void) { irq64 = 1; }
void platform_irq_enable64(void) { irq64 = 0; }
int keyboard64_shift(void) { return shift64; }
int keyboard64_ctrl(void) { return ctrl64; }
int keyboard64_alt(void) { return 0; }
int keyboard64_track_modifier(uint16_t key)
{
	if (key == 0x1d || key == 0x9d) { ctrl64 = key == 0x1d; return 1; }
	if (key == 0x2a || key == 0xaa) { shift64 = key == 0x2a; return 1; }
	return 0;
}
struct TASK64 *task_now64(void) { return current64; }
struct TASK64 *task_alloc64(void)
{
	for (int i = 1; i < MAX_TASKS64; i++) {
		if (taskctl64.tasks0[i].flags == TASK64_FLAGS_UNUSED) {
			taskctl64.tasks0[i] = (struct TASK64) { .flags = TASK64_FLAGS_ALLOCATED };
			return &taskctl64.tasks0[i];
		}
	}
	return NULL;
}
int task_set_entry64(struct TASK64 *task, void (*entry)(void), uintptr_t base, size_t size)
{
	assert(entry != NULL && base != 0 && size == CONSOLE64_STACK_SIZE);
	task->stack_base = base;
	task->stack_size = size;
	return 0;
}
int task_run64(struct TASK64 *task, int level, int priority)
{
	(void) level;
	(void) priority;
	/* FIFO wake may inherit the enclosing lifecycle IRQ guard. */
	task->flags = TASK64_FLAGS_RUNNING;
	wakes64++;
	return 0;
}
int task_sleep64(struct TASK64 *task)
{
	assert(task != NULL && irq64 != 0);
	sleeps64++;
	return 0;
}
uintptr_t memman64_alloc_4k(struct MEMMAN64 *man, size_t size)
{
	(void) man;
	allocations64++;
	return (uintptr_t) malloc(size);
}
int memman64_free_4k(struct MEMMAN64 *man, uintptr_t address, size_t size)
{
	(void) man;
	(void) size;
	frees64++;
	free((void *) address);
	return 0;
}
size_t memman64_total(const struct MEMMAN64 *man) { (void) man; return 4096; }
int task_kill64(struct TASK64 *task)
{
	assert(task != current64);
	if (task->stack_base != 0) {
		memman64_free_4k(&memman64, task->stack_base, task->stack_size);
	}
	*task = (struct TASK64) { 0 };
	return 0;
}
struct SURFACE64 sheet64_surface(const struct SHEET64 *sheet)
{
	if (sheet == NULL) { return (struct SURFACE64) { 0 }; }
	return (struct SURFACE64) {
		sheet->buf, sheet->buf_size, sheet->stride, sheet->bxsize, sheet->bysize,
		0, 0, sheet->bxsize, sheet->bysize
	};
}
void sheet64_refresh(struct SHEET64 *sheet, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
	assert(sheet != NULL && x0 >= 0 && y0 >= 0 && x1 >= x0 && y1 >= y0);
	refreshes64++;
}
void window64_set_hangul_font(const uint8_t *font) { assert(font == font64); }
void gui64_bind_console(struct SHEET64 *sheet, struct CONSOLE64 *con)
{
	assert(sheet != NULL && con != NULL);
}
void gui_update_status64(void) { }
struct SHEET64 *gui64_init(const struct BOOTINFO64 *info)
{
	assert(info != NULL);
	return &sheets64[0];
}
int gui64_open_console_window(struct CONSOLE64 *con, const char *title)
{
	assert(strcmp(title, "터미널") == 0);
	assert(opens64 + 1 < CONSOLE64_MAX);
	opens64++;
	console64_attach_sheet(con, &sheets64[opens64], 0, 0, WIDTH64, HEIGHT64);
	return 0;
}
void gui64_close_console(struct CONSOLE64 *con)
{
	closes64++;
	console64_destroy(con);
}
int gui_reclaim64(void) { return 0; }
int gui_has_console64(const struct CONSOLE64 *con) { return con != NULL && con->close_ready; }
int gui_prepare_console64(struct CONSOLE64 *con, const char *title)
{
	return gui64_open_console_window(con, title);
}
void gui_publish_console64(struct CONSOLE64 *con) { (void) con; }
int gui_close_console_checked64(struct CONSOLE64 *con)
{
	int status = console_stop_checked64(con);

	if (status == 0) { closes64++; console_finish_destroy64(con); }
	return status;
}
void gui64_toggle_window(void) { toggles64++; }
uint32_t fd64_file_count(void) { return 1; }
int fd64_file_at(uint32_t index, struct FDINFO64 *out, char *name, size_t size)
{
	assert(index == 0);
	assert(str_copy64(name, size, "한글.txt") == 0);
	*out = (struct FDINFO64) { .size = 12 };
	return 1;
}
int fd64_open(struct FDHANDLE64 *file, const char *name)
{
	assert(strcmp(name, "readme.txt") == 0);
	memset(file, 0, sizeof(*file));
	return 0;
}
size_t fd64_read(struct FDHANDLE64 *file, void *dst, size_t size)
{
	(void) file;
	(void) dst;
	(void) size;
	assert(0);
	return 0;
}
int process64_exec_file(const char *path, const char *argv, struct CONSOLE64 *con)
{
	assert(str_copy64(path64, sizeof(path64), path) == 0);
	assert(str_copy64(argv64, sizeof(argv64), argv) == 0);
	owner64 = con;
	executions64++;
	return strncmp(path, "hello.elf", 9) == 0 ? 0 : -2;
}
void mpport_repl(void) { interpreters64++; }
void mpport_run_file(const char *path)
{
	/* Exercise interpreter argument snapshot before the shared editor is reused. */
	assert(str_copy64(captured_path64, sizeof(captured_path64), path) == 0);
	captured_length64 = console64_read_line(captured64, sizeof(captured64));
	interpreters64++;
}
void mpport_run_mowkow(const char *arg)
{
	assert(arg == NULL || strcmp(arg, "hello.mk") == 0);
	interpreters64++;
}

static void command64(struct CONSOLE64 *con, const char *line)
{
	assert(str_copy64(con->input_line, sizeof(con->input_line), line) == 0);
	con->input_len = (uint16_t) strlen(line);
	console64_process_key(con, 0x1c);
	assert(con->input_len == 0 && con->line_full_warned == 0);
}

static void post64(struct CONSOLE64 *con, const uint16_t *keys, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		assert(console64_post_input_key(con, keys[i]) == 0);
	}
}

static void guards64(void)
{
	for (int slot = 0; slot < CONSOLE64_MAX; slot++) {
		for (size_t i = 0; i < BYTES64 + 32; i++) {
			if (i < 16 || i >= BYTES64 + 16 || (i - 16) % STRIDE64 >= WIDTH64) {
				assert(pixels64[slot][i] == 0xa5);
			}
		}
	}
}

int main(void)
{
	struct BOOTINFO64 info = { .scrnx = WIDTH64, .scrny = HEIGHT64,
		.bytes_per_scanline = STRIDE64, .bpp = 8 };
	struct CONSOLE64 *boot, *second, *third;
	struct CONSOLE64 input = { 0 };
	uint16_t key;
	char line[16];
	uint64_t event;
	static const uint16_t hangul[] = { 0x13, 0x25, 0x1c };
	static const uint16_t ctrl_c[] = { 0x1d, 0x2e, 0x9d };
	static const uint16_t ctrl_d[] = { 0x1d, 0x20, 0x9d };
	static const uint16_t ascii[] = { 0x1e, 0x30, 0x0e, 0x2e, 0x1c };

	for (int i = 0; i < CONSOLE64_MAX; i++) {
		memset(pixels64[i], 0xa5, sizeof(pixels64[i]));
		sheets64[i] = (struct SHEET64) {
			.buf = pixels64[i] + 16, .buf_size = (HEIGHT64 - 1) * STRIDE64 + WIDTH64,
			.stride = STRIDE64, .bxsize = WIDTH64, .bysize = HEIGHT64, .height = -1
		};
	}
	current64 = &taskctl64.tasks0[0];
	current64->flags = TASK64_FLAGS_RUNNING;
	info.vram = (uintptr_t) sheets64[0].buf;
	console64_set_hangul_font(font64);
	console64_init_on_sheet(&info, &sheets64[0]);
	boot = console64_active();
	assert(boot->lang_hangul == 1 && boot->size_generation == 1);
	assert(console64_start_task(boot) == 0);
	current64 = boot->task;
	assert(console64_size() == (16U | (4U << 16) | ((uint64_t) 1 << 32)));
	assert(console64_hangul_smoke() == 1);
	command64(boot, "help");
	command64(boot, "ls");
	command64(boot, "type readme.txt");
	command64(boot, "mem");
	command64(boot, "tasks");
	command64(boot, "hello alpha 가나다");
	assert(executions64 == 2 && owner64 == boot);
	assert(strcmp(argv64, "hello.elf alpha 가나다") == 0);
	command64(boot, "xwindow");
	assert(toggles64 == 1);
	command64(boot, "py");
	post64(boot, hangul, sizeof(hangul) / sizeof(hangul[0]));
	command64(boot, "py script.py");
	assert(strcmp(captured_path64, "script.py") == 0);
	assert(captured_length64 == 3 && memcmp(captured64, "가", 3) == 0);
	command64(boot, "머꼬");
	command64(boot, "머꼬 hello.mk");
	assert(interpreters64 == 4);
	post64(boot, ctrl_c, sizeof(ctrl_c) / sizeof(ctrl_c[0]));
	assert(console64_read_line(line, sizeof(line)) == -1);
	post64(boot, ctrl_d, sizeof(ctrl_d) / sizeof(ctrl_d[0]));
	assert(console64_read_line(line, sizeof(line)) == -2);
	post64(boot, ascii, sizeof(ascii) / sizeof(ascii[0]));
	assert(console64_read(line, sizeof(line)) == 3 && memcmp(line, "ac\n", 3) == 0);
	boot->lang_hangul = 0;
	console64_repl_set_active(1);
	post64(boot, ascii, 3);
	assert(console64_repl_getchar() == 'a');
	assert(console64_repl_getchar() == 'b');
	assert(console64_repl_getchar() == '\b');
	console64_repl_set_active(0);
	boot->lang_hangul = 1;
	console64_set_raw(1);
	post64(boot, hangul, sizeof(hangul) / sizeof(hangul[0]));
	static const unsigned int kinds[] = {
		TTY_KIND_PREEDIT, TTY_KIND_PREEDIT, TTY_KIND_CHAR, TTY_KIND_CHAR
	};
	static const unsigned int payloads[] = { 0x3131, 0xac00, 0xac00, '\n' };
	for (size_t i = 0; i < 4; i++) {
		event = console64_read_key();
		assert((uint32_t) event == payloads[i] && (event >> 32 & 0xff) == kinds[i]);
	}
	console64_attach_sheet(boot, &sheets64[0], 0, 0, WIDTH64, HEIGHT64);
	event = console64_read_key();
	assert((event >> 32 & 0xff) == TTY_KIND_RESIZE);
	console64_move(1, 2);
	console64_set_attr(9, 3);
	console64_write("A", 1);
	assert(pixels64[0][16 + STRIDE64 * 16 + 16] == 9);
	assert(boot->dirty_valid != 0);
	int before = refreshes64;
	console64_flush();
	assert(refreshes64 == before + 2 && boot->dirty_valid == 0 && boot->caret_drawn);
	console64_set_raw(0);
	assert(console64_is_raw() == 0 && boot->cursor_x == 0 && boot->composing.state == 0);
	console64_write("A", 1);
	assert(pixels64[0][16] == CONSOLE64_FG_DEFAULT);
	second = console64_create();
	third = console64_create();
	assert(second != NULL && third != NULL && second != third);
	current64 = second->task;
	console64_move(0, 0);
	console64_puts("A");
	assert(second->cursor_x == CONSOLE64_FONT_W && third->cursor_x == 16);
	assert(pixels64[1][16] == CONSOLE64_FG_DEFAULT);
	command64(second, "xwindow");
	assert(toggles64 == 1);
	current64 = boot->task;
	console64_input_queue_init(&input);
	input.lang_hangul = 0;
	input.vram = sheets64[3].buf;
	input.vram_size = sheets64[3].buf_size;
	input.stride = STRIDE64;
	input.width = WIDTH64;
	input.height = HEIGHT64;
	assert(console64_start_input_task(&input) == 0);
	assert(console_pump_event64(&input, &key) == 0 && sleeps64 == 1 && irq64 == 0);
	post64(&input, ascii, sizeof(ascii) / sizeof(ascii[0]));
	assert(console64_drain_input(&input) == 5 && input.input_len == 0);
	console64_destroy(&input);
	console64_request_close(second);
	assert(second->close_requested != 0 && fifo64_status(&second->keys) == 1);
	second->close_ready = 1; /* model the task's safe return-to-prompt point */
	console64_reap_closed();
	assert(closes64 == 1 && second->task == NULL && second->vram == NULL);
	console64_destroy(third);
	current64 = &taskctl64.tasks0[0];
	console64_destroy(boot);
	assert(allocations64 == frees64 && wakes64 >= 4 && irq64 == 0);
	guards64();
#ifndef __aarch64__
	assert(strstr(serial64, "commands:") != NULL);
	assert(strstr(serial64, "hello.elf") == NULL); /* argv stays out of normal output */
	assert(strstr(serial64, "fullscreen") != NULL);
#endif
	puts("Console modules: dispatch, readline/IME, raw/REPL, task routing and lifecycle: PASS");
	return 0;
}
