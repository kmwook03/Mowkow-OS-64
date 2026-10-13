#include <arch/platform64.h>
#include <fifo64.h>
#include <mtask64.h>
#include <stddef.h>
#include <stdint.h>

#define CHECK64(condition) do { if (!(condition)) return __LINE__; } while (0)

static uint64_t irq_enabled = 1;
static uint32_t irq_depth;
static uint32_t wake_calls;
static uint32_t wake_depth;

uint64_t platform_irq_save64(void)
{
	uint64_t previous;

	previous = irq_enabled;
	irq_enabled = 0;
	irq_depth++;
	return previous;
}

void platform_irq_restore64(uint64_t state)
{
	if (irq_depth > 0) {
		irq_depth--;
	}
	irq_enabled = state;
}

int task_run64(struct TASK64 *task, int level, int priority)
{
	(void) level;
	(void) priority;
	wake_calls++;
	wake_depth = irq_depth;
	if (task == NULL ||
			(task->flags != TASK64_FLAGS_ALLOCATED &&
			task->flags != TASK64_FLAGS_SLEEP_PENDING)) {
		return TASK64_ERR_INVALID;
	}
	task->flags = TASK64_FLAGS_RUNNING;
	return 0;
}

static struct EVENT64 event64(uint32_t data)
{
	struct EVENT64 event;

	event.type = EVENT64_KEYBOARD;
	event.data = data;
	return event;
}

static int test_init_boundaries64(void)
{
	struct EVENT64 buffer[2];
	struct EVENT64 event;
	struct FIFO64 fifo;

	CHECK64(fifo64_init(NULL, 2, buffer, NULL) == FIFO64_ERR_INVALID);
	CHECK64(fifo64_put(NULL, event64(1)) == FIFO64_ERR_INVALID);
	CHECK64(fifo64_get(NULL, &event) == FIFO64_ERR_INVALID);
	CHECK64(fifo64_status(NULL) == 0);
	CHECK64(fifo64_overruns(NULL) == 0);
	CHECK64(fifo64_init(&fifo, 0, buffer, NULL) == FIFO64_ERR_INVALID);
	CHECK64(fifo.size == 0 && fifo.buf == NULL);
	CHECK64(fifo64_init(&fifo, 2, NULL, NULL) == FIFO64_ERR_INVALID);
	CHECK64(fifo64_init(&fifo, 2, buffer, NULL) == 0);
	CHECK64(fifo.size == 2 && fifo.free == 2);
	CHECK64(fifo.flags == 0 && fifo.overruns == 0);
	CHECK64(fifo64_status(&fifo) == 0);
	CHECK64(fifo64_overruns(&fifo) == 0);
	CHECK64(fifo64_get(&fifo, NULL) == FIFO64_ERR_INVALID);
	CHECK64(fifo64_get(&fifo, &buffer[0]) == FIFO64_ERR_EMPTY);
	CHECK64(irq_enabled == 1 && irq_depth == 0);
	return 0;
}

static int test_wrap_and_overrun64(void)
{
	struct EVENT64 buffer[3];
	struct EVENT64 event;
	struct FIFO64 fifo;

	CHECK64(fifo64_init(&fifo, 3, buffer, NULL) == 0);
	CHECK64(fifo64_put(&fifo, event64(1)) == 0);
	CHECK64(fifo64_put(&fifo, event64(2)) == 0);
	CHECK64(fifo64_put(&fifo, event64(3)) == 0);
	CHECK64(fifo64_put(&fifo, event64(9)) == FIFO64_ERR_FULL);
	CHECK64(fifo64_put(&fifo, event64(9)) == FIFO64_ERR_FULL);
	CHECK64(fifo64_put(&fifo, event64(9)) == FIFO64_ERR_FULL);
	CHECK64((fifo.flags & FIFO64_FLAGS_OVERRUN) != 0);
	CHECK64(fifo64_overruns(&fifo) == 3);
	CHECK64(fifo64_get(&fifo, &event) == 0 && event.data == 1);
	CHECK64(fifo64_put(&fifo, event64(4)) == 0);
	CHECK64(fifo64_get(&fifo, &event) == 0 && event.data == 2);
	CHECK64(fifo64_get(&fifo, &event) == 0 && event.data == 3);
	CHECK64(fifo64_get(&fifo, &event) == 0 && event.data == 4);
	CHECK64(fifo64_get(&fifo, &event) == FIFO64_ERR_EMPTY);
	CHECK64(fifo64_status(&fifo) == 0);
	fifo.overruns = UINT64_MAX;
	CHECK64(fifo64_put(&fifo, event64(5)) == 0);
	CHECK64(fifo64_put(&fifo, event64(6)) == 0);
	CHECK64(fifo64_put(&fifo, event64(7)) == 0);
	CHECK64(fifo64_put(&fifo, event64(8)) == FIFO64_ERR_FULL);
	CHECK64(fifo64_overruns(&fifo) == UINT64_MAX);
	return 0;
}

static int test_wake_after_unlock64(void)
{
	struct EVENT64 buffer[2];
	struct TASK64 task;
	struct FIFO64 fifo;

	task.flags = TASK64_FLAGS_SLEEP_PENDING;
	wake_calls = 0;
	wake_depth = UINT32_MAX;
	CHECK64(fifo64_init(&fifo, 2, buffer, &task) == 0);
	CHECK64(fifo64_put(&fifo, event64(1)) == 0);
	CHECK64(wake_calls == 1 && wake_depth == 0);
	CHECK64(task.flags == TASK64_FLAGS_RUNNING);
	CHECK64(irq_enabled == 1 && irq_depth == 0);
	CHECK64(fifo64_put(&fifo, event64(2)) == 0);
	CHECK64(wake_calls == 1);

	CHECK64(fifo64_init(&fifo, 2, buffer, &task) == 0);
	task.flags = TASK64_FLAGS_UNUSED;
	CHECK64(fifo64_put(&fifo, event64(3)) == 0);
	CHECK64(wake_calls == 1);
	return 0;
}

static int test_invalid_metadata_and_irq_state64(void)
{
	struct EVENT64 buffer[2];
	struct EVENT64 event;
	struct FIFO64 fifo;

	CHECK64(fifo64_init(&fifo, 2, buffer, NULL) == 0);
	fifo.free = 3;
	CHECK64(fifo64_put(&fifo, event64(1)) == FIFO64_ERR_INVALID);
	CHECK64(fifo64_get(&fifo, &event) == FIFO64_ERR_INVALID);
	CHECK64(fifo64_status(&fifo) == 0);
	CHECK64(fifo64_overruns(&fifo) == 0);
	CHECK64(fifo64_init(&fifo, 2, buffer, NULL) == 0);
	irq_enabled = 0;
	CHECK64(fifo64_put(&fifo, event64(2)) == 0);
	CHECK64(irq_enabled == 0 && irq_depth == 0);
	irq_enabled = 1;
	return 0;
}

int main(void)
{
	int status;

	status = test_init_boundaries64();
	if (status != 0) {
		return status;
	}
	status = test_wrap_and_overrun64();
	if (status != 0) {
		return status;
	}
	status = test_wake_after_unlock64();
	if (status != 0) {
		return status;
	}
	return test_invalid_metadata_and_irq_state64();
}
