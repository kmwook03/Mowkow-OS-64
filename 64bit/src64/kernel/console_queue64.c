/* Console key FIFO, event waits and raw/REPL transport. */
#include "console_internal64.h"
#include <arch/platform64.h>
#include <keyboard64.h>
#include <mtask64.h>
#include <syscall64.h>

static void console_irq_disable64(void)
{
	platform_irq_disable64();
}

static void console_irq_enable64(void)
{
	platform_irq_enable64();
}

int console_pump_event64(struct CONSOLE64 *con, uint16_t *key)
{
	struct EVENT64 event;

	console_irq_disable64();
	if (fifo64_get(&con->keys, &event) != 0) {
		task_sleep64(con->task);
		console_irq_enable64();
		return 0;
	}
	console_irq_enable64();
	if (event.type == EVENT64_KEYBOARD) {
		*key = (uint16_t) event.data;
		return 1;
	}
	return 0;
}

uint16_t console_wait_key64(struct CONSOLE64 *con)
{
	uint16_t key;

	for (;;) {
		if (console_pump_event64(con, &key) != 0) {
			return key;
		}
	}
}

void console_repl_push64(struct CONSOLE64 *con, char c)
{
	uint32_t next;

	next = (con->repl_queue_tail + 1) % CONSOLE64_REPL_QUEUE_SIZE;
	if (next == con->repl_queue_head) {
		return;
	}
	con->repl_queue[con->repl_queue_tail] = c;
	con->repl_queue_tail = next;
}

int console_repl_pop64(struct CONSOLE64 *con, char *out)
{
	if (con->repl_queue_head == con->repl_queue_tail) {
		return 0;
	}
	*out = con->repl_queue[con->repl_queue_head];
	con->repl_queue_head = (con->repl_queue_head + 1) % CONSOLE64_REPL_QUEUE_SIZE;
	return 1;
}

void console_raw_push64(struct CONSOLE64 *con, unsigned int kind, unsigned int payload)
{
	uint32_t next;
	uint64_t mods;

	next = (con->raw_queue_tail + 1) % CONSOLE64_RAW_QUEUE_SIZE;
	if (next == con->raw_queue_head) {
		return;
	}
	mods = 0;
	if (keyboard64_shift() != 0) {
		mods |= TTY_MOD_SHIFT;
	}
	if (keyboard64_ctrl() != 0) {
		mods |= TTY_MOD_CTRL;
	}
	if (keyboard64_alt() != 0) {
		mods |= TTY_MOD_ALT;
	}
	con->raw_queue[con->raw_queue_tail] = (uint64_t) payload |
		((uint64_t) kind << 32) | (mods << 40);
	con->raw_queue_tail = next;
}

int console_raw_pop64(struct CONSOLE64 *con, uint64_t *out)
{
	if (con->raw_queue_head == con->raw_queue_tail) {
		return 0;
	}
	*out = con->raw_queue[con->raw_queue_head];
	con->raw_queue_head = (con->raw_queue_head + 1) % CONSOLE64_RAW_QUEUE_SIZE;
	return 1;
}

void console64_post_key(struct CONSOLE64 *con, uint16_t key)
{
	(void) console64_post_input_key(con, key);
}

void console64_input_queue_init(struct CONSOLE64 *con)
{
	if (con != NULL) {
		(void) fifo64_init(&con->keys, CONSOLE64_KEY_BUF, con->key_buf,
			NULL);
	}
}

int console64_post_input_key(struct CONSOLE64 *con, uint16_t key)
{
	struct EVENT64 event;

	if (con == NULL || con->close_ready != 0 || con->slot_state == CONSOLE64_CLOSING ||
		con->keys.buf == NULL || con->keys.size == 0U) {
		return -1;
	}
	event.type = EVENT64_KEYBOARD;
	event.data = key;
	return fifo64_put(&con->keys, event);
}

int console64_drain_input(struct CONSOLE64 *con)
{
	struct EVENT64 event;
	int count;

	if (con == NULL || con->keys.buf == NULL || con->keys.size == 0U) {
		return -1;
	}
	count = 0;
	while (fifo64_get(&con->keys, &event) == 0) {
		if (event.type == EVENT64_KEYBOARD) {
			console64_process_input_key(con, (uint16_t) event.data);
			count++;
		}
	}
	return count;
}

void console64_repl_set_active(int active)
{
	/* REPL을 켜는 건 MicroPython을 돌리는 그 콘솔이다. console_active로
	   두면 콘솔 1에서 py를 띄웠는데 콘솔 0이 REPL 모드가 된다. */
	struct CONSOLE64 *con = console_self64();

	con->repl_active = active;
	con->repl_queue_head = 0;
	con->repl_queue_tail = 0;
}
