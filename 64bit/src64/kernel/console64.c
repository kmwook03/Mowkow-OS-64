/* Console slot, boot initialization and task lifecycle. */
#include "console_internal64.h"
#include "gui_internal64.h"
#include <arch/platform64.h>
#include <bootinfo64.h>
#include <gui64.h>
#include <kstring64.h>
#include <memory64.h>
#include <mtask64.h>

/* Slot 0 is reserved for boot; slot_state also reserves incomplete cleanup. */
static struct CONSOLE64 console_table[CONSOLE64_MAX];
static struct CONSOLE64 *console_active = &console_table[0];

struct CONSOLE64 *console_self64(void)
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

struct CONSOLE64 *console64_active(void)
{
	return console_active;
}

static void console64_init_with_sheet(const struct BOOTINFO64 *boot_info,
	struct SHEET64 *sht)
{
	struct CONSOLE64 *con = console_active;

	con->slot_state = CONSOLE64_LIVE;
	con->vram = (uint8_t *) boot_info->vram;
	con->width = boot_info->scrnx != 0 ? boot_info->scrnx : 800;
	con->height = boot_info->scrny != 0 ? boot_info->scrny : 600;
	con->stride = boot_info->bytes_per_scanline != 0 ?
		boot_info->bytes_per_scanline : con->width;
	con->vram_size = (size_t) con->stride * con->height;
	con->cursor_x = 0;
	con->cursor_y = 0;
	con->input_len = 0;
	con->lang_hangul = 1;
	hangul64_init(&con->composing);
	if (sht != NULL) {
		gui64_bind_console(sht, con);
		console64_attach_sheet(con, sht, 0, 0, con->width, con->height);
	} else {
		console_clear_screen64(con);
	}
#ifdef __aarch64__
	console64_puts("머꼬 OS AArch64 콘솔\n");
#else
	console64_puts("머꼬 OS x86_64 콘솔\n");
#endif
	console64_puts("한글 입력이 기본입니다. Shift+Space로 영어 입력으로 전환합니다.\n");
	console_prompt64(con);
}

void console64_init(const struct BOOTINFO64 *boot_info)
{
	/* 컴포지터가 뜨면 콘솔은 전체 화면 창 하나가 된다 (로드맵 decision 3b).
	   실패하면 지금까지처럼 LFB에 직접 그린다. */
	console64_init_with_sheet(boot_info, gui64_init(boot_info));
}

void console64_init_on_sheet(const struct BOOTINFO64 *boot_info,
	struct SHEET64 *sheet)
{
	console64_init_with_sheet(boot_info, sheet);
}

static void console_task_main64(void)
{
	struct CONSOLE64 *con = console_self64();

	for (;;) {
		/* 명령 하나가 끝나 프롬프트로 돌아온 자리 -- 자물쇠를 쥔 것이
		   없는, 콘솔을 접기에 안전한 유일한 지점이다. */
		if (con->close_requested != 0) {
			uint64_t irq_state;

			if (con->close_ready == 0) {
				console_puts_con64(con, "콘솔을 닫습니다.\n");
			}
			irq_state = platform_irq_save64();
			con->close_ready = 1;
			(void) task_sleep64(task_now64());
			platform_irq_restore64(irq_state);
			/* A spurious wake must never resume command execution after ready. */
			continue;
		}
		console64_process_key(con, console_wait_key64(con));
	}
}

static void console_input_task_main64(void)
{
	struct CONSOLE64 *con = console_self64();

	for (;;) {
		console64_process_input_key(con, console_wait_key64(con));
	}
}

void console64_request_close(struct CONSOLE64 *con)
{
	uint64_t irq_state = platform_irq_save64();

	if (con != NULL && con->task != NULL && con->close_requested == 0) {
		con->close_requested = 1;
		/* Keep the full-FIFO overrun visible, but do not lose the close wake. */
		if (console64_post_input_key(con, 0) != 0) {
			(void) task_run64(con->task, -1, 0);
		}
	}
	platform_irq_restore64(irq_state);
}

void console64_reap_closed(void)
{
	uint64_t irq_state = platform_irq_save64();

	(void) gui_reclaim64();
	for (uint32_t i = 0; i < CONSOLE64_MAX; i++) {
		struct CONSOLE64 *con = &console_table[i];

		if (con->close_ready == 0) { continue; }
		/* Keep ready set until successful teardown so failures retry next tick. */
		if (gui_has_console64(con)) {
			(void) gui_close_console_checked64(con);
		} else {
			console64_destroy(con);
		}
	}
	platform_irq_restore64(irq_state);
}

struct CONSOLE64 *console64_create(void)
{
	struct CONSOLE64 *con = NULL;
	uint64_t irq_state = platform_irq_save64();

