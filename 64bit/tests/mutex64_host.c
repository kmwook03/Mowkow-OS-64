#include <arch/platform64.h>
#include <mtask64.h>
#include <mutex64.h>
#include <stddef.h>
#include <stdint.h>

#define CHECK64(condition) do { \
	if (!(condition)) { \
		return __LINE__; \
	} \
} while (0)

static struct MUTEX64 *contended_mutex;
static struct TASK64 task_a;
static struct TASK64 task_b;
static struct TASK64 *current_task;
static uint32_t prepare_calls;
static uint32_t commit_calls;
static uint32_t wake_calls;
static uint64_t irq_enabled = 1;

uint64_t platform_irq_save64(void)
{
	uint64_t previous;

	previous = irq_enabled;
	irq_enabled = 0;
	return previous;
}

void platform_irq_restore64(uint64_t state)
{
	irq_enabled = state;
}

struct TASK64 *task_now64(void)
{
	return current_task;
}

int task_sleep_prepare64(struct TASK64 *task)
{
	if (task == NULL || task != current_task ||
			task->flags != TASK64_FLAGS_RUNNING) {
		return TASK64_ERR_INVALID;
	}
	prepare_calls++;
	task->flags = TASK64_FLAGS_SLEEP_PENDING;
	return 0;
}

int task_sleep_commit64(struct TASK64 *task)
{
	struct TASK64 *saved;

	if (task == NULL || task != current_task ||
			task->flags != TASK64_FLAGS_SLEEP_PENDING) {
		return TASK64_ERR_INVALID;
	}
	commit_calls++;
	/* Simulate the owner running while this waiter is asleep. */
	saved = current_task;
	current_task = &task_a;
	if (mutex64_unlock(contended_mutex) != 0) {
		return TASK64_ERR_INVALID;
	}
	current_task = saved;
	return task->flags == TASK64_FLAGS_RUNNING ? 0 : TASK64_ERR_INVALID;
}

int task_run64(struct TASK64 *task, int level, int priority)
{
	(void) level;
	(void) priority;
	if (task == NULL || task->flags != TASK64_FLAGS_SLEEP_PENDING) {
		return TASK64_ERR_INVALID;
	}
	wake_calls++;
	task->flags = TASK64_FLAGS_RUNNING;
	return 0;
}

static int test_boot_owner64(void)
{
	struct MUTEX64 mutex;

	mutex64_init(&mutex);
	current_task = NULL;
	CHECK64(mutex64_lock(&mutex) == 0);
	CHECK64(mutex.locked != 0);
	CHECK64(mutex64_lock(&mutex) == MUTEX64_ERR_BUSY);
	CHECK64(mutex64_unlock(&mutex) == 0);
	CHECK64(mutex.locked == 0);
	return 0;
}

static int test_wait_and_ownership64(void)
{
	struct MUTEX64 mutex;

	mutex64_init(&mutex);
	task_a.flags = TASK64_FLAGS_RUNNING;
	task_b.flags = TASK64_FLAGS_RUNNING;
	prepare_calls = 0;
	commit_calls = 0;
	wake_calls = 0;
	contended_mutex = &mutex;
	current_task = &task_a;
	CHECK64(mutex64_lock(&mutex) == 0);
	current_task = &task_b;
	CHECK64(mutex64_lock(&mutex) == 0);
	CHECK64(mutex.owner == &task_b && mutex.waiter_count == 0);
	CHECK64(prepare_calls == 1 && commit_calls == 1 && wake_calls == 1);
	current_task = &task_a;
	CHECK64(mutex64_unlock(&mutex) == MUTEX64_ERR_INVALID);
	current_task = &task_b;
	CHECK64(mutex64_lock(&mutex) == MUTEX64_ERR_BUSY);
	CHECK64(mutex64_unlock(&mutex) == 0);
	CHECK64(irq_enabled == 1);
	return 0;
}

int main(void)
{
	int status;

	status = test_boot_owner64();
	if (status != 0) {
		return status;
	}
	return test_wait_and_ownership64();
}