	for (int32_t slot = 1; slot < CONSOLE64_MAX; slot++) {
		if (console_table[slot].slot_state == CONSOLE64_FREE) {
			con = &console_table[slot];
			break;
		}
	}
	if (con == NULL) { goto out; }
	*con = (struct CONSOLE64) { .slot_state = CONSOLE64_RESERVED, .lang_hangul = 1 };
	hangul64_init(&con->composing);
	if (gui_prepare_console64(con, "터미널") != 0 || console64_start_task(con) != 0) {
		con->close_ready = 1;
		if (gui_has_console64(con)) {
			(void) gui_close_console_checked64(con);
		} else if (console_stop_checked64(con) == 0) {
			console_finish_destroy64(con);
		}
		con = NULL;
		goto out;
	}
	gui_publish_console64(con);
	console_prompt64(con);
out:
	platform_irq_restore64(irq_state);
	return con;
}

/* Failure before task removal preserves the live task. If task_kill stopped
 * the task but its stack free failed, keep that allocation until retry.
 */
int console_stop_checked64(struct CONSOLE64 *con)
{
	uintptr_t stack;
	size_t size;
	int status;
	enum CONSOLE_SLOT_STATE64 old_state;

	if (con == NULL) { return -22; }
	if (con->render_users != 0) { return -16; }
	if (con->pending_stack != 0) {
		status = memman64_free_4k(&memman64, con->pending_stack, con->pending_stack_size);
		if (status != 0) { return status; }
		con->pending_stack = 0;
		con->pending_stack_size = 0;
	}
	if (con->task == NULL) { return 0; }
	stack = con->task->stack_base;
	size = con->task->stack_size;
	old_state = con->slot_state;
	con->slot_state = CONSOLE64_CLOSING;
	status = task_kill64(con->task);
	if (status != 0 && con->task->flags != TASK64_FLAGS_UNUSED) {
		con->slot_state = old_state;
		return status;
	}
	con->task = NULL;
	con->keys.task = NULL;
	if (status != 0 && stack != 0) {
		con->pending_stack = stack;
		con->pending_stack_size = size;
		return status;
	}
	return 0;
}

void console_finish_destroy64(struct CONSOLE64 *con)
{
	if (con != NULL && con->task == NULL && con->pending_stack == 0) {
		*con = (struct CONSOLE64) { 0 };
	}
}

void console64_destroy(struct CONSOLE64 *con)
{
	uint64_t irq_state = platform_irq_save64();

	if (con != NULL) {
		if (gui_has_console64(con)) {
			(void) gui_close_console_checked64(con);
		} else if (console_stop_checked64(con) == 0) {
			console_finish_destroy64(con);
		}
	}
	platform_irq_restore64(irq_state);
}

/* Unbound stack ownership stays here until task_set_entry succeeds. */
static int console_start_task64(struct CONSOLE64 *con, void (*entry)(void))
{
	uintptr_t stack = 0;
	uint64_t irq_state = platform_irq_save64();
	struct FIFO64 old_keys;
	enum CONSOLE_SLOT_STATE64 old_state;
	int old_ready;
	int status = -22;

	if (con == NULL || con->task != NULL || con->slot_state == CONSOLE64_CLOSING) {
		goto out;
	}
	old_keys = con->keys;
	old_state = con->slot_state;
	old_ready = con->close_ready;
	con->task = task_alloc64();
	if (con->task == NULL) { status = -12; goto out; }
	stack = memman64_alloc_4k(&memman64, CONSOLE64_STACK_SIZE);
	if (stack == 0) { status = -12; goto fail; }
	status = fifo64_init(&con->keys, CONSOLE64_KEY_BUF, con->key_buf, con->task);
	if (status != 0) { goto fail; }
	status = task_set_entry64(con->task, entry, stack, CONSOLE64_STACK_SIZE);
	if (status != 0) { goto fail; }
	stack = 0; /* The task now owns the stack. */
	status = task_run64(con->task, 0, 2);
	if (status != 0) { goto fail; }
	con->slot_state = CONSOLE64_LIVE;
	goto out;
fail:
	con->slot_state = CONSOLE64_CLOSING;
	con->pending_stack = stack;
	con->pending_stack_size = stack != 0 ? CONSOLE64_STACK_SIZE : 0;
	con->close_ready = 1;
	(void) console_stop_checked64(con);
	con->keys = old_keys;
	if (con->task == NULL && con->pending_stack == 0) {
		con->slot_state = old_state;
		con->close_ready = old_ready;
	}
out:
	platform_irq_restore64(irq_state);
	return status;
}

int console64_start_task(struct CONSOLE64 *con)
{
	return console_start_task64(con, console_task_main64) == 0 ? 0 : -1;
}

int console64_start_input_task(struct CONSOLE64 *con)
{
	return console_start_task64(con, console_input_task_main64) == 0 ? 0 : -1;
}
